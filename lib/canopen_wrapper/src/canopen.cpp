#include "canopen/canopen.h"

#include <sys/socket.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <poll.h>
#include <errno.h>
#include <time.h>

/* Maximum number of nodes tracked for emergency / NMT state */
#define MAX_CANOPEN_NODES 128

/* Emergency object data (CiA 301 §7.2.7) */
typedef struct {
    uint16_t error_code;
    uint8_t  error_register;
    uint8_t  manufacturer_data[5];
    time_t   timestamp;
} canopen_emergency_t;

/* -----------------------------------------------------------------------
 *  Internal context
 * ----------------------------------------------------------------------- */
struct canopen_ctx {
    int                 sock;
    int                 node_id;
    bool                connected;
    char                ifname[32];

    /* Background reader thread */
    pthread_t           reader_thread;
    bool                reader_running;

    /* NMT state table (node_id -> state) */
    uint8_t             nmt_state[MAX_CANOPEN_NODES];

    /* Last emergency object per node */
    canopen_emergency_t last_emergency[MAX_CANOPEN_NODES];

    /* Callbacks */
    canopen_pdo_cb_t    pdo_cb;
    void*               pdo_userdata;
    canopen_nmt_cb_t    nmt_cb;
    void*               nmt_userdata;
    canopen_emcy_cb_t   emcy_cb;
    void*               emcy_userdata;

    /* Mutex protecting socket access.
     * SDO operations lock this mutex for their entire send+recv cycle.
     * The reader thread uses trylock() so it skips a poll cycle if an
     * SDO is in progress — preventing the reader from stealing SDO
     * response frames from the socket. */
    pthread_mutex_t     sock_mutex;
};

/* -----------------------------------------------------------------------
 *  Helper: send a CAN frame
 * ----------------------------------------------------------------------- */
static bool send_frame(int sock, uint32_t can_id,
                       const uint8_t* data, uint8_t dlc) {
    struct can_frame frame;
    memset(&frame, 0, sizeof(frame));
    frame.can_id  = can_id;
    frame.can_dlc = dlc;
    if (data && dlc > 0)
        memcpy(frame.data, data, dlc);
    return write(sock, &frame, sizeof(frame)) == (ssize_t)sizeof(frame);
}

/* -----------------------------------------------------------------------
 *  Helper: receive a CAN frame with timeout
 * ----------------------------------------------------------------------- */
static int recv_frame(int sock, struct can_frame* frame, int timeout_ms) {
    struct pollfd pfd;
    memset(&pfd, 0, sizeof(pfd));
    pfd.fd     = sock;
    pfd.events = POLLIN;
    int ret = poll(&pfd, 1, timeout_ms);
    if (ret <= 0)
        return ret; /* -1 error, 0 timeout */
    if (!(pfd.revents & POLLIN))
        return 0;
    return read(sock, frame, sizeof(*frame)) == (ssize_t)sizeof(*frame) ? 1 : -1;
}

/* -----------------------------------------------------------------------
 *  Background reader thread
 *
 *  Acquires sock_mutex BEFORE reading from the socket to prevent race
 *  conditions with SDO operations. This guarantees that SDO responses
 *  are never consumed by the reader thread, eliminating the deadlock
 *  scenario where:
 *    1. SDO holds mutex, sends request, waits for response
 *    2. Reader consumes SDO response from socket
 *    3. Reader trylock fails → frame discarded
 *    4. SDO blocks forever on empty socket → mutex never released
 *
 *  The reader uses a short poll (100 ms) so it releases the mutex
 *  frequently.  A brief sleep after releasing the mutex prevents
 *  starvation of SDO operations — without it, unlock → immediate
 *  re-lock in a tight loop would prevent the SDO thread from ever
 *  acquiring the mutex between reader cycles.
 * ----------------------------------------------------------------------- */
