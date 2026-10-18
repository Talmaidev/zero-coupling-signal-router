#if !defined(__APPLE__)
#error "verify_macos.c can only be compiled on macOS"
#endif

#define _POSIX_C_SOURCE 200809L

#include "zcsr/audio_mix.h"

#include <math.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

enum {
    VERIFY_SAMPLE_RATE = 44100,
    VERIFY_CHANNELS = 2,
    VERIFY_SECONDS = 1,
    VERIFY_RUN_SECONDS = 30
};

typedef struct verify_audio_stats {
    atomic_uint lost;
    atomic_uint changed;
    atomic_uint recovered;
} verify_audio_stats;

static const char* verify_event_name(zcsr_audio_device_event event) {
    switch (event) {
        case ZCSR_AUDIO_DEVICE_LOST:
            return "LOST";
        case ZCSR_AUDIO_DEVICE_CHANGED:
            return "CHANGED";
        case ZCSR_AUDIO_DEVICE_RECOVERED:
            return "RECOVERED";
        default:
            return "UNKNOWN";
    }
}

static void verify_device_callback(
    zcsr_mixer* mixer,
    zcsr_audio_device_event event,
    void* user) {
    verify_audio_stats* stats = (verify_audio_stats*)user;

    (void)mixer;

    if (!stats) return;

    switch (event) {
        case ZCSR_AUDIO_DEVICE_LOST:
            atomic_fetch_add_explicit(
                &stats->lost, 1U, memory_order_relaxed);
            break;

        case ZCSR_AUDIO_DEVICE_CHANGED:
            atomic_fetch_add_explicit(
                &stats->changed, 1U, memory_order_relaxed);
            break;

        case ZCSR_AUDIO_DEVICE_RECOVERED:
            atomic_fetch_add_explicit(
                &stats->recovered, 1U, memory_order_relaxed);
            break;

        default:
            break;
    }

    printf("[device] %s\n", verify_event_name(event));
    fflush(stdout);
}

static bool verify_write_bytes(FILE* file, const void* data, size_t bytes) {
    return file && data && fwrite(data, 1U, bytes, file) == bytes;
}

static bool verify_write_u16le(FILE* file, uint16_t value) {
    unsigned char bytes[2];

    bytes[0] = (unsigned char)(value & 0xffU);
    bytes[1] = (unsigned char)((value >> 8) & 0xffU);

    return verify_write_bytes(file, bytes, sizeof(bytes));
}

static bool verify_write_u32le(FILE* file, uint32_t value) {
    unsigned char bytes[4];

    bytes[0] = (unsigned char)(value & 0xffU);
    bytes[1] = (unsigned char)((value >> 8) & 0xffU);
    bytes[2] = (unsigned char)((value >> 16) & 0xffU);
    bytes[3] = (unsigned char)((value >> 24) & 0xffU);

    return verify_write_bytes(file, bytes, sizeof(bytes));
}

static bool verify_write_wav(FILE* file) {
    const uint32_t frame_count =
        (uint32_t)(VERIFY_SAMPLE_RATE * VERIFY_SECONDS);
    const uint32_t data_bytes =
        frame_count * VERIFY_CHANNELS * (uint32_t)sizeof(int16_t);
    const uint32_t riff_bytes = 36U + data_bytes;

    if (!verify_write_bytes(file, "RIFF", 4U) ||
        !verify_write_u32le(file, riff_bytes) ||
        !verify_write_bytes(file, "WAVE", 4U) ||
        !verify_write_bytes(file, "fmt ", 4U) ||
        !verify_write_u32le(file, 16U) ||
        !verify_write_u16le(file, 1U) ||
        !verify_write_u16le(file, VERIFY_CHANNELS) ||
        !verify_write_u32le(file, VERIFY_SAMPLE_RATE) ||
        !verify_write_u32le(
            file,
            VERIFY_SAMPLE_RATE * VERIFY_CHANNELS * 2U) ||
        !verify_write_u16le(file, (uint16_t)(VERIFY_CHANNELS * 2U)) ||
        !verify_write_u16le(file, 16U) ||
        !verify_write_bytes(file, "data", 4U) ||
        !verify_write_u32le(file, data_bytes)) {
        return false;
    }

    for (uint32_t frame = 0; frame < frame_count; ++frame) {
        const double phase =
            6.28318530717958647692 * 440.0 *
            (double)frame / (double)VERIFY_SAMPLE_RATE;
        const int16_t sample = (int16_t)(sin(phase) * 9000.0);
        const uint16_t raw = (uint16_t)sample;

        if (!verify_write_u16le(file, raw) ||
            !verify_write_u16le(file, raw)) {
            return false;
        }
    }

    return true;
}

