/* q8_precision_test.c — Q8 fixed-point vs float volume precision comparison.
 *
 * Independent test: does NOT link or modify production audiomix.c.
 * Reimplements both mixing paths as pure functions for deterministic comparison.
 *
 * Build (Windows, from project root):
 *   cl /O2 /Fe:build\q8_precision_test.exe integration\q8_precision_test.c
 *
 * Build (gcc/clang):
 *   cc -O2 -o build/q8_precision_test integration/q8_precision_test.c -lm
 *
 * Outputs:
 *   - Console: error statistics per scenario
 *   - q8_out.wav / float_out.wav: A/B listening samples for worst-case scenario
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>

#define CHANNELS        16
#define PERIOD_FRAMES   512
#define SAMPLE_RATE     44100
#define TEST_SECONDS    3
#define TEST_FRAMES     (SAMPLE_RATE * TEST_SECONDS)

/* ---- Production path replicas (pure functions) ---- */

/* Float path: pre-007b70b implementation */
static inline int mix_sample_float(int16_t sample, float volume) {
    return (int)((float)sample * volume);
}

/* Q8 path: post-007b70b implementation */
static inline int32_t volume_to_q8(float volume) {
    return (int32_t)(volume * 256.0f + 0.5f);
}
static inline int mix_sample_q8(int16_t sample, int32_t volume_q8) {
    return (sample * volume_q8) >> 8;
}

/* Clamp (same as production zcsr_mix_clamp) */
static inline int16_t clamp_int(int v) {
    if (v > 32767) return 32767;
    if (v < -32768) return -32768;
    return (int16_t)v;
}

/* ---- Test signal generation ---- */

/* Generate a stereo sine wave at given frequency and amplitude */
static void gen_sine(int16_t* buf, int frames, float freq, float amplitude) {
    for (int i = 0; i < frames; i++) {
        float t = (float)i / SAMPLE_RATE;
        float v = sinf(2.0f * 3.14159265f * freq * t) * amplitude * 32767.0f;
        int16_t s = (int16_t)(v > 32767 ? 32767 : (v < -32768 ? -32768 : v));
        buf[i * 2] = s;
        buf[i * 2 + 1] = s;
    }
}

/* Generate silence */
static void gen_silence(int16_t* buf, int frames) {
    memset(buf, 0, frames * 2 * sizeof(int16_t));
}

/* ---- Error statistics ---- */

typedef struct {
    int64_t max_abs_error;
    double  rms_error;
    int64_t diff_samples;
    int64_t total_samples;
    int64_t clamp_diff;       /* samples where clamp results differ */
    double  max_relative_error; /* relative to full scale 32768 */
} ErrorStats;

static void reset_stats(ErrorStats* s) {
    memset(s, 0, sizeof(*s));
}

static void update_stats(ErrorStats* s, int q8, int flt, int q8_clamped, int flt_clamped) {
    int err = q8 - flt;
    int abs_err = abs(err);
    s->total_samples++;
    if (err != 0) s->diff_samples++;
    if (abs_err > s->max_abs_error) s->max_abs_error = abs_err;
    s->rms_error += (double)err * (double)err;
    if (q8_clamped != flt_clamped) s->clamp_diff++;
}

static void finalize_stats(ErrorStats* s) {
    if (s->total_samples > 0) {
        s->rms_error = sqrt(s->rms_error / (double)s->total_samples);
        s->max_relative_error = (double)s->max_abs_error / 32768.0 * 100.0;
    }
}

/* ---- Mixing engine: runs both paths on same input ---- */

typedef struct {
    int16_t* samples;   /* interleaved stereo source */
    int      frames;
    float    volume;
    int32_t  volume_q8;
    int      active;
} TestChannel;

/* Mix N channels using float path */
static void mix_float(TestChannel* chans, int nch, int16_t* out, int frames) {
    for (int f = 0; f < frames; f++) {
        int left = 0, right = 0;
        for (int c = 0; c < nch; c++) {
            if (!chans[c].active) continue;
            uint32_t idx = (f % chans[c].frames) * 2U;
            left  += mix_sample_float(chans[c].samples[idx],     chans[c].volume);
            right += mix_sample_float(chans[c].samples[idx + 1], chans[c].volume);
        }
        out[f * 2]     = clamp_int(left);
        out[f * 2 + 1] = clamp_int(right);
    }
}

