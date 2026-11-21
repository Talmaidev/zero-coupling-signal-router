/* Deterministic fuzz smoke test: random byte strings through the safe "%s"
 * entry point plus structured events. This runs on every CTest platform; the
 * opt-in libFuzzer target lives in tests/fuzz/fuzz_log.c. */
#include "zcsr/log.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int g_count;

static void handler(zcsr_log_level level, const char* tag,
                    const char* msg, void* user) {
    (void)level; (void)tag; (void)msg; (void)user;
    g_count++;
}

static uint32_t g_rng = 0x12345678u;

static uint32_t next_u32(void) {
    uint32_t x = g_rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    g_rng = x;
    return x;
}

int main(void) {
    zcsr_log_set_handler(handler, NULL);
    zcsr_log_set_level(ZCSR_LOG_TRACE);

    for (int i = 0; i < 20000; i++) {
        char buf[8192];
        size_t n = (size_t)(next_u32() % 8192u);
        for (size_t j = 0; j < n; j++) {
            buf[j] = (char)(next_u32() & 0xffu);
        }
        buf[n] = '\0';

        zcsr_log_write(ZCSR_LOG_INFO, "fuzz", "%s", buf);
        zcsr_log_field field = { "s", ZCSR_LOG_FIELD_STR, { .str = buf } };
        zcsr_log_event(ZCSR_LOG_INFO, "fuzz", (uint32_t)i, &field, 1);
    }
    if (g_count != 40000) return 1;

    /* Long-message boundaries, custom-handler path. */
    char big[8193];
    const int sizes[] = { 2047, 2048, 4096, 8192 };
    int before = g_count;
    for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
        memset(big, 'x', sizeof(big) - 1);
        big[sizeof(big) - 1] = '\0';
        big[sizes[i]] = '\0';
        zcsr_log_write(ZCSR_LOG_INFO, "big", "%s", big);
    }
    if (g_count != before + (int)(sizeof(sizes) / sizeof(sizes[0]))) return 2;

    zcsr_log_set_handler(NULL, NULL);
    printf("test_log_fuzz: PASS\n");
    return 0;
}
