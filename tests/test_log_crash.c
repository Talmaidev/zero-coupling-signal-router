/* P3 crash sink: pre-opened file, last-64 ring, raw crash line, ordered dump. */
#include "zcsr/log.h"

#include <stdio.h>
#include <string.h>

static void noop_handler(zcsr_log_level level, const char* tag,
                         const char* msg, void* user) {
    (void)level; (void)tag; (void)msg; (void)user;
}

int main(void) {
    remove("log_crash_test.log");
    zcsr_log_set_handler(noop_handler, NULL);
    zcsr_log_set_level(ZCSR_LOG_INFO);

    if (zcsr_log_set_crash_file("no_such_dir/crash.log")) return 1;
    if (zcsr_log_set_crash_file("log_crash_test.log") == 0) return 2;

    for (int i = 0; i < 100; i++) {
        zcsr_log_write(ZCSR_LOG_INFO, "crash", "ring message %03d", i);
    }
    zcsr_log_crash_write("crash", "direct crash line");
    zcsr_log_crash_write(NULL, NULL);
    zcsr_log_crash_flush();
    zcsr_log_clear_crash_file();
    zcsr_log_clear_crash_file(); /* idempotent */

    FILE* f = fopen("log_crash_test.log", "rb");
    if (f == NULL) return 3;
    char buf[16384];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    if (n == 0) return 4;
    buf[n] = '\0';

    if (strstr(buf, "--- crash ring ---") == NULL) return 5;
    if (strstr(buf, "direct crash line") == NULL) return 6;
    /* 100 messages, ring keeps the last 64: 036..099. */
    if (strstr(buf, "ring message 036") == NULL) return 7;
    if (strstr(buf, "ring message 099") == NULL) return 8;
    if (strstr(buf, "ring message 035") != NULL) return 9;

    /* Chronological dump: 036 must appear before 099. */
    const char* p036 = strstr(buf, "ring message 036");
    const char* p099 = strstr(buf, "ring message 099");
    if (p036 == NULL || p099 == NULL || p099 < p036) return 10;

    remove("log_crash_test.log");
    zcsr_log_set_handler(NULL, NULL);
    printf("test_log_crash: PASS\n");
    return 0;
}