/* Mix N channels using Q8 path */
static void mix_q8(TestChannel* chans, int nch, int16_t* out, int frames) {
    for (int f = 0; f < frames; f++) {
        int left = 0, right = 0;
        for (int c = 0; c < nch; c++) {
            if (!chans[c].active) continue;
            uint32_t idx = (f % chans[c].frames) * 2U;
            left  += mix_sample_q8(chans[c].samples[idx],     chans[c].volume_q8);
            right += mix_sample_q8(chans[c].samples[idx + 1], chans[c].volume_q8);
        }
        out[f * 2]     = clamp_int(left);
        out[f * 2 + 1] = clamp_int(right);
    }
}

/* Compare and collect stats, also returns pre-clamp sums for clamp analysis */
static void compare_and_stats(TestChannel* chans, int nch, int frames, ErrorStats* stats) {
    for (int f = 0; f < frames; f++) {
        int fl = 0, fr = 0, ql = 0, qr = 0;
        for (int c = 0; c < nch; c++) {
            if (!chans[c].active) continue;
            uint32_t idx = (f % chans[c].frames) * 2U;
            fl += mix_sample_float(chans[c].samples[idx],     chans[c].volume);
            fr += mix_sample_float(chans[c].samples[idx + 1], chans[c].volume);
            ql += mix_sample_q8(chans[c].samples[idx],     chans[c].volume_q8);
            qr += mix_sample_q8(chans[c].samples[idx + 1], chans[c].volume_q8);
        }
        int16_t fl_c = clamp_int(fl), fr_c = clamp_int(fr);
        int16_t ql_c = clamp_int(ql), qr_c = clamp_int(qr);
        update_stats(stats, ql, fl, ql_c, fl_c);
        update_stats(stats, qr, fr, qr_c, fr_c);
    }
}

/* ---- WAV output (for A/B listening) ---- */

static void write_wav(const char* path, const int16_t* data, int frames) {
    FILE* f = fopen(path, "wb");
    if (!f) { printf("WARN: cannot write %s\n", path); return; }
    int data_size = frames * 2 * sizeof(int16_t);
    int riff_size = 36 + data_size;
    /* RIFF header */
    fwrite("RIFF", 1, 4, f);
    fwrite(&riff_size, 4, 1, f);
    fwrite("WAVE", 1, 4, f);
    /* fmt chunk */
    fwrite("fmt ", 1, 4, f);
    int fmt_size = 16;
    fwrite(&fmt_size, 4, 1, f);
    short audio_format = 1; /* PCM */
    short num_channels = 2;
    int sample_rate = SAMPLE_RATE;
    int byte_rate = SAMPLE_RATE * 2 * 2;
    short block_align = 4;
    short bits_per_sample = 16;
    fwrite(&audio_format, 2, 1, f);
    fwrite(&num_channels, 2, 1, f);
    fwrite(&sample_rate, 4, 1, f);
    fwrite(&byte_rate, 4, 1, f);
    fwrite(&block_align, 2, 1, f);
    fwrite(&bits_per_sample, 2, 1, f);
    /* data chunk */
    fwrite("data", 1, 4, f);
    fwrite(&data_size, 4, 1, f);
    fwrite(data, sizeof(int16_t), frames * 2, f);
    fclose(f);
    printf("  WROTE: %s (%d frames, %.1fs)\n", path, frames, (float)frames / SAMPLE_RATE);
}

/* ---- Scenario runners ---- */

static void run_scenario(const char* name, TestChannel* chans, int nch, int frames) {
    ErrorStats stats;
    reset_stats(&stats);
    compare_and_stats(chans, nch, frames, &stats);
    finalize_stats(&stats);
    printf("  %-40s max_abs=%4lld  rms=%6.2f  diff=%5lld/%lld (%.1f%%)  clamp_diff=%4lld  max_rel=%.4f%%\n",
           name,
           (long long)stats.max_abs_error,
           stats.rms_error,
           (long long)stats.diff_samples,
           (long long)stats.total_samples,
           stats.total_samples > 0 ? 100.0 * stats.diff_samples / stats.total_samples : 0,
           (long long)stats.clamp_diff,
           stats.max_relative_error);
}

