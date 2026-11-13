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

static zcsr_music* load_music(const char* data_dir, const char* name,
                              zcsr_music_format fmt, unsigned char** out_buffer) {
    char path[512];
    unsigned char* encoded;
    unsigned char* buffer;
    size_t encoded_len = 0;
    size_t needed;
    zcsr_music* music;
    snprintf(path, sizeof(path), "%s/%s", data_dir, name);
    encoded = read_file(path, &encoded_len);
    if (!encoded) {
        printf("SKIP %s (fixture missing)\n", name);
        return NULL;
    }
    needed = zcsr_music_buffer_bytes_required(encoded_len);
    buffer = (unsigned char*)malloc(needed);
    if (!buffer) {
        free(encoded);
        return NULL;
    }
    music = zcsr_music_load(encoded, encoded_len, fmt, buffer, needed);
    free(encoded);
    if (!music) {
        free(buffer);
        return NULL;
    }
    *out_buffer = buffer;
    return music;
}

static void test_metadata(const char* data_dir, const char* name,
                          zcsr_music_format fmt) {
    unsigned char* buffer = NULL;
    unsigned char* cover = NULL;
    zcsr_music* music = load_music(data_dir, name, fmt, &buffer);
    zcsr_music_info info;
    size_t cover_bytes;
    if (!music) {
        CHECK(0, "metadata fixture load");
        return;
    }
    memset(&info, 0, sizeof(info));
    CHECK(zcsr_music_get_info(music, &info), "get_info accepted");
    CHECK(strcmp(info.title, "测试曲目 Test Song") == 0, "title UTF-8 roundtrip");
    CHECK(strcmp(info.artist, "测试歌手 Artist") == 0, "artist UTF-8 roundtrip");
    CHECK(strcmp(info.album, "测试专辑 Album") == 0, "album UTF-8 roundtrip");
    CHECK(info.year == 2026u, "year parsed");
    CHECK(info.track == 2u, "track parsed");
    cover_bytes = zcsr_music_cover_bytes_required(music);
    CHECK(cover_bytes == 70u, "cover byte count");
    CHECK(!zcsr_music_get_cover(music, NULL, cover_bytes), "null cover buffer rejected");
    cover = (unsigned char*)malloc(cover_bytes);
    if (cover && cover_bytes > 0) {
        CHECK(!zcsr_music_get_cover(music, cover, cover_bytes - 1u),
              "short cover buffer rejected");
        CHECK(zcsr_music_get_cover(music, cover, cover_bytes), "cover copied");
        CHECK(memcmp(cover, "\x89PNG\r\n\x1a\n", 8) == 0, "cover is encoded PNG");
    } else {
        CHECK(0, "cover allocation");
    }
    free(cover);
    zcsr_music_destroy(music);
    free(buffer);
}

static void test_empty_metadata(const char* data_dir, const char* name,
                                zcsr_music_format fmt) {
    unsigned char* buffer = NULL;
    zcsr_music* music = load_music(data_dir, name, fmt, &buffer);
    zcsr_music_info info;
    if (!music) {
        CHECK(0, "empty metadata fixture load");
        return;
    }
    memset(&info, 0xff, sizeof(info));
    CHECK(zcsr_music_get_info(music, &info), "get_info on untagged file");
    CHECK(info.title[0] == '\0' && info.artist[0] == '\0' && info.album[0] == '\0',
          "missing tags are empty strings");
    CHECK(info.year == 0u && info.track == 0u, "missing numeric tags are zero");
    CHECK(zcsr_music_cover_bytes_required(music) == 0u, "no cover means zero bytes");
    zcsr_music_destroy(music);
    free(buffer);
}

