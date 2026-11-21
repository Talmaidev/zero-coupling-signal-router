/* P2 file sink: append, 5 backups, rotation bound, failure fallback. */
#include "zcsr/log.h"

#include <stdio.h>
#include <string.h>

#if !defined(_WIN32)
#  include <unistd.h>
#endif

static void noop_handler(zcsr_log_level level, const char* tag,
                         const char* msg, void* user) {
    (void)level; (void)tag; (void)msg; (void)user;
}

static long file_size(const char* path) {
    FILE* f = fopen(path, "rb");
    if (f == NULL) return -1;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return -1; }
    long n = ftell(f);
    fclose(f);
    return n;
}

static int first_line_is(const char* path, const char* prefix) {
    FILE* f = fopen(path, "rb");
    if (f == NULL) return 0;
    char line[256];
    int ok = fgets(line, sizeof(line), f) != NULL &&
             strncmp(line, prefix, strlen(prefix)) == 0;
    fclose(f);
    return ok;
}

static int exists(const char* path) {
    FILE* f = fopen(path, "rb");
    if (f == NULL) return 0;
    fclose(f);
    return 1;
}

int main(void) {
    zcsr_log_set_handler(noop_handler, NULL);
    zcsr_log_set_level(ZCSR_LOG_INFO);

    /* A directory cannot be opened as an append file. */
    if (zcsr_log_set_file(".", 5, 4096)) return 1;
    if (zcsr_log_set_file("no_such_dir/zcsr.log", 5, 4096)) return 2;
    if (zcsr_log_set_file("log_file_test.log", 5, 4096) == 0) return 3;

    for (int i = 0; i < 500; i++) {
        zcsr_log_write(ZCSR_LOG_INFO, "file",
                       "line %03d 0123456789012345678901234567890123456789"
                       "012345678901234567890123456789", i);
    }
    zcsr_log_clear_file();

    if (!exists("log_file_test.log")) return 4;
    if (!exists("log_file_test.log.1")) return 5;
    if (!exists("log_file_test.log.5")) return 6;
    if (exists("log_file_test.log.6")) return 7;
    if (!first_line_is("log_file_test.log", "[INFO][file] line ")) return 8;
    if (!first_line_is("log_file_test.log.1", "[INFO][file] line ")) return 9;

    for (int i = 0; i <= 5; i++) {
        char path[64];
        if (i == 0) snprintf(path, sizeof(path), "log_file_test.log");
        else snprintf(path, sizeof(path), "log_file_test.log.%d", i);
        long n = file_size(path);
        if (n <= 0 || n > 4096) return 10;
        remove(path);
    }

    /* A truncated long line must not write the terminating NUL into the file. */
    if (!zcsr_log_set_file("log_file_long.log", 1, 4096)) return 13;
    char big[3000];
    memset(big, 'x', sizeof(big) - 1);
    big[sizeof(big) - 1] = '\0';
    zcsr_log_write(ZCSR_LOG_INFO, "file", "%s", big);
    zcsr_log_clear_file();
    {
        FILE* f = fopen("log_file_long.log", "rb");
        if (f == NULL) return 14;
        unsigned char data[4096];
        size_t n = fread(data, 1, sizeof(data), f);
        int ok = !ferror(f) && n > 0 && n <= 4096 && data[n - 1] == '\n' &&
                 memchr(data, 0, n) == NULL;
        fclose(f);
        remove("log_file_long.log");
        if (!ok) return 15;
    }

#if !defined(_WIN32)
    /* /dev/full returns ENOSPC on every write. The sink must disable itself,
     * not corrupt the process, and a later set_file must recover. */
    if (access("/dev/full", F_OK) == 0) {
        if (!zcsr_log_set_file("/dev/full", 1, 4096)) return 11;
        zcsr_log_write(ZCSR_LOG_INFO, "file", "disk full probe");
        zcsr_log_clear_file();
        if (!zcsr_log_set_file("log_file_recover.log", 1, 4096)) return 12;
        zcsr_log_write(ZCSR_LOG_INFO, "file", "recovered");
        zcsr_log_clear_file();
        remove("log_file_recover.log");
    }
#endif

    zcsr_log_set_handler(NULL, NULL);
    printf("test_log_file: PASS\n");
    return 0;
}
