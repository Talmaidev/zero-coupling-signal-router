#include "music_t0_helper.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <zcsr/audio_mix.h>
#include "music_ffmpeg.h"

unsigned char* mt_read_file(const char* path, size_t* out_len) {
    FILE* f = fopen(path, "rb");
    unsigned char* buf;
    long sz;
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    sz = ftell(f);
    if (sz <= 0) { fclose(f); return NULL; }
    rewind(f);
    buf = (unsigned char*)malloc((size_t)sz);
    if (!buf) { fclose(f); return NULL; }
    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
        free(buf);
        fclose(f);
        return NULL;
    }
    fclose(f);
    if (out_len) *out_len = (size_t)sz;
    return buf;
}

static void join_path(char* dst, size_t cap, const char* dir, const char* file) {
    snprintf(dst, cap, "%s/%s", dir, file);
}

int mt_public_contract(const char* data_dir, const char* fixture,
                       zcsr_music_format fmt, uint32_t exp_rate,
                       uint16_t exp_ch) {
    char path[1024];
    size_t len = 0;
    unsigned char* data;
    zcsr_music_format detected = ZCSR_MUSIC_COUNT;
    size_t req;
    void* buf;
    zcsr_music* m;
    zcsr_music_spec spec;
    zcsr_mixer* mixer = NULL;
    zcsr_music_state st;
    int fails = 0;
    uint32_t i;

    join_path(path, sizeof(path), data_dir, fixture);
    data = mt_read_file(path, &len);
    if (!data) { printf("  [fail] cannot read %s\n", path); return 1; }

    if (!zcsr_music_detect_format(data, len, &detected) || detected != fmt) {
        printf("  [fail] detect: expected %d got %d\n", (int)fmt, (int)detected);
        ++fails;
    }

    req = zcsr_music_buffer_bytes_required(len);
    buf = malloc(req ? req : 1u);
    if (!buf) { free(data); return 1; }

    m = zcsr_music_load(data, len, fmt, buf, req);
    if (!m) {
        printf("  [fail] load returned NULL\n");
        ++fails;
        goto cleanup;
    }

    mixer = zcsr_mix_start();
    if (mixer && !zcsr_music_attach_mixer(m, mixer)) {
        printf("  [fail] attach_mixer failed\n");
        ++fails;
    }
    if (zcsr_music_set_threaded(m, false)) {
        if (!zcsr_music_play(m)) {
            printf("  [fail] play rejected\n");
            ++fails;
        }
        for (i = 0u; i < 4000u; ++i) {
            zcsr_music_pump(m, 2048u);
            st = zcsr_music_get_state(m);
            if (st == ZCSR_MUSIC_ENDED || st == ZCSR_MUSIC_ERROR) break;
        }
        st = zcsr_music_get_state(m);
        if (st == ZCSR_MUSIC_ERROR) {
            printf("  [fail] reached ERROR during drain\n");
            ++fails;
        }
    }

    if (!zcsr_music_get_spec(m, &spec)) {
        printf("  [fail] get_spec failed\n");
        ++fails;
    } else {
        if (spec.sample_rate != exp_rate) {
            printf("  [fail] sample_rate %u != expected %u\n",
                   spec.sample_rate, exp_rate);
            ++fails;
        }
        if (spec.channels != exp_ch) {
            printf("  [fail] channels %u != expected %u\n",
                   (unsigned)spec.channels, (unsigned)exp_ch);
            ++fails;
        }
        if (spec.sample_rate == 0 || spec.channels == 0) {
            printf("  [fail] zero rate/channels in spec\n");
            ++fails;
        }
        if (spec.total_frames == 0) {
            uint64_t dur = 0;
            if (!zcsr_music_duration_ms(m, &dur) || dur == 0u) {
                printf("  [warn] neither frames nor duration reported\n");
            }
        }
        printf("  spec: rate=%u channels=%u frames=%llu\n",
               spec.sample_rate, (unsigned)spec.channels,
               (unsigned long long)spec.total_frames);
    }

    if (!zcsr_music_seek_ms(m, 0u)) {
        printf("  [fail] seek to 0 rejected\n");
        ++fails;
    }
    zcsr_music_stop(m);

cleanup:
    if (m) zcsr_music_destroy(m);
    if (mixer) zcsr_mix_stop(mixer);
    free(buf);
    free(data);
    return fails;
}

long mt_ffmpeg_nonzero(const unsigned char* data, size_t len) {
    zcsr_ffmpeg_backend* b;
    int rate = 0, ch = 0;
    uint64_t frames = 0, dur = 0;
    float scratch[4096u * 2u];
    long nonzero = 0;
    size_t total = 0;
    int status = ZFF_OK;

    b = zcsr_ffmpeg_open(data, len, &rate, &ch, &frames, &dur);
    if (!b) return -1;
    while (total < 4096u) {
        size_t n = zcsr_ffmpeg_decode(b, scratch, 4096u, &status);
        size_t i;
        if (status != ZFF_OK || n == 0u) break;
        for (i = 0u; i < n * 2u; ++i) {
            if (scratch[i] > 1e-4f || scratch[i] < -1e-4f) ++nonzero;
        }
        total += n;
    }
    zcsr_ffmpeg_close(b);
    return nonzero;
}

long mt_wav_nonzero(const unsigned char* data, size_t len) {
    size_t k;
    size_t off = 0u, payload = 0u;
    size_t i;
    long nonzero = 0;

    if (len < 12u || memcmp(data, "RIFF", 4u) != 0 ||
        memcmp(data + 8u, "WAVE", 4u) != 0) {
        return -1;
    }
    /* Locate the `data` chunk directly; count non-zero payload bytes. For
     * 16-bit PCM a silent sample is two zero bytes, so a non-zero byte means
     * the payload is not decoded to silence. */
    for (k = 0u; k + 8u <= len; ++k) {
        if (memcmp(data + k, "data", 4u) == 0) {
            uint32_t sz = (uint32_t)data[k + 4u] |
                          ((uint32_t)data[k + 5u] << 8) |
                          ((uint32_t)data[k + 6u] << 16) |
                          ((uint32_t)data[k + 7u] << 24);
            off = k + 8u;
            payload = sz;
            if (off + payload > len) payload = len - off;
            break;
        }
    }
    if (off == 0u || payload == 0u) return -1;
    for (i = 0u; i < payload; ++i) {
        if (data[off + i] != 0u) ++nonzero;
    }
    return nonzero;
}
