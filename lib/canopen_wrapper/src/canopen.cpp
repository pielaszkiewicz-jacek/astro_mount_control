/*
 * Minimal CANopen master (CiA 301) over Linux SocketCAN.
 * C++ implementation exposing a C ABI (see canopen/canopen.h).
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

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>

namespace {

struct CtxMutex {
    std::mutex m;
};

CtxMutex* ctx_mutex(canopen_ctx_t* ctx) {
    return static_cast<CtxMutex*>(ctx->mutex);
}

constexpr int kMaxRetries = 2;
constexpr int kRetryBackoffMs = 50;

uint16_t frame_index(const canopen_frame_t& f) {
    return static_cast<uint16_t>(f.data[1] | (f.data[2] << 8));
}

bool is_abort(uint8_t cmd) {
    return (cmd & 0xE0) == 0x80;
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

bool recv_one(canopen_ctx_t* ctx, canopen_frame_t* out) {
    struct can_frame frame;
    const ssize_t n = ::read(ctx->sock_fd, &frame, sizeof(frame));
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

/* Waits for the SDO response matching node/index/subindex.
 * Returns: 1 = success, 0 = timeout, -1 = abort. */
int sdo_wait_response(canopen_ctx_t* ctx, uint8_t node_id,
                      uint16_t index, uint8_t subindex,
                      uint32_t* value_out, uint8_t* size_out) {
    const uint32_t resp_cob = CANOPEN_COBID_TSDO_BASE + node_id;
    for (;;) {
        canopen_frame_t f;
        if (!recv_one(ctx, &f)) {
            return 0; /* timeout */
        }
        if (f.cob_id == resp_cob && f.dlc >= 8) {
            if (frame_index(f) == index && f.data[3] == subindex) {
                const uint32_t value = static_cast<uint32_t>(f.data[4]) |
                                       (static_cast<uint32_t>(f.data[5]) << 8) |
                                       (static_cast<uint32_t>(f.data[6]) << 16) |
                                       (static_cast<uint32_t>(f.data[7]) << 24);
                if (is_abort(f.data[0])) {
                    if (value_out) *value_out = value;
                    return -1;
                }
                uint8_t size = 0;
                switch (f.data[0]) {
                    case CANOPEN_SDO_READ_RESP_1B: size = 1; break;
                    case CANOPEN_SDO_READ_RESP_2B: size = 2; break;
                    case CANOPEN_SDO_READ_RESP_4B: size = 4; break;
                    case CANOPEN_SDO_WRITE_RESP:  size = 0; break;
                    default: continue;
                }
                if (value_out) *value_out = value;
                if (size_out) *size_out = size;
                return 1;
            }
        } else {
            dispatch_frame(ctx, f);
        }
    }
}

void log_abort(uint8_t node_id, uint16_t index, uint8_t subindex, uint32_t abort_code) {
    std::fprintf(stderr,
        "[canopen] SDO abort node=%u index=0x%04X sub=0x%02X code=0x%08X\n",
        static_cast<unsigned>(node_id), static_cast<unsigned>(index),
        static_cast<unsigned>(subindex), static_cast<unsigned>(abort_code));
}

} // namespace

