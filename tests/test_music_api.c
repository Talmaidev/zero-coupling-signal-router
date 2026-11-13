/* Music P0 contract test: duration, seek, error detail.
 * Uses the real decoder and, when a mixer backend is available, the real
 * stream-channel position. No fake mixer, no game-layer simulation. */
#include "zcsr/music.h"
#include "zcsr/audio_mix.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
static void sleep_ms(unsigned ms) { Sleep(ms); }
#else
#include <time.h>
static void sleep_ms(unsigned ms) {
    struct timespec ts = { (time_t)(ms / 1000u), (long)(ms % 1000u) * 1000000L };
    nanosleep(&ts, NULL);
}
#endif

static int failures;

#define CHECK(cond, name) do { \
    if (!(cond)) { printf("FAIL %s (line %d)\n", (name), __LINE__); ++failures; } \
} while (0)

static unsigned char* read_file(const char* path, size_t* out_len) {
    FILE* f = fopen(path, "rb");
    unsigned char* buf;
    long len;
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    len = ftell(f);
    if (len <= 0) { fclose(f); return NULL; }
    if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return NULL; }
    buf = (unsigned char*)malloc((size_t)len);
    if (!buf) { fclose(f); return NULL; }
    if (fread(buf, 1, (size_t)len, f) != (size_t)len) { free(buf); fclose(f); return NULL; }
    fclose(f);
    *out_len = (size_t)len;
    return buf;
}

static void test_format(const char* data_dir, const char* name,
                        zcsr_music_format fmt) {
    char path[512];
    unsigned char* encoded;
    unsigned char* buffer;
    size_t encoded_len = 0;
    size_t needed;
    zcsr_music* music;
    zcsr_mixer* mixer;
    uint64_t duration = 0;
    uint64_t position;

    snprintf(path, sizeof(path), "%s/%s", data_dir, name);
    encoded = read_file(path, &encoded_len);
    if (!encoded) {
        printf("SKIP %s (fixture missing)\n", name);
        return;
    }
    needed = zcsr_music_buffer_bytes_required(encoded_len);
    CHECK(needed > 0, "buffer size computed");
    buffer = (unsigned char*)malloc(needed);
    if (!buffer) {
        free(encoded);
        CHECK(0, "buffer allocation");
        return;
    }
    music = zcsr_music_load(encoded, encoded_len, fmt, buffer, needed);
    free(encoded);
    if (!music) {
        free(buffer);
        CHECK(0, "music load");
        return;
    }
    printf("music_api: %s loaded\n", name);

    CHECK(zcsr_music_duration_ms(music, &duration), "duration known");
    printf("music_api: %s duration=%llu\n", name, (unsigned long long)duration);
    CHECK(duration >= 900u && duration <= 1100u, "duration ~1s");
    CHECK(!zcsr_music_duration_ms(NULL, &duration), "null instance rejected");
    CHECK(!zcsr_music_duration_ms(music, NULL), "null out rejected");

    printf("music_api: %s before seek\n", name);
    CHECK(zcsr_music_seek_ms(music, 500u), "seek stopped accepted");
    printf("music_api: %s after seek state=%d\n", name, (int)zcsr_music_get_state(music));
    CHECK(zcsr_music_get_state(music) == ZCSR_MUSIC_STOPPED, "seek stopped state");
    CHECK(zcsr_music_seek_ms(music, 5000u), "seek clamps past duration");
    CHECK(zcsr_music_seek_ms(music, UINT64_MAX), "seek clamps max");
    printf("music_api: %s before repeated seek\n", name);
    for (int i = 0; i < 100; ++i) {
        CHECK(zcsr_music_seek_ms(music, (i & 1) ? 100u : 500u), "repeated seek");
    }
    printf("music_api: %s after repeated seek\n", name);
    CHECK(zcsr_music_get_error(music) == ZCSR_MUSIC_ERR_NONE, "no error after seek");
    CHECK(!zcsr_music_play(music), "play without mixer rejected");
    CHECK(zcsr_music_get_error(music) == ZCSR_MUSIC_ERR_NO_DEVICE,
          "no device error classified");

    printf("music_api: %s before mix start\n", name);
    mixer = zcsr_mix_start();
    printf("music_api: %s mixer=%p\n", name, (void*)mixer);
    if (mixer) {
        CHECK(zcsr_music_attach_mixer(music, mixer), "attach mixer");
        printf("music_api: %s attached\n", name);
        CHECK(zcsr_music_seek_ms(music, 500u), "seek with mixer");
        position = zcsr_music_position_ms(music);
        printf("music_api: %s position after attach seek=%llu\n", name, (unsigned long long)position);
        CHECK(position >= 500u && position <= 600u, "position starts at seek target");
        CHECK(zcsr_music_play(music), "play after seek");
        printf("music_api: %s playing\n", name);
        sleep_ms(120u);
        position = zcsr_music_position_ms(music);
        printf("music_api: %s position after play=%llu\n", name, (unsigned long long)position);
        CHECK(position >= 500u && position <= 800u, "playing position advances from target");
        CHECK(zcsr_music_seek_ms(music, 800u), "seek while playing");
        position = zcsr_music_position_ms(music);
        CHECK(position >= 800u && position <= 900u, "playing position jumps to target");
        zcsr_music_stop(music);
        zcsr_mix_stop(mixer);
    } else {
        printf("SKIP %s playback (no mixer backend)\n", name);
    }

    CHECK(zcsr_music_get_error(music) == ZCSR_MUSIC_ERR_NONE, "error stays none");
    zcsr_music_destroy(music);
    free(buffer);
}

int main(int argc, char** argv) {
    const char* data_dir = (argc > 1) ? argv[1] : "tests/data";
    setvbuf(stdout, NULL, _IONBF, 0);

    printf("music_api: ogg start\n");
    test_format(data_dir, "sample.ogg", ZCSR_MUSIC_OGG);
    printf("music_api: mp3 start\n");
    test_format(data_dir, "sample.mp3", ZCSR_MUSIC_MP3);

    CHECK(zcsr_music_get_error(NULL) == ZCSR_MUSIC_ERR_NONE, "null error detail");
    CHECK(!zcsr_music_play(NULL), "null play rejected");

    if (failures == 0) {
        printf("test_music_api PASS\n");
        return 0;
    }
    printf("test_music_api FAIL (%d)\n", failures);
    return 1;
}
