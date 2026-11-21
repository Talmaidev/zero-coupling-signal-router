#include "zcsr/log.h"
#include "zcsr/diagnostics.h"

#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <threads.h>
#include <time.h>

/* TSan cannot run C11 thrd_create on this glibc/toolchain combination
 * (__tsan_func_entry crashes before the thread body runs), so POSIX uses
 * pthread directly. Windows keeps thrd_create for its C11 runtime. The
 * function signature changes with the API, hence the macros. */
#if defined(_WIN32)
typedef thrd_t zcsr_test_thread;
typedef int (*zcsr_test_thread_fn)(void*);
#  define ZCSR_TEST_THREAD_RET  int
#  define ZCSR_TEST_THREAD_DONE 0
static void zcsr_test_thread_create(zcsr_test_thread* t, zcsr_test_thread_fn fn, void* arg) {
    (void)thrd_create(t, fn, arg);
}
static void zcsr_test_thread_join(zcsr_test_thread t) { (void)thrd_join(t, NULL); }
#else
#include <pthread.h>
typedef pthread_t zcsr_test_thread;
typedef void* (*zcsr_test_thread_fn)(void*);
#  define ZCSR_TEST_THREAD_RET  void*
#  define ZCSR_TEST_THREAD_DONE NULL
static void zcsr_test_thread_create(zcsr_test_thread* t, zcsr_test_thread_fn fn, void* arg) {
    (void)pthread_create(t, NULL, fn, arg);
}
static void zcsr_test_thread_join(zcsr_test_thread t) { (void)pthread_join(t, NULL); }
#endif

static int fail_if(int condition) {
    return condition ? 1 : 0;
}

/* --- Single-threaded collector handler --- */
static zcsr_log_level g_last_level;
static char g_last_tag[64];
static char g_last_msg[2048];
static int g_collect_count;

static void collector(zcsr_log_level level, const char* tag,
                      const char* msg, void* user) {
    (void)user;
    g_collect_count++;
    g_last_level = level;
    strncpy(g_last_tag, tag, sizeof(g_last_tag) - 1);
    g_last_tag[sizeof(g_last_tag) - 1] = '\0';
    strncpy(g_last_msg, msg, sizeof(g_last_msg) - 1);
    g_last_msg[sizeof(g_last_msg) - 1] = '\0';
}

static void reset_collector(void) {
    g_last_level = ZCSR_LOG_TRACE;
    g_last_tag[0] = '\0';
    g_last_msg[0] = '\0';
    g_collect_count = 0;
}

/* --- Reentrant handler --- */
static int g_depth;
static int g_max_depth;

static void reentrant_handler(zcsr_log_level level, const char* tag,
                              const char* msg, void* user) {
    (void)level; (void)tag; (void)msg; (void)user;
    g_depth++;
    if (g_depth > g_max_depth) g_max_depth = g_depth;
    if (g_depth == 1) {
        /* Nested log must be routed to the default sink, not back here. */
        zcsr_log_write(ZCSR_LOG_ERROR, "nest", "nested from handler");
    }
    g_depth--;
}

/* --- Concurrency --- */
static atomic_int g_counting_count;

static void counting_handler(zcsr_log_level level, const char* tag,
                             const char* msg, void* user) {
    (void)level; (void)tag; (void)msg; (void)user;
    atomic_fetch_add_explicit(&g_counting_count, 1, memory_order_relaxed);
}

enum { N_WRITERS = 8, WRITER_ITERS = 10000 };

static ZCSR_TEST_THREAD_RET writer_thread(void* arg) {
    int id = (int)(intptr_t)arg;
    char tag[16];
    snprintf(tag, sizeof(tag), "t%d", id);
    for (int i = 0; i < WRITER_ITERS; i++) {
        zcsr_log_write((i & 1) ? ZCSR_LOG_INFO : ZCSR_LOG_WARN,
                       tag, "message %d %d", id, i);
    }
    return ZCSR_TEST_THREAD_DONE;
}

static ZCSR_TEST_THREAD_RET tag_writer_thread(void* arg) {
    int id = (int)(intptr_t)arg;
    const char* tag = (id & 1) ? "audio" : "ui";
    for (int i = 0; i < WRITER_ITERS; i++) {
        zcsr_log_write(ZCSR_LOG_INFO, tag, "tag message %d %d", id, i);
    }
    return ZCSR_TEST_THREAD_DONE;
}

