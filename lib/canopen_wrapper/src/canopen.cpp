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
    uint8_t             nmt_state[256];

    /* Callbacks */
    canopen_pdo_cb_t    pdo_cb;
    void*               pdo_userdata;
    canopen_nmt_cb_t    nmt_cb;
    void*               nmt_userdata;
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
 * ----------------------------------------------------------------------- */
static void* reader_thread_func(void* arg) {
    canopen_ctx_t* ctx = (canopen_ctx_t*)arg;
    struct can_frame frame;

    while (ctx->reader_running) {
        int ret = recv_frame(ctx->sock, &frame, 100);
        if (ret < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (ret == 0) continue;

        uint32_t cob_id = frame.can_id & 0x7FF;
        uint8_t  node   = cob_id & 0x7F;

        /* Heartbeat / NMT (COB-ID 0x700 + node) */
        if ((cob_id & ~0x7F) == CANOPEN_COBID_HEARTBEAT) {
            uint8_t state = frame.data[0];
            ctx->nmt_state[node] = state;
            if (ctx->nmt_cb)
                ctx->nmt_cb(ctx->nmt_userdata, node, state);
            continue;
        }

        /* PDO (TPDO1=0x180+node, TPDO2=0x280+node) */
        if ((cob_id & ~0x7F) == CANOPEN_COBID_TPDO1 ||
            (cob_id & ~0x7F) == CANOPEN_COBID_TPDO2) {
            if (ctx->pdo_cb)
                ctx->pdo_cb(ctx->pdo_userdata, cob_id,
                            frame.data, frame.can_dlc);
            continue;
        }
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
    return ctx;
}

void canopen_destroy(canopen_ctx_t* ctx) {
    if (!ctx) return;
    canopen_shutdown(ctx);
    free(ctx);
}

bool canopen_init(canopen_ctx_t* ctx, const char* interface,
                  int node_id, int baud_rate) {
    if (!ctx || !interface) return false;

    ctx->node_id = node_id;
    strncpy(ctx->ifname, interface, sizeof(ctx->ifname) - 1);

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

    /* Stop reader thread */
    if (ctx->reader_running) {
        ctx->reader_running = false;
        pthread_join(ctx->reader_thread, NULL);
    }

    /* Close socket */
    if (ctx->sock >= 0) {
        close(ctx->sock);
        ctx->sock = -1;
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
    (void)ctx;
    (void)node_id;
    (void)heartbeat_ms;
    /* Heartbeat consumption is handled by the reader thread.
     * This is a placeholder for setting up HB monitoring if needed. */
    return true;
}

/* ======================================================================
 *  SDO — expedited write
 * ====================================================================== */

bool canopen_sdo_write_expedited(canopen_ctx_t* ctx, uint8_t node_id,
                                 uint16_t index, uint8_t subindex,
                                 const void* data, size_t len) {
    if (!ctx || !ctx->connected || !data || len < 1 || len > 4)
        return false;

    uint32_t cob_id = CANOPEN_COBID_RSDO + node_id;
    uint8_t cmd;

    switch (len) {
        case 1: cmd = CANOPEN_SDO_CMD_WRITE_1; break;
        case 2: cmd = CANOPEN_SDO_CMD_WRITE_2; break;
        case 3: cmd = CANOPEN_SDO_CMD_WRITE_3; break;
        case 4: cmd = CANOPEN_SDO_CMD_WRITE_4; break;
        default: return false;
    }

    uint8_t sdo_data[8];
    memset(sdo_data, 0, sizeof(sdo_data));
    sdo_data[0] = cmd;
    sdo_data[1] = index & 0xFF;
    sdo_data[2] = (index >> 8) & 0xFF;
    sdo_data[3] = subindex;
    memcpy(&sdo_data[4], data, len);

    if (!send_frame(ctx->sock, cob_id, sdo_data, 8))
        return false;

    /* Wait for SDO response with 1s timeout */
    struct can_frame resp;
    int ret = recv_frame(ctx->sock, &resp, 1000);
    if (ret <= 0) return false;

    /* Check SDO response: bit 0 of first byte = 0 means success */
    if (resp.can_dlc < 8) return false;
    return (resp.data[0] & 0x01) == 0;
}

/* ======================================================================
 *  SDO — expedited read
 * ====================================================================== */

bool canopen_sdo_read_expedited(canopen_ctx_t* ctx, uint8_t node_id,
                                uint16_t index, uint8_t subindex,
                                void* data, size_t* len) {
    if (!ctx || !ctx->connected || !data || !len) return false;

    uint32_t cob_id = CANOPEN_COBID_RSDO + node_id;
    uint8_t sdo_data[8];

    memset(sdo_data, 0, sizeof(sdo_data));
    sdo_data[0] = CANOPEN_SDO_CMD_READ;
    sdo_data[1] = index & 0xFF;
    sdo_data[2] = (index >> 8) & 0xFF;
    sdo_data[3] = subindex;

    if (!send_frame(ctx->sock, cob_id, sdo_data, 8))
        return false;

    /* Wait for response with 1s timeout */
    struct can_frame resp;
    int ret = recv_frame(ctx->sock, &resp, 1000);
    if (ret <= 0) return false;
    if (resp.can_dlc < 8) return false;

    /* Check response command byte */
    uint8_t scs = resp.data[0] & 0x0F; /* SDO Command Specifier */
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
            return false;
    }

    memcpy(data, &resp.data[4], *len);
    return true;
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
    canopen_sdo_read_expedited(ctx, node_id,
                                OD_INDEX_STATUS_WORD, 0, &sw, &len);
    return sw;
}

bool canopen_402_enable_drive(canopen_ctx_t* ctx, uint8_t node_id) {
    /* CiA 402 state machine:
     * 1. Shutdown -> Ready to Switch On (0x06)
     * 2. Ready -> Switched On (0x07)
     * 3. Switched On -> Operation Enabled (0x0F)
     */
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

    /* Toggle bit 4 of control word to signal new position */
    uint16_t cw = canopen_402_get_status_word(ctx, node_id);
    (void)cw;
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