static void* reader_thread_func(void* arg) {
    canopen_ctx_t* ctx = (canopen_ctx_t*)arg;
    struct can_frame frame;

    while (ctx->reader_running) {
        /* Acquire the socket lock BEFORE reading. This blocks while
         * an SDO operation is in progress. Once acquired, the reader
         * is guaranteed that no SDO response can be stolen. */
        pthread_mutex_lock(&ctx->sock_mutex);

        int ret = recv_frame(ctx->sock, &frame, 100);
        if (ret < 0) {
            if (errno == EINTR) {
                pthread_mutex_unlock(&ctx->sock_mutex);
                usleep(1000);  /* yield to SDO waiters */
                continue;
            }
            pthread_mutex_unlock(&ctx->sock_mutex);
            break;
        }
        if (ret == 0) {
            pthread_mutex_unlock(&ctx->sock_mutex);
            usleep(1000);  /* yield to SDO waiters — prevents starvation */
            continue;
        }

        uint32_t cob_id = frame.can_id & 0x7FF;
        uint8_t  node   = cob_id & 0x7F;

        /* --- SDO response frames (TSDO=0x580+node, RSDO=0x600+node) ---
         * These should never be received here under normal operation
         * because SDO operations hold sock_mutex. If they appear due
         * to a protocol error or misconfigured node, discard them
         * rather than falling through. */
        if ((cob_id & ~0x7F) == CANOPEN_COBID_TSDO ||
            (cob_id & ~0x7F) == CANOPEN_COBID_RSDO) {
            pthread_mutex_unlock(&ctx->sock_mutex);
        }
        /* --- Heartbeat / NMT (COB-ID 0x700 + node) --- */
        else if ((cob_id & ~0x7F) == CANOPEN_COBID_HEARTBEAT) {
            uint8_t state = frame.data[0];
            ctx->nmt_state[node] = state;
            /* Save callback+data while holding lock, call outside */
            canopen_nmt_cb_t cb = ctx->nmt_cb;
            void* ud = ctx->nmt_userdata;
            pthread_mutex_unlock(&ctx->sock_mutex);
            if (cb)
                cb(ud, node, state);
        }
        /* --- PDO (TPDO1=0x180+node, TPDO2=0x280+node) --- */
        else if ((cob_id & ~0x7F) == CANOPEN_COBID_TPDO1 ||
                 (cob_id & ~0x7F) == CANOPEN_COBID_TPDO2) {
            canopen_pdo_cb_t cb = ctx->pdo_cb;
            void* ud = ctx->pdo_userdata;
            uint32_t cob = cob_id;
            uint8_t d[8];
            uint8_t dlc = frame.can_dlc;
            if (dlc > 8) dlc = 8;
            memcpy(d, frame.data, dlc);
            pthread_mutex_unlock(&ctx->sock_mutex);
            if (cb)
                cb(ud, cob, d, dlc);
        }
        /* --- Emergency object (COB-ID 0x080 + node) --- */
        else if ((cob_id & ~0x7F) == CANOPEN_COBID_EMERGENCY_BASE && node > 0) {
            /* Emergency frame: 8 bytes — error_code(2) + err_reg(1) + mfgr_specific(5) */
            /* Store last emergency for the node */
            ctx->last_emergency[node].error_code = (uint16_t)frame.data[0]
                                                  | ((uint16_t)frame.data[1] << 8);
            ctx->last_emergency[node].error_register = frame.data[2];
            memcpy(ctx->last_emergency[node].manufacturer_data, &frame.data[3], 5);
            ctx->last_emergency[node].timestamp = time(NULL);
            canopen_emcy_cb_t cb = ctx->emcy_cb;
            void* ud = ctx->emcy_userdata;
            uint16_t ec = ctx->last_emergency[node].error_code;
            pthread_mutex_unlock(&ctx->sock_mutex);
            if (cb)
                cb(ud, node, ec, frame.data, frame.can_dlc);
        }
        else {
            pthread_mutex_unlock(&ctx->sock_mutex);
        }

        /* Yield to prevent starvation of SDO operations.
         * Without this sleep, the reader would re-acquire sock_mutex
         * immediately after releasing it, and the SDO thread blocked
         * on pthread_mutex_lock() would never get scheduled. */
        usleep(1000);
    }
    return NULL;
}