extern "C" {

bool canopen_init(canopen_ctx_t* ctx, const char* iface_name, uint32_t timeout_us) {
    if (!ctx || !iface_name) return false;
    std::memset(ctx, 0, sizeof(*ctx));
    ctx->sock_fd = -1;
    ctx->timeout_us = timeout_us ? timeout_us : 500000;

    ctx->mutex = new CtxMutex();
    if (!ctx->mutex) return false;

    ctx->sock_fd = ::socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (ctx->sock_fd < 0) {
        std::perror("[canopen] socket() failed");
        delete ctx_mutex(ctx);
        ctx->mutex = nullptr;
        return false;
    }

    struct ifreq ifr;
    std::memset(&ifr, 0, sizeof(ifr));
    std::strncpy(ifr.ifr_name, iface_name, IFNAMSIZ - 1);
    if (::ioctl(ctx->sock_fd, SIOCGIFINDEX, &ifr) < 0) {
        std::fprintf(stderr, "[canopen] ioctl SIOCGIFINDEX failed for %s: %s\n",
                     iface_name, std::strerror(errno));
        ::close(ctx->sock_fd);
        ctx->sock_fd = -1;
        delete ctx_mutex(ctx);
        ctx->mutex = nullptr;
        return false;
    }

    struct sockaddr_can addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;
    if (::bind(ctx->sock_fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::fprintf(stderr, "[canopen] bind() failed: %s\n", std::strerror(errno));
        ::close(ctx->sock_fd);
        ctx->sock_fd = -1;
        delete ctx_mutex(ctx);
        ctx->mutex = nullptr;
        return false;
    }

    struct timeval tv;
    tv.tv_sec = static_cast<time_t>(ctx->timeout_us / 1000000);
    tv.tv_usec = static_cast<suseconds_t>(ctx->timeout_us % 1000000);
    ::setsockopt(ctx->sock_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    ::setsockopt(ctx->sock_fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    std::strncpy(ctx->iface_name, iface_name, sizeof(ctx->iface_name) - 1);
    return true;
}

void canopen_shutdown(canopen_ctx_t* ctx) {
    if (!ctx) return;
    if (ctx->mutex) {
        // Close the socket while holding the mutex so an in-flight SDO
        // exchange cannot race with the fd teardown. The mutex itself is
        // intentionally NOT deleted: a thread already blocked on it would
        // otherwise dereference freed memory after being unblocked.
        std::lock_guard<std::mutex> lock(ctx_mutex(ctx)->m);
        if (ctx->sock_fd >= 0) {
            ::close(ctx->sock_fd);
            ctx->sock_fd = -1;
        }
    }
}

bool canopen_is_open(const canopen_ctx_t* ctx) {
    return ctx && ctx->sock_fd >= 0;
}

bool canopen_send_frame(canopen_ctx_t* ctx, uint32_t cob_id,
                        const uint8_t* data, uint8_t dlc) {
    if (!ctx || ctx->sock_fd < 0 || dlc > 8) return false;
    struct can_frame frame;
    std::memset(&frame, 0, sizeof(frame));
    frame.can_id = cob_id & CAN_EFF_MASK;
    frame.can_dlc = dlc;
    if (data && dlc) std::memcpy(frame.data, data, dlc);

    std::lock_guard<std::mutex> lock(ctx_mutex(ctx)->m);
    const ssize_t n = ::write(ctx->sock_fd, &frame, sizeof(frame));
    return n == static_cast<ssize_t>(sizeof(frame));
}

bool canopen_recv_frame(canopen_ctx_t* ctx, canopen_frame_t* frame) {
    if (!ctx || ctx->sock_fd < 0 || !frame) return false;
    std::lock_guard<std::mutex> lock(ctx_mutex(ctx)->m);
    canopen_frame_t tmp;
    if (!recv_one(ctx, &tmp)) return false;
    dispatch_frame(ctx, tmp);
    *frame = tmp;
    return true;
}

int canopen_poll_events(canopen_ctx_t* ctx, int max_frames) {
    if (!ctx || ctx->sock_fd < 0 || max_frames <= 0) return 0;
    std::lock_guard<std::mutex> lock(ctx_mutex(ctx)->m);
    int consumed = 0;
    for (int i = 0; i < max_frames; ++i) {
        struct can_frame frame;
        const ssize_t n = ::recv(ctx->sock_fd, &frame, sizeof(frame), MSG_DONTWAIT);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            break;
        }
        if (n < static_cast<ssize_t>(sizeof(struct can_frame))) continue;
        canopen_frame_t f;
        f.cob_id = frame.can_id & CAN_EFF_MASK;
        f.dlc = frame.can_dlc;
        std::memcpy(f.data, frame.data, sizeof(f.data));
        dispatch_frame(ctx, f);
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

    std::unique_lock<std::mutex> lock(ctx_mutex(ctx)->m);
    for (int attempt = 0; attempt <= kMaxRetries; ++attempt) {
        struct can_frame frame;
        std::memset(&frame, 0, sizeof(frame));
        frame.can_id = (CANOPEN_COBID_RSDO_BASE + node_id) & CAN_EFF_MASK;
        frame.can_dlc = 8;
        std::memcpy(frame.data, data, 8);
        if (::write(ctx->sock_fd, &frame, sizeof(frame)) != static_cast<ssize_t>(sizeof(frame))) {
            lock.unlock();
            std::this_thread::sleep_for(std::chrono::milliseconds(kRetryBackoffMs));
            lock.lock();
            continue;
        }
        uint32_t value_out = 0;
        uint8_t size_out = 0;
        const int rc = sdo_wait_response(ctx, node_id, index, subindex,
                                         &value_out, &size_out);
        if (rc == 1) return true;
        if (rc == -1) {
            log_abort(node_id, index, subindex, value_out);
            return false;
        }
        /* timeout — retry */
        lock.unlock();
        std::this_thread::sleep_for(std::chrono::milliseconds(kRetryBackoffMs));
        lock.lock();
    }
    return false;
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

    std::unique_lock<std::mutex> lock(ctx_mutex(ctx)->m);
    for (int attempt = 0; attempt <= kMaxRetries; ++attempt) {
        struct can_frame frame;
        std::memset(&frame, 0, sizeof(frame));
        frame.can_id = (CANOPEN_COBID_RSDO_BASE + node_id) & CAN_EFF_MASK;
        frame.can_dlc = 8;
        std::memcpy(frame.data, data, 8);
        if (::write(ctx->sock_fd, &frame, sizeof(frame)) != static_cast<ssize_t>(sizeof(frame))) {
            lock.unlock();
            std::this_thread::sleep_for(std::chrono::milliseconds(kRetryBackoffMs));
            lock.lock();
            continue;
        }
        uint32_t value_out = 0;
        uint8_t size_out = 0;
        const int rc = sdo_wait_response(ctx, node_id, index, subindex,
                                         &value_out, &size_out);
        if (rc == 1) {
            if (value) *value = value_out;
            if (size) *size = size_out;
            return true;
        }
        if (rc == -1) {
            log_abort(node_id, index, subindex, value_out);
            return false;
        }
        lock.unlock();
        std::this_thread::sleep_for(std::chrono::milliseconds(kRetryBackoffMs));
        lock.lock();
    }
    return false;
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
