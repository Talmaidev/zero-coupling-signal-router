#ifndef ZCSR_LOG_H
#define ZCSR_LOG_H
/* Unified logging API (platform-independent contract). Implementer: modules/core.
 * CONTRACT ONLY. Modules include this header, never another module's source.
 *
 * C linkage: this header wraps its declarations in extern "C" when parsed as
 * C++, so a C++ caller can include it directly (without diagnostics.h).
 *
 * Design notes:
 *  - Zero runtime/heap allocation: each zcsr_log_write() formats into a fixed
 *    caller stack buffer (2048 bytes including NUL). File/crash sinks use
 *    pre-opened OS handles, not stdio FILE objects.
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
 *  - Per-tag overrides: a fixed 32-entry table, no heap. Tag names are copied
 *    (max 31 bytes). Reads are lock-free; register tags before worker threads
 *    start logging, or accept that the first registration is not synchronized
 *    with concurrent readers.
 *  - Rate-limited variants (ZCSR_LOG_ONCE / FIRST_N / EVERY_N / EVERY_MS) live in
 *    diagnostics.h; each call site owns a private counter, no heap.
 *  - ZCSR_LOG_FATAL is only a severity label; it never aborts or traps.
 *  - The file sink is an ADDITIONAL sink: enabling it does not disable the
 *    handler/default sink. It is opt-in because file writes can block.
 *  - The crash sink is a separate pre-opened append handle plus a last-64-message
 *    ring. It is intended to be called from a host-installed signal handler; the
 *    engine does not install signal handlers itself. */

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ZCSR_LOG_TRACE = 0,
    ZCSR_LOG_DEBUG = 1,
    ZCSR_LOG_INFO  = 2,
    ZCSR_LOG_WARN  = 3,
    ZCSR_LOG_ERROR = 4,
    ZCSR_LOG_FATAL = 5,
} zcsr_log_level;

/* Level order is part of the public contract: filters use `<` comparisons and
 * callers persist the numeric value. Freeze it at compile time.
 * _Static_assert is C11-only; it is skipped when the header is parsed as C++.
 * The declarations above already sit inside extern "C", so a C++ caller does
 * not need to add its own wrapper. */
#ifndef __cplusplus
_Static_assert(ZCSR_LOG_TRACE == 0, "log level ABI: TRACE must stay 0");
_Static_assert(ZCSR_LOG_DEBUG == 1, "log level ABI: DEBUG must stay 1");
_Static_assert(ZCSR_LOG_INFO  == 2, "log level ABI: INFO must stay 2");
_Static_assert(ZCSR_LOG_WARN  == 3, "log level ABI: WARN must stay 3");
_Static_assert(ZCSR_LOG_ERROR == 4, "log level ABI: ERROR must stay 4");
_Static_assert(ZCSR_LOG_FATAL == 5, "log level ABI: FATAL must stay 5");
#endif

/* Lets the compiler validate every call site's format string and argument
 * types against the printf rules (GCC/Clang). MSVC has SAL
 * `_Printf_format_string_` for `/analyze`/IDE, but it is not a GCC-style
 * compiler attribute and is not a hard build gate here; this macro expands to
 * nothing under MSVC. Use zcsr_log_event() for typed fields when a call site
 * must be checked on MSVC too. */
#if defined(__GNUC__) || defined(__clang__)
#  define ZCSR_PRINTF_FORMAT(fmt_index, first_arg) \
    __attribute__((format(printf, fmt_index, first_arg)))
#else
#  define ZCSR_PRINTF_FORMAT(fmt_index, first_arg)
#endif

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

/* Read the current global minimum level. */
zcsr_log_level zcsr_log_get_level(void);

/* Fast path: true when a message at `level` with `tag` would pass the current
 * filters. Does not format, allocate, or invoke the handler. The answer can
 * become stale immediately if another thread changes a level. */
bool zcsr_log_enabled(zcsr_log_level level, const char* tag);

/* Install a per-tag minimum level. `tag` must be a stable string (literal or
 * static storage); the logger copies up to 31 bytes. Returns false for a NULL
 * or empty tag, a tag longer than 31 bytes, an invalid level, or a full table.
 * A registered tag's concrete level is the effective minimum for that tag and
 * may be lower (more verbose) or higher (stricter) than the global level. */
bool zcsr_log_set_tag_level(const char* tag, zcsr_log_level level);

/* Read a tag override. Returns false when the tag has no override; on success
 * writes the level to `out_level` (which may be NULL). */
bool zcsr_log_get_tag_level(const char* tag, zcsr_log_level* out_level);

/* Clear every tag override. Tag slots stay allocated (only levels are reset),
 * so this is safe to call repeatedly; do not call it concurrently with
 * logging from other threads. */
