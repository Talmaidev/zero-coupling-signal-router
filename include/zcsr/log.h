#ifndef ZCSR_LOG_H
#define ZCSR_LOG_H
/* Unified logging API (platform-independent contract). Implementer: modules/core.
 * CONTRACT ONLY. Modules include this header, never another module's source.
 *
 * Design notes:
 *  - Zero runtime/heap allocation: each zcsr_log_write() formats into a fixed
 *    caller stack buffer (2048 bytes including NUL).
 *  - Configuration (handler/user/level) is mutex-protected. A consistent snapshot
 *    is taken under the lock and released BEFORE the user handler is invoked, so a
 *    handler that reconfigures the logger cannot deadlock.
 *  - The user handler may be invoked concurrently from multiple threads. It must
 *    be thread-safe and must not assume serialization across threads.
 *  - Reentrancy: if a handler calls zcsr_log_write() on the same thread, the nested
 *    message is routed to the platform default sink instead of re-entering the
 *    handler (no infinite recursion). Other threads are unaffected.
 *  - Lifetime: the caller owns the `user` pointer. Replacing a handler does not
 *    wait for in-flight callbacks to finish; the caller must keep the old `user`
 *    valid until no thread can still be inside the old handler.
 *  - ZCSR_LOG_FATAL is only a severity label; it never aborts or traps. */
#include <stdarg.h>

typedef enum {
    ZCSR_LOG_TRACE = 0,
    ZCSR_LOG_DEBUG = 1,
    ZCSR_LOG_INFO  = 2,
    ZCSR_LOG_WARN  = 3,
    ZCSR_LOG_ERROR = 4,
    ZCSR_LOG_FATAL = 5,
} zcsr_log_level;

/* Callback invoked for every message at or above the current minimum level.
 * `tag` is never NULL (a stable default is supplied by the logger), and `msg`
 * is a NUL-terminated, already-formatted string. */
typedef void (*zcsr_log_fn)(
    zcsr_log_level level,
    const char* tag,
    const char* msg,
    void* user);

/* Install a custom handler. Passing NULL for handler restores the platform
 * default sink. The caller owns `user`'s lifetime (see header notes). */
void zcsr_log_set_handler(zcsr_log_fn handler, void* user);

/* Set the minimum level that produces output. Out-of-range levels are ignored
 * and leave the current setting unchanged. */
void zcsr_log_set_level(zcsr_log_level level);

/* Format and emit a message at the given level.
 *  - A NULL tag uses the stable default tag ("zcsr").
 *  - A NULL fmt returns safely without emitting.
 *  - Out-of-range levels are ignored.
 *  - Formatting follows vsnprintf rules; output is truncated to 2047 bytes
 *    and always NUL-terminated. */
void zcsr_log_write(
    zcsr_log_level level,
    const char* tag,
    const char* fmt,
    ...);

#endif /* ZCSR_LOG_H */
