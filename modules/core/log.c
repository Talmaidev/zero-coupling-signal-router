/* Unified logging API — implementation.
 * Constraints: no runtime/heap allocation, per-call stack formatting, mutex
 * protected configuration, snapshot released before invoking user handler,
 * thread-local reentrancy guard.
 * Includes ONLY shared contracts. Platform default sinks are reached via
 * conditionally-included OS headers (no third-party dependencies).
 *
 * Sinks:
 *  - platform default: stderr on desktop, UTF-16 console/ODS on Windows,
 *    logcat on Android, os_log on Apple;
 *  - optional file sink: pre-opened OS handle, append + rotation;
 *  - optional crash sink: pre-opened OS handle + last-64 ring, raw writes. */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#  define _POSIX_C_SOURCE 200809L
#endif
#include "zcsr/log.h"

#include <errno.h>
#include <inttypes.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <threads.h>
#include <time.h>

#if defined(_WIN32)
#  if !defined(_WIN32_WINNT)
#    define _WIN32_WINNT 0x0601 /* Win7+: GetTickCount64, no 49.7-day wrap */
#  endif
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#  include <io.h>
#elif defined(__ANDROID__)
#  include <android/log.h>
#  include <pthread.h>
#  include <unistd.h>
#  include <fcntl.h>
#  include <sys/stat.h>
#elif defined(__APPLE__)
#  include <os/log.h>
#  include <pthread.h>
#  include <unistd.h>
#  include <fcntl.h>
#  include <sys/stat.h>
#else
#  include <pthread.h>
#  include <unistd.h>
#  include <fcntl.h>
#  include <sys/stat.h>
#endif

#define ZCSR_LOG_BUF_SIZE 2048
#define ZCSR_LOG_DEFAULT_TAG "zcsr"
#define ZCSR_LOG_MAX_TAGS 32
#define ZCSR_LOG_TAG_NAME_MAX 32 /* 31 chars + NUL */
#define ZCSR_LOG_PATH_MAX 512
#define ZCSR_LOG_FILE_DEFAULT_BYTES ((size_t)1024 * 1024)
#define ZCSR_LOG_FILE_MIN_BYTES ((size_t)4096)
#define ZCSR_LOG_FILE_MAX_BACKUPS 16
#define ZCSR_LOG_RING_SLOTS 64
#define ZCSR_LOG_RING_MSG_MAX 256

/* ------------------------------------------------------------------ config */
typedef struct {
    char name[ZCSR_LOG_TAG_NAME_MAX];
    _Atomic int level; /* -1 = no override */
} zcsr_log_tag_slot;

static mtx_t          zcsr_log_lock;
/* Handler/user are published as atomic bit patterns: the dispatch path reads
 * them without taking the lock, and set_handler publishes user first, then
 * handler with release. C11 mtx_t is opaque to TSan, so guarding these with
 * the mutex alone is reported as a data race even though it is correct. */
static _Atomic(uintptr_t) zcsr_log_handler_bits = 0;
static _Atomic(uintptr_t) zcsr_log_user_bits = 0;
static _Atomic int    zcsr_log_min_level = ZCSR_LOG_INFO;

static zcsr_log_tag_slot zcsr_log_tags[ZCSR_LOG_MAX_TAGS];
static _Atomic int       zcsr_log_tag_count = 0;

/* File sink state. The handle and counters are protected by zcsr_log_file_lock;
 * `enabled` is atomic so the hot path can skip the lock when the sink is off. */
static mtx_t       zcsr_log_file_lock;
static intptr_t    zcsr_log_file_handle = -1;
static char        zcsr_log_file_path[ZCSR_LOG_PATH_MAX];
static size_t      zcsr_log_file_bytes = 0;
static size_t      zcsr_log_file_max_bytes = ZCSR_LOG_FILE_DEFAULT_BYTES;
static int         zcsr_log_file_max_backups = 5;
static bool        zcsr_log_file_reported_failure = false;
static _Atomic bool zcsr_log_file_enabled = false;

/* Crash sink: pre-opened handle + overwrite ring. */
typedef struct {
    _Atomic unsigned seq;     /* even/stable, odd/being written; 0 = never used */
    _Atomic unsigned msg_seq; /* global 1-based sequence, valid while seq is even */
    char msg[ZCSR_LOG_RING_MSG_MAX];
} zcsr_log_ring_slot;

static zcsr_log_ring_slot zcsr_log_ring[ZCSR_LOG_RING_SLOTS];
static _Atomic unsigned  zcsr_log_ring_head = 0;
static _Atomic unsigned  zcsr_log_ring_seq = 0;
static _Atomic intptr_t  zcsr_log_crash_handle = -1;
static _Atomic bool      zcsr_log_crash_enabled = false;

static once_flag zcsr_log_once = ONCE_FLAG_INIT;
static void zcsr_log_init(void) {
    mtx_init(&zcsr_log_lock, mtx_plain);
    mtx_init(&zcsr_log_file_lock, mtx_plain);
    for (int i = 0; i < ZCSR_LOG_MAX_TAGS; i++) {
        atomic_store_explicit(&zcsr_log_tags[i].level, -1, memory_order_relaxed);
    }
    atomic_store_explicit(&zcsr_log_tag_count, 0, memory_order_relaxed);
}

/* Returns the slot index for `tag`, or -1. Safe to call without the lock once
 * the tag has been published: the count load acquires, and registration
 * publishes the name/level before releasing the count. */
static int zcsr_log_find_tag(const char* tag) {
    int count = atomic_load_explicit(&zcsr_log_tag_count, memory_order_acquire);
    for (int i = 0; i < count; i++) {
        if (strcmp(zcsr_log_tags[i].name, tag) == 0) return i;
    }
    return -1;
}

