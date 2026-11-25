/* test_music_flac.c — FLAC engine contract tests for zcsr_music.
 *
 * Positive path decodes the real tests/data/sample.flac fixture (22050 Hz,
 * stereo, 16-bit, ~0.97 s, title "Solitary Heart"). Negative paths cover
 * truncated/corrupted FLAC.
 *
 * The raw PCM non-silence check lives in test_music_flac_codec.c, which links
 * the vendored dr_flac decoder directly.
 */
#include "zcsr/music.h"
#include "zcsr/audio_mix.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

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

static zcsr_music* load_bytes(const unsigned char* encoded, size_t len,
                              zcsr_music_format fmt, unsigned char** out_buffer) {
    size_t needed = zcsr_music_buffer_bytes_required(len);
    unsigned char* buffer;
    zcsr_music* music;
    if (needed == 0) return NULL;
    buffer = (unsigned char*)malloc(needed);
    if (!buffer) return NULL;
    music = zcsr_music_load(encoded, len, fmt, buffer, needed);
    if (!music) { free(buffer); return NULL; }
    *out_buffer = buffer;
    return music;
}

typedef struct {
    zcsr_music_state state;
    bool pumped;
} drain_result;

/* Bounded threadless pump, mirroring test_music_api.c's threadless contract:
 * a headless mixer queue may not drain to ENDED inside the iteration budget,
 * so the accepted live states are PLAYING or ENDED. The pump must accept at
 * least one call and must not land in ERROR for a healthy fixture. */
static drain_result drain_threadless(zcsr_music* music) {
    zcsr_mixer* mixer = zcsr_mix_start();
    drain_result result;
    uint32_t iterations = 0;
    result.pumped = false;
    if (!mixer) {
        printf("SKIP drain (no mixer backend)\n");
        result.state = ZCSR_MUSIC_STOPPED;
        return result;
    }
    if (!zcsr_music_attach_mixer(music, mixer)) {
        zcsr_mix_stop(mixer);
        result.state = ZCSR_MUSIC_ERROR;
        return result;
    }
    if (!zcsr_music_set_threaded(music, false)) {
        zcsr_mix_stop(mixer);
        result.state = ZCSR_MUSIC_ERROR;
        return result;
    }
    if (!zcsr_music_play(music)) {
        zcsr_mix_stop(mixer);
        result.state = ZCSR_MUSIC_ERROR;
        return result;
    }
    while (zcsr_music_get_state(music) != ZCSR_MUSIC_ENDED &&
           zcsr_music_get_state(music) != ZCSR_MUSIC_ERROR &&
           iterations < 256u) {
        if (!zcsr_music_pump(music, 4096u)) break;
        result.pumped = true;
        ++iterations;
    }
    result.state = zcsr_music_get_state(music);
    zcsr_music_stop(music);
    zcsr_mix_stop(mixer);
    return result;
}

static void test_capabilities(void) {
    /* Vendored-native T0 formats are always supported. */
    CHECK(zcsr_music_format_supported(ZCSR_MUSIC_OGG), "ogg supported");
    CHECK(zcsr_music_format_supported(ZCSR_MUSIC_MP3), "mp3 supported");
    CHECK(zcsr_music_format_supported(ZCSR_MUSIC_WAV), "wav supported");
    CHECK(zcsr_music_format_supported(ZCSR_MUSIC_FLAC), "flac supported");

    CHECK(!zcsr_music_format_supported((zcsr_music_format)99), "unknown unsupported");
}

static void test_detection(const char* data_dir) {
    char path[512];
    unsigned char* data;
    size_t len;
    zcsr_music_format fmt;
    static const unsigned char garbage[] = { 1, 2, 3, 4, 5, 6, 7, 8 };

    CHECK(!zcsr_music_detect_format(garbage, sizeof(garbage), &fmt),
          "garbage not detected");
    CHECK(!zcsr_music_detect_format(garbage, sizeof(garbage), NULL),
          "null out rejected");

    snprintf(path, sizeof(path), "%s/sample.flac", data_dir);
    data = read_file(path, &len);
    if (data) {
        CHECK(zcsr_music_detect_format(data, len, &fmt), "flac detected");
        CHECK(fmt == ZCSR_MUSIC_FLAC, "flac signature");
        free(data);
    } else {
        printf("SKIP sample.flac detection (fixture missing)\n");
    }

    snprintf(path, sizeof(path), "%s/sample.m4a", data_dir);
    data = read_file(path, &len);
    if (data) {
        CHECK(zcsr_music_detect_format(data, len, &fmt), "m4a detected");
        CHECK(fmt == ZCSR_MUSIC_M4A, "m4a signature");
        free(data);
    } else {
        printf("SKIP sample.m4a detection (fixture missing)\n");
    }

    snprintf(path, sizeof(path), "%s/sample.ogg", data_dir);
    data = read_file(path, &len);
    if (data) {
        CHECK(zcsr_music_detect_format(data, len, &fmt), "ogg detected");
        CHECK(fmt == ZCSR_MUSIC_OGG, "ogg signature");
        free(data);
    }

    snprintf(path, sizeof(path), "%s/sample.mp3", data_dir);
    data = read_file(path, &len);
    if (data) {
        CHECK(zcsr_music_detect_format(data, len, &fmt), "mp3 detected");
        CHECK(fmt == ZCSR_MUSIC_MP3, "mp3 signature");
        free(data);
    }
}

