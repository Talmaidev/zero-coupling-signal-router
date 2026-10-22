/* test_music_codec.c — direct codec tests for minimp3 (MP3) and stb_vorbis (OGG).
 * Does NOT link zcsr_music library; tests third-party decoders directly.
 * Test data: tests/data/sample.mp3 + sample.ogg (1 sec 440Hz sine, 44100Hz stereo,
 * generated via ffmpeg in WSL2 and committed to repo). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

#define MINIMP3_FLOAT_OUTPUT
#include "../third_party/minimp3.h"
#define STB_VORBIS_HEADER_ONLY
#include "../third_party/stb_vorbis.c"
#undef STB_VORBIS_HEADER_ONLY

static unsigned char* read_file(const char* path, size_t* out_len) {
    FILE* f = fopen(path, "rb");
    unsigned char* buf;
    long len;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len <= 0) { fclose(f); return NULL; }
    buf = (unsigned char*)malloc((size_t)len);
    if (!buf) { fclose(f); return NULL; }
    if (fread(buf, 1, (size_t)len, f) != (size_t)len) { free(buf); fclose(f); return NULL; }
    fclose(f);
    *out_len = (size_t)len;
    return buf;
}

/* Count nonzero samples in a float buffer; record first 16 for diagnostics. */
static int count_nonzero(const float* pcm, int n, float* first_nz, int* nz_count) {
    int i, nz = 0;
    for (i = 0; i < n; i++) {
        if (fabsf(pcm[i]) > 1e-6f) {
            nz++;
            if (*nz_count < 16) first_nz[(*nz_count)++] = pcm[i];
        }
    }
    return nz;
}

static int test_mp3_decode(void) {
    unsigned char* data;
    size_t len;
    mp3dec_t dec;
    mp3dec_frame_info_t info;
    float pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
    size_t offset = 0;
    int frames_decoded = 0;
    int total_samples = 0;
    int sample_rate = 0;
    int channels = 0;
    int nonzero_samples = 0;
    float first_nonzero[16] = {0};
    int first_nonzero_count = 0;

    data = read_file("tests/data/sample.mp3", &len);
    if (!data) {
        printf("MP3: SKIP (sample.mp3 not found)\n");
        return 0;
    }

    mp3dec_init(&dec);
    while (offset < len) {
        int samples = mp3dec_decode_frame(&dec, data + offset, (int)(len - offset), pcm, &info);
        if (info.frame_bytes == 0) break;
        offset += (size_t)info.frame_bytes;
        if (samples > 0) {
            frames_decoded++;
            total_samples += samples;
            sample_rate = info.hz;
            channels = info.channels;
            nonzero_samples += count_nonzero(pcm, samples, first_nonzero, &first_nonzero_count);
        }
    }
    free(data);

    if (frames_decoded == 0) {
        printf("MP3: FAIL (no frames decoded)\n");
        return 1;
    }
    if (sample_rate != 44100) {
        printf("MP3: FAIL (expected 44100Hz, got %d)\n", sample_rate);
        return 1;
    }
    if (channels != 2) {
        printf("MP3: FAIL (expected 2ch, got %d)\n", channels);
        return 1;
    }
    /* Nonzero assertion: 440Hz sine means most samples are nonzero. */
    if (nonzero_samples == 0) {
        printf("MP3: FAIL (all samples zero — expected 440Hz sine)\n");
        return 1;
    }
    if (nonzero_samples < total_samples / 2) {
        printf("MP3: FAIL (only %d/%d nonzero — expected sine wave)\n",
               nonzero_samples, total_samples);
        return 1;
    }
    printf("MP3: PASS (%d frames, %d samples, %dHz, %d ch, %d nonzero)\n",
           frames_decoded, total_samples, sample_rate, channels, nonzero_samples);
    return 0;
}

static int test_ogg_decode(void) {
    unsigned char* data;
    size_t len;
    stb_vorbis* vorbis;
    int error = 0;
    stb_vorbis_info info;
    float pcm[4096 * 2];
    int total_samples = 0;
    int nonzero_samples = 0;
    float first_nonzero[16] = {0};
    int first_nonzero_count = 0;

    data = read_file("tests/data/sample.ogg", &len);
    if (!data) {
        printf("OGG: SKIP (sample.ogg not found)\n");
        return 0;
    }

    vorbis = stb_vorbis_open_memory(data, (int)len, &error, NULL);
    if (!vorbis) {
        printf("OGG: FAIL (stb_vorbis_open_memory error %d)\n", error);
        free(data);
        return 1;
    }
    info = stb_vorbis_get_info(vorbis);
    if (info.sample_rate != 44100) {
        printf("OGG: FAIL (expected 44100Hz, got %d)\n", info.sample_rate);
        stb_vorbis_close(vorbis);
        free(data);
        return 1;
    }
    if (info.channels != 2) {
        printf("OGG: FAIL (expected 2ch, got %d)\n", info.channels);
        stb_vorbis_close(vorbis);
        free(data);
        return 1;
    }

    for (;;) {
        int got = stb_vorbis_get_samples_float_interleaved(
            vorbis, info.channels, pcm, 4096 * 2);
        if (got <= 0) break;
        total_samples += got * info.channels;
        nonzero_samples += count_nonzero(pcm, got * info.channels,
                                         first_nonzero, &first_nonzero_count);
    }
    stb_vorbis_close(vorbis);
    free(data);

    if (total_samples == 0) {
        printf("OGG: FAIL (no samples decoded)\n");
        return 1;
    }
    if (nonzero_samples == 0) {
        printf("OGG: FAIL (all samples zero — expected 440Hz sine)\n");
        return 1;
    }
    if (nonzero_samples < total_samples / 2) {
        printf("OGG: FAIL (only %d/%d nonzero — expected sine wave)\n",
               nonzero_samples, total_samples);
        return 1;
    }
    printf("OGG: PASS (%d samples, %dHz, %d ch, %d nonzero)\n",
           total_samples, info.sample_rate, info.channels, nonzero_samples);
    return 0;
}

int main(void) {
    int fail = 0;
    fail += test_mp3_decode();
    fail += test_ogg_decode();
    if (fail == 0) {
        printf("test_music_codec PASS\n");
        return 0;
    }
    printf("test_music_codec FAIL (%d)\n", fail);
    return 1;
}