static void test_threadless_pump(const char* data_dir) {
    unsigned char* buffer = NULL;
    zcsr_music* music = load_music(data_dir, "sample_30s.mp3", ZCSR_MUSIC_MP3, &buffer);
    zcsr_mixer* mixer;
    uint64_t duration = 0;
    uint64_t position;
    uint32_t iterations = 0;
    if (!music) {
        CHECK(0, "30s fixture load");
        return;
    }
    CHECK(zcsr_music_duration_ms(music, &duration), "30s duration known");
    printf("music_api: 30s duration=%llu\n", (unsigned long long)duration);
    CHECK(duration >= 29000u && duration <= 31000u, "30s fixture duration");
    CHECK(zcsr_music_set_threaded(music, false), "threadless switch while stopped");
    CHECK(!zcsr_music_set_threaded(NULL, false), "null threadless switch rejected");
    mixer = zcsr_mix_start();
    if (!mixer) {
        printf("SKIP threadless pump (no mixer backend)\n");
        zcsr_music_destroy(music);
        free(buffer);
        return;
    }
    CHECK(zcsr_music_attach_mixer(music, mixer), "threadless attach mixer");
    CHECK(zcsr_music_play(music), "threadless play");
    CHECK(!zcsr_music_set_threaded(music, true), "cannot switch while playing");
    CHECK(!zcsr_music_pump(NULL, 4096u), "null pump rejected");
    while (zcsr_music_get_state(music) != ZCSR_MUSIC_ENDED && iterations < 40000u) {
        bool ok = zcsr_music_pump(music, 4096u);
        if (!ok) break;
        sleep_ms(1u);
        ++iterations;
    }
    printf("music_api: threadless iterations=%u state=%d\n",
           iterations, (int)zcsr_music_get_state(music));
    CHECK(zcsr_music_get_state(music) == ZCSR_MUSIC_ENDED, "pump reaches ENDED");
    CHECK(!zcsr_music_pump(music, 4096u), "pump after ENDED returns false");
    position = zcsr_music_position_ms(music);
    CHECK(position >= duration - 1000u, "pumped position reaches duration");
    zcsr_music_stop(music);
    zcsr_mix_stop(mixer);
    zcsr_music_destroy(music);
    free(buffer);
}

static void test_output_device_contract(void) {
    zcsr_mixer* mixer = zcsr_mix_start();
    zcsr_mix_device_info devices[8];
    char before[ZCSR_MIX_DEVICE_ID_CAP];
    int count;
    if (!mixer) {
        printf("SKIP output device contract (no mixer backend)\n");
        return;
    }
#if defined(_WIN32)
    CHECK(zcsr_mix_set_output_device(mixer, "") , "empty id is system default");
    snprintf(before, sizeof(before), "%s", zcsr_mix_get_output_device(mixer));
    CHECK(!zcsr_mix_set_output_device(mixer, "zcsr-no-such-endpoint"),
          "unknown endpoint rejected");
    CHECK(strcmp(zcsr_mix_get_output_device(mixer), before) == 0,
          "failed switch keeps current endpoint");
    count = zcsr_mix_list_output_devices(devices, 8);
    printf("music_api: output devices=%d\n", count);
    if (count >= 1) {
        CHECK(zcsr_mix_set_output_device(mixer, devices[0].id),
              "known endpoint accepted");
        CHECK(strcmp(zcsr_mix_get_output_device(mixer), devices[0].id) == 0,
              "known endpoint recorded");
    }
    if (count >= 2) {
        CHECK(zcsr_mix_set_output_device(mixer, devices[1].id),
              "switch to second endpoint");
        CHECK(zcsr_mix_set_output_device(mixer, devices[0].id),
              "switch back to first endpoint");
    } else {
        printf("SKIP physical dual-device switch (only %d endpoint)\n", count);
    }
#else
    (void)devices; (void)count; (void)before;
    CHECK(!zcsr_mix_set_output_device(mixer, ""), "unsupported host returns false");
#endif
    zcsr_mix_stop(mixer);
}

int main(int argc, char** argv) {
    const char* data_dir = (argc > 1) ? argv[1] : "tests/data";
    setvbuf(stdout, NULL, _IONBF, 0);

    printf("music_api: ogg start\n");
    test_format(data_dir, "sample.ogg", ZCSR_MUSIC_OGG);
    printf("music_api: mp3 start\n");
    test_format(data_dir, "sample.mp3", ZCSR_MUSIC_MP3);
    printf("music_api: metadata start\n");
    test_metadata(data_dir, "sample_meta.ogg", ZCSR_MUSIC_OGG);
    test_metadata(data_dir, "sample_meta.mp3", ZCSR_MUSIC_MP3);
    test_empty_metadata(data_dir, "sample.ogg", ZCSR_MUSIC_OGG);
    test_empty_metadata(data_dir, "sample.mp3", ZCSR_MUSIC_MP3);
    printf("music_api: threadless start\n");
    test_threadless_pump(data_dir);
    printf("music_api: device contract start\n");
    test_output_device_contract();

    CHECK(zcsr_music_get_error(NULL) == ZCSR_MUSIC_ERR_NONE, "null error detail");
    CHECK(!zcsr_music_play(NULL), "null play rejected");

    if (failures == 0) {
        printf("test_music_api PASS\n");
        return 0;
    }
    printf("test_music_api FAIL (%d)\n", failures);
    return 1;
}
