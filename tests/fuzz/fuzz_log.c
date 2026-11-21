/* libFuzzer harness for zcsr_log_write / zcsr_log_event.
 *
 * Build with:
 *   cmake -DZCSR_BUILD_FUZZERS=ON -DCMAKE_C_COMPILER=clang ...
 * The default CTest suite also has a deterministic fuzz smoke test
 * (tests/test_log_fuzz.c) so this target is not required on every machine. */
#include "zcsr/log.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

static void noop_handler(zcsr_log_level level, const char* tag,
                         const char* msg, void* user) {
    (void)level; (void)tag; (void)msg; (void)user;
}

int LLVMFuzzerInitialize(int* argc, char*** argv) {
    (void)argc; (void)argv;
    zcsr_log_set_handler(noop_handler, NULL);
    zcsr_log_set_level(ZCSR_LOG_TRACE);
    return 0;
}

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    char buf[8192];
    size_t n = size < sizeof(buf) - 1 ? size : sizeof(buf) - 1;
    if (n > 0) memcpy(buf, data, n);
    buf[n] = '\0';

    zcsr_log_write(ZCSR_LOG_INFO, "fuzz", "%s", buf);
    zcsr_log_field field = { "s", ZCSR_LOG_FIELD_STR, { .str = buf } };
    zcsr_log_event(ZCSR_LOG_INFO, "fuzz", 1, &field, 1);
    return 0;
}
