/* Negative format-gate fixture: int64_t passed to "%d" must fail under
 * clang-cl /W4 /WX or clang/GCC -Wformat -Werror. */
#include "zcsr/log.h"

#include <stdint.h>

void zcsr_format_gate_bad(void) {
    int64_t value = 0;
    zcsr_log_write(ZCSR_LOG_INFO, "gate", "%d", value);
}
