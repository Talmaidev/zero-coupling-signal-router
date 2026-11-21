/* Positive format-gate fixture: PRId64 is the correct width-independent
 * spelling and must compile under the same flags as bad_format.c. */
#include "zcsr/log.h"

#include <inttypes.h>
#include <stdint.h>

void zcsr_format_gate_good(void) {
    int64_t value = 0;
    zcsr_log_write(ZCSR_LOG_INFO, "gate", "%" PRId64, value);
}