static ZCSR_TEST_THREAD_RET config_thread(void* arg) {
    (void)arg;
    for (int n = 0; n < 2000; n++) {
        zcsr_log_set_level((n & 1) ? ZCSR_LOG_DEBUG : ZCSR_LOG_WARN);
        zcsr_log_set_handler(counting_handler, NULL);
    }
    return ZCSR_TEST_THREAD_DONE;
}

static ZCSR_TEST_THREAD_RET tag_config_thread(void* arg) {
    (void)arg;
    for (int n = 0; n < 2000; n++) {
        zcsr_log_set_tag_level("audio", (n & 1) ? ZCSR_LOG_WARN : ZCSR_LOG_INFO);
        zcsr_log_set_tag_level("ui", (n & 1) ? ZCSR_LOG_INFO : ZCSR_LOG_WARN);
    }
    return ZCSR_TEST_THREAD_DONE;
}

static void every_ms_100(void) {
    ZCSR_LOG_EVERY_MS(ZCSR_LOG_INFO, "rate", 100, "every ms");
}

static void every_ms_zero(void) {
    ZCSR_LOG_EVERY_MS(ZCSR_LOG_INFO, "rate", 0, "never ms");
}

#define CHECK(cond, code) do { if (fail_if(cond)) { \
    printf("test_log FAIL at line %d: code %d (%s)\n", __LINE__, (code), #cond); \
    return (code); } } while (0)

