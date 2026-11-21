/* P3 structured events: JSON shape, field types, escaping, filter integration. */
#include "zcsr/log.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static char g_msg[2048];
static int g_count;

static void collector(zcsr_log_level level, const char* tag,
                      const char* msg, void* user) {
    (void)level; (void)user;
    g_count++;
    snprintf(g_msg, sizeof(g_msg), "%s", msg);
    (void)tag;
}

static void reset(void) {
    g_msg[0] = '\0';
    g_count = 0;
}

int main(void) {
    zcsr_log_set_handler(collector, NULL);
    zcsr_log_set_level(ZCSR_LOG_INFO);
    zcsr_log_reset_tag_levels();

    volatile double zero = 0.0;
    double nan_value = zero / zero;

    zcsr_log_field fields[] = {
        { "i64",  ZCSR_LOG_FIELD_I64,  { .i64 = -42 } },
        { "u64",  ZCSR_LOG_FIELD_U64,  { .u64 = UINT64_MAX } },
        { "f64",  ZCSR_LOG_FIELD_F64,  { .f64 = 1.25 } },
        { "nan",  ZCSR_LOG_FIELD_F64,  { .f64 = nan_value } },
        { "str",  ZCSR_LOG_FIELD_STR,  { .str = "a\"b\nc" } },
        { "ctrl", ZCSR_LOG_FIELD_STR,  { .str = "\x01" } },
        { "bool", ZCSR_LOG_FIELD_BOOL, { .b = true } },
        { "ptr",  ZCSR_LOG_FIELD_PTR,  { .ptr = (void*)(uintptr_t)0x1234 } },
        { "bad",  (zcsr_log_field_type)99, { .i64 = 0 } },
    };

    reset();
    zcsr_log_event(ZCSR_LOG_INFO, "event", 7, fields,
                   sizeof(fields) / sizeof(fields[0]));
    if (g_count != 1) return 1;
    if (strstr(g_msg, "\"level\":\"INFO\"") == NULL) return 2;
    if (strstr(g_msg, "\"tag\":\"event\"") == NULL) return 3;
    /* Adjacency matters: if the tag is emitted from an unterminated scratch
     * buffer, uninitialized bytes sit between the tag and the next key. */
    if (strstr(g_msg, "\"tag\":\"event\",") == NULL) return 32;
    if (strstr(g_msg, "\"event\":7") == NULL) return 4;
    if (strstr(g_msg, "\"i64\":-42") == NULL) return 5;
    if (strstr(g_msg, "\"u64\":18446744073709551615") == NULL) return 6;
    if (strstr(g_msg, "\"f64\":1.250000") == NULL) return 7;
    if (strstr(g_msg, "\"nan\":null") == NULL) return 8;
    if (strstr(g_msg, "\"str\":\"a\\\"b\\nc\"") == NULL) return 9;
    if (strstr(g_msg, "\"ctrl\":\"\\u0001\"") == NULL) return 35;
    if (strstr(g_msg, "\"bool\":true") == NULL) return 10;
    if (strstr(g_msg, "\"ptr\":\"0x1234\"") == NULL) return 11;
    if (strstr(g_msg, "\"bad\":null") == NULL) return 12;
    if (g_msg[0] != '{') return 13;
    if (g_msg[strlen(g_msg) - 1] != '}') return 14;
    if (strstr(g_msg, "\"truncated\":true") != NULL) return 15;

    /* NULL fields + count 0 is a valid empty object. */
    reset();
    zcsr_log_event(ZCSR_LOG_INFO, "event", 0, NULL, 0);
    if (g_count != 1) return 15;
    if (strstr(g_msg, "\"fields\":{}") == NULL) return 16;

    /* count > 0 with NULL fields is rejected, not crashed. */
    reset();
    zcsr_log_event(ZCSR_LOG_INFO, "event", 0, NULL, 1);
    if (g_count != 0) return 17;

    /* Tag filter applies before formatting. */
    if (!zcsr_log_set_tag_level("event", ZCSR_LOG_WARN)) return 18;
    reset();
    zcsr_log_event(ZCSR_LOG_INFO, "event", 0, NULL, 0);
    if (g_count != 0) return 19;
    zcsr_log_event(ZCSR_LOG_WARN, "event", 0, NULL, 0);
    if (g_count != 1) return 20;
    zcsr_log_reset_tag_levels();

    /* Oversized string: JSON stays NUL-terminated, <= 2047 bytes, and is
     * closed legally with an explicit truncation marker. */
    char big[3000];
    memset(big, 'x', sizeof(big) - 1);
    big[sizeof(big) - 1] = '\0';
    zcsr_log_field one = { "big", ZCSR_LOG_FIELD_STR, { .str = big } };
    reset();
    zcsr_log_event(ZCSR_LOG_INFO, "event", 0, &one, 1);
    if (g_count != 1) return 23;
    if (strlen(g_msg) > 2047) return 24;
    if (g_msg[0] != '{') return 25;
    if (strlen(g_msg) < 2 || g_msg[strlen(g_msg) - 2] != '}' ||
        g_msg[strlen(g_msg) - 1] != '}') return 26;
    if (strstr(g_msg, "\"fields\":{\"truncated\":true}") == NULL) return 27;

    /* A truncated later field keeps the complete earlier field and inserts
     * exactly one separating comma before the marker. */
    zcsr_log_field two[] = {
        { "ok", ZCSR_LOG_FIELD_I64, { .i64 = 1 } },
        { "big", ZCSR_LOG_FIELD_STR, { .str = big } },
    };
    reset();
    zcsr_log_event(ZCSR_LOG_INFO, "event", 0, two, 2);
    if (g_count != 1) return 28;
    if (strstr(g_msg, "\"ok\":1") == NULL) return 29;
    if (strstr(g_msg, "\"ok\":1,\"truncated\":true}") == NULL) return 30;
    if (strlen(g_msg) < 2 || g_msg[strlen(g_msg) - 2] != '}' ||
        g_msg[strlen(g_msg) - 1] != '}') return 31;

    /* Malformed UTF-8 is escaped, not copied raw: the event must stay valid
     * JSON even when a caller passes arbitrary bytes. */
    const unsigned char bad_utf8[] = { 0xc3, (unsigned char)'(', 0 };
    zcsr_log_field bad = { "badutf8", ZCSR_LOG_FIELD_STR,
                           { .str = (const char*)bad_utf8 } };
    reset();
    zcsr_log_event(ZCSR_LOG_INFO, "event", 0, &bad, 1);
    if (g_count != 1) return 33;
    if (strstr(g_msg, "\"badutf8\":\"\\u00c3(\"") == NULL) return 34;

    zcsr_log_set_handler(NULL, NULL);
    printf("test_log_event: PASS\n");
    return 0;
}