/* ======================================================================
 *  Core API implementation
 * ====================================================================== */

canopen_ctx_t* canopen_create(void) {
    canopen_ctx_t* ctx = (canopen_ctx_t*)calloc(1, sizeof(canopen_ctx_t));
    if (!ctx) return NULL;
    ctx->sock      = -1;
    ctx->connected = false;
    ctx->reader_running = false;
    memset(ctx->nmt_state, 0, sizeof(ctx->nmt_state));

    /* Initialize socket mutex (recursive not needed) */
    if (pthread_mutex_init(&ctx->sock_mutex, NULL) != 0) {
        free(ctx);
        return NULL;
    }

    return ctx;
}

void canopen_destroy(canopen_ctx_t* ctx) {
    if (!ctx) return;
    canopen_shutdown(ctx);
    pthread_mutex_destroy(&ctx->sock_mutex);
    free(ctx);
}

bool canopen_init(canopen_ctx_t* ctx, const char* interface,
                  int node_id, int baud_rate) {
    if (!ctx || !interface) return false;

    ctx->node_id = node_id;
    strncpy(ctx->ifname, interface, sizeof(ctx->ifname) - 1);
    ctx->ifname[sizeof(ctx->ifname) - 1] = '\0';

    /* Create SocketCAN socket */
    ctx->sock = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (ctx->sock < 0) {
        perror("socket(PF_CAN, SOCK_RAW, CAN_RAW)");
        return false;
    }

    /* Bind to interface */
    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, interface, IFNAMSIZ - 1);
    if (ioctl(ctx->sock, SIOCGIFINDEX, &ifr) < 0) {
        perror("ioctl(SIOCGIFINDEX)");
        close(ctx->sock);
        ctx->sock = -1;
        return false;
    }

    struct sockaddr_can addr;
    memset(&addr, 0, sizeof(addr));
    addr.can_family  = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;

    if (bind(ctx->sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind CAN socket");
        close(ctx->sock);
        ctx->sock = -1;
        return false;
    }

    ctx->connected = true;

    /* Start reader thread */
    ctx->reader_running = true;
    if (pthread_create(&ctx->reader_thread, NULL, reader_thread_func, ctx) != 0) {
        perror("pthread_create");
        ctx->reader_running = false;
        close(ctx->sock);
        ctx->sock = -1;
        ctx->connected = false;
        return false;
    }

    return true;
}

void canopen_shutdown(canopen_ctx_t* ctx) {
    if (!ctx) return;

    /* Signal the reader thread to stop */
    ctx->reader_running = false;

    /* Close the socket FIRST.  This unblocks any SDO operation that is
     * waiting in recv_frame() (poll/read).  Once the SDO releases
     * sock_mutex, the reader thread can acquire it, discover that the
     * socket is closed, and exit its loop.  Without this ordering,
     * pthread_join() below would hang forever:
     *
     *   reader thread  ── blocked on pthread_mutex_lock(sock_mutex)
     *   SDO operation  ── holds sock_mutex, blocked in read(sock)
     *   shutdown       ── blocked on pthread_join(reader_thread)
     */
    if (ctx->sock >= 0) {
        close(ctx->sock);
        ctx->sock = -1;
    }

    /* Join the reader thread.  Zero the thread ID immediately so that
     * a second call to canopen_shutdown() (e.g. from canopen_destroy()
     * after an explicit disconnect()) does not call pthread_join() on
     * an already-joined thread — that is undefined behaviour and can
     * segfault. */
    if (ctx->reader_thread) {
        pthread_t t = ctx->reader_thread;
        ctx->reader_thread = 0;
        pthread_join(t, NULL);
    }

    ctx->connected = false;
}

bool canopen_is_connected(canopen_ctx_t* ctx) {
    return ctx && ctx->connected;
}

int canopen_get_node_id(canopen_ctx_t* ctx) {
    return ctx ? ctx->node_id : -1;
}

