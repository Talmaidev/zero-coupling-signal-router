#include "zcsr/log.h"

#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <threads.h>

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
static atomic_int g_atomic_count;
static atomic_int g_stop_threads;

static int writer_thread(void* arg) {
    int id = (int)(intptr_t)arg;
    char tag[16];
    snprintf(tag, sizeof(tag), "t%d", id);
    while (!atomic_load(&g_stop_threads)) {
        zcsr_log_write(ZCSR_LOG_INFO, tag, "message %d", id);
        atomic_fetch_add(&g_atomic_count, 1);
    }
    return 0;
}

static int config_thread(void* arg) {
    (void)arg;
    int n = 0;
    while (!atomic_load(&g_stop_threads) && n < 2000) {
        zcsr_log_set_level((n & 1) ? ZCSR_LOG_DEBUG : ZCSR_LOG_WARN);
        zcsr_log_set_handler(collector, NULL);
        zcsr_log_set_handler(NULL, NULL);
        n++;
    }
    return 0;
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

    /* Concurrent writers: stable, no crash/corruption, no deadlock. */
    zcsr_log_set_handler(collector, NULL);
    zcsr_log_set_level(ZCSR_LOG_INFO);
    atomic_store(&g_atomic_count, 0);
    atomic_store(&g_stop_threads, 0);
    enum { N_WRITERS = 4, WRITER_ITERS = 2000 };
    thrd_t writers[N_WRITERS];
    for (int i = 0; i < N_WRITERS; i++) {
        thrd_create(&writers[i], writer_thread, (void*)(intptr_t)i);
    }
    /* Let writers run a bounded number of rounds, then stop. */
    int target = N_WRITERS * WRITER_ITERS;
    while (atomic_load(&g_atomic_count) < target) {
        if (atomic_load(&g_atomic_count) >= target) break;
    }
    atomic_store(&g_stop_threads, 1);
    for (int i = 0; i < N_WRITERS; i++) {
        thrd_join(writers[i], NULL);
    }
    CHECK(atomic_load(&g_atomic_count) < target, 25);

    /* Concurrent config churn while writers run. */
    atomic_store(&g_atomic_count, 0);
    atomic_store(&g_stop_threads, 0);
    thrd_t cfg;
    thrd_create(&cfg, config_thread, NULL);
    for (int i = 0; i < N_WRITERS; i++) {
        thrd_create(&writers[i], writer_thread, (void*)(intptr_t)i);
    }
    while (atomic_load(&g_atomic_count) < target) {
        if (atomic_load(&g_atomic_count) >= target) break;
    }
    atomic_store(&g_stop_threads, 1);
    for (int i = 0; i < N_WRITERS; i++) {
        thrd_join(writers[i], NULL);
    }
    thrd_join(cfg, NULL);
    CHECK(atomic_load(&g_atomic_count) < target, 26);

    /* Restore a sane default state for later test runs. */
    zcsr_log_set_handler(NULL, NULL);
    zcsr_log_set_level(ZCSR_LOG_INFO);

    printf("test_log: all checks passed\n");
    return 0;
}
