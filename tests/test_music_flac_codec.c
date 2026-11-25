/* test_music_flac_codec.c — direct dr_flac decode check for the real
 * tests/data/sample.flac fixture. Links the vendored decoder itself and
 * asserts the decoded PCM is not all silence and that stream attributes match
 * the FLAC STREAMINFO.
 */
#define DR_FLAC_IMPLEMENTATION
#define DR_FLAC_NO_STDIO
#define DR_FLAC_NO_SIMD
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4244 4245 4267 4310 4389 4456 4457 4701 4702 4189 4505 4100)
#endif
#include "../modules/music/third_party/dr_flac.h"
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

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

int main(int argc, char** argv) {
    const char* data_dir = (argc > 1) ? argv[1] : "tests/data";
    char path[512];
    unsigned char* data;
    size_t len;
    drflac* flac;
    float pcm[4096 * 2];
    uint64_t total_frames = 0;
    uint64_t nonzero = 0;
    uint64_t samples = 0;
    uint64_t got;
    int failures = 0;

    snprintf(path, sizeof(path), "%s/sample.flac", data_dir);
    data = read_file(path, &len);
    if (!data) {
        printf("SKIP sample.flac (fixture missing)\n");
        return 0;
    }

    flac = drflac_open_memory(data, len, NULL);
    if (!flac) {
        printf("FAIL drflac_open_memory returned NULL\n");
        free(data);
        return 1;
    }
    printf("codec: rate=%u ch=%u bps=%u frames=%llu\n",
           flac->sampleRate, flac->channels, flac->bitsPerSample,
           (unsigned long long)flac->totalPCMFrameCount);

    if (flac->sampleRate != 22050u) { printf("FAIL rate\n"); ++failures; }
    if (flac->channels != 2u) { printf("FAIL channels\n"); ++failures; }
    if (flac->bitsPerSample != 16u) { printf("FAIL bits\n"); ++failures; }
    if (flac->totalPCMFrameCount != 21393u) { printf("FAIL frames\n"); ++failures; }

    while ((got = drflac_read_pcm_frames_f32(flac, 4096u, pcm)) > 0u) {
        uint64_t i;
        total_frames += got;
        for (i = 0u; i < got * (uint64_t)flac->channels; ++i) {
            ++samples;
            if (fabsf(pcm[i]) > 1e-6f) ++nonzero;
        }
    }
    drflac_close(flac);
    free(data);

    printf("codec: decoded=%llu frames, %llu samples, %llu nonzero\n",
           (unsigned long long)total_frames, (unsigned long long)samples,
           (unsigned long long)nonzero);
    if (total_frames == 0u) { printf("FAIL no frames decoded\n"); ++failures; }
    if (nonzero == 0u) { printf("FAIL PCM all zero\n"); ++failures; }
    if (samples > 0u && nonzero < samples / 2u) {
        printf("FAIL too few nonzero samples (%llu/%llu)\n",
               (unsigned long long)nonzero, (unsigned long long)samples);
        ++failures;
    }

    if (failures == 0) {
        printf("test_music_flac_codec PASS\n");
        return 0;
    }
    printf("test_music_flac_codec FAIL (%d)\n", failures);
    return 1;
}
