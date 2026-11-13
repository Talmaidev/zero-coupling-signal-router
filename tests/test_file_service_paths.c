/* test_file_service_paths.c — File Service path normalization contract.
 *
 * RED before the normalization fix: normalize_path() tokenized and always
 * rebuilt with a leading '/', so:
 *   C:\Users\x  ->  /C:/Users/x   (invalid Win32 drive path)
 *   uirtest     ->  /uirtest      (absolute root, not cwd-relative)
 * This broke UIR Project Browser create/rename/remove and made its root
 * enumerate the current drive root instead of the project directory.
 */
#include "zcsr/file_service.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;
#define CHECK_RESOLVE(fs, in, expected, what) do { \
    char out[256]; \
    zcsr_file_result_t rc = zcsr_file_service_resolve((fs), (in), out, sizeof(out)); \
    if (rc != ZCSR_FILE_OK || strcmp(out, (expected)) != 0) { \
        printf("FAIL: %s: %s -> %s (rc=%d), expected %s\n", \
               (what), (in), out, (int)rc, (expected)); \
        failures++; \
    } else { \
        printf("ok: %s -> %s\n", (what), out); \
    } \
} while (0)

int main(void) {
    zcsr_file_service_t *fs = zcsr_file_service_create();
    if (!fs) {
        printf("FAIL: create file service\n");
        return 1;
    }

    CHECK_RESOLVE(fs, "C:\\Users\\chen\\Documents", "C:/Users/chen/Documents",
                  "windows drive path");
    CHECK_RESOLVE(fs, "C:/Users/chen/Documents", "C:/Users/chen/Documents",
                  "windows drive path with slashes");
    CHECK_RESOLVE(fs, "uirtest", "uirtest", "relative path stays relative");
    CHECK_RESOLVE(fs, "C:\\Users\\chen\\Documents\\..\\GitHub",
                  "C:/Users/chen/GitHub", "parent segment collapses");

    zcsr_file_result_t mrc = zcsr_file_service_mount(fs, "project", "C:/base", false);
    if (mrc != ZCSR_FILE_OK) {
        printf("FAIL: mount project rc=%d\n", (int)mrc);
        failures++;
    } else {
        CHECK_RESOLVE(fs, "project://sub/dir", "C:/base/sub/dir",
                      "mounted URI");
    }

    zcsr_file_service_destroy(fs);
    if (failures == 0) {
        printf("file_service_paths_test PASS\n");
        return 0;
    }
    printf("file_service_paths_test FAIL failures=%d\n", failures);
    return 1;
}