int main(void) {
    /* Default level is INFO: below-level messages are dropped once a handler
     * is installed. */
    zcsr_log_set_level(ZCSR_LOG_INFO);
    zcsr_log_set_handler(collector, NULL);
    reset_collector();

    zcsr_log_write(ZCSR_LOG_DEBUG, "t", "should be filtered");
    CHECK(g_collect_count != 0, 1);
    zcsr_log_write(ZCSR_LOG_TRACE, "t", "should be filtered");
    CHECK(g_collect_count != 0, 2);
    zcsr_log_write(ZCSR_LOG_INFO, "t", "visible info");
    CHECK(g_collect_count != 1, 3);
    CHECK(g_last_level != ZCSR_LOG_INFO, 4);
    CHECK(strcmp(g_last_msg, "visible info") != 0, 5);
    zcsr_log_write(ZCSR_LOG_FATAL, "t", "visible fatal");
    CHECK(g_last_level != ZCSR_LOG_FATAL, 6);

    /* Raising the minimum level to WARN drops INFO/DEBUG/TRACE. */
    zcsr_log_set_level(ZCSR_LOG_WARN);
    reset_collector();
    zcsr_log_write(ZCSR_LOG_INFO, "t", "below warn");
    CHECK(g_collect_count != 0, 7);
    zcsr_log_write(ZCSR_LOG_WARN, "t", "at warn");
    CHECK(g_collect_count != 1, 8);
    zcsr_log_write(ZCSR_LOG_ERROR, "t", "above warn");
    CHECK(g_collect_count != 2, 9);

    /* Lowering to DEBUG admits DEBUG. */
    zcsr_log_set_level(ZCSR_LOG_DEBUG);
    reset_collector();
    zcsr_log_write(ZCSR_LOG_DEBUG, "t", "debug now visible");
    CHECK(g_collect_count != 1, 10);
    /* TRACE is still filtered at DEBUG. */
    zcsr_log_write(ZCSR_LOG_TRACE, "t", "trace still filtered");
    CHECK(g_collect_count != 1, 11);
    zcsr_log_set_level(ZCSR_LOG_TRACE);
    zcsr_log_write(ZCSR_LOG_TRACE, "t", "trace visible");
    CHECK(g_collect_count != 2, 12);

    /* NULL tag uses the stable default. */
    zcsr_log_set_level(ZCSR_LOG_INFO);
    reset_collector();
    zcsr_log_write(ZCSR_LOG_INFO, NULL, "default tag");
    CHECK(strcmp(g_last_tag, "zcsr") != 0, 13);

    /* NULL fmt is safe and emits nothing. */
    reset_collector();
    zcsr_log_write(ZCSR_LOG_FATAL, "t", NULL);
    CHECK(g_collect_count != 0, 14);

    /* Out-of-range levels are ignored. */
    reset_collector();
    zcsr_log_write((zcsr_log_level)99, "t", "bad level");
    zcsr_log_write((zcsr_log_level)(-1), "t", "bad level neg");
    CHECK(g_collect_count != 0, 15);

    /* Out-of-range set_level keeps the current setting. */
    zcsr_log_set_level(ZCSR_LOG_INFO);
    zcsr_log_set_level((zcsr_log_level)42);
    reset_collector();
    zcsr_log_write(ZCSR_LOG_DEBUG, "t", "still filtered");
    CHECK(g_collect_count != 0, 16);

    /* Handler NULL restores the default sink (collector must not be called). */
    reset_collector();
    zcsr_log_set_handler(NULL, NULL);
    zcsr_log_write(ZCSR_LOG_INFO, "t", "goes to default sink");
    CHECK(g_collect_count != 0, 17);

    /* Reinstall and verify the custom handler works again. */
    zcsr_log_set_handler(collector, NULL);
    reset_collector();
    zcsr_log_write(ZCSR_LOG_INFO, "t", "custom again");
    CHECK(g_collect_count != 1, 18);

    /* Long message truncation: NUL-terminated and at most 2047 chars. */
    {
        char big[4096];
        memset(big, 'x', sizeof(big));
        big[sizeof(big) - 1] = '\0';
        zcsr_log_set_level(ZCSR_LOG_INFO);
        zcsr_log_set_handler(collector, NULL);
        reset_collector();
        zcsr_log_write(ZCSR_LOG_INFO, "big", "%s", big);
        size_t len = strlen(g_last_msg);
        CHECK(len > 2047, 19);   /* truncated to 2047 */
        CHECK(len != 2047, 20);
        CHECK(g_last_msg[len] != '\0', 21);
        CHECK(g_last_msg[2047] != '\0', 22);
        CHECK(g_last_msg[0] != 'x', 23);
    }

    /* Reentrant handler: nested log does not re-enter the same handler. */
    zcsr_log_set_handler(reentrant_handler, NULL);
    zcsr_log_set_level(ZCSR_LOG_INFO);
    g_depth = 0;
    g_max_depth = 0;
    zcsr_log_write(ZCSR_LOG_INFO, "t", "outer");
    CHECK(g_max_depth != 1, 24);

    /* Per-tag overrides: audio at WARN, global INFO. */
    zcsr_log_set_handler(collector, NULL);
    zcsr_log_set_level(ZCSR_LOG_INFO);
    reset_collector();
    CHECK(!zcsr_log_set_tag_level("audio", ZCSR_LOG_WARN), 25);
    CHECK(zcsr_log_enabled(ZCSR_LOG_INFO, "audio"), 26);
    CHECK(!zcsr_log_enabled(ZCSR_LOG_WARN, "audio"), 27);
    zcsr_log_write(ZCSR_LOG_INFO, "audio", "filtered by tag");
    CHECK(g_collect_count != 0, 28);
    zcsr_log_write(ZCSR_LOG_WARN, "audio", "visible by tag");
    CHECK(g_collect_count != 1, 29);
    /* A tag without an override still follows the global level. */
    zcsr_log_write(ZCSR_LOG_INFO, "ui", "other tag");
    CHECK(g_collect_count != 2, 30);

    zcsr_log_level tag_level = ZCSR_LOG_TRACE;
    CHECK(!zcsr_log_get_tag_level("audio", &tag_level), 31);
    CHECK(tag_level != ZCSR_LOG_WARN, 32);
    CHECK(zcsr_log_get_tag_level("ui", &tag_level), 33);

    /* A tag override may also be more verbose than the global default. */
    CHECK(!zcsr_log_set_tag_level("verbose", ZCSR_LOG_DEBUG), 50);
    CHECK(!zcsr_log_enabled(ZCSR_LOG_DEBUG, "verbose"), 51);
    CHECK(zcsr_log_enabled(ZCSR_LOG_TRACE, "verbose"), 52);
    CHECK(!zcsr_log_get_tag_level("verbose", NULL), 53);

    zcsr_log_reset_tag_levels();
    CHECK(!zcsr_log_enabled(ZCSR_LOG_INFO, "audio"), 34);
    CHECK(zcsr_log_get_tag_level("audio", &tag_level), 35);

    /* Invalid tag arguments are rejected, not silently stored. */
    CHECK(zcsr_log_set_tag_level(NULL, ZCSR_LOG_WARN), 36);
    CHECK(zcsr_log_set_tag_level("", ZCSR_LOG_WARN), 37);
    {
        char long_tag[40];
        memset(long_tag, 'x', sizeof(long_tag));
        long_tag[sizeof(long_tag) - 1] = '\0';
        CHECK(zcsr_log_set_tag_level(long_tag, ZCSR_LOG_WARN), 38);
    }

    /* Rate-limited macros: independent counter per call site. */
    reset_collector();
    for (int i = 0; i < 10; i++) {
        ZCSR_LOG_EVERY_N(ZCSR_LOG_INFO, "rate", 3, "every %d", i);
    }
    CHECK(g_collect_count != 4, 39);

    reset_collector();
    for (int i = 0; i < 10; i++) {
        ZCSR_LOG_FIRST_N(ZCSR_LOG_INFO, "rate", 3, "first %d", i);
    }
    CHECK(g_collect_count != 3, 40);

    reset_collector();
    for (int i = 0; i < 2; i++) {
        ZCSR_LOG_ONCE(ZCSR_LOG_INFO, "once", "only once");
    }
    CHECK(g_collect_count != 1, 41);

    /* NULL tag uses the default tag for the enabled check too. */
    CHECK(!zcsr_log_enabled(ZCSR_LOG_INFO, NULL), 42);

    /* Global level readback. */
    zcsr_log_set_level(ZCSR_LOG_INFO);
    CHECK(zcsr_log_get_level() != ZCSR_LOG_INFO, 46);

    /* EVERY_MS: first call logs; an immediate second call is filtered; after
     * the interval the same call site logs again. ms <= 0 never logs. */
    reset_collector();
    zcsr_log_set_handler(collector, NULL);
    zcsr_log_set_level(ZCSR_LOG_INFO);
    every_ms_100();
    every_ms_100();
    CHECK(g_collect_count != 1, 47);
    struct timespec pause = { 0, 150000000L }; /* 150 ms */
    thrd_sleep(&pause, NULL);
    every_ms_100();
    CHECK(g_collect_count != 2, 48);
    reset_collector();
    every_ms_zero();
    every_ms_zero();
    every_ms_zero();
    CHECK(g_collect_count != 0, 49);

    /* 8 threads x 10k: no lost lines, no deadlock, no line interleaving at the
     * handler boundary (the handler only increments an atomic counter). */
    zcsr_log_set_handler(counting_handler, NULL);
    zcsr_log_set_level(ZCSR_LOG_INFO);
    atomic_store(&g_counting_count, 0);
    zcsr_test_thread writers[N_WRITERS];
    for (int i = 0; i < N_WRITERS; i++) {
        zcsr_test_thread_create(&writers[i], writer_thread, (void*)(intptr_t)i);
    }
    for (int i = 0; i < N_WRITERS; i++) {
        zcsr_test_thread_join(writers[i]);
    }
    CHECK(atomic_load(&g_counting_count) != N_WRITERS * WRITER_ITERS, 43);

    /* Concurrent config churn while writers run. Some lines are filtered by
     * the changing level; the count must stay within the upper bound and no
     * deadlock/corruption is allowed. */
    atomic_store(&g_counting_count, 0);
    zcsr_test_thread cfg;
    zcsr_test_thread_create(&cfg, config_thread, NULL);
    for (int i = 0; i < N_WRITERS; i++) {
        zcsr_test_thread_create(&writers[i], writer_thread, (void*)(intptr_t)i);
    }
    for (int i = 0; i < N_WRITERS; i++) {
        zcsr_test_thread_join(writers[i]);
    }
    zcsr_test_thread_join(cfg);
    CHECK(atomic_load(&g_counting_count) > N_WRITERS * WRITER_ITERS, 44);

    /* T12: per-tag level writes concurrent with per-tag logging. This is the
     * TSan target for the lock-free read path. */
    zcsr_log_reset_tag_levels();
    zcsr_log_set_handler(counting_handler, NULL);
    zcsr_log_set_level(ZCSR_LOG_INFO);
    atomic_store(&g_counting_count, 0);
    zcsr_test_thread tagcfg;
    zcsr_test_thread_create(&tagcfg, tag_config_thread, NULL);
    for (int i = 0; i < N_WRITERS; i++) {
        zcsr_test_thread_create(&writers[i], tag_writer_thread, (void*)(intptr_t)i);
    }
    for (int i = 0; i < N_WRITERS; i++) {
        zcsr_test_thread_join(writers[i]);
    }
    zcsr_test_thread_join(tagcfg);
    CHECK(atomic_load(&g_counting_count) == 0, 45);
    zcsr_log_reset_tag_levels();

    /* Restore a sane default state for later test runs. */
    zcsr_log_set_handler(NULL, NULL);
    zcsr_log_set_level(ZCSR_LOG_INFO);

    printf("test_log: all checks passed\n");
    return 0;
}
