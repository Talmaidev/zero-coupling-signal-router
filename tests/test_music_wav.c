#include "music_t0_helper.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

int main(int argc, char** argv) {
    const char* dir = argc > 1 ? argv[1] : "tests/data";
    char path[1024];
    size_t len = 0;
    unsigned char* d;
    long nz;
    int fails = mt_public_contract(dir, "t0.wav", ZCSR_MUSIC_WAV, 44100u, 2u);

    snprintf(path, sizeof(path), "%s/%s", dir, "t0.wav");
    d = mt_read_file(path, &len);
    if (!d) { printf("music_wav: FAIL (read)\n"); return 1; }
    nz = mt_wav_nonzero(d, len);
    free(d);
    if (nz <= 0) { printf("  [fail] WAV payload silence / unreadable\n"); ++fails; }
    else printf("  nonzero int16 samples=%ld\n", nz);

    printf("music_wav: %s\n", fails ? "FAIL" : "PASS");
    return fails ? 1 : 0;
}