void zcsr_log_reset_tag_levels(void);

/* Monotonic milliseconds since an unspecified origin. Never goes backwards
 * during a process lifetime; precision is platform-dependent (usually 1 ms). */
uint64_t zcsr_log_now_ms(void);

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
    ...) ZCSR_PRINTF_FORMAT(3, 4);

/* ---------------------------------------------------------------- file sink */

/* Append UTF-8 text lines to `path` and rotate at `max_bytes`.
 *
 *  - `max_files` is the number of rotated backups kept (`.1` .. `.N`).
 *    0 selects the default of 5; valid explicit values are 1..16.
 *  - `max_bytes` is the per-file limit; 0 selects 1 MiB; values below 4096
 *    are rejected.
 *  - Path length is limited to 511 bytes; longer paths are rejected.
 *  - Returns false for invalid arguments or if the file cannot be opened.
 *    An already-active file sink is left untouched on failure.
 *  - The file sink is additive: handler/default output still happens.
 *  - On write/rotation failure (disk full, read-only, permissions), the file
 *    sink disables itself once and the normal stderr/default path continues.
 *    `zcsr_log_set_file` can re-enable it.
 */
bool zcsr_log_set_file(const char* path, int max_files, size_t max_bytes);

/* Disable and close the file sink. Safe to call when it is not active. */
void zcsr_log_clear_file(void);

/* ---------------------------------------------------------------- crash sink */

/* Pre-open `path` for append and enable the last-64-message crash ring.
 * The handle is kept open until zcsr_log_clear_crash_file(); no fopen/stdio
 * allocation is performed at crash time. Returns false for an invalid path
 * or open failure. */
bool zcsr_log_set_crash_file(const char* path);

/* Disable the crash ring, close the crash handle, and clear the ring. */
void zcsr_log_clear_crash_file(void);

/* Write one already-formatted line to the crash file (or stderr if no crash
 * file is set). Uses raw OS writes only: no heap, no mutex, no stdio, no
 * vsnprintf. Intended for a host-installed signal handler. */
void zcsr_log_crash_write(const char* tag, const char* msg);

/* Dump the last-64 ring entries in chronological order to the crash file.
 * No heap, no mutex, no stdio; uses atomics + raw OS writes. */
void zcsr_log_crash_flush(void);

/* --------------------------------------------------------- structured events */

typedef enum {
    ZCSR_LOG_FIELD_I64,
    ZCSR_LOG_FIELD_U64,
    ZCSR_LOG_FIELD_F64,
    ZCSR_LOG_FIELD_STR,
    ZCSR_LOG_FIELD_BOOL,
    ZCSR_LOG_FIELD_PTR
} zcsr_log_field_type;

/* Field type numbers are part of the public event ABI (stable across user
 * handlers and serialized consumers). Freeze them like the level enum above. */
#ifndef __cplusplus
_Static_assert(ZCSR_LOG_FIELD_I64  == 0, "log field ABI: I64 must stay 0");
_Static_assert(ZCSR_LOG_FIELD_U64  == 1, "log field ABI: U64 must stay 1");
_Static_assert(ZCSR_LOG_FIELD_F64  == 2, "log field ABI: F64 must stay 2");
_Static_assert(ZCSR_LOG_FIELD_STR  == 3, "log field ABI: STR must stay 3");
_Static_assert(ZCSR_LOG_FIELD_BOOL == 4, "log field ABI: BOOL must stay 4");
_Static_assert(ZCSR_LOG_FIELD_PTR  == 5, "log field ABI: PTR must stay 5");
#endif

typedef struct {
    const char* key;
    zcsr_log_field_type type;
    union {
        int64_t i64;
        uint64_t u64;
        double f64;
        const char* str;
        bool b;
        void* ptr;
    } v;
} zcsr_log_field;

/* Emit one JSON object through the normal filter/sink path:
 *   {"ts":<monotonic-ms>,"tid":<thread-id>,"level":"INFO","tag":"...",
 *    "event":<id>,"fields":{...}}
 * Strings must be valid UTF-8 for normal text, but malformed bytes are escaped
 * as \u00XX so the emitted JSON remains syntactically valid; non-finite or
 * out-of-range doubles become null; pointer values are quoted lower-case hex
 * strings ("0x..."). The whole object is capped at 2047 bytes and always
 * NUL-terminated. If a field would overrun the cap, trailing fields are omitted
 * and the object is closed with `"truncated":true`; the emitted text is always
 * syntactically valid JSON. `fields` may be NULL when `count` is 0. */
void zcsr_log_event(
    zcsr_log_level level,
    const char* tag,
    uint32_t event_id,
    const zcsr_log_field* fields,
    size_t count);

#ifdef __cplusplus
}
#endif

#endif /* ZCSR_LOG_H */