static double verify_elapsed_seconds(
    const struct timespec* start,
    const struct timespec* now) {
    return (double)(now->tv_sec - start->tv_sec) +
           (double)(now->tv_nsec - start->tv_nsec) / 1000000000.0;
}

int main(void) {
    zcsr_mixer* mixer;
    verify_audio_stats stats;
    char wav_path[] = "/tmp/zcsr_verify_macos_XXXXXX";
    int fd;
    FILE* file;
    bool wav_ok;
    bool registered;
    int channel;
    unsigned finished_total = 0U;
    struct timespec started;
    struct timespec now;
    struct timespec nap = { 0, 100000000L };

    atomic_init(&stats.lost, 0U);
    atomic_init(&stats.changed, 0U);
    atomic_init(&stats.recovered, 0U);

    mixer = zcsr_mix_start();
    if (!mixer) {
        fprintf(stderr, "zcsr_mix_start failed\n");
        return 1;
    }

    zcsr_mix_set_device_callback(
        mixer, verify_device_callback, &stats);

    fd = mkstemp(wav_path);
    if (fd < 0) {
        perror("mkstemp");
        zcsr_mix_set_device_callback(mixer, NULL, NULL);
        zcsr_mix_stop(mixer);
        return 1;
    }

    file = fdopen(fd, "wb");
    if (!file) {
        perror("fdopen");
        close(fd);
        unlink(wav_path);
        zcsr_mix_set_device_callback(mixer, NULL, NULL);
        zcsr_mix_stop(mixer);
        return 1;
    }

    wav_ok = verify_write_wav(file);
    if (fclose(file) != 0) {
        wav_ok = false;
    }

    if (!wav_ok) {
        fprintf(stderr, "failed to write temporary WAV\n");
        unlink(wav_path);
        zcsr_mix_set_device_callback(mixer, NULL, NULL);
        zcsr_mix_stop(mixer);
        return 1;
    }

    registered = zcsr_mix_register_wav(mixer, "verify440", wav_path);
    unlink(wav_path);

    if (!registered) {
        fprintf(stderr, "zcsr_mix_register_wav failed\n");
        zcsr_mix_set_device_callback(mixer, NULL, NULL);
        zcsr_mix_stop(mixer);
        return 1;
    }

    channel = zcsr_mix_play(mixer, "verify440", 1.0f, false);
    if (channel < 0) {
        fprintf(stderr, "zcsr_mix_play failed\n");
        zcsr_mix_set_device_callback(mixer, NULL, NULL);
        zcsr_mix_stop(mixer);
        return 1;
    }

    printf("Started 440 Hz playback on channel %d.\n", channel);
    printf("请在 macOS 系统声音设置中切换输出设备，或拔插耳机。\n");
    printf("观察接下来的 30 秒内的 LOST / CHANGED / RECOVERED 事件。\n");
    fflush(stdout);

    if (clock_gettime(CLOCK_MONOTONIC, &started) != 0) {
        perror("clock_gettime");
        zcsr_mix_set_device_callback(mixer, NULL, NULL);
        zcsr_mix_stop(mixer);
        return 1;
    }

    for (;;) {
        int finished[ZCSR_MIX_CHANNELS];
        size_t count;

        if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
            perror("clock_gettime");
            break;
        }

        if (verify_elapsed_seconds(&started, &now) >=
            (double)VERIFY_RUN_SECONDS) {
            break;
        }

        count = zcsr_mix_poll_finished(
            mixer, finished, ZCSR_MIX_CHANNELS);

        for (size_t i = 0; i < count; ++i) {
            printf("[finished] channel=%d\n", finished[i]);
            ++finished_total;
        }

        (void)nanosleep(&nap, NULL);
    }

    printf("\nmacOS AudioUnit verification statistics:\n");
    printf("LOST=%u\n",
        atomic_load_explicit(&stats.lost, memory_order_relaxed));
    printf("CHANGED=%u\n",
        atomic_load_explicit(&stats.changed, memory_order_relaxed));
    printf("RECOVERED=%u\n",
        atomic_load_explicit(&stats.recovered, memory_order_relaxed));
    printf("finished_channels=%u\n", finished_total);

    zcsr_mix_set_device_callback(mixer, NULL, NULL);
    zcsr_mix_stop(mixer);
    return 0;
}
