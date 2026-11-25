#include "music_t0_helper.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Negative contract: garbage, truncated and mislabeled inputs must fail closed
 * (detect false / load NULL) without crashing. */
int main(int argc, char** argv) {
    const char* dir = argc > 1 ? argv[1] : "tests/data";
    char path[1024];
    size_t len = 0;
    unsigned char* d;
    int fails = 0;
    zcsr_music_format det;
    size_t req;
    void* buf;
    zcsr_music* m;

    /* 1. Pure garbage must not detect as any T0 format. */
    {
        unsigned char garbage[64];
        size_t i;
        for (i = 0; i < sizeof(garbage); ++i) garbage[i] = (unsigned char)(i * 37u + 11u);
        if (zcsr_music_detect_format(garbage, sizeof(garbage), &det)) {
            printf("  [fail] garbage detected as %d\n", (int)det);
            ++fails;
        }
    }

    /* 2. Truncated WAV header must fail load. */
    snprintf(path, sizeof(path), "%s/%s", dir, "t0.wav");
    d = mt_read_file(path, &len);
    if (d && len > 32u) {
        size_t cut = 32u;
        req = zcsr_music_buffer_bytes_required(cut);
        buf = malloc(req ? req : 1u);
        m = zcsr_music_load(d, cut, ZCSR_MUSIC_WAV, buf, req);
        if (m) { printf("  [fail] truncated WAV loaded\n"); ++fails; zcsr_music_destroy(m); }
        free(buf);
    }
    free(d);

    /* 3. Every deleted enum must report unsupported. */
    if (zcsr_music_format_supported((zcsr_music_format)42)) {
        printf("  [fail] unknown enum reported supported\n");
        ++fails;
    }

    printf("music_negative: %s\n", fails ? "FAIL" : "PASS");
    return fails ? 1 : 0;
}