/* ======================================================================
 *  NMT
 * ====================================================================== */

bool canopen_nmt_send_command(canopen_ctx_t* ctx, uint8_t node_id,
                              uint8_t command) {
    if (!ctx || !ctx->connected) return false;
    uint8_t data[2] = { command, node_id };
    return send_frame(ctx->sock, CANOPEN_COBID_NMT, data, 2);
}

uint8_t canopen_nmt_get_state(canopen_ctx_t* ctx, uint8_t node_id) {
    if (!ctx) return 0;
    return ctx->nmt_state[node_id];
}

bool canopen_nmt_set_hb_consumer(canopen_ctx_t* ctx, uint8_t node_id,
                                 uint16_t heartbeat_ms) {
    if (!ctx || !ctx->connected || node_id == 0) return false;
    /* Configure Consumer Heartbeat Time (OD 0x1016 sub 1) per CiA 301 §7.2.6.1.
     * The 32-bit value encodes: (node_id << 16) | heartbeat_ms
     * This tells the local CANopen stack to expect heartbeat from this node. */
    uint32_t hb_value = ((uint32_t)node_id << 16) | heartbeat_ms;
    return canopen_sdo_write_expedited(ctx, ctx->node_id,
                                       0x1016, 1, &hb_value, 4);
}

/* ======================================================================
 *  SDO — expedited write
 * ====================================================================== */

/* Helper: log SDO abort code (CiA 301 §7.2.10.7) */
static void log_sdo_abort(uint32_t abort_code) {
    const char* desc;
    switch (abort_code) {
        case CANOPEN_SDO_ABORT_TOGGLE_BIT:     desc = "Toggle bit not alternated"; break;
        case CANOPEN_SDO_ABORT_TIMEOUT:        desc = "SDO protocol timed out"; break;
        case CANOPEN_SDO_ABORT_CMD_UNKNOWN:    desc = "Command specifier not valid"; break;
        case CANOPEN_SDO_ABORT_NO_ACCESS:      desc = "Object cannot be mapped/accessed"; break;
        case CANOPEN_SDO_ABORT_WRITE_ONLY:     desc = "Attempt to read write-only object"; break;
        case CANOPEN_SDO_ABORT_READ_ONLY:      desc = "Attempt to write read-only object"; break;
        case CANOPEN_SDO_ABORT_OD_NO_EXIST:    desc = "Object does not exist in OD"; break;
        case CANOPEN_SDO_ABORT_PARAM_INCOMP:   desc = "Parameter incompatibility"; break;
        case CANOPEN_SDO_ABORT_TYPE_MISMATCH:  desc = "Type mismatch (length diff)"; break;
        case CANOPEN_SDO_ABORT_DATA_RANGE:     desc = "Value out of range"; break;
        case CANOPEN_SDO_ABORT_DATA_TOO_LARGE: desc = "Data too large"; break;
        case CANOPEN_SDO_ABORT_DATA_TOO_SMALL: desc = "Data too small"; break;
        case CANOPEN_SDO_ABORT_LOCAL_CTRL:     desc = "Local control prevents access"; break;
        case CANOPEN_SDO_ABORT_DEVICE:         desc = "Device hardware error"; break;
        default:
            if ((abort_code & 0xFF000000) == 0x08000000)
                desc = "Manufacturer-specific / generic";
            else
                desc = "Unknown abort code";
            break;
    }
    fprintf(stderr, "SDO ABORT 0x%08lX: %s\n",
            (unsigned long)abort_code, desc);
}

