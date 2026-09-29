/*
 * Minimal CANopen master (CiA 301) over Linux SocketCAN.
 * C++ implementation exposing a C ABI (see canopen/canopen.h).
 *
 * Thread model:
 *   - A dedicated reader thread owns the socket and demultiplexes incoming
 *     frames.  SDO responses are delivered to the matching per-node pending
 *     transaction; heartbeat/EMCY/PDO frames are dispatched to the registered
 *     callbacks and queued for canopen_recv_frame()/canopen_poll_events().
 *   - SDO exchanges are serialized PER NODE (one in-flight request per node),
 *     so a hung node that never answers its SDO only blocks that node's queue,
 *     not every other CAN caller.  Socket writes are serialized by a separate
 *     short-lived write mutex.
 */

#ifndef __linux__
#error "canopen wrapper requires Linux (SocketCAN)."
#endif

#include "canopen/canopen.h"

#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>
#include <thread>

namespace {

constexpr int kMaxRetries = 2;
constexpr int kRetryBackoffMs = 50;
constexpr size_t kFrameQueueLimit = 256;

uint16_t frame_index(const canopen_frame_t& f) {
    return static_cast<uint16_t>(f.data[1] | (f.data[2] << 8));
}

bool is_abort(uint8_t cmd) {
    return (cmd & 0xE0) == 0x80;
}

// ── Internal state ─────────────────────────────────────────────────────────
// Stored behind ctx->mutex (opaque to the C header).
struct CtxState {
    int sock_fd{-1};
    uint32_t timeout_us{500000};
    std::atomic<bool> running{false};

    std::mutex write_mutex;   // serializes ::write on the socket (short hold)
    std::mutex state_mutex;   // guards slots + frame_queue
    std::condition_variable cv;

    // One in-flight SDO transaction per node.
    struct Slot {
        bool pending{false};
        uint16_t index{0};
        uint8_t subindex{0};
        uint32_t value{0};
        uint8_t size{0};
        int result{0};  // 0 = waiting, 1 = success, -1 = abort
    };
    Slot slots[CANOPEN_MAX_NODES];

    // Non-SDO frames already dispatched to callbacks, queued for
    // canopen_recv_frame()/canopen_poll_events() consumers.
    std::deque<canopen_frame_t> frame_queue;