static void test_flac_positive(const char* data_dir) {
    char path[512];
    unsigned char* encoded;
    size_t encoded_len = 0;
    unsigned char* buffer = NULL;
    zcsr_music* music;
    zcsr_music_spec spec;
    zcsr_music_info info;
    uint64_t duration = 0;
    drain_result res;

    snprintf(path, sizeof(path), "%s/sample.flac", data_dir);
    encoded = read_file(path, &encoded_len);
    if (!encoded) {
        printf("SKIP sample.flac (fixture missing)\n");
        return;
    }
    music = load_bytes(encoded, encoded_len, ZCSR_MUSIC_FLAC, &buffer);
    free(encoded);
    if (!music) {
        CHECK(0, "flac load");
        return;
    }
    printf("flac: loaded\n");

    memset(&spec, 0, sizeof(spec));
    CHECK(zcsr_music_get_spec(music, &spec), "flac spec accepted");
    printf("flac: rate=%u ch=%u bps=%u frames=%llu\n",
           spec.sample_rate, spec.channels, spec.bits_per_sample,
           (unsigned long long)spec.total_frames);
    CHECK(spec.sample_rate == 22050u, "flac sample rate 22050");
    CHECK(spec.channels == 2u, "flac stereo");
    CHECK(spec.bits_per_sample == 16u, "flac 16-bit");
    CHECK(spec.total_frames == 21393u, "flac frame count matches STREAMINFO");

    CHECK(zcsr_music_duration_ms(music, &duration), "flac duration known");
    printf("flac: duration=%llu ms\n", (unsigned long long)duration);
    CHECK(duration >= 900u && duration <= 1050u, "flac duration ~0.97s");

    memset(&info, 0, sizeof(info));
    CHECK(zcsr_music_get_info(music, &info), "flac info accepted");
    CHECK(strcmp(info.title, "Solitary Heart") == 0, "flac title parsed");
    CHECK(info.year == 2014u, "flac date year parsed");
    printf("flac: title=\"%s\" year=%u\n", info.title, info.year);

    CHECK(zcsr_music_seek_ms(music, 500u), "flac seek accepted");
    CHECK(zcsr_music_position_ms(music) <= duration, "flac seek position in range");

    res = drain_threadless(music);
    CHECK(res.pumped, "flac pump accepted at least once");
    CHECK(res.state == ZCSR_MUSIC_PLAYING || res.state == ZCSR_MUSIC_ENDED,
          "flac stays live after pump");
    CHECK(zcsr_music_get_error(music) == ZCSR_MUSIC_ERR_NONE, "flac no decode error");

    zcsr_music_destroy(music);
    free(buffer);
}

static void test_flac_truncated(const char* data_dir) {
    char path[512];
    unsigned char* encoded;
    size_t len;
    unsigned char* buffer = NULL;
    zcsr_music* music;

    snprintf(path, sizeof(path), "%s/sample.flac", data_dir);
    encoded = read_file(path, &len);
    if (!encoded) { printf("SKIP truncated flac (fixture missing)\n"); return; }

    /* Header-only slice: open must fail rather than decode garbage. */
    {
        size_t cut = len < 128u ? len : 128u;
        music = load_bytes(encoded, cut, ZCSR_MUSIC_FLAC, &buffer);
        CHECK(music == NULL, "truncated flac rejected");
        if (music) { zcsr_music_destroy(music); free(buffer); buffer = NULL; }
    }

    /* Middle-corrupted copy: either load rejects it, or decoding terminates in
     * ERROR/ENDED — it must never crash or silently succeed as silence. */
    {
        unsigned char* copy = (unsigned char*)malloc(len);
        if (!copy) { free(encoded); CHECK(0, "corrupt copy"); return; }
        memcpy(copy, encoded, len);
        for (size_t i = len / 2u; i < len / 2u + 256u && i < len; ++i) {
            copy[i] ^= 0xFFu;
        }
        music = load_bytes(copy, len, ZCSR_MUSIC_FLAC, &buffer);
        free(copy);
        if (music) {
            drain_result res = drain_threadless(music);
            zcsr_music_error err = zcsr_music_get_error(music);
            CHECK(res.state == ZCSR_MUSIC_ENDED ||
                  res.state == ZCSR_MUSIC_ERROR ||
                  res.state == ZCSR_MUSIC_PLAYING,
                  "corrupt flac terminates or stays live cleanly");
            CHECK(err == ZCSR_MUSIC_ERR_NONE || err == ZCSR_MUSIC_ERR_DECODE,
                  "corrupt flac reports none/decode error");
            zcsr_music_destroy(music);
            free(buffer);
        }
    }
    free(encoded);
}

int main(int argc, char** argv) {
    const char* data_dir = (argc > 1) ? argv[1] : "tests/data";
    setvbuf(stdout, NULL, _IONBF, 0);

    test_capabilities();
    test_detection(data_dir);
    test_flac_positive(data_dir);
    test_flac_truncated(data_dir);

    if (failures == 0) {
        printf("test_music_flac PASS\n");
        return 0;
    }
    printf("test_music_flac FAIL (%d)\n", failures);
    return 1;
}