bool canopen_sdo_write_expedited(canopen_ctx_t* ctx, uint8_t node_id,
                                 uint16_t index, uint8_t subindex,
                                 const void* data, size_t len) {
    if (!ctx || !ctx->connected || !data || len < 1 || len > 4)
        return false;

    /* SDO retry configuration.
     * Intermittent CAN bus timeouts can occur when the reader thread holds
     * sock_mutex for up to 100 ms (poll cycle) while SYNC / heartbeat /
     * PDO traffic is high.  A single retry with a small backoff greatly
     * improves reliability without adding significant latency (worst case
     * adds ~150 ms per retry). */
    const int MAX_RETRIES = 2;      /* 1 initial + 2 retries = 3 total */
    const int RETRY_BACKOFF_MS = 50; /* wait between retries */
    const int SDO_TIMEOUT_MS = 1000; /* per-attempt timeout */

    for (int attempt = 0; attempt <= MAX_RETRIES; attempt++) {
        /* Lock socket to prevent reader thread from stealing our response */
        pthread_mutex_lock(&ctx->sock_mutex);

        uint32_t cob_id = CANOPEN_COBID_RSDO + node_id;
        uint8_t cmd;

        switch (len) {
            case 1: cmd = CANOPEN_SDO_CMD_WRITE_1; break;
            case 2: cmd = CANOPEN_SDO_CMD_WRITE_2; break;
            case 3: cmd = CANOPEN_SDO_CMD_WRITE_3; break;
            case 4: cmd = CANOPEN_SDO_CMD_WRITE_4; break;
            default: pthread_mutex_unlock(&ctx->sock_mutex); return false;
        }

        uint8_t sdo_data[8];
        memset(sdo_data, 0, sizeof(sdo_data));
        sdo_data[0] = cmd;
        sdo_data[1] = index & 0xFF;
        sdo_data[2] = (index >> 8) & 0xFF;
        sdo_data[3] = subindex;
        memcpy(&sdo_data[4], data, len);

        if (!send_frame(ctx->sock, cob_id, sdo_data, 8)) {
            pthread_mutex_unlock(&ctx->sock_mutex);
            if (attempt < MAX_RETRIES) {
                usleep(RETRY_BACKOFF_MS * 1000);
                continue;
            }
            return false;
        }

        /* Wait for SDO response with timeout */
        struct can_frame resp;
        int ret = recv_frame(ctx->sock, &resp, SDO_TIMEOUT_MS);
        if (ret <= 0) {
            pthread_mutex_unlock(&ctx->sock_mutex);
            if (attempt < MAX_RETRIES) {
                fprintf(stderr, "SDO timeout 0x%04X:%d (node %d), retry %d/%d\n",
                        index, subindex, node_id, attempt + 1, MAX_RETRIES);
                usleep(RETRY_BACKOFF_MS * 1000);
                continue;
            }
            fprintf(stderr, "SDO timeout 0x%04X:%d (node %d), all retries exhausted\n",
                    index, subindex, node_id);
            return false;
        }

        if (resp.can_dlc < 8) {
            pthread_mutex_unlock(&ctx->sock_mutex);
            if (attempt < MAX_RETRIES) {
                usleep(RETRY_BACKOFF_MS * 1000);
                continue;
            }
            return false;
        }

        /* Check SDO response: bits 7-5 (SCS) = 100 binary means abort (CiA 301 §7.2.10.7).
         * Valid responses have:
         *   - Download success: resp.data[0] = 0x60 (SCS=011)
         *   - Upload response:  resp.data[0] = 0x43..0x4F (SCS=010)
         * Abort has SCS=100, so (byte & 0xE0) == 0x80 */
        if ((resp.data[0] & 0xE0) == 0x80) {
            /* SDO abort — extract and log the 32-bit abort code (bytes 4-7).
             * SDO aborts from the drive are definitive (not transient bus errors),
             * so we do NOT retry on abort — it would fail again for the same reason. */
            uint32_t abort_code = (uint32_t)resp.data[4]
                                | ((uint32_t)resp.data[5] << 8)
                                | ((uint32_t)resp.data[6] << 16)
                                | ((uint32_t)resp.data[7] << 24);
            log_sdo_abort(abort_code);
            pthread_mutex_unlock(&ctx->sock_mutex);
            return false;
        }

        pthread_mutex_unlock(&ctx->sock_mutex);
        return true;
    }

    /* Unreachable, but keeps compiler happy */
    return false;
}

/* ======================================================================
 *  SDO — expedited read
 * ====================================================================== */

