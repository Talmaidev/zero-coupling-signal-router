#include "music_t0_helper.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char** argv) {
    const char* dir = argc > 1 ? argv[1] : "tests/data";
    char path[1024];
    size_t len = 0;
    unsigned char* d;
    long nz;
    int fails = mt_public_contract(dir, "t0.opus", ZCSR_MUSIC_OPUS, 48000u, 2u);

    snprintf(path, sizeof(path), "%s/%s", dir, "t0.opus");
    d = mt_read_file(path, &len);
    if (!d) { printf("music_opus: FAIL (read)\n"); return 1; }
    nz = mt_ffmpeg_nonzero(d, len);
    free(d);
    if (nz <= 0) { printf("  [fail] decoded silence / backend open fail\n"); ++fails; }
    else printf("  nonzero samples=%ld\n", nz);

    printf("music_opus: %s\n", fails ? "FAIL" : "PASS");
    return fails ? 1 : 0;
}