/* Setup helper: allocate and fill a channel with sine */
static void setup_channel(TestChannel* ch, int frames, float freq, float amplitude, float volume) {
    ch->samples = (int16_t*)malloc(frames * 2 * sizeof(int16_t));
    ch->frames = frames;
    ch->volume = volume;
    ch->volume_q8 = volume_to_q8(volume);
    ch->active = 1;
    gen_sine(ch->samples, frames, freq, amplitude);
}

static void free_channels(TestChannel* chans, int nch) {
    for (int i = 0; i < nch; i++) {
        if (chans[i].samples) free(chans[i].samples);
        chans[i].samples = NULL;
    }
}

int main(void) {
    printf("=== zcsr audiomix Q8 fixed-point vs float volume precision test ===\n");
    printf("Test frames: %d (%.1fs), sample rate: %d Hz\n\n", TEST_FRAMES, (float)TEST_FRAMES / SAMPLE_RATE, SAMPLE_RATE);
    printf("Q8 conversion: volume_q8 = (int)(volume * 256 + 0.5)\n");
    printf("Float path:   (int)((float)sample * volume)\n");
    printf("Q8 path:      (sample * volume_q8) >> 8\n\n");

    int sig_frames = PERIOD_FRAMES; /* one period, loops in mixer */

    /* ---- Scenario 1: Single channel, various volumes ---- */
    printf("--- Scenario 1: Single channel, 440Hz sine, various volumes ---\n");
    {
        float volumes[] = {0.0f, 0.1f, 0.25f, 0.333f, 0.5f, 0.75f, 0.9f, 1.0f};
        for (int vi = 0; vi < (int)(sizeof(volumes)/sizeof(volumes[0])); vi++) {
            TestChannel ch;
            memset(&ch, 0, sizeof(ch));
            setup_channel(&ch, sig_frames, 440.0f, 1.0f, volumes[vi]);
            char label[64];
            snprintf(label, sizeof(label), "vol=%.2f (q8=%d)", volumes[vi], ch.volume_q8);
            run_scenario(label, &ch, 1, TEST_FRAMES);
            free_channels(&ch, 1);
        }
    }

    /* ---- Scenario 2: 16 channels, same phase, same volume (constructive) ---- */
    printf("\n--- Scenario 2: 16 channels, same phase (440Hz), various volumes ---\n");
    {
        float volumes[] = {0.0625f, 0.1f, 0.25f, 0.5f, 1.0f};
        for (int vi = 0; vi < (int)(sizeof(volumes)/sizeof(volumes[0])); vi++) {
            TestChannel chans[CHANNELS];
            memset(chans, 0, sizeof(chans));
            for (int c = 0; c < CHANNELS; c++) {
                setup_channel(&chans[c], sig_frames, 440.0f, 1.0f, volumes[vi]);
            }
            char label[64];
            snprintf(label, sizeof(label), "16ch in-phase vol=%.3f", volumes[vi]);
            run_scenario(label, chans, CHANNELS, TEST_FRAMES);
            free_channels(chans, CHANNELS);
        }
    }

    /* ---- Scenario 3: 16 channels, alternating phase (destructive/cancellation) ---- */
    printf("\n--- Scenario 3: 16 channels, alternating phase (440Hz/880Hz), vol=0.5 ---\n");
    {
        TestChannel chans[CHANNELS];
        memset(chans, 0, sizeof(chans));
        for (int c = 0; c < CHANNELS; c++) {
            /* Alternate frequencies to create complex interference */
            float freq = (c % 2 == 0) ? 440.0f : 880.0f;
            setup_channel(&chans[c], sig_frames, freq, 1.0f, 0.5f);
        }
        run_scenario("16ch mixed freq vol=0.5", chans, CHANNELS, TEST_FRAMES);

        /* Generate WAV for A/B */
        int16_t* q8_out = (int16_t*)malloc(TEST_FRAMES * 2 * sizeof(int16_t));
        int16_t* fl_out = (int16_t*)malloc(TEST_FRAMES * 2 * sizeof(int16_t));
        mix_q8(chans, CHANNELS, q8_out, TEST_FRAMES);
        mix_float(chans, CHANNELS, fl_out, TEST_FRAMES);
        write_wav("q8_out.wav", q8_out, TEST_FRAMES);
        write_wav("float_out.wav", fl_out, TEST_FRAMES);
        free(q8_out);
        free(fl_out);
        free_channels(chans, CHANNELS);
    }

    /* ---- Scenario 4: Near clipping (16ch, high volume, should clip) ---- */
    printf("\n--- Scenario 4: Clipping boundary (16ch in-phase, high volume) ---\n");
    {
        float volumes[] = {0.5f, 0.6f, 0.7f, 0.8f, 0.9f, 1.0f};
        for (int vi = 0; vi < (int)(sizeof(volumes)/sizeof(volumes[0])); vi++) {
            TestChannel chans[CHANNELS];
            memset(chans, 0, sizeof(chans));
            for (int c = 0; c < CHANNELS; c++) {
                setup_channel(&chans[c], sig_frames, 440.0f, 1.0f, volumes[vi]);
            }
            char label[64];
            snprintf(label, sizeof(label), "16ch in-phase vol=%.1f (clip?)", volumes[vi]);
            run_scenario(label, chans, CHANNELS, TEST_FRAMES);
            free_channels(chans, CHANNELS);
        }
    }

    /* ---- Scenario 5: Random volumes, mixed frequencies (worst-case realistic) ---- */
    printf("\n--- Scenario 5: 16ch random volumes & frequencies (realistic mix) ---\n");
    {
        TestChannel chans[CHANNELS];
        memset(chans, 0, sizeof(chans));
        /* Deterministic pseudo-random using LCG */
        unsigned seed = 12345;
        for (int c = 0; c < CHANNELS; c++) {
            seed = seed * 1103515245 + 12345;
            float vol = 0.1f + (float)(seed % 90) / 100.0f; /* 0.1 - 0.99 */
            seed = seed * 1103515245 + 12345;
            float freq = 100.0f + (float)(seed % 2000); /* 100 - 2099 Hz */
            setup_channel(&chans[c], sig_frames, freq, 1.0f, vol);
        }
        run_scenario("16ch random vol/freq", chans, CHANNELS, TEST_FRAMES);

        /* Also generate WAV for this realistic scenario */
        int16_t* q8_out = (int16_t*)malloc(TEST_FRAMES * 2 * sizeof(int16_t));
        int16_t* fl_out = (int16_t*)malloc(TEST_FRAMES * 2 * sizeof(int16_t));
        mix_q8(chans, CHANNELS, q8_out, TEST_FRAMES);
        mix_float(chans, CHANNELS, fl_out, TEST_FRAMES);
        write_wav("q8_realistic.wav", q8_out, TEST_FRAMES);
        write_wav("float_realistic.wav", fl_out, TEST_FRAMES);
        free(q8_out);
        free(fl_out);
        free_channels(chans, CHANNELS);
    }

    /* ---- Scenario 6: Single sample edge cases ---- */
    printf("\n--- Scenario 6: Single sample edge cases (max positive/negative) ---\n");
    {
        int16_t edge_samples[] = {32767, -32768, 1, -1, 0, 16384, -16384};
        float volumes[] = {1.0f, 0.5f, 0.333f, 0.1f};
        for (int si = 0; si < (int)(sizeof(edge_samples)/sizeof(edge_samples[0])); si++) {
            for (int vi = 0; vi < (int)(sizeof(volumes)/sizeof(volumes[0])); vi++) {
                int q8 = mix_sample_q8(edge_samples[si], volume_to_q8(volumes[vi]));
                int fl = mix_sample_float(edge_samples[si], volumes[vi]);
                if (q8 != fl) {
                    printf("  sample=%6d vol=%.3f q8=%6d float=%6d diff=%+d\n",
                           edge_samples[si], volumes[vi], q8, fl, q8 - fl);
                }
            }
        }
        printf("  (only differing samples shown above)\n");
    }

    printf("\n=== Test complete ===\n");
    printf("WAV files generated for A/B listening: q8_out.wav, float_out.wav, q8_realistic.wav, float_realistic.wav\n");
    return 0;
}