bool canopen_sdo_read_expedited(canopen_ctx_t* ctx, uint8_t node_id,
                                uint16_t index, uint8_t subindex,
                                void* data, size_t* len) {
    if (!ctx || !ctx->connected || !data || !len) return false;

    /* SDO retry configuration — matches write retry for consistency.
     * Intermittent CAN bus timeouts can occur when the reader thread
     * holds sock_mutex for up to 100 ms. */
    const int MAX_RETRIES = 2;
    const int RETRY_BACKOFF_MS = 50;
    const int SDO_TIMEOUT_MS = 1000;

    for (int attempt = 0; attempt <= MAX_RETRIES; attempt++) {
        /* Lock socket to prevent reader thread from stealing our response */
        pthread_mutex_lock(&ctx->sock_mutex);

        uint32_t cob_id = CANOPEN_COBID_RSDO + node_id;
        uint8_t sdo_data[8];

        memset(sdo_data, 0, sizeof(sdo_data));
        sdo_data[0] = CANOPEN_SDO_CMD_READ;
        sdo_data[1] = index & 0xFF;
        sdo_data[2] = (index >> 8) & 0xFF;
        sdo_data[3] = subindex;

        if (!send_frame(ctx->sock, cob_id, sdo_data, 8)) {
            pthread_mutex_unlock(&ctx->sock_mutex);
            if (attempt < MAX_RETRIES) {
                usleep(RETRY_BACKOFF_MS * 1000);
                continue;
            }
            return false;
        }

        /* Wait for response with timeout */
        struct can_frame resp;
        int ret = recv_frame(ctx->sock, &resp, SDO_TIMEOUT_MS);
        if (ret <= 0) {
            pthread_mutex_unlock(&ctx->sock_mutex);
            if (attempt < MAX_RETRIES) {
                fprintf(stderr, "SDO read timeout 0x%04X:%d (node %d), retry %d/%d\n",
                        index, subindex, node_id, attempt + 1, MAX_RETRIES);
                usleep(RETRY_BACKOFF_MS * 1000);
                continue;
            }
            fprintf(stderr, "SDO read timeout 0x%04X:%d (node %d), all retries exhausted\n",
                    index, subindex, node_id);
            return false;
        }
        if (resp.can_dlc < 8) {
            pthread_mutex_unlock(&ctx->sock_mutex);
            if (attempt < MAX_RETRIES) {
                usleep(RETRY_BACKOFF_MS * 1000);
                continue;
            }
            return false;
        }

        /* Check SDO response: bits 7-5 (SCS) = 100 binary means abort (CiA 301 §7.2.10.7).
         * Valid responses have:
         *   - Upload response:  resp.data[0] = 0x43..0x4F (SCS=010)
         * Abort has SCS=100, so (byte & 0xE0) == 0x80
         * SDO aborts are definitive — do not retry. */
        if ((resp.data[0] & 0xE0) == 0x80) {
            uint32_t abort_code = (uint32_t)resp.data[4]
                                | ((uint32_t)resp.data[5] << 8)
                                | ((uint32_t)resp.data[6] << 16)
                                | ((uint32_t)resp.data[7] << 24);
            log_sdo_abort(abort_code);
            pthread_mutex_unlock(&ctx->sock_mutex);
            return false;
        }

        /* Validate expedited transfer flags (CiA 301 §7.2.10.3):
         *   e-bit (bit 1) = 1 means expedited
         *   s-bit (bit 0) = 0 means data size is specified by SCS nibble
         */
        uint8_t resp_byte0 = resp.data[0];
        uint8_t scs = resp_byte0 & 0x0F; /* SDO Command Specifier */

        /* If not expedited (e-bit = 0), we cannot process it here */
        if (!(resp_byte0 & 0x02)) {
            fprintf(stderr, "SDO: Server returned segmented transfer (not expedited) for 0x%04X sub 0x%02X\n",
                    index, subindex);
            pthread_mutex_unlock(&ctx->sock_mutex);
            return false;
        }

        /* Determine data size from SCS nibble */
        switch (scs) {
            case CANOPEN_SDO_CMD_READ_RESP_1 & 0x0F:
                *len = 1; break;
            case CANOPEN_SDO_CMD_READ_RESP_2 & 0x0F:
                *len = 2; break;
            case CANOPEN_SDO_CMD_READ_RESP_3 & 0x0F:
                *len = 3; break;
            case CANOPEN_SDO_CMD_READ_RESP_4 & 0x0F:
                *len = 4; break;
            default:
                fprintf(stderr, "SDO: Unknown response SCS 0x%02X for 0x%04X sub 0x%02X\n",
                        scs, index, subindex);
                pthread_mutex_unlock(&ctx->sock_mutex);
                return false;
        }

        memcpy(data, &resp.data[4], *len);
        pthread_mutex_unlock(&ctx->sock_mutex);
        return true;
    }

    /* Unreachable */
    return false;
}