    std::thread reader;
};

CtxState* ctx_state(canopen_ctx_t* ctx) {
    return static_cast<CtxState*>(ctx->mutex);
}

const CtxState* ctx_state(const canopen_ctx_t* ctx) {
    return static_cast<const CtxState*>(ctx->mutex);
}

/* Dispatches a received frame to heartbeat / EMCY / PDO callbacks.
 * Returns true if the frame was consumed by a callback. */
bool dispatch_frame(canopen_ctx_t* ctx, const canopen_frame_t& f) {
    if (f.cob_id == CANOPEN_COBID_SYNC) {
        return true; /* SYNC — nothing to do */
    }
    if (f.cob_id >= (CANOPEN_COBID_HEARTBEAT_BASE + 1) &&
        f.cob_id <= (CANOPEN_COBID_HEARTBEAT_BASE + 127)) {
        const uint8_t node = static_cast<uint8_t>(f.cob_id - CANOPEN_COBID_HEARTBEAT_BASE);
        const uint8_t state = f.dlc >= 1 ? f.data[0] : 0;
        ctx->nmt_state[node] = state;
        if (ctx->nmt_cb) {
            ctx->nmt_cb(node, state, ctx->nmt_userdata);
        }
        return true;
    }
    if (f.cob_id >= (CANOPEN_COBID_EMCY_BASE + 1) &&
        f.cob_id <= (CANOPEN_COBID_EMCY_BASE + 127)) {
        const uint8_t node = static_cast<uint8_t>(f.cob_id - CANOPEN_COBID_EMCY_BASE);
        canopen_emergency_t emcy;
        std::memset(&emcy, 0, sizeof(emcy));
        if (f.dlc >= 8) {
            emcy.error_code = static_cast<uint16_t>(f.data[0] | (f.data[1] << 8));
            emcy.error_register = f.data[2];
            for (int i = 0; i < 5; ++i) emcy.aux[i] = f.data[3 + i];
        }
        ctx->last_emergency[node] = emcy;
        if (ctx->emcy_cb) {
            ctx->emcy_cb(node, &emcy, ctx->emcy_userdata);
        }
        return true;
    }
    if (f.cob_id >= 0x180 && f.cob_id <= 0x57F) {
        if (ctx->pdo_cb) {
            ctx->pdo_cb(f.cob_id, f.data, f.dlc, ctx->pdo_userdata);
        }
        return true;
    }
    return false;
}

bool recv_one_frame(int fd, canopen_frame_t* out) {
    struct can_frame frame;
    ssize_t n;
    do {
        n = ::read(fd, &frame, sizeof(frame));
    } while (n < 0 && errno == EINTR);
    if (n < 0) {
        return false;
    }
    if (n < static_cast<ssize_t>(sizeof(struct can_frame))) {
        /* Short read — ignore. */
        return false;
    }
    out->cob_id = frame.can_id & CAN_EFF_MASK;
    out->dlc = frame.can_dlc;
    std::memcpy(out->data, frame.data, sizeof(out->data));
    return true;
}

void reader_loop(CtxState* st, canopen_ctx_t* ctx) {
    while (st->running.load(std::memory_order_acquire)) {
        canopen_frame_t f;
        if (!recv_one_frame(st->sock_fd, &f)) {
            // Socket receive timeout or transient error.  Loop again; the
            // running flag is re-checked so shutdown() can terminate us.
            continue;
        }

        // Route SDO responses (0x580 + node_id) to the pending transaction.
        if (f.cob_id >= (CANOPEN_COBID_TSDO_BASE + 1) &&
            f.cob_id <= (CANOPEN_COBID_TSDO_BASE + CANOPEN_MAX_NODES - 1)) {
            const uint8_t node =
                static_cast<uint8_t>(f.cob_id - CANOPEN_COBID_TSDO_BASE);
            {
                std::lock_guard<std::mutex> lk(st->state_mutex);
                CtxState::Slot& slot = st->slots[node];
                if (slot.pending && f.dlc >= 8 &&
                    frame_index(f) == slot.index && f.data[3] == slot.subindex) {
                    const uint32_t value = static_cast<uint32_t>(f.data[4]) |
                                           (static_cast<uint32_t>(f.data[5]) << 8) |
                                           (static_cast<uint32_t>(f.data[6]) << 16) |
                                           (static_cast<uint32_t>(f.data[7]) << 24);
                    if (is_abort(f.data[0])) {
                        slot.value = value;
                        slot.result = -1;
                        slot.pending = false;
                        st->cv.notify_all();
                        continue;
                    }
                    uint8_t size = 0;
                    bool recognized = true;
                    switch (f.data[0]) {
                        case CANOPEN_SDO_READ_RESP_1B: size = 1; break;
                        case CANOPEN_SDO_READ_RESP_2B: size = 2; break;
                        case CANOPEN_SDO_READ_RESP_4B: size = 4; break;
                        case CANOPEN_SDO_WRITE_RESP:  size = 0; break;
                        default: recognized = false; break;
                    }
                    if (recognized) {
                        slot.value = value;
                        slot.size = size;
                        slot.result = 1;
                        slot.pending = false;
                        st->cv.notify_all();
                    }
                    // Unrecognized SDO command byte — drop the frame and keep
                    // the slot pending so the sender can time out and retry.
                    continue;
                }
            }
            // SDO response with no matching slot (stale/late reply) — drop it.
            continue;
        }

        // Non-SDO frame: dispatch to callbacks, then queue for consumers.
        dispatch_frame(ctx, f);
        {
            std::lock_guard<std::mutex> lk(st->state_mutex);
            if (st->frame_queue.size() < kFrameQueueLimit) {
                st->frame_queue.push_back(f);
            }
        }
        st->cv.notify_all();
    }
}

void log_abort(uint8_t node_id, uint16_t index, uint8_t subindex, uint32_t abort_code) {
    std::fprintf(stderr,
        "[canopen] SDO abort node=%u index=0x%04X sub=0x%02X code=0x%08X\n",
        static_cast<unsigned>(node_id), static_cast<unsigned>(index),
        static_cast<unsigned>(subindex), static_cast<unsigned>(abort_code));
}

/* Performs one SDO expedited exchange with per-node serialization and retries.
 * Returns: 1 = success, -1 = abort, 0 = failure/timeout. */
int sdo_exchange(canopen_ctx_t* ctx, uint8_t node_id, uint16_t index,
                 uint8_t subindex, const uint8_t* data, uint8_t dlc,
                 uint32_t* value, uint8_t* size) {
    if (!ctx || node_id >= CANOPEN_MAX_NODES) return 0;
    CtxState* st = ctx_state(ctx);
    if (!st || st->sock_fd < 0) return 0;

    CtxState::Slot& slot = st->slots[node_id];

    // Serialize per node: wait until no other request for this node is in
    // flight.  Other nodes proceed independently.
    {
        std::unique_lock<std::mutex> lk(st->state_mutex);
        st->cv.wait(lk, [&]{ return !slot.pending || !st->running.load(); });
        if (!st->running.load()) return 0;
    }

    for (int attempt = 0; attempt <= kMaxRetries; ++attempt) {
        {
            std::lock_guard<std::mutex> lk(st->state_mutex);
            slot.pending = true;
            slot.index = index;
            slot.subindex = subindex;
            slot.result = 0;
            slot.value = 0;
            slot.size = 0;
        }

        bool sent = false;
        {
            std::lock_guard<std::mutex> wl(st->write_mutex);
            struct can_frame frame;
            std::memset(&frame, 0, sizeof(frame));
            frame.can_id = (CANOPEN_COBID_RSDO_BASE + node_id) & CAN_EFF_MASK;
            frame.can_dlc = dlc;
            std::memcpy(frame.data, data, dlc);
            sent = (::write(st->sock_fd, &frame, sizeof(frame)) ==
                    static_cast<ssize_t>(sizeof(frame)));
        }

        if (!sent) {
            {
                std::lock_guard<std::mutex> lk(st->state_mutex);
                slot.pending = false;
            }
            st->cv.notify_all();
            std::this_thread::sleep_for(std::chrono::milliseconds(kRetryBackoffMs));
            continue;
        }

        std::unique_lock<std::mutex> lk(st->state_mutex);
        st->cv.wait_for(lk, std::chrono::microseconds(st->timeout_us),
            [&]{ return !slot.pending || !st->running.load(); });

        if (!slot.pending) {
            // Response delivered by the reader thread.
            const int result = slot.result;
            const uint32_t v = slot.value;
            const uint8_t sz = slot.size;
            lk.unlock();

            if (result == 1) {
                if (value) *value = v;
                if (size) *size = sz;
                return 1;
            }
            if (result == -1) {
                log_abort(node_id, index, subindex, v);
                return -1;
            }
            // Unexpected result — retry.
            std::this_thread::sleep_for(std::chrono::milliseconds(kRetryBackoffMs));
            continue;
        }

        // slot.pending is still true: response timeout or shutdown.
        slot.pending = false;
        lk.unlock();
        st->cv.notify_all();
        if (!st->running.load()) return 0;
        std::this_thread::sleep_for(std::chrono::milliseconds(kRetryBackoffMs));
    }

    {
        std::lock_guard<std::mutex> lk(st->state_mutex);
        if (slot.pending && slot.index == index && slot.subindex == subindex) {
            slot.pending = false;
        }
    }
    st->cv.notify_all();
    return 0;
}

} // namespace

