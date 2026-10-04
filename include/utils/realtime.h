#pragma once
// Realtime thread helpers — best-effort SCHED_FIFO promotion with graceful
// fallback.  Used by the CANopen HAL monitor thread and the tracking I/O
// (command ticker) thread so that command emission is not delayed by
// SCHED_OTHER scheduling jitter.  Promotions fail harmlessly when the process
// lacks CAP_SYS_NICE / a sufficient RLIMIT_RTPRIO, in which case the caller
// simply keeps running on the default CFS scheduler.

#include <string>

#if defined(__linux__)
#include <pthread.h>
#include <sched.h>
#include <sys/mman.h>
#include <cerrno>
#include <cstring>

#ifndef _GNU_SOURCE
extern "C" int pthread_setname_np(pthread_t, const char*);
#endif
#endif

namespace astro_mount {
namespace utils {

/**
 * Promote an arbitrary (joinable) std::thread to SCHED_FIFO.
 *
 * @param t          thread to promote (must be joinable)
 * @param priority   desired FIFO priority, clamped to [1, 98]; values >98 are
 *                   kept clear of the kernel's 99 (migration/watchdog).
 * @param error_out  optional diagnostic message on failure
 * @return true on success
 */
template <typename ThreadT>
inline bool setThreadRealtime(ThreadT& t, int priority, std::string* error_out = nullptr) {
#if defined(__linux__)
    if (!t.joinable()) {
        if (error_out) *error_out = "thread not joinable";
        return false;
    }
    if (priority < 1) priority = 1;
    if (priority > 98) priority = 98;
    struct sched_param param;
    std::memset(&param, 0, sizeof(param));
    param.sched_priority = priority;
    const int rc = pthread_setschedparam(t.native_handle(), SCHED_FIFO, &param);
    if (rc != 0) {
        if (error_out) *error_out = std::string("pthread_setschedparam(SCHED_FIFO) failed: ") +
                                    std::strerror(rc);
        return false;
    }
    return true;
#else
    (void)t;
    (void)priority;
    if (error_out) *error_out = "realtime scheduling not supported on this platform";
    return false;
#endif
}

/**
 * Promote the calling thread to SCHED_FIFO.  Intended to be called as the first
 * statement of a new thread's entry function (before the first sleep) so the
 * thread never runs an iteration under CFS.
 */
inline bool setCurrentThreadRealtime(int priority, std::string* error_out = nullptr) {
#if defined(__linux__)
    if (priority < 1) priority = 1;
    if (priority > 98) priority = 98;
    struct sched_param param;
    std::memset(&param, 0, sizeof(param));
    param.sched_priority = priority;
    const int rc = pthread_setschedparam(pthread_self(), SCHED_FIFO, &param);
    if (rc != 0) {
        if (error_out) *error_out = std::string("pthread_setschedparam(SCHED_FIFO) failed: ") +
                                    std::strerror(rc);
        return false;
    }
    return true;
#else
    (void)priority;
    if (error_out) *error_out = "realtime scheduling not supported on this platform";
    return false;
#endif
}

/**
 * Lock the whole process address space (mlockall).  Prevents page-faults from
 * breaking determinism once a realtime thread touches a fresh page.  Requires
 * a sufficient RLIMIT_MEMLOCK; returns false otherwise.
 */
inline bool lockMemory() {
#if defined(__linux__)
    return mlockall(MCL_CURRENT | MCL_FUTURE) == 0;
#else
    return false;
#endif
}

/** Best-effort thread name (visible in `top -H` / `ps -T`). */
inline void setCurrentThreadName(const char* name) {
#if defined(__linux__)
    if (name) pthread_setname_np(pthread_self(), name);
#else
    (void)name;
#endif
}

} // namespace utils
} // namespace astro_mount