/* ======================================================================
 *  CiA 402 — Drive profile
 * ====================================================================== */

bool canopen_402_set_control_word(canopen_ctx_t* ctx, uint8_t node_id,
                                  uint16_t cw) {
    return canopen_sdo_write_expedited(ctx, node_id,
                                       OD_INDEX_CONTROL_WORD, 0, &cw, 2);
}

uint16_t canopen_402_get_status_word(canopen_ctx_t* ctx, uint8_t node_id) {
    uint16_t sw = 0;
    size_t len = 2;
    if (!canopen_sdo_read_expedited(ctx, node_id,
                                     OD_INDEX_STATUS_WORD, 0, &sw, &len)) {
        /* SDO read failed — the drive is not responding.
         * Return 0 so the caller sees (status & OP_ENABLED) == 0 and
         * attempts re-enable.  The re-enable will also fail if the
         * drive is truly offline, producing a clear error message. */
        return 0;
    }
    return sw;
}

bool canopen_402_enable_drive(canopen_ctx_t* ctx, uint8_t node_id) {
    /* First, ensure the node is in NMT Operational state.
     * Some drives require this before they accept CiA 402 control words. */
    if (!canopen_nmt_send_command(ctx, node_id, CANOPEN_NMT_START_REMOTE_NODE))
        return false;
    usleep(10000);

    /* Read the current status word via SDO to determine CiA 402 state.
     * Use a direct SDO read (not the getter) so we can distinguish
     * "drive responded with 0x0000" from "drive did not respond".
     * If the drive is not on the bus, bail early — ~1 s instead of ~5 s. */
    uint16_t sw = 0;
    size_t sw_len = sizeof(sw);
    bool sw_ok = canopen_sdo_read_expedited(ctx, node_id,
                                            OD_INDEX_STATUS_WORD, 0,
                                            &sw, &sw_len);
    if (!sw_ok) {
        /* Drive not responding — nothing to enable.  Skip the entire
         * CiA 402 control word sequence (3 more SDO writes that would
         * each time out after ~1 s). */
        return false;
    }

    bool in_fault = (sw & CIA402_STATUS_FAULT) != 0;

    /* CiA 402 §6.4 State machine recovery:
     *
     *   Fault state  → Fault Reset (0x0080→0x0000) → Switch On Disabled
     *   Quick Stop   → Shutdown (0x0006)            → Ready to Switch On
     *   Other        → Shutdown (0x0006)            → Ready to Switch On
     *
     * The standard enable sequence from Switch On Disabled is:
     *   0x0006 (Shutdown)       → Ready to Switch On
     *   0x0007 (Switch On)      → Switched On
     *   0x000F (Enable Op.)     → Operation Enabled
     */
    if (in_fault) {
        /* From Fault state: Fault Reset → Switch On Disabled */
        if (!canopen_402_fault_reset(ctx, node_id)) return false;
        usleep(20000);
    }

    /* CiA 402 enable sequence */
    if (!canopen_402_set_control_word(ctx, node_id, 0x0006)) return false;
    usleep(10000);
    if (!canopen_402_set_control_word(ctx, node_id, 0x0007)) return false;
    usleep(10000);
    return canopen_402_set_control_word(ctx, node_id, 0x000F);
}