extern "C" {

bool canopen_init(canopen_ctx_t* ctx, const char* iface_name, uint32_t timeout_us) {
    if (!ctx || !iface_name) return false;
    std::memset(ctx, 0, sizeof(*ctx));
    ctx->sock_fd = -1;
    ctx->timeout_us = timeout_us ? timeout_us : 500000;

    CtxState* st = new CtxState();
    if (!st) return false;
    st->timeout_us = ctx->timeout_us;
    ctx->mutex = st;

    st->sock_fd = ::socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (st->sock_fd < 0) {
        std::perror("[canopen] socket() failed");
        delete st;
        ctx->mutex = nullptr;
        return false;
    }
    ctx->sock_fd = st->sock_fd;

    struct ifreq ifr;
    std::memset(&ifr, 0, sizeof(ifr));
    std::strncpy(ifr.ifr_name, iface_name, IFNAMSIZ - 1);
    if (::ioctl(st->sock_fd, SIOCGIFINDEX, &ifr) < 0) {
        std::fprintf(stderr, "[canopen] ioctl SIOCGIFINDEX failed for %s: %s\n",
                     iface_name, std::strerror(errno));
        ::close(st->sock_fd);
        st->sock_fd = -1;
        ctx->sock_fd = -1;
        delete st;
        ctx->mutex = nullptr;
        return false;
    }

    struct sockaddr_can addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;
    if (::bind(st->sock_fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::fprintf(stderr, "[canopen] bind() failed: %s\n", std::strerror(errno));
        ::close(st->sock_fd);
        st->sock_fd = -1;
        ctx->sock_fd = -1;
        delete st;
        ctx->mutex = nullptr;
        return false;
    }

    struct timeval tv;
    tv.tv_sec = static_cast<time_t>(ctx->timeout_us / 1000000);
    tv.tv_usec = static_cast<suseconds_t>(ctx->timeout_us % 1000000);
    ::setsockopt(st->sock_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    ::setsockopt(st->sock_fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    std::strncpy(ctx->iface_name, iface_name, sizeof(ctx->iface_name) - 1);

    st->running.store(true);
    st->reader = std::thread(reader_loop, st, ctx);
    return true;
}

void canopen_shutdown(canopen_ctx_t* ctx) {
    if (!ctx) return;
    CtxState* st = ctx_state(ctx);
    if (!st) return;

    st->running.store(false);
    st->cv.notify_all();
    // Wake a reader blocked in ::read so it can observe running=false and exit.
    if (st->sock_fd >= 0) {
        ::shutdown(st->sock_fd, SHUT_RDWR);
    }
    if (st->reader.joinable()) {
        st->reader.join();
    }
    if (st->sock_fd >= 0) {
        ::close(st->sock_fd);
        st->sock_fd = -1;
    }
    ctx->sock_fd = -1;
    delete st;
    ctx->mutex = nullptr;
}

bool canopen_is_open(const canopen_ctx_t* ctx) {
    return ctx && ctx->sock_fd >= 0;
}

bool canopen_send_frame(canopen_ctx_t* ctx, uint32_t cob_id,
                        const uint8_t* data, uint8_t dlc) {
    if (!ctx || ctx->sock_fd < 0 || dlc > 8) return false;
    CtxState* st = ctx_state(ctx);
    if (!st) return false;

    struct can_frame frame;
    std::memset(&frame, 0, sizeof(frame));
    frame.can_id = cob_id & CAN_EFF_MASK;
    frame.can_dlc = dlc;
    if (data && dlc) std::memcpy(frame.data, data, dlc);

    std::lock_guard<std::mutex> lock(st->write_mutex);
    const ssize_t n = ::write(ctx->sock_fd, &frame, sizeof(frame));
    return n == static_cast<ssize_t>(sizeof(frame));
}

bool canopen_recv_frame(canopen_ctx_t* ctx, canopen_frame_t* frame) {
    if (!ctx || !frame) return false;
    CtxState* st = ctx_state(ctx);
    if (!st || st->sock_fd < 0) return false;

    std::unique_lock<std::mutex> lk(st->state_mutex);
    if (!st->cv.wait_for(lk, std::chrono::microseconds(st->timeout_us),
                         [&]{ return !st->frame_queue.empty() || !st->running.load(); })) {
        return false;
    }
    if (st->frame_queue.empty()) return false;
    *frame = st->frame_queue.front();
    st->frame_queue.pop_front();
    return true;
}

int canopen_poll_events(canopen_ctx_t* ctx, int max_frames) {
    if (!ctx || max_frames <= 0) return 0;
    CtxState* st = ctx_state(ctx);
    if (!st) return 0;

    std::lock_guard<std::mutex> lk(st->state_mutex);
    int consumed = 0;
    while (consumed < max_frames && !st->frame_queue.empty()) {
        st->frame_queue.pop_front();
        ++consumed;
    }
    return consumed;
}

bool canopen_sdo_write_expedited(canopen_ctx_t* ctx, uint8_t node_id,
                                 uint16_t index, uint8_t subindex,
                                 uint32_t value, uint8_t size) {
    if (!ctx || ctx->sock_fd < 0) return false;
    if (size != 1 && size != 2 && size != 4) return false;

    uint8_t cmd = 0;
    switch (size) {
        case 1: cmd = CANOPEN_SDO_WRITE_1B; break;
        case 2: cmd = CANOPEN_SDO_WRITE_2B; break;
        case 4: cmd = CANOPEN_SDO_WRITE_4B; break;
        default: return false;
    }

    uint8_t data[8];
    std::memset(data, 0, sizeof(data));
    data[0] = cmd;
    data[1] = static_cast<uint8_t>(index & 0xFF);
    data[2] = static_cast<uint8_t>((index >> 8) & 0xFF);
    data[3] = subindex;
    for (uint8_t i = 0; i < size; ++i) {
        data[4 + i] = static_cast<uint8_t>((value >> (8 * i)) & 0xFF);
    }

    return sdo_exchange(ctx, node_id, index, subindex, data, 8, nullptr, nullptr) == 1;
}

bool canopen_sdo_read_expedited(canopen_ctx_t* ctx, uint8_t node_id,
                                uint16_t index, uint8_t subindex,
                                uint32_t* value, uint8_t* size) {
    if (!ctx || ctx->sock_fd < 0) return false;

    uint8_t data[8];
    std::memset(data, 0, sizeof(data));
    data[0] = CANOPEN_SDO_READ_REQ;
    data[1] = static_cast<uint8_t>(index & 0xFF);
    data[2] = static_cast<uint8_t>((index >> 8) & 0xFF);
    data[3] = subindex;

    return sdo_exchange(ctx, node_id, index, subindex, data, 8, value, size) == 1;
}

bool canopen_nmt_send(canopen_ctx_t* ctx, uint8_t node_id, uint8_t command) {
    if (!ctx || ctx->sock_fd < 0) return false;
    uint8_t data[2] = { command, node_id };
    return canopen_send_frame(ctx, CANOPEN_COBID_NMT, data, 2);
}

bool canopen_nmt_set_heartbeat(canopen_ctx_t* ctx, uint8_t node_id,
                               uint16_t heartbeat_ms) {
    return canopen_sdo_write_expedited(ctx, node_id, 0x1017, 0x00,
                                       heartbeat_ms, 2);
}

void canopen_set_pdo_callback(canopen_ctx_t* ctx, canopen_pdo_cb cb, void* userdata) {
    if (!ctx) return;
    ctx->pdo_cb = cb;
    ctx->pdo_userdata = userdata;
}

void canopen_set_nmt_callback(canopen_ctx_t* ctx, canopen_nmt_cb cb, void* userdata) {
    if (!ctx) return;
    ctx->nmt_cb = cb;
    ctx->nmt_userdata = userdata;
}

void canopen_set_emergency_callback(canopen_ctx_t* ctx, canopen_emcy_cb cb, void* userdata) {
    if (!ctx) return;
    ctx->emcy_cb = cb;
    ctx->emcy_userdata = userdata;
}

bool canopen_get_emergency(canopen_ctx_t* ctx, uint8_t node_id,
                           canopen_emergency_t* out) {
    if (!ctx || node_id >= CANOPEN_MAX_NODES || !out) return false;
    *out = ctx->last_emergency[node_id];
    return true;
}

uint8_t canopen_get_nmt_state(canopen_ctx_t* ctx, uint8_t node_id) {
    if (!ctx || node_id >= CANOPEN_MAX_NODES) return 0xFF;
    return ctx->nmt_state[node_id];
}

} // extern "C"
