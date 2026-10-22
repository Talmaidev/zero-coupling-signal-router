/* test_music_logic.c — headless logic tests for zcsr_music (load/state/buffer/error paths).
 * Does not require actual audio playback; tests buffer sizing, invalid format, etc. */
#include "zcsr/music.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Minimal fake OGG header (not valid for decoding, but enough to test buffer sizing) */
static const unsigned char fake_ogg[] = {
    'O', 'g', 'g', 'S', 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 30, 1, 'v', 'o', 'r', 'b', 'i', 's'
};

int main(void) {
    size_t need;
    unsigned char* buf;
    zcsr_music* m;

    /* buffer_bytes_required sanity */
    need = zcsr_music_buffer_bytes_required(sizeof(fake_ogg));
    assert(need > 0);
    assert(need > sizeof(fake_ogg));

    /* overflow protection */
    assert(zcsr_music_buffer_bytes_required(SIZE_MAX) == 0);

    buf = (unsigned char*)malloc(need);
    assert(buf);

    /* invalid format returns NULL */
    m = zcsr_music_load(fake_ogg, sizeof(fake_ogg), (zcsr_music_format)99, buf, need);
    assert(m == NULL);

    /* NULL inputs */
    assert(zcsr_music_load(NULL, 10, ZCSR_MUSIC_OGG, buf, need) == NULL);
    assert(zcsr_music_load(fake_ogg, 0, ZCSR_MUSIC_OGG, buf, need) == NULL);
    assert(zcsr_music_load(fake_ogg, sizeof(fake_ogg), ZCSR_MUSIC_OGG, NULL, need) == NULL);

    /* buffer too small */
    assert(zcsr_music_load(fake_ogg, sizeof(fake_ogg), ZCSR_MUSIC_OGG, buf, need / 2) == NULL);

    /* state on NULL */
    assert(zcsr_music_get_state(NULL) == ZCSR_MUSIC_ERROR);
    assert(zcsr_music_position_ms(NULL) == 0);

    free(buf);
    printf("test_music_logic PASS\n");
    return 0;
}
