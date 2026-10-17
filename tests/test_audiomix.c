#include "zcsr/audio_mix.h"
#include "zcsr/signal.h"

#include <stdint.h>
#include <stdio.h>
#include <assert.h>
#include <stdatomic.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
static void test_sleep_ms(unsigned ms) { Sleep(ms); }
#else
#include <time.h>
static void test_sleep_ms(unsigned ms) {
    struct timespec ts;
    ts.tv_sec = (time_t)(ms / 1000U);
    ts.tv_nsec = (long)(ms % 1000U) * 1000000L;
    nanosleep(&ts, 0);
}
#endif

static bool slot_play_seen;
static bool slot_stop_seen;

static bool slot_play_probe(const char* payload) {
    (void)payload;
    slot_play_seen = true;
    return zcsr_mix_slot_play(payload);
}

static bool slot_stop_probe(const char* payload) {
    (void)payload;
    slot_stop_seen = true;
    return zcsr_mix_slot_stop(payload);
}

#define MIX_TEST_CONNS(X)            \
    X("PLAY_SOUND", slot_play_probe) \
    X("STOP_SOUND", slot_stop_probe)
ZCSR_DEFINE_ROUTER(mix_test, MIX_TEST_CONNS)

static void put_u16(FILE* f, uint16_t v) {
    fputc((int)(v & 0xffU), f);
    fputc((int)((v >> 8) & 0xffU), f);
}

static void put_u32(FILE* f, uint32_t v) {
    fputc((int)(v & 0xffU), f);
    fputc((int)((v >> 8) & 0xffU), f);
    fputc((int)((v >> 16) & 0xffU), f);
    fputc((int)((v >> 24) & 0xffU), f);
}

static int write_wav(const char* path, uint32_t frames) {
    FILE* f = fopen(path, "wb");
    uint32_t data_bytes = frames * 2U * 2U;
    if (!f) return 0;
    fwrite("RIFF", 1, 4, f);
    put_u32(f, 36U + data_bytes);
    fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f);
    put_u32(f, 16);
    put_u16(f, 1);
    put_u16(f, 2);
    put_u32(f, 44100);
    put_u32(f, 44100U * 2U * 2U);
    put_u16(f, 4);
    put_u16(f, 16);
    fwrite("data", 1, 4, f);
    put_u32(f, data_bytes);
    for (uint32_t i = 0; i < frames; ++i) {
        int16_t s = (i & 1U) ? 12000 : -12000;
        put_u16(f, (uint16_t)s);
        put_u16(f, (uint16_t)-s);
    }
    fclose(f);
    return 1;
}

typedef struct zcsr_test_audio_device_callback_state {
    atomic_uint calls;
    atomic_bool bad_event;
} zcsr_test_audio_device_callback_state;

static void zcsr_test_audio_device_callback(
    zcsr_mixer* mixer,
    zcsr_audio_device_event event,
    void* user) {
    zcsr_test_audio_device_callback_state* state =
        (zcsr_test_audio_device_callback_state*)user;

    (void)mixer;

    if (!state) return;

    if (event != ZCSR_AUDIO_DEVICE_LOST &&
        event != ZCSR_AUDIO_DEVICE_CHANGED &&
        event != ZCSR_AUDIO_DEVICE_RECOVERED) {
        atomic_store_explicit(
            &state->bad_event, true, memory_order_release);
    }

    atomic_fetch_add_explicit(
        &state->calls, 1U, memory_order_relaxed);
}

static void test_audiomix_device_callback_api(void) {
    zcsr_mixer* mixer;
    zcsr_test_audio_device_callback_state first;
    zcsr_test_audio_device_callback_state second;

    atomic_init(&first.calls, 0U);
    atomic_init(&first.bad_event, false);

    atomic_init(&second.calls, 0U);
    atomic_init(&second.bad_event, false);

    mixer = zcsr_mix_start();

    /*
     * Follow the existing audio-test policy if the current target has no
     * usable mixer backend. The Linux headless path should still be able to
     * exercise the mixer logic, as documented by audiomix.c.
     */
    if (!mixer) {
        return;
    }

    /* Register, then replace the callback/user pair. */
    zcsr_mix_set_device_callback(
        mixer, zcsr_test_audio_device_callback, &first);

    zcsr_mix_set_device_callback(
        mixer, zcsr_test_audio_device_callback, &second);

    /* Unregister before stopping so both stack-owned user objects stay valid. */
    zcsr_mix_set_device_callback(mixer, NULL, NULL);

    zcsr_mix_stop(mixer);

    /*
     * No hardware event is injected. If a real platform event arrived during
     * the test, it must still map to one of the three public enum values.
     */
    assert(!atomic_load_explicit(
        &first.bad_event, memory_order_acquire));

    assert(!atomic_load_explicit(
        &second.bad_event, memory_order_acquire));
}

int main(void) {
    const char* wav = "zcsr_audiomix_test.wav";
    int finished[32];
    if (!write_wav(wav, 1000)) {
        printf("FAIL: could not write wav fixture\n");
        return 1;
    }

    zcsr_mixer* m = zcsr_mix_start();
    if (!m) {
        printf("FAIL: mixer did not start\n");
        return 1;
    }
    if (!zcsr_mix_register_wav(m, "tick", wav)) {
        printf("FAIL: register wav\n");
        zcsr_mix_stop(m);
        return 1;
    }
    if (zcsr_mix_register_wav(m, "tick", wav)) {
        printf("FAIL: duplicate id accepted\n");
        zcsr_mix_stop(m);
        return 1;
    }
    if (zcsr_mix_play(m, "missing", 1.0f, false) != -1) {
        printf("FAIL: missing id played\n");
        zcsr_mix_stop(m);
        return 1;
    }

    int channels[ZCSR_MIX_CHANNELS];
    for (int i = 0; i < ZCSR_MIX_CHANNELS; ++i) {
        channels[i] = zcsr_mix_play(m, "tick", 0.75f, false);
        if (channels[i] < 0) {
            printf("FAIL: channel %d did not start\n", i);
            zcsr_mix_stop(m);
            return 1;
        }
    }
    if (zcsr_mix_play(m, "tick", 1.0f, false) != -1) {
        printf("FAIL: channel overflow accepted\n");
        zcsr_mix_stop(m);
        return 1;
    }

    test_sleep_ms(160);
    size_t n = zcsr_mix_poll_finished(m, finished, 32);
    if (n == 0) {
        printf("FAIL: no finished channels reported\n");
        zcsr_mix_stop(m);
        return 1;
    }

    int loop = zcsr_mix_play(m, "tick", 1.0f, true);
    if (loop < 0) {
        printf("FAIL: loop channel did not start\n");
        zcsr_mix_stop(m);
        return 1;
    }
    zcsr_mix_stop_channel(m, loop);
    test_sleep_ms(40);
    (void)zcsr_mix_poll_finished(m, finished, 32);

    slot_play_seen = false;
    slot_stop_seen = false;
    mix_test_emit("PLAY_SOUND", "tick");
    mix_test_emit("STOP_SOUND", "tick");
    if (!slot_play_seen || !slot_stop_seen) {
        printf("FAIL: signal slots\n");
        zcsr_mix_stop(m);
        return 1;
    }

    zcsr_mix_stop(m);

    test_audiomix_device_callback_api();

    printf("PASS: audiomix logic\n");
    return 0;
}