bool canopen_402_disable_drive(canopen_ctx_t* ctx, uint8_t node_id) {
    return canopen_402_set_control_word(ctx, node_id, 0x0000);
}

bool canopen_402_quick_stop(canopen_ctx_t* ctx, uint8_t node_id) {
    return canopen_402_set_control_word(ctx, node_id, 0x0002);
}

bool canopen_402_fault_reset(canopen_ctx_t* ctx, uint8_t node_id) {
    if (!canopen_402_set_control_word(ctx, node_id, 0x0080)) return false;
    usleep(10000);
    return canopen_402_set_control_word(ctx, node_id, 0x0000);
}

bool canopen_402_set_mode(canopen_ctx_t* ctx, uint8_t node_id, int8_t mode) {
    return canopen_sdo_write_expedited(ctx, node_id,
                                       OD_INDEX_MODES_OF_OP, 0, &mode, 1);
}

bool canopen_402_set_target_position(canopen_ctx_t* ctx, uint8_t node_id,
                                     int32_t position) {
    if (!canopen_sdo_write_expedited(ctx, node_id,
                                     OD_INDEX_TARGET_POSITION, 0,
                                     &position, 4))
        return false;

    /* CiA 402 "new set-point" signalling: toggle bit 4 of control word.
     * Write 0x001F (Operation Enabled + new set-point bit), then
     * write 0x000F (Operation Enabled, new set-point cleared).
     * This tells the drive that a new target position has been written. */
    if (!canopen_402_set_control_word(ctx, node_id, 0x001F)) return false;
    usleep(10000);
    return canopen_402_set_control_word(ctx, node_id, 0x000F);
}

/* ======================================================================
 *  Raw frame I/O
 * ====================================================================== */

bool canopen_send_frame(canopen_ctx_t* ctx, const canopen_frame_t* frame) {
    if (!ctx || !ctx->connected || !frame) return false;
    return send_frame(ctx->sock, frame->can_id,
                      frame->data, frame->can_dlc);
}

int canopen_recv_frame(canopen_ctx_t* ctx, canopen_frame_t* frame,
                       int timeout_ms) {
    if (!ctx || !frame) return -1;
    struct can_frame raw;
    int ret = recv_frame(ctx->sock, &raw, timeout_ms);
    if (ret <= 0) return ret;
    frame->can_id  = raw.can_id;
    frame->can_dlc = raw.can_dlc;
    memcpy(frame->data, raw.data, 8);
    return 1;
}

/* ======================================================================
 *  PDO / NMT callbacks
 * ====================================================================== */

void canopen_set_pdo_callback(canopen_ctx_t* ctx, canopen_pdo_cb_t cb,
                              void* userdata) {
    if (!ctx) return;
    ctx->pdo_cb       = cb;
    ctx->pdo_userdata = userdata;
}

void canopen_set_nmt_callback(canopen_ctx_t* ctx, canopen_nmt_cb_t cb,
                              void* userdata) {
    if (!ctx) return;
    ctx->nmt_cb       = cb;
    ctx->nmt_userdata = userdata;
}

void canopen_set_emergency_callback(canopen_ctx_t* ctx, canopen_emcy_cb_t cb,
                                    void* userdata) {
    if (!ctx) return;
    ctx->emcy_cb       = cb;
    ctx->emcy_userdata = userdata;
}

bool canopen_get_emergency(canopen_ctx_t* ctx, uint8_t node_id,
                           uint16_t* error_code, uint8_t* error_register) {
    if (!ctx || !error_code || !error_register || node_id >= MAX_CANOPEN_NODES)
        return false;
    if (ctx->last_emergency[node_id].timestamp == 0)
        return false; /* No emergency received for this node */
    *error_code    = ctx->last_emergency[node_id].error_code;
    *error_register = ctx->last_emergency[node_id].error_register;
    return true;
}
