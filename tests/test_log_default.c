/* Regression test for the default sink's second-stage truncation.
 *
 * The custom-handler path receives the already-formatted 2048-byte buffer and
 * is covered by test_log.c. This test exercises zcsr_log_default() itself at
 * the 2047/2048/4096/8192-byte boundaries: it redirects stderr to a file,
 * logs the message in binary mode, and checks that the emitted line is still
 * NUL-terminated, at most 2048 bytes, ends with exactly one newline, and has
 * no embedded NUL. Before the fix, line[2047] was overwritten with '\n', so
 * fputs() read past the stack buffer. */
#include "zcsr/log.h"

#include <stdio.h>
#include <string.h>

#if defined(_WIN32)
#  include <io.h>
#  define zcsr_dup _dup
#  define zcsr_dup2 _dup2
#  define zcsr_close _close
#  define zcsr_fileno _fileno
#else
#  include <unistd.h>
#  define zcsr_dup dup
#  define zcsr_dup2 dup2
#  define zcsr_close close
#  define zcsr_fileno fileno
#endif

static int run_case(int saved, const char* path, int msg_len) {
    FILE* cap = freopen(path, "wb", stderr);
    if (cap == NULL) return 1;

    char big[8193];
    memset(big, 'x', sizeof(big) - 1);
    big[sizeof(big) - 1] = '\0';
    big[msg_len] = '\0';

    zcsr_log_set_level(ZCSR_LOG_INFO);
    zcsr_log_set_handler(NULL, NULL);
    zcsr_log_write(ZCSR_LOG_INFO, "big", "%s", big);
    fflush(stderr);

    /* cap is stderr; restore the original descriptor instead of fclose(cap). */
    if (zcsr_dup2(saved, zcsr_fileno(stderr)) < 0) return 2;

    FILE* in = fopen(path, "rb");
    if (in == NULL) return 3;
    if (fseek(in, 0, SEEK_END) != 0) { fclose(in); return 4; }
    long size = ftell(in);
    if (size <= 0 || size > 2048) { fclose(in); return 5; }
    if (fseek(in, 0, SEEK_SET) != 0) { fclose(in); return 6; }

    char buf[2048];
    size_t got = fread(buf, 1, sizeof(buf), in);
    fclose(in);
    if (got != (size_t)size) return 7;
    if (buf[size - 1] != '\n') return 8;
    if (memchr(buf, '\0', (size_t)size) != NULL) return 9;

    int newlines = 0;
    for (long i = 0; i < size; i++) {
        if (buf[i] == '\n') newlines++;
    }
    if (newlines != 1) return 10;
    if (strncmp(buf, "[INFO][big] ", 12) != 0) return 11;
    if (size != 2047) return 12; /* binary mode: truncation is deterministic */
    return 0;
}

int main(void) {
    const char* path = "log_default_capture.txt";
    int saved = zcsr_dup(zcsr_fileno(stderr));
    if (saved < 0) return 1;

    const int sizes[] = { 2047, 2048, 4096, 8192 };
    for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
        int rc = run_case(saved, path, sizes[i]);
        if (rc != 0) {
            zcsr_close(saved);
            return 20 + rc;
        }
    }

    zcsr_close(saved);
    remove(path);
    printf("test_log_default: PASS (2047 bytes at 2047/2048/4096/8192)\n");
    return 0;
}