/* Per-thread reentrancy guard: set while a user handler is running so any
 * nested log on the same thread goes to the platform default sink. */
static thread_local int zcsr_log_in_handler = 0;

/* ----------------------------------------------------------- common helpers */
static const char* zcsr_log_level_tag(zcsr_log_level level) {
    switch (level) {
        case ZCSR_LOG_TRACE: return "TRACE";
        case ZCSR_LOG_DEBUG: return "DEBUG";
        case ZCSR_LOG_INFO:  return "INFO";
        case ZCSR_LOG_WARN:  return "WARN";
        case ZCSR_LOG_ERROR: return "ERROR";
        case ZCSR_LOG_FATAL: return "FATAL";
        default:             return "?";
    }
}

/* Canonical line: [LEVEL][TAG] message\n. Truncation replaces the last two
 * bytes with "\n\0" so the buffer always stays NUL-terminated. Returns the
 * number of bytes to write (excluding NUL). */
static size_t zcsr_log_format_line(
    zcsr_log_level level, const char* tag, const char* msg,
    char* out, size_t cap)
{
    int n = snprintf(out, cap, "[%s][%s] %s\n",
                     zcsr_log_level_tag(level), tag, msg);
    if (n < 0) {
        out[0] = '\0';
        return 0;
    }
    if ((size_t)n >= cap) {
        out[cap - 2] = '\n';
        out[cap - 1] = '\0';
        return cap - 1; /* bytes 0..cap-2 include '\n', exclude the NUL */
    }
    return (size_t)n;
}

static size_t zcsr_log_strlen(const char* s) {
    size_t n = 0;
    if (s == NULL) return 0;
    while (s[n] != '\0') n++;
    return n;
}

static void zcsr_log_append(char* buf, size_t cap, size_t* len, const char* s) {
    while (s != NULL && *s != '\0' && *len + 1 < cap) {
        buf[(*len)++] = *s++;
    }
    buf[*len] = '\0';
}

static void zcsr_log_append_char(char* buf, size_t cap, size_t* len, char c) {
    if (*len + 1 < cap) {
        buf[(*len)++] = c;
        buf[*len] = '\0';
    }
}

/* --------------------------------------------------------- platform file I/O */
#if defined(_WIN32)
static bool zcsr_log_utf8_to_wide(const char* path, wchar_t* out, int cap) {
    if (path == NULL || out == NULL || cap <= 0) return false;
    return MultiByteToWideChar(CP_UTF8, 0, path, -1, out, cap) > 0;
}

