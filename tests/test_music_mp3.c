#include "music_t0_helper.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char** argv) {
    const char* dir = argc > 1 ? argv[1] : "tests/data";
    int fails = mt_public_contract(dir, "t0.mp3", ZCSR_MUSIC_MP3, 44100u, 2u);
    printf("music_mp3: %s\n", fails ? "FAIL" : "PASS");
    return fails ? 1 : 0;
}
