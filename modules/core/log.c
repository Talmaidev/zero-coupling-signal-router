/* Unified logging API — implementation.
 * Constraints: no runtime/heap allocation, per-call stack formatting, mutex
 * protected configuration, snapshot released before invoking user handler,
 * thread-local reentrancy guard.
 * Includes ONLY shared contracts. Platform default sinks are reached via
 * conditionally-included OS headers (no third-party dependencies). */
#include "zcsr/log.h"

#include <stdio.h>
#include <string.h>
#include <threads.h>

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#elif defined(__ANDROID__)
#  include <android/log.h>
#endif

#define ZCSR_LOG_BUF_SIZE 2048
#define ZCSR_LOG_DEFAULT_TAG "zcsr"

/* ------------------------------------------------------------------ config */
static mtx_t          zcsr_log_lock;
static zcsr_log_fn    zcsr_log_handler = NULL; /* NULL = platform default */
static void*          zcsr_log_user = NULL;
static zcsr_log_level zcsr_log_min_level = ZCSR_LOG_INFO;

static once_flag zcsr_log_once = ONCE_FLAG_INIT;
static void zcsr_log_init(void) {
    mtx_init(&zcsr_log_lock, mtx_plain);
}

/* Per-thread reentrancy guard: set while a user handler is running so any
 * nested log on the same thread goes to the platform default sink. */
static thread_local int zcsr_log_in_handler = 0;

/* ----------------------------------------------------------- default sink */
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
    __android_log_print(prio, "%s", tag, "%s", msg);
#else
    /* Common formatted line for stderr (Windows mirrors it to the debugger too). */
    char line[ZCSR_LOG_BUF_SIZE];
    int n = snprintf(line, sizeof(line), "[%s] %s: %s\n",
                     zcsr_log_level_tag(level), tag, msg);
    if (n < 0) {
        line[0] = '\0';
    } else if ((size_t)n >= sizeof(line)) {
        line[sizeof(line) - 1] = '\n';
    }
    fputs(line, stderr);
    fflush(stderr);
#if defined(_WIN32)
    OutputDebugStringA(line);
#endif
#endif
}

/* -------------------------------------------------------------- public API */
void zcsr_log_set_handler(zcsr_log_fn handler, void* user) {
    call_once(&zcsr_log_once, zcsr_log_init);
    mtx_lock(&zcsr_log_lock);
    zcsr_log_handler = handler;
    zcsr_log_user = user;
    mtx_unlock(&zcsr_log_lock);
}

void zcsr_log_set_level(zcsr_log_level level) {
    if (level < ZCSR_LOG_TRACE || level > ZCSR_LOG_FATAL) {
        return; /* keep current setting */
    }
    call_once(&zcsr_log_once, zcsr_log_init);
    mtx_lock(&zcsr_log_lock);
    zcsr_log_min_level = level;
    mtx_unlock(&zcsr_log_lock);
}

void zcsr_log_write(
    zcsr_log_level level,
    const char* tag,
    const char* fmt,
    ...)
{
    if (fmt == NULL) return;
    if (level < ZCSR_LOG_TRACE || level > ZCSR_LOG_FATAL) return;

    call_once(&zcsr_log_once, zcsr_log_init);

    /* Snapshot configuration under the lock, then release before dispatching. */
    mtx_lock(&zcsr_log_lock);
    zcsr_log_fn handler = zcsr_log_handler;
    void* user = zcsr_log_user;
    zcsr_log_level min_level = zcsr_log_min_level;
    mtx_unlock(&zcsr_log_lock);

    if (level < min_level) return;

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

    /* Route nested reentrant logs to the default sink; never re-enter the
     * user handler on the same thread. */
    if (handler != NULL && zcsr_log_in_handler == 0) {
        zcsr_log_in_handler = 1;
        handler(level, safe_tag, buf, user);
        zcsr_log_in_handler = 0;
    } else {
        zcsr_log_default(level, safe_tag, buf, NULL);
    }
}