static intptr_t zcsr_log_open_append(const char* path) {
    wchar_t wpath[ZCSR_LOG_PATH_MAX + 8];
    if (!zcsr_log_utf8_to_wide(path, wpath,
                               (int)(sizeof(wpath) / sizeof(wpath[0])))) {
        return -1;
    }
    HANDLE h = CreateFileW(
        wpath,
        FILE_APPEND_DATA | FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return -1;
    return (intptr_t)h;
}

static void zcsr_log_close_handle(intptr_t handle) {
    if (handle >= 0) CloseHandle((HANDLE)handle);
}

static bool zcsr_log_write_handle(intptr_t handle, const char* data, size_t len) {
    HANDLE h = (HANDLE)handle;
    size_t off = 0;
    while (off < len) {
        DWORD chunk = (DWORD)((len - off) > 0x7fffffffu
                              ? 0x7fffffffu : (len - off));
        DWORD written = 0;
        if (!WriteFile(h, data + off, chunk, &written, NULL) || written == 0) {
            return false;
        }
        off += (size_t)written;
    }
    return true;
}

static size_t zcsr_log_handle_size(intptr_t handle) {
    LARGE_INTEGER size;
    if (handle < 0) return 0;
    if (!GetFileSizeEx((HANDLE)handle, &size)) return 0;
    if (size.QuadPart < 0) return 0;
    return (size_t)size.QuadPart;
}

static bool zcsr_log_sync_handle(intptr_t handle) {
    return handle >= 0 && FlushFileBuffers((HANDLE)handle) != 0;
}

static bool zcsr_log_rename_path(const char* old_path, const char* new_path) {
    wchar_t wold[ZCSR_LOG_PATH_MAX + 8];
    wchar_t wnew[ZCSR_LOG_PATH_MAX + 8];
    if (!zcsr_log_utf8_to_wide(old_path, wold,
                               (int)(sizeof(wold) / sizeof(wold[0])))) {
        return false;
    }
    if (!zcsr_log_utf8_to_wide(new_path, wnew,
                               (int)(sizeof(wnew) / sizeof(wnew[0])))) {
        return false;
    }
    return MoveFileExW(wold, wnew, MOVEFILE_REPLACE_EXISTING) != 0;
}

static bool zcsr_log_remove_path(const char* path) {
    wchar_t wpath[ZCSR_LOG_PATH_MAX + 8];
    if (!zcsr_log_utf8_to_wide(path, wpath,
                               (int)(sizeof(wpath) / sizeof(wpath[0])))) {
        return false;
    }
    return DeleteFileW(wpath) != 0;
}

static void zcsr_log_write_stderr(const char* data, size_t len) {
    size_t off = 0;
    while (off < len) {
        int chunk = (int)((len - off) > 0x7fffffffu
                          ? 0x7fffffffu : (len - off));
        int written = _write(2, data + off, chunk);
        if (written <= 0) return;
        off += (size_t)written;
    }
}
#else
#if defined(O_CLOEXEC)
#  define ZCSR_LOG_OPEN_FLAGS (O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC)
#else
#  define ZCSR_LOG_OPEN_FLAGS (O_WRONLY | O_CREAT | O_APPEND)
#endif

static intptr_t zcsr_log_open_append(const char* path) {
    int fd = open(path, ZCSR_LOG_OPEN_FLAGS, 0644);
    return fd >= 0 ? (intptr_t)fd : -1;
}

static void zcsr_log_close_handle(intptr_t handle) {
    if (handle >= 0) close((int)handle);
}

static bool zcsr_log_write_handle(intptr_t handle, const char* data, size_t len) {
    int fd = (int)handle;
    size_t off = 0;
    while (off < len) {
        ssize_t written = write(fd, data + off, len - off);
        if (written < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        if (written == 0) return false;
        off += (size_t)written;
    }
    return true;
}

static size_t zcsr_log_handle_size(intptr_t handle) {
    struct stat st;
    if (handle < 0) return 0;
    if (fstat((int)handle, &st) != 0) return 0;
    if (st.st_size < 0) return 0;
    return (size_t)st.st_size;
}

static bool zcsr_log_sync_handle(intptr_t handle) {
    return handle >= 0 && fsync((int)handle) == 0;
}

static bool zcsr_log_rename_path(const char* old_path, const char* new_path) {
    return rename(old_path, new_path) == 0;
}

static bool zcsr_log_remove_path(const char* path) {
    return remove(path) == 0;
}

static void zcsr_log_write_stderr(const char* data, size_t len) {
    size_t off = 0;
    while (off < len) {
        ssize_t written = write(2, data + off, len - off);
        if (written < 0) {
            if (errno == EINTR) continue;
            return;
        }
        if (written == 0) return;
        off += (size_t)written;
    }
}
#endif

/* --------------------------------------------------------------- file sink */
static void zcsr_log_file_disable_locked(void) {
    if (zcsr_log_file_handle >= 0) {
        zcsr_log_close_handle(zcsr_log_file_handle);
        zcsr_log_file_handle = -1;
    }
    atomic_store_explicit(&zcsr_log_file_enabled, false, memory_order_release);
    if (!zcsr_log_file_reported_failure) {
        zcsr_log_file_reported_failure = true;
        fputs("[WARN][log] file sink disabled after write/rotation failure; "
              "default sink continues\n", stderr);
        fflush(stderr);
    }
}

/* Caller holds zcsr_log_file_lock and the old handle is closed by the caller
 * before this runs. Returns true when a fresh handle was opened. */
static bool zcsr_log_file_rotate_locked(void) {
    char src[ZCSR_LOG_PATH_MAX + 32];
    char dst[ZCSR_LOG_PATH_MAX + 32];
    int backups = zcsr_log_file_max_backups;
    if (backups < 1) backups = 1;

    for (int i = backups - 1; i >= 1; i--) {
        snprintf(src, sizeof(src), "%s.%d", zcsr_log_file_path, i);
        snprintf(dst, sizeof(dst), "%s.%d", zcsr_log_file_path, i + 1);
        zcsr_log_remove_path(dst);
        /* Missing source is normal; a real permission failure surfaces when
         * the current file cannot be renamed below. */
        (void)zcsr_log_rename_path(src, dst);
    }

    snprintf(dst, sizeof(dst), "%s.1", zcsr_log_file_path);
    zcsr_log_remove_path(dst);
    if (!zcsr_log_rename_path(zcsr_log_file_path, dst)) {
        return false;
    }

    intptr_t h = zcsr_log_open_append(zcsr_log_file_path);
    if (h < 0) return false;
    zcsr_log_file_handle = h;
    zcsr_log_file_bytes = 0;
    return true;
}

static void zcsr_log_file_write(zcsr_log_level level, const char* tag, const char* msg) {
    if (!atomic_load_explicit(&zcsr_log_file_enabled, memory_order_acquire)) {
        return;
    }

    mtx_lock(&zcsr_log_file_lock);
    if (!atomic_load_explicit(&zcsr_log_file_enabled, memory_order_relaxed) ||
        zcsr_log_file_handle < 0) {
        mtx_unlock(&zcsr_log_file_lock);
        return;
    }

    char line[ZCSR_LOG_BUF_SIZE];
    size_t len = zcsr_log_format_line(level, tag, msg, line, sizeof(line));
    if (len == 0) {
        mtx_unlock(&zcsr_log_file_lock);
        return;
    }

    if (zcsr_log_file_bytes > 0 &&
        (len > zcsr_log_file_max_bytes ||
         zcsr_log_file_bytes > zcsr_log_file_max_bytes - len)) {
        zcsr_log_close_handle(zcsr_log_file_handle);
        zcsr_log_file_handle = -1;
        if (!zcsr_log_file_rotate_locked()) {
            zcsr_log_file_disable_locked();
            mtx_unlock(&zcsr_log_file_lock);
            return;
        }
    }

    if (!zcsr_log_write_handle(zcsr_log_file_handle, line, len)) {
        zcsr_log_file_disable_locked();
        mtx_unlock(&zcsr_log_file_lock);
        return;
    }
    zcsr_log_file_bytes += len;
    mtx_unlock(&zcsr_log_file_lock);
}

bool zcsr_log_set_file(const char* path, int max_files, size_t max_bytes) {
    if (path == NULL || path[0] == '\0') return false;
    size_t path_len = strlen(path);
    if (path_len >= ZCSR_LOG_PATH_MAX) return false;
    if (max_files < 0 || max_files > ZCSR_LOG_FILE_MAX_BACKUPS) return false;
    if (max_files == 0) max_files = 5;
    if (max_bytes == 0) max_bytes = ZCSR_LOG_FILE_DEFAULT_BYTES;
    if (max_bytes < ZCSR_LOG_FILE_MIN_BYTES) return false;

    call_once(&zcsr_log_once, zcsr_log_init);

    /* Open the replacement first so a failed reconfiguration leaves the old
     * sink untouched. */
    intptr_t new_handle = zcsr_log_open_append(path);
    if (new_handle < 0) return false;
    size_t initial_bytes = zcsr_log_handle_size(new_handle);

    mtx_lock(&zcsr_log_file_lock);
    if (zcsr_log_file_handle >= 0) {
        zcsr_log_close_handle(zcsr_log_file_handle);
        zcsr_log_file_handle = -1;
    }
    memcpy(zcsr_log_file_path, path, path_len + 1);
    zcsr_log_file_handle = new_handle;
    zcsr_log_file_bytes = initial_bytes;
    zcsr_log_file_max_bytes = max_bytes;
    zcsr_log_file_max_backups = max_files;
    zcsr_log_file_reported_failure = false;
    atomic_store_explicit(&zcsr_log_file_enabled, true, memory_order_release);
    mtx_unlock(&zcsr_log_file_lock);
    return true;
}

void zcsr_log_clear_file(void) {
    call_once(&zcsr_log_once, zcsr_log_init);
    mtx_lock(&zcsr_log_file_lock);
    atomic_store_explicit(&zcsr_log_file_enabled, false, memory_order_release);
    if (zcsr_log_file_handle >= 0) {
        zcsr_log_close_handle(zcsr_log_file_handle);
        zcsr_log_file_handle = -1;
    }
    zcsr_log_file_path[0] = '\0';
    zcsr_log_file_bytes = 0;
    mtx_unlock(&zcsr_log_file_lock);
}

/* -------------------------------------------------------------- crash sink */
static void zcsr_log_crash_raw(const char* data, size_t len) {
    intptr_t h = atomic_load_explicit(&zcsr_log_crash_handle,
                                      memory_order_acquire);
    if (h >= 0 && zcsr_log_write_handle(h, data, len)) return;
    zcsr_log_write_stderr(data, len);
}

void zcsr_log_crash_write(const char* tag, const char* msg) {
    char line[ZCSR_LOG_RING_MSG_MAX];
    size_t n = 0;
    zcsr_log_append_char(line, sizeof(line), &n, '[');
    zcsr_log_append(line, sizeof(line), &n, "CRASH");
    zcsr_log_append(line, sizeof(line), &n, "][");
    zcsr_log_append(line, sizeof(line), &n, tag != NULL ? tag : ZCSR_LOG_DEFAULT_TAG);
    zcsr_log_append(line, sizeof(line), &n, "] ");
    zcsr_log_append(line, sizeof(line), &n, msg != NULL ? msg : "");
    zcsr_log_append_char(line, sizeof(line), &n, '\n');
    zcsr_log_crash_raw(line, n);
}

void zcsr_log_crash_flush(void) {
    struct entry {
        unsigned seq;
        const char* msg;
    };
    struct entry entries[ZCSR_LOG_RING_SLOTS];
    int count = 0;
    unsigned min_seq = 0xffffffffu;
    unsigned max_seq = 0;

    for (int i = 0; i < ZCSR_LOG_RING_SLOTS; i++) {
        unsigned s1 = atomic_load_explicit(&zcsr_log_ring[i].seq,
                                           memory_order_acquire);
        if (s1 == 0 || (s1 & 1u)) continue;
        const char* msg = zcsr_log_ring[i].msg;
        unsigned s2 = atomic_load_explicit(&zcsr_log_ring[i].seq,
                                           memory_order_acquire);
        if (s2 != s1) continue;
        unsigned gseq = atomic_load_explicit(&zcsr_log_ring[i].msg_seq,
                                             memory_order_relaxed);
        if (gseq == 0) continue;
        entries[count].seq = gseq;
        entries[count].msg = msg;
        count++;
        if (gseq < min_seq) min_seq = gseq;
        if (gseq > max_seq) max_seq = gseq;
    }
    if (count == 0) return;

    static const char header[] = "--- crash ring ---\n";
    zcsr_log_crash_raw(header, sizeof(header) - 1);
    unsigned s = min_seq;
    while (1) {
        for (int i = 0; i < count; i++) {
            if (entries[i].seq == s) {
                zcsr_log_crash_raw(entries[i].msg, zcsr_log_strlen(entries[i].msg));
                break;
            }
        }
        if (s == max_seq) break;
        s += 1u;
    }

    intptr_t h = atomic_load_explicit(&zcsr_log_crash_handle,
                                      memory_order_acquire);
    if (h >= 0) (void)zcsr_log_sync_handle(h);
}

bool zcsr_log_set_crash_file(const char* path) {
    if (path == NULL || path[0] == '\0') return false;
    if (strlen(path) >= ZCSR_LOG_PATH_MAX) return false;

    call_once(&zcsr_log_once, zcsr_log_init);
    intptr_t h = zcsr_log_open_append(path);
    if (h < 0) return false;

    intptr_t old = atomic_exchange_explicit(&zcsr_log_crash_handle, h,
                                            memory_order_acq_rel);
    if (old >= 0) zcsr_log_close_handle(old);
    atomic_store_explicit(&zcsr_log_crash_enabled, true, memory_order_release);
    return true;
}

void zcsr_log_clear_crash_file(void) {
    call_once(&zcsr_log_once, zcsr_log_init);
    atomic_store_explicit(&zcsr_log_crash_enabled, false, memory_order_release);
    intptr_t h = atomic_exchange_explicit(&zcsr_log_crash_handle, -1,
                                          memory_order_acq_rel);
    if (h >= 0) zcsr_log_close_handle(h);

    for (int i = 0; i < ZCSR_LOG_RING_SLOTS; i++) {
        atomic_store_explicit(&zcsr_log_ring[i].seq, 0, memory_order_relaxed);
        atomic_store_explicit(&zcsr_log_ring[i].msg_seq, 0, memory_order_relaxed);
        zcsr_log_ring[i].msg[0] = '\0';
    }
    atomic_store_explicit(&zcsr_log_ring_head, 0, memory_order_relaxed);
    atomic_store_explicit(&zcsr_log_ring_seq, 0, memory_order_relaxed);
}

/* ------------------------------------------------------------ default sink */
static void zcsr_log_default(
    zcsr_log_level level,
    const char* tag,
    const char* msg,
    void* user)
{
    (void)user;
#if defined(__ANDROID__)
    int prio;
    switch (level) {
        case ZCSR_LOG_TRACE: prio = ANDROID_LOG_VERBOSE; break;
        case ZCSR_LOG_DEBUG: prio = ANDROID_LOG_DEBUG;   break;
        case ZCSR_LOG_INFO:  prio = ANDROID_LOG_INFO;    break;
        case ZCSR_LOG_WARN:  prio = ANDROID_LOG_WARN;    break;
        case ZCSR_LOG_ERROR: prio = ANDROID_LOG_ERROR;   break;
        case ZCSR_LOG_FATAL: prio = ANDROID_LOG_FATAL;   break;
        default:             prio = ANDROID_LOG_DEFAULT; break;
    }
    __android_log_print(prio, tag, "%s", msg);
#elif defined(__APPLE__)
    os_log_type_t type;
    switch (level) {
        case ZCSR_LOG_TRACE:
        case ZCSR_LOG_DEBUG: type = OS_LOG_TYPE_DEBUG;   break;
        case ZCSR_LOG_INFO:  type = OS_LOG_TYPE_INFO;    break;
        case ZCSR_LOG_WARN:  type = OS_LOG_TYPE_DEFAULT; break;
        case ZCSR_LOG_ERROR: type = OS_LOG_TYPE_ERROR;   break;
        case ZCSR_LOG_FATAL: type = OS_LOG_TYPE_FAULT;   break;
        default:             type = OS_LOG_TYPE_DEFAULT; break;
    }
    /* Literal format is required by os_log; dynamic strings are arguments. */
    os_log_with_type(OS_LOG_DEFAULT, type,
                     "[%{public}s][%{public}s] %{public}s",
                     zcsr_log_level_tag(level), tag, msg);
#else
    /* Canonical line: [LEVEL][TAG] [FUNC:LINE] message
     * (diagnostics.h already prefixes [FUNC:LINE]; direct callers may omit it). */
    char line[ZCSR_LOG_BUF_SIZE];
    size_t len = zcsr_log_format_line(level, tag, msg, line, sizeof(line));
    if (len == 0) return;

#if defined(_WIN32)
    /* Windows console: write UTF-16 through WriteConsoleW so Chinese never
     * depends on the console code page. Redirected stderr keeps bytes. */
    HANDLE h = GetStdHandle(STD_ERROR_HANDLE);
    DWORD mode = 0;
    bool is_console = (h != NULL && h != INVALID_HANDLE_VALUE &&
                       GetConsoleMode(h, &mode));
    if (is_console) {
        wchar_t wline[ZCSR_LOG_BUF_SIZE];
        int wlen = MultiByteToWideChar(CP_UTF8, 0, line, -1, wline,
                                       (int)ZCSR_LOG_BUF_SIZE);
        fflush(stderr);
        if (wlen > 0) {
            DWORD written = 0;
            if (WriteConsoleW(h, wline, (DWORD)(wlen - 1), &written, NULL)) {
                /* ODS still gets a copy when a debugger is attached. */
            } else {
                fputs(line, stderr);
                fflush(stderr);
            }
        } else {
            fputs(line, stderr);
            fflush(stderr);
        }
    } else {
        fputs(line, stderr);
        fflush(stderr);
    }

    if (IsDebuggerPresent()) {
        wchar_t wline[ZCSR_LOG_BUF_SIZE];
        int wlen = MultiByteToWideChar(CP_UTF8, 0, line, -1, wline,
                                       (int)ZCSR_LOG_BUF_SIZE);
        if (wlen > 0) OutputDebugStringW(wline);
        else OutputDebugStringA(line);
    }
#else
    fputs(line, stderr);
    fflush(stderr);
#endif
#endif
}

/* ------------------------------------------------------------- crash ring */
static void zcsr_log_ring_push(zcsr_log_level level, const char* tag, const char* msg) {
    if (!atomic_load_explicit(&zcsr_log_crash_enabled, memory_order_acquire)) {
        return;
    }
    unsigned index = atomic_fetch_add_explicit(&zcsr_log_ring_head, 1u,
                                               memory_order_relaxed) %
                     ZCSR_LOG_RING_SLOTS;
    unsigned gseq = atomic_fetch_add_explicit(&zcsr_log_ring_seq, 1u,
                                              memory_order_relaxed) + 1u;
    zcsr_log_ring_slot* slot = &zcsr_log_ring[index];
    /* Claim the slot only from an even (stable) sequence. With more than 64
     * concurrent writers two writers can target the same overwritten slot;
     * the loser drops this message instead of interleaving a second body into
     * the slot. A writer that dies while the slot is odd leaves a slot the
     * flush path skips — acceptable best-effort crash behaviour. */
    unsigned seq = atomic_load_explicit(&slot->seq, memory_order_relaxed);
    if ((seq & 1u) != 0u) return;
    if (!atomic_compare_exchange_strong_explicit(
            &slot->seq, &seq, seq + 1u,
            memory_order_acq_rel, memory_order_relaxed)) {
        return;
    }
    atomic_store_explicit(&slot->msg_seq, gseq, memory_order_relaxed);
    int n = snprintf(slot->msg, sizeof(slot->msg), "[%s][%s] %s\n",
                     zcsr_log_level_tag(level), tag, msg);
    if (n < 0) slot->msg[0] = '\0';
    atomic_store_explicit(&slot->seq, seq + 2u, memory_order_release);
}

/* --------------------------------------------------------------- dispatch */
static void zcsr_log_dispatch(zcsr_log_level level, const char* tag, const char* msg) {
    zcsr_log_ring_push(level, tag, msg);
    zcsr_log_file_write(level, tag, msg);

    zcsr_log_fn handler = (zcsr_log_fn)atomic_load_explicit(
        &zcsr_log_handler_bits, memory_order_acquire);
    void* user = (void*)atomic_load_explicit(
        &zcsr_log_user_bits, memory_order_acquire);

    if (handler != NULL && zcsr_log_in_handler == 0) {
        zcsr_log_in_handler = 1;
        handler(level, tag, msg, user);
        zcsr_log_in_handler = 0;
    } else {
        zcsr_log_default(level, tag, msg, NULL);
    }
}

/* -------------------------------------------------------------- public API */
void zcsr_log_set_handler(zcsr_log_fn handler, void* user) {
    call_once(&zcsr_log_once, zcsr_log_init);
    mtx_lock(&zcsr_log_lock);
    atomic_store_explicit(&zcsr_log_user_bits, (uintptr_t)user,
                          memory_order_relaxed);
    atomic_store_explicit(&zcsr_log_handler_bits, (uintptr_t)handler,
                          memory_order_release);
    mtx_unlock(&zcsr_log_lock);
}

void zcsr_log_set_level(zcsr_log_level level) {
    if (level < ZCSR_LOG_TRACE || level > ZCSR_LOG_FATAL) {
        return; /* keep current setting */
    }
    call_once(&zcsr_log_once, zcsr_log_init);
    atomic_store_explicit(&zcsr_log_min_level, (int)level, memory_order_relaxed);
}

zcsr_log_level zcsr_log_get_level(void) {
    call_once(&zcsr_log_once, zcsr_log_init);
    return (zcsr_log_level)atomic_load_explicit(&zcsr_log_min_level,
                                                memory_order_relaxed);
}

bool zcsr_log_enabled(zcsr_log_level level, const char* tag) {
    if (level < ZCSR_LOG_TRACE || level > ZCSR_LOG_FATAL) return false;

    const char* safe_tag = (tag != NULL) ? tag : ZCSR_LOG_DEFAULT_TAG;
    int slot = zcsr_log_find_tag(safe_tag);
    if (slot >= 0) {
        int tag_level = atomic_load_explicit(&zcsr_log_tags[slot].level,
                                             memory_order_relaxed);
        /* A registered tag with a concrete level is the effective minimum:
         * it may be stricter or more verbose than the global default. */
        if (tag_level >= 0) return (int)level >= tag_level;
    }

    int global_level = atomic_load_explicit(&zcsr_log_min_level,
                                            memory_order_relaxed);
    return (int)level >= global_level;
}

bool zcsr_log_set_tag_level(const char* tag, zcsr_log_level level) {
    if (tag == NULL || level < ZCSR_LOG_TRACE || level > ZCSR_LOG_FATAL) {
        return false;
    }
    size_t len = strlen(tag);
    if (len == 0 || len >= ZCSR_LOG_TAG_NAME_MAX) return false;

    call_once(&zcsr_log_once, zcsr_log_init);
    mtx_lock(&zcsr_log_lock);
    int slot = zcsr_log_find_tag(tag);
    if (slot < 0) {
        int count = atomic_load_explicit(&zcsr_log_tag_count,
                                         memory_order_relaxed);
        if (count >= ZCSR_LOG_MAX_TAGS) {
            mtx_unlock(&zcsr_log_lock);
            return false;
        }
        slot = count;
        memcpy(zcsr_log_tags[slot].name, tag, len + 1);
        atomic_store_explicit(&zcsr_log_tags[slot].level, (int)level,
                              memory_order_relaxed);
        /* Publish name + level before the count becomes visible. */
        atomic_store_explicit(&zcsr_log_tag_count, count + 1,
                              memory_order_release);
    } else {
        atomic_store_explicit(&zcsr_log_tags[slot].level, (int)level,
                              memory_order_release);
    }
    mtx_unlock(&zcsr_log_lock);
    return true;
}

bool zcsr_log_get_tag_level(const char* tag, zcsr_log_level* out_level) {
    if (tag == NULL) return false;
    int slot = zcsr_log_find_tag(tag);
    if (slot < 0) return false;
    int value = atomic_load_explicit(&zcsr_log_tags[slot].level,
                                     memory_order_relaxed);
    if (value < 0) return false;
    if (out_level != NULL) *out_level = (zcsr_log_level)value;
    return true;
}

void zcsr_log_reset_tag_levels(void) {
    call_once(&zcsr_log_once, zcsr_log_init);
    mtx_lock(&zcsr_log_lock);
    int count = atomic_load_explicit(&zcsr_log_tag_count, memory_order_relaxed);
    for (int i = 0; i < count; i++) {
        atomic_store_explicit(&zcsr_log_tags[i].level, -1, memory_order_relaxed);
    }
    mtx_unlock(&zcsr_log_lock);
}

uint64_t zcsr_log_now_ms(void) {
#if defined(_WIN32)
    return (uint64_t)GetTickCount64();
#else
#  if defined(CLOCK_MONOTONIC)
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {
        return (uint64_t)ts.tv_sec * 1000u +
               (uint64_t)(ts.tv_nsec / 1000000);
    }
    return 0;
#  else
    struct timespec ts;
    if (timespec_get(&ts, TIME_UTC) == TIME_UTC) {
        return (uint64_t)ts.tv_sec * 1000u +
               (uint64_t)(ts.tv_nsec / 1000000);
    }
    return 0;
#  endif
#endif
}

void zcsr_log_write(
    zcsr_log_level level,
    const char* tag,
    const char* fmt,
    ...)
{
    if (fmt == NULL) return;
    if (!zcsr_log_enabled(level, tag)) return;

    call_once(&zcsr_log_once, zcsr_log_init);
    const char* safe_tag = (tag != NULL) ? tag : ZCSR_LOG_DEFAULT_TAG;

    char buf[ZCSR_LOG_BUF_SIZE];
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    if (n < 0) {
        buf[0] = '\0';
    } else if (n >= ZCSR_LOG_BUF_SIZE) {
        /* vsnprintf already NUL-terminates at buf[2047]; guarantee it explicitly. */
        buf[ZCSR_LOG_BUF_SIZE - 1] = '\0';
    }

    zcsr_log_dispatch(level, safe_tag, buf);
}

/* ------------------------------------------------------- structured events */
/* Reserve enough room to close the fields object, append a truncation marker
 * when needed, and close the outer object without producing partial JSON. */
#define ZCSR_JSON_TAIL_RESERVE 40

typedef struct {
    char* buf;
    size_t cap;
    size_t len;
    bool truncated;
} zcsr_json_buf;

static void zcsr_json_putc(zcsr_json_buf* b, char c) {
    if (b->len + 1 < b->cap) {
        b->buf[b->len++] = c;
    } else {
        b->truncated = true;
    }
}

static void zcsr_json_raw(zcsr_json_buf* b, const char* s) {
    while (s != NULL && *s != '\0') zcsr_json_putc(b, *s++);
}

static void zcsr_json_escape_byte(zcsr_json_buf* b, unsigned char c) {
    static const char hex[] = "0123456789abcdef";
    char esc[7];
    esc[0] = '\\';
    esc[1] = 'u';
    esc[2] = '0';
    esc[3] = '0';
    esc[4] = hex[(c >> 4) & 0xf];
    esc[5] = hex[c & 0xf];
    esc[6] = '\0';
    zcsr_json_raw(b, esc);
}

/* Returns 2/3/4 for a valid UTF-8 sequence starting at p, 0 otherwise. Invalid
 * bytes are escaped as \u00XX by the caller so a malformed C string can never
 * turn the whole event into invalid JSON. */
static size_t zcsr_json_utf8_len(const unsigned char* p) {
    unsigned char c = p[0];
    if (c < 0x80) return 1;
    if (c >= 0xc2 && c <= 0xdf) {
        return (p[1] & 0xc0) == 0x80 ? 2 : 0;
    }
    if (c >= 0xe0 && c <= 0xef) {
        if (p[1] == '\0' || p[2] == '\0') return 0;
        if ((p[1] & 0xc0) != 0x80 || (p[2] & 0xc0) != 0x80) return 0;
        if (c == 0xe0 && p[1] < 0xa0) return 0;       /* overlong */
        if (c == 0xed && p[1] >= 0xa0) return 0;      /* surrogate */
        return 3;
    }
    if (c >= 0xf0 && c <= 0xf4) {
        if (p[1] == '\0' || p[2] == '\0' || p[3] == '\0') return 0;
        if ((p[1] & 0xc0) != 0x80 || (p[2] & 0xc0) != 0x80 ||
            (p[3] & 0xc0) != 0x80) return 0;
        if (c == 0xf0 && p[1] < 0x90) return 0;       /* overlong */
        if (c == 0xf4 && p[1] >= 0x90) return 0;      /* > U+10FFFF */
        return 4;
    }
    return 0;
}

static void zcsr_json_string(zcsr_json_buf* b, const char* s) {
    if (s == NULL) {
        zcsr_json_raw(b, "null");
        return;
    }
    zcsr_json_putc(b, '"');
    for (const char* p = s; *p != '\0'; p++) {
        unsigned char c = (unsigned char)*p;
        switch (c) {
            case '"':  zcsr_json_raw(b, "\\\""); break;
            case '\\': zcsr_json_raw(b, "\\\\"); break;
            case '\b': zcsr_json_raw(b, "\\b");  break;
            case '\f': zcsr_json_raw(b, "\\f");  break;
            case '\n': zcsr_json_raw(b, "\\n");  break;
            case '\r': zcsr_json_raw(b, "\\r");  break;
            case '\t': zcsr_json_raw(b, "\\t");  break;
            default:
                if (c < 0x20) {
                    zcsr_json_escape_byte(b, c);
                } else if (c < 0x80) {
                    zcsr_json_putc(b, (char)c);
                } else {
                    size_t n = zcsr_json_utf8_len((const unsigned char*)p);
                    if (n == 0) {
                        zcsr_json_escape_byte(b, c);
                    } else {
                        for (size_t i = 0; i < n; i++) {
                            zcsr_json_putc(b, p[i]);
                        }
                        p += n - 1;
                    }
                }
                break;
        }
    }
    zcsr_json_putc(b, '"');
}

static void zcsr_json_u64(zcsr_json_buf* b, uint64_t v) {
    char tmp[32];
    snprintf(tmp, sizeof(tmp), "%" PRIu64, v);
    zcsr_json_raw(b, tmp);
}

static void zcsr_json_i64(zcsr_json_buf* b, int64_t v) {
    char tmp[32];
    snprintf(tmp, sizeof(tmp), "%" PRId64, v);
    zcsr_json_raw(b, tmp);
}

static void zcsr_json_f64(zcsr_json_buf* b, double v) {
    /* JSON has no NaN/Inf. Values outside +/-1e15 are also emitted as null to
     * avoid integer overflow in the locale-independent fixed-point below. */
    if (!(v >= -1e15 && v <= 1e15)) {
        zcsr_json_raw(b, "null");
        return;
    }
    bool neg = v < 0;
    if (neg) v = -v;
    uint64_t ip = (uint64_t)v;
    uint32_t fp = (uint32_t)((v - (double)ip) * 1000000.0 + 0.5);
    if (fp >= 1000000u) {
        ip++;
        fp = 0;
    }
    char tmp[64];
    snprintf(tmp, sizeof(tmp), "%s%llu.%06u",
             neg ? "-" : "", (unsigned long long)ip, fp);
    zcsr_json_raw(b, tmp);
}

static void zcsr_json_ptr(zcsr_json_buf* b, const void* p) {
    char tmp[32];
    snprintf(tmp, sizeof(tmp), "0x%" PRIxPTR, (uintptr_t)p);
    /* JSON has no hexadecimal number literal. Keep the readable 0x form, but as
     * a string value; an unquoted 0x... would make the whole event invalid JSON. */
    zcsr_json_putc(b, '"');
    zcsr_json_raw(b, tmp);
    zcsr_json_putc(b, '"');
}

static uint64_t zcsr_log_thread_id(void) {
#if defined(_WIN32)
    return (uint64_t)GetCurrentThreadId();
#elif defined(__linux__) || defined(__APPLE__) || defined(__ANDROID__)
    /* C11 only guarantees that thrd_t identifies a thread; it does not promise
     * an integer representation. On the supported POSIX targets pthread_self()
     * is an integer or a pointer, both of which convert to uintptr_t. */
    return (uint64_t)(uintptr_t)pthread_self();
#else
    return (uint64_t)(uintptr_t)thrd_current();
#endif
}

void zcsr_log_event(
    zcsr_log_level level,
    const char* tag,
    uint32_t event_id,
    const zcsr_log_field* fields,
    size_t count)
{
    if (level < ZCSR_LOG_TRACE || level > ZCSR_LOG_FATAL) return;
    if (count > 0 && fields == NULL) return;
    if (!zcsr_log_enabled(level, tag)) return;

    call_once(&zcsr_log_once, zcsr_log_init);
    const char* safe_tag = (tag != NULL) ? tag : ZCSR_LOG_DEFAULT_TAG;

    char json[ZCSR_LOG_BUF_SIZE];
    zcsr_json_buf b = { json, sizeof(json), 0, false };

    /* Tag is caller-controlled. Render it into a small scratch first so a
     * pathological tag cannot leave an unterminated string in `json`. */
    char tag_json[256];
    zcsr_json_buf tag_buf = { tag_json, sizeof(tag_json), 0, false };
    zcsr_json_string(&tag_buf, safe_tag);
    if (tag_buf.truncated) {
        tag_buf.len = 0;
        tag_buf.truncated = false;
        zcsr_json_string(&tag_buf, "...");
    }
    /* zcsr_json_string() does not add a terminator, and zcsr_json_raw() below
     * reads the scratch as a C string: terminate explicitly, otherwise
     * uninitialized stack bytes leak into the event line and the JSON is
     * invalid (strstr-based tests would not notice). */
    tag_json[tag_buf.len < sizeof(tag_json) ? tag_buf.len : sizeof(tag_json) - 1] = '\0';

    zcsr_json_raw(&b, "{\"ts\":");
    zcsr_json_u64(&b, zcsr_log_now_ms());
    zcsr_json_raw(&b, ",\"tid\":");
    zcsr_json_u64(&b, zcsr_log_thread_id());
    zcsr_json_raw(&b, ",\"level\":\"");
    zcsr_json_raw(&b, zcsr_log_level_tag(level));
    zcsr_json_raw(&b, "\",\"tag\":");
    zcsr_json_raw(&b, tag_json);
    zcsr_json_raw(&b, ",\"event\":");
    zcsr_json_u64(&b, (uint64_t)event_id);
    zcsr_json_raw(&b, ",\"fields\":{");

    size_t fields_written = 0;
    bool fields_truncated = false;
    for (size_t i = 0; i < count; i++) {
        size_t mark = b.len;
        if (b.cap - b.len < ZCSR_JSON_TAIL_RESERVE) {
            fields_truncated = true;
            break;
        }
        if (fields_written != 0) zcsr_json_raw(&b, ",");
        zcsr_json_string(&b, fields[i].key != NULL ? fields[i].key : "?");
        zcsr_json_putc(&b, ':');
        switch (fields[i].type) {
            case ZCSR_LOG_FIELD_I64:
                zcsr_json_i64(&b, fields[i].v.i64);
                break;
            case ZCSR_LOG_FIELD_U64:
                zcsr_json_u64(&b, fields[i].v.u64);
                break;
            case ZCSR_LOG_FIELD_F64:
                zcsr_json_f64(&b, fields[i].v.f64);
                break;
            case ZCSR_LOG_FIELD_STR:
                zcsr_json_string(&b, fields[i].v.str);
                break;
            case ZCSR_LOG_FIELD_BOOL:
                zcsr_json_raw(&b, fields[i].v.b ? "true" : "false");
                break;
            case ZCSR_LOG_FIELD_PTR:
                zcsr_json_ptr(&b, fields[i].v.ptr);
                break;
            default:
                zcsr_json_raw(&b, "null");
                break;
        }
        if (b.truncated || b.cap - b.len < ZCSR_JSON_TAIL_RESERVE) {
            /* Discard the incomplete field and close the object with an
             * explicit marker; this is valid JSON, not a partial string. */
            b.len = mark;
            b.truncated = false;
            fields_truncated = true;
            break;
        }
        fields_written++;
    }

    if (fields_truncated) {
        zcsr_json_raw(&b, fields_written == 0
                              ? "\"truncated\":true"
                              : ",\"truncated\":true");
    }
    zcsr_json_raw(&b, "}}");
    json[b.len < b.cap ? b.len : b.cap - 1] = '\0';

    zcsr_log_dispatch(level, safe_tag, json);
}
