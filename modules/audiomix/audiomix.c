/* Engine-ext Agent 2 - multi-channel audio mixer.
 * Fixed pools only, one mixer thread, command/finished rings, no cross-module includes.
 * Linux attempts ALSA dynamically (no CMake find_package); if no device exists, the mixer
 * still runs its logic path so headless CI can validate threading/state.
 */
#include "zcsr/audio_mix.h"
#include "audiomix_internal.h"

#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#if defined(__ANDROID__)
#include <aaudio/AAudio.h>
#include <dlfcn.h>
#include <pthread.h>
#include <time.h>
#elif defined(__APPLE__)
#include <AudioToolbox/AudioToolbox.h>
#include <AudioUnit/AudioUnit.h>
#include <CoreAudio/CoreAudio.h>
#include <pthread.h>
#include <time.h>
#elif defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define CINTERFACE
#include <initguid.h>
#include <windows.h>
#include <objbase.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <mmsystem.h>
#include <limits.h>
#include <wchar.h>

DEFINE_GUID(CLSID_MMDeviceEnumerator, 0xBCDE0395, 0xE52F, 0x467C, 0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E);
DEFINE_GUID(IID_IMMDeviceEnumerator, 0xA95664D2, 0x9614, 0x4F35, 0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6);
DEFINE_GUID(IID_IAudioClient, 0x1CB9AD4C, 0xDBFA, 0x4C32, 0xB1, 0x78, 0x2A, 0x69, 0x72, 0x6B, 0x5C, 0x3F);
DEFINE_GUID(IID_IAudioRenderClient, 0xF294ACFC, 0x3146, 0x4483, 0xA7, 0xBF, 0xAD, 0xDC, 0xA7, 0xC2, 0x60, 0xE2);
DEFINE_GUID(IID_IMMNotificationClient, 0x7991EEC9, 0x7E89, 0x4D85, 0x83, 0x90, 0x6C, 0x70, 0x3C, 0xEC, 0x60, 0xC0);
#else
#include <pthread.h>
#include <time.h>
#endif

#if defined(__ANDROID__)
typedef aaudio_result_t (*zcsr_aaudio_create_builder_fn)(AAudioStreamBuilder**);
typedef void (*zcsr_aaudio_builder_direction_fn)(AAudioStreamBuilder*, aaudio_direction_t);
typedef void (*zcsr_aaudio_builder_format_fn)(AAudioStreamBuilder*, aaudio_format_t);
typedef void (*zcsr_aaudio_builder_int_fn)(AAudioStreamBuilder*, int32_t);
typedef void (*zcsr_aaudio_builder_performance_fn)(AAudioStreamBuilder*, aaudio_performance_mode_t);
typedef void (*zcsr_aaudio_builder_sharing_fn)(AAudioStreamBuilder*, aaudio_sharing_mode_t);
typedef void (*zcsr_aaudio_data_callback_set_fn)(
    AAudioStreamBuilder*, AAudioStream_dataCallback, void*);
/* Optional symbol: missing this setter must not prevent stream creation. */
typedef void (*zcsr_aaudio_error_callback_set_fn)(
    AAudioStreamBuilder*, AAudioStream_errorCallback, void*);
typedef aaudio_result_t (*zcsr_aaudio_open_stream_fn)(AAudioStreamBuilder*, AAudioStream**);
typedef void (*zcsr_aaudio_delete_builder_fn)(AAudioStreamBuilder*);
typedef aaudio_result_t (*zcsr_aaudio_stream_action_fn)(AAudioStream*);
typedef aaudio_result_t (*zcsr_aaudio_close_stream_fn)(AAudioStream*);
typedef union {
    void* obj;
    zcsr_aaudio_create_builder_fn create_builder;
    zcsr_aaudio_builder_direction_fn set_direction;
    zcsr_aaudio_builder_format_fn set_format;
    zcsr_aaudio_builder_int_fn set_channel_count;
    zcsr_aaudio_builder_int_fn set_sample_rate;
    zcsr_aaudio_builder_performance_fn set_performance_mode;
    zcsr_aaudio_builder_sharing_fn set_sharing_mode;
    zcsr_aaudio_data_callback_set_fn set_data_callback;
    zcsr_aaudio_error_callback_set_fn set_error_callback;
    zcsr_aaudio_open_stream_fn open_stream;
    zcsr_aaudio_delete_builder_fn delete_builder;
    zcsr_aaudio_stream_action_fn request_start;
    zcsr_aaudio_stream_action_fn request_stop;
    zcsr_aaudio_close_stream_fn close_stream;
} zcsr_aaudio_sym;
typedef struct {
    zcsr_aaudio_create_builder_fn create_builder;
    zcsr_aaudio_builder_direction_fn set_direction;
    zcsr_aaudio_builder_format_fn set_format;
    zcsr_aaudio_builder_int_fn set_channel_count;
    zcsr_aaudio_builder_int_fn set_sample_rate;
    zcsr_aaudio_builder_performance_fn set_performance_mode;
    zcsr_aaudio_builder_sharing_fn set_sharing_mode;
    zcsr_aaudio_data_callback_set_fn set_data_callback;
    /* Optional; NULL means this platform/runtime lacks error notifications. */
    zcsr_aaudio_error_callback_set_fn set_error_callback;
    zcsr_aaudio_open_stream_fn open_stream;
    zcsr_aaudio_delete_builder_fn delete_builder;
    zcsr_aaudio_stream_action_fn request_start;
    zcsr_aaudio_stream_action_fn request_stop;
    zcsr_aaudio_close_stream_fn close_stream;
} zcsr_aaudio_api;
#endif

enum {
    ZCSR_MIX_POOL = 1,
    ZCSR_MIX_MAX_SOUNDS = 32,
    ZCSR_MIX_ID_CAP = 64,
    ZCSR_MIX_CMD_CAP = 128,
    ZCSR_MIX_FINISHED_CAP = 128,
    ZCSR_MIX_SAMPLE_FRAMES = ZCSR_MIX_RATE * 20
};

enum {
    ZCSR_MIX_CH_FREE = 0,
    ZCSR_MIX_CH_RESERVED = 1,
    ZCSR_MIX_CH_ACTIVE = 2
};

enum {
    ZCSR_MIX_DEVICE_RUNNING = 0,
    ZCSR_MIX_DEVICE_CHANGE_PENDING = 1,
    ZCSR_MIX_DEVICE_REBUILDING = 2,
    ZCSR_MIX_DEVICE_RECOVERY_PENDING = 3
};

enum {
    ZCSR_MIX_DEVICE_PENDING_LOST = 1U << 0,
    ZCSR_MIX_DEVICE_PENDING_CHANGED = 1U << 1
};

#define ZCSR_MIX_DEVICE_PENDING_MASK \
    (ZCSR_MIX_DEVICE_PENDING_LOST | \
     ZCSR_MIX_DEVICE_PENDING_CHANGED)

typedef enum {
    ZCSR_MIX_CMD_PLAY = 1,
    ZCSR_MIX_CMD_STOP_CHANNEL = 2,
    ZCSR_MIX_CMD_STOP_ID = 3
} zcsr_mix_cmd_type;

typedef struct {
    bool used;
    char id[ZCSR_MIX_ID_CAP];
    uint32_t offset;
    uint32_t frames;
} zcsr_mix_sound;

typedef struct {
    atomic_int state;
    int sound;
    uint32_t frame;
    float volume;     /* public-facing, set from API */
    int32_t volume_q8; /* Q8 fixed-point (0..256), precomputed in apply_cmd for render hot path */
    bool loop;
    /* ---- streaming PCM fields (additive) ---- */
    _Atomic bool is_stream;
    _Atomic bool stream_reserved;
    _Atomic bool stream_paused;
    _Atomic unsigned int stream_volume_q8;
    int16_t pcm_ring[4096 * 2];          /* 4096 frames * 2 channels */
    _Atomic unsigned int stream_write;   /* monotonic write count (frames) */
    _Atomic unsigned int stream_read;    /* monotonic read count (frames) */
    _Atomic unsigned int stream_consumed; /* frames consumed by mixer (monotonic) */
} zcsr_mix_channel;

typedef struct {
    zcsr_mix_cmd_type type;
    int channel;
    int sound;
    float volume;
    bool loop;
    char id[ZCSR_MIX_ID_CAP];
} zcsr_mix_cmd;

struct zcsr_mixer {
    bool in_use;
    atomic_bool running;
    bool thread_started;
    zcsr_mix_sound sounds[ZCSR_MIX_MAX_SOUNDS];
    zcsr_mix_channel channels[ZCSR_MIX_CHANNELS];
    int16_t samples[ZCSR_MIX_SAMPLE_FRAMES * 2];
    uint32_t sample_frames_used;
    zcsr_mix_cmd commands[ZCSR_MIX_CMD_CAP];
    atomic_uint cmd_head;
    atomic_uint cmd_tail;
    int finished[ZCSR_MIX_FINISHED_CAP];
    atomic_uint finished_head;
    atomic_uint finished_tail;
    int16_t mix_buffer[ZCSR_MIX_PERIOD_FRAMES * 2];
#if defined(__ANDROID__) || defined(__APPLE__)
    /* SPSC PCM ring: mixer thread produces; platform output callback consumes. */
    atomic_uint pcm_head;
    atomic_uint pcm_tail;

    /* Used by Android startup only; macOS initializes it but does not gate start. */
    atomic_bool pcm_prefilled;

    atomic_uint pcm_underruns;
    int16_t pcm_ring[4096U * 2U];
#endif

#if defined(__APPLE__)
    /* CoreAudio property-listener lifecycle; all storage stays in the mixer pool. */
    atomic_uint mac_listener_callbacks_active;
    atomic_bool mac_listener_accepting;
    atomic_bool mac_listener_removal_failed;
#endif

    zcsr_mix_backend backend;

    /* Device recovery state; platform callbacks only set pending flags. */
    atomic_int device_state;
    atomic_uint device_pending_flags;
    bool device_recovery_after_loss;

    /* Protects the callback/user pair only; never call user code while locked. */
    atomic_flag device_callback_lock;
    zcsr_audio_device_fn device_cb;
    void *device_user;

#if defined(_WIN32)
    /* Registered IMMNotificationClient implementation; managed in later blocks. */
    IMMNotificationClient *device_notification_client;
#endif

#if defined(__linux__) && !defined(__ANDROID__)
    /* Fixed-capacity snapshot of /proc/asound/cards; polling is in a later block. */
    char device_cards_snapshot[8192];
    size_t device_cards_snapshot_len;
    bool device_cards_snapshot_valid;
    bool device_cards_snapshot_ever_succeeded;
#endif

#if defined(_WIN32)
    HANDLE thread;
#else
    pthread_t thread;
#endif
};

static zcsr_mixer g_mix_pool[ZCSR_MIX_POOL];
static zcsr_mixer* g_active_mixer;

static bool zcsr_mix_copy(char* dst, size_t cap, const char* src) {
    size_t len;
    if (!dst || !src || cap == 0) return false;
    len = strlen(src);
    if (len == 0 || len >= cap) return false;
    memcpy(dst, src, len + 1);
    return true;
}

static bool zcsr_mix_eq(const char* a, const char* b) {
    return a && b && strcmp(a, b) == 0;
}

void zcsr_mix_sleep_period(void) {
#if defined(_WIN32)
    Sleep(12);
#else
    struct timespec ts;
    ts.tv_sec = 0;
    ts.tv_nsec = 12000000L;
    nanosleep(&ts, 0);
#endif
}

static bool zcsr_mix_ring_push(zcsr_mixer* m, const zcsr_mix_cmd* cmd) {
    unsigned tail;
    unsigned next;
    unsigned head;
    if (!m || !cmd) return false;
    tail = atomic_load_explicit(&m->cmd_tail, memory_order_relaxed);
    next = (tail + 1U) % ZCSR_MIX_CMD_CAP;
    head = atomic_load_explicit(&m->cmd_head, memory_order_acquire);
    if (next == head) return false;
    m->commands[tail] = *cmd;
    atomic_store_explicit(&m->cmd_tail, next, memory_order_release);
    return true;
}

static bool zcsr_mix_ring_pop(zcsr_mixer* m, zcsr_mix_cmd* out) {
    unsigned head;
    unsigned tail;
    if (!m || !out) return false;
    head = atomic_load_explicit(&m->cmd_head, memory_order_relaxed);
    tail = atomic_load_explicit(&m->cmd_tail, memory_order_acquire);
    if (head == tail) return false;
    *out = m->commands[head];
    atomic_store_explicit(&m->cmd_head, (head + 1U) % ZCSR_MIX_CMD_CAP, memory_order_release);
    return true;
}

static void zcsr_mix_finished_push(zcsr_mixer* m, int channel) {
    unsigned tail;
    unsigned next;
    unsigned head;
    if (!m || channel < 0) return;
    tail = atomic_load_explicit(&m->finished_tail, memory_order_relaxed);
    next = (tail + 1U) % ZCSR_MIX_FINISHED_CAP;
    head = atomic_load_explicit(&m->finished_head, memory_order_acquire);
    if (next == head) return;
    m->finished[tail] = channel;
    atomic_store_explicit(&m->finished_tail, next, memory_order_release);
}

static int zcsr_mix_find_sound(const zcsr_mixer* m, const char* id) {
    if (!m || !id) return -1;
    for (int i = 0; i < ZCSR_MIX_MAX_SOUNDS; ++i) {
        if (m->sounds[i].used && zcsr_mix_eq(m->sounds[i].id, id)) return i;
    }
    return -1;
}

static int16_t zcsr_mix_read_i16le(FILE* f, bool* ok) {
    unsigned char b[2];
    if (!f || fread(b, 1, 2, f) != 2) {
        if (ok) *ok = false;
        return 0;
    }
    if (ok) *ok = true;
    return (int16_t)((uint16_t)b[0] | ((uint16_t)b[1] << 8));
}

static bool zcsr_mix_read_exact(FILE* f, unsigned char* out, size_t n) {
    return f && out && fread(out, 1, n, f) == n;
}

static bool zcsr_mix_read_u16(FILE* f, uint16_t* out) {
    unsigned char b[2];
    if (!zcsr_mix_read_exact(f, b, sizeof b)) return false;
    *out = (uint16_t)b[0] | ((uint16_t)b[1] << 8);
    return true;
}

static bool zcsr_mix_read_u32(FILE* f, uint32_t* out) {
    unsigned char b[4];
    if (!zcsr_mix_read_exact(f, b, sizeof b)) return false;
    *out = (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
    return true;
}

static bool zcsr_mix_chunk_eq(const unsigned char id[4], const char* lit) {
    return id[0] == (unsigned char)lit[0] && id[1] == (unsigned char)lit[1] &&
           id[2] == (unsigned char)lit[2] && id[3] == (unsigned char)lit[3];
}

static bool zcsr_mix_parse_wav(FILE* f, uint16_t* channels, uint32_t* rate, uint32_t* data_size, long* data_at) {
    unsigned char riff[4];
    unsigned char wave[4];
    uint32_t riff_size;
    bool saw_fmt = false;
    bool saw_data = false;
    uint16_t format = 0;
    uint16_t bits = 0;
    if (!f || !channels || !rate || !data_size || !data_at) return false;
    if (!zcsr_mix_read_exact(f, riff, sizeof riff) || !zcsr_mix_read_u32(f, &riff_size) ||
        !zcsr_mix_read_exact(f, wave, sizeof wave) || !zcsr_mix_chunk_eq(riff, "RIFF") ||
        !zcsr_mix_chunk_eq(wave, "WAVE")) {
        return false;
    }
    (void)riff_size;
    for (;;) {
        unsigned char chunk[4];
        uint32_t size;
        long payload;
        if (!zcsr_mix_read_exact(f, chunk, sizeof chunk)) break;
        if (!zcsr_mix_read_u32(f, &size)) return false;
        payload = ftell(f);
        if (payload < 0) return false;
        if (zcsr_mix_chunk_eq(chunk, "fmt ")) {
            uint32_t byte_rate;
            uint16_t align;
            if (size < 16 || !zcsr_mix_read_u16(f, &format) || !zcsr_mix_read_u16(f, channels) ||
                !zcsr_mix_read_u32(f, rate) || !zcsr_mix_read_u32(f, &byte_rate) ||
                !zcsr_mix_read_u16(f, &align) || !zcsr_mix_read_u16(f, &bits)) {
                return false;
            }
            (void)byte_rate;
            (void)align;
            saw_fmt = true;
        } else if (zcsr_mix_chunk_eq(chunk, "data")) {
            *data_size = size;
            *data_at = payload;
            saw_data = true;
        }
        if (fseek(f, payload + (long)size + (long)(size & 1U), SEEK_SET) != 0) return false;
    }
    return saw_fmt && saw_data && format == 1 && bits == 16 && (*channels == 1 || *channels == 2) &&
           *rate == ZCSR_MIX_RATE && *data_size > 0;
}

static bool zcsr_mix_load_wav(zcsr_mixer* m, zcsr_mix_sound* s, const char* path) {
    FILE* f;
    uint16_t channels = 0;
    uint32_t rate = 0;
    uint32_t data_size = 0;
    long data_at = 0;
    uint32_t frames;
    uint32_t offset;
    if (!m || !s || !path) return false;
    f = fopen(path, "rb");
    if (!f) return false;
    if (!zcsr_mix_parse_wav(f, &channels, &rate, &data_size, &data_at)) {
        fclose(f);
        return false;
    }
    frames = data_size / ((uint32_t)channels * 2U);
    if (frames == 0 || frames > ZCSR_MIX_SAMPLE_FRAMES - m->sample_frames_used) {
        fclose(f);
        return false;
    }
    if (fseek(f, data_at, SEEK_SET) != 0) {
        fclose(f);
        return false;
    }
    offset = m->sample_frames_used;
    for (uint32_t i = 0; i < frames; ++i) {
        bool ok_l = false;
        bool ok_r = true;
        int16_t l = zcsr_mix_read_i16le(f, &ok_l);
        int16_t r = channels == 2 ? zcsr_mix_read_i16le(f, &ok_r) : l;
        if (!ok_l || !ok_r) {
            fclose(f);
            return false;
        }
        m->samples[(offset + i) * 2U] = l;
        m->samples[(offset + i) * 2U + 1U] = r;
    }
    fclose(f);
    s->offset = offset;
    s->frames = frames;
    m->sample_frames_used += frames;
    (void)rate;
    return true;
}

/* === Device-recovery state machine helpers (block 3) === */

static void zcsr_mix_device_callback_lock(zcsr_mixer *m)
{
    while (atomic_flag_test_and_set_explicit(
        &m->device_callback_lock,
        memory_order_acquire)) {
    }
}

static void zcsr_mix_device_callback_unlock(zcsr_mixer *m)
{
    atomic_flag_clear_explicit(
        &m->device_callback_lock,
        memory_order_release
    );
}

void zcsr_mix_set_device_callback(
    zcsr_mixer *m,
    zcsr_audio_device_fn callback,
    void *user
)
{
    if (m == NULL || !m->in_use) {
        return;
    }

    zcsr_mix_device_callback_lock(m);

    m->device_cb = callback;
    m->device_user = callback != NULL ? user : NULL;

    zcsr_mix_device_callback_unlock(m);
}

static void zcsr_mix_device_set_pending(
    zcsr_mixer *m,
    unsigned int flags
)
{
    int expected;

    if (m == NULL ||
        !atomic_load_explicit(&m->running, memory_order_acquire)) {
        return;
    }

    flags &= ZCSR_MIX_DEVICE_PENDING_MASK;

    if (flags == 0U) {
        return;
    }

    (void)atomic_fetch_or_explicit(
        &m->device_pending_flags,
        flags,
        memory_order_release
    );

    expected = ZCSR_MIX_DEVICE_RUNNING;

    (void)atomic_compare_exchange_strong_explicit(
        &m->device_state,
        &expected,
        ZCSR_MIX_DEVICE_CHANGE_PENDING,
        memory_order_acq_rel,
        memory_order_acquire
    );
}

static unsigned int zcsr_mix_device_take_pending(zcsr_mixer *m)
{
    unsigned int flags;

    if (m == NULL) {
        return 0U;
    }

    flags = atomic_exchange_explicit(
        &m->device_pending_flags,
        0U,
        memory_order_acq_rel
    );

    return flags & ZCSR_MIX_DEVICE_PENDING_MASK;
}

static void zcsr_mix_device_notify_worker(
    zcsr_mixer *m,
    zcsr_audio_device_event event
)
{
    zcsr_audio_device_fn callback;
    void *user;

    if (m == NULL) {
        return;
    }

    if (event != ZCSR_AUDIO_DEVICE_LOST &&
        event != ZCSR_AUDIO_DEVICE_CHANGED &&
        event != ZCSR_AUDIO_DEVICE_RECOVERED) {
        return;
    }

    zcsr_mix_device_callback_lock(m);
    callback = m->device_cb;
    user = m->device_user;
    zcsr_mix_device_callback_unlock(m);

    if (callback != NULL) {
        callback(m, event, user);
    }
}

static bool zcsr_mix_device_begin_rebuild(
    zcsr_mixer *m,
    unsigned int pending_flags,
    bool retry
)
{
    int state;

    if (m == NULL) {
        return false;
    }

    state = atomic_load_explicit(
        &m->device_state,
        memory_order_acquire
    );

    if (state == ZCSR_MIX_DEVICE_REBUILDING) {
        return false;
    }

    if (retry) {
        if (state != ZCSR_MIX_DEVICE_RECOVERY_PENDING) {
            return false;
        }
    } else {
        if (state != ZCSR_MIX_DEVICE_CHANGE_PENDING ||
            (pending_flags & ZCSR_MIX_DEVICE_PENDING_MASK) == 0U) {
            return false;
        }
    }

    atomic_store_explicit(
        &m->device_state,
        ZCSR_MIX_DEVICE_REBUILDING,
        memory_order_release
    );

    if (!retry &&
        (pending_flags & ZCSR_MIX_DEVICE_PENDING_LOST) != 0U) {
        if (!m->device_recovery_after_loss) {
            m->device_recovery_after_loss = true;

            zcsr_mix_device_notify_worker(
                m,
                ZCSR_AUDIO_DEVICE_LOST
            );
        }
    }

    return true;
}

static void zcsr_mix_device_rebuild_succeeded(zcsr_mixer *m)
{
    bool recovered_after_loss;

    if (m == NULL) {
        return;
    }

    recovered_after_loss = m->device_recovery_after_loss;

    atomic_store_explicit(
        &m->device_state,
        ZCSR_MIX_DEVICE_RUNNING,
        memory_order_release
    );

    if (recovered_after_loss) {
        m->device_recovery_after_loss = false;

        zcsr_mix_device_notify_worker(
            m,
            ZCSR_AUDIO_DEVICE_RECOVERED
        );
    } else {
        zcsr_mix_device_notify_worker(
            m,
            ZCSR_AUDIO_DEVICE_CHANGED
        );
    }
}

static void zcsr_mix_device_rebuild_failed(zcsr_mixer *m)
{
    if (m == NULL) {
        return;
    }

    atomic_store_explicit(
        &m->device_state,
        ZCSR_MIX_DEVICE_RECOVERY_PENDING,
        memory_order_release
    );
}

#if defined(__ANDROID__)
enum { ZCSR_PCM_RING_FRAMES = 4096U, ZCSR_PCM_PREFILL_FRAMES = 2048U };

static zcsr_aaudio_sym zcsr_mix_aaudio_sym(void* lib, const char* name) {
    zcsr_aaudio_sym sym;
    sym.obj = lib ? dlsym(lib, name) : 0;
    return sym;
}

static aaudio_data_callback_result_t zcsr_mix_aaudio_callback(
    AAudioStream* stream, void* user_data, void* audio_data, int32_t num_frames) {
    zcsr_mixer* m = (zcsr_mixer*)user_data;
    int16_t* out = (int16_t*)audio_data;
    unsigned head;
    unsigned tail;
    int32_t frame;
    bool underrun = false;
    (void)stream;
    if (!m || !out || num_frames <= 0) return AAUDIO_CALLBACK_RESULT_STOP;

    head = atomic_load_explicit(&m->pcm_head, memory_order_relaxed);
    for (frame = 0; frame < num_frames; ++frame) {
        tail = atomic_load_explicit(&m->pcm_tail, memory_order_acquire);
        if (head != tail) {
            unsigned slot = head & (ZCSR_PCM_RING_FRAMES - 1U);
            out[(size_t)frame * 2U] = m->pcm_ring[(size_t)slot * 2U];
            out[(size_t)frame * 2U + 1U] = m->pcm_ring[(size_t)slot * 2U + 1U];
            ++head;
        } else {
            out[(size_t)frame * 2U] = 0;
            out[(size_t)frame * 2U + 1U] = 0;
            underrun = true;
        }
    }
    atomic_store_explicit(&m->pcm_head, head, memory_order_release);
    if (underrun) atomic_fetch_add_explicit(&m->pcm_underruns, 1U, memory_order_relaxed);
    return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

/*
 * AAudio error callback.
 *
 * IMPORTANT:
 * - This callback must not block.
 * - Do not allocate memory, lock, sleep, stop/close the stream, reopen the
 *   backend, or call application callbacks here.
 * - Publish only an atomic pending flag. The mixer worker owns recovery.
 */
static void zcsr_mix_aaudio_error_callback(
    AAudioStream* stream,
    void* user_data,
    aaudio_result_t error) {
    zcsr_mixer* m = (zcsr_mixer*)user_data;

    (void)stream;

    if (!m) return;

    if (error == AAUDIO_ERROR_DISCONNECTED) {
        zcsr_mix_device_set_pending(
            m, ZCSR_MIX_DEVICE_PENDING_LOST);
    }

    /*
     * Other errors are intentionally ignored in this block.
     * AAudio can report timeout/internal errors too; they do not, by
     * themselves, prove that the output endpoint was disconnected.
     */
}

static bool zcsr_mix_android_open(zcsr_mixer* m) {
    zcsr_mix_backend* b;
    AAudioStreamBuilder* builder = 0;
    aaudio_result_t result;

    /*
     * Emit the optional-symbol warning at most once per process, even if
     * the backend is opened/reopened multiple times.
     */
    static atomic_bool warned_missing_error_callback =
        ATOMIC_VAR_INIT(false);

    if (!m) return false;

    b = &m->backend;
    *b = (zcsr_mix_backend){ 0 };

    b->lib = dlopen("libaaudio.so", RTLD_NOW | RTLD_LOCAL);
    if (!b->lib) return false;

#define ZCSR_LOAD_AAUDIO(field, symbol, member) \
    do { \
        b->api.field = zcsr_mix_aaudio_sym(b->lib, symbol).member; \
        if (!b->api.field) goto fail; \
    } while (0)

    /* All original required symbols remain mandatory. */
    ZCSR_LOAD_AAUDIO(
        create_builder,
        "AAudio_createStreamBuilder",
        create_builder);

    ZCSR_LOAD_AAUDIO(
        set_direction,
        "AAudioStreamBuilder_setDirection",
        set_direction);

    ZCSR_LOAD_AAUDIO(
        set_format,
        "AAudioStreamBuilder_setFormat",
        set_format);

    ZCSR_LOAD_AAUDIO(
        set_channel_count,
        "AAudioStreamBuilder_setChannelCount",
        set_channel_count);

    ZCSR_LOAD_AAUDIO(
        set_sample_rate,
        "AAudioStreamBuilder_setSampleRate",
        set_sample_rate);

    ZCSR_LOAD_AAUDIO(
        set_performance_mode,
        "AAudioStreamBuilder_setPerformanceMode",
        set_performance_mode);

    ZCSR_LOAD_AAUDIO(
        set_sharing_mode,
        "AAudioStreamBuilder_setSharingMode",
        set_sharing_mode);

    ZCSR_LOAD_AAUDIO(
        set_data_callback,
        "AAudioStreamBuilder_setDataCallback",
        set_data_callback);

    ZCSR_LOAD_AAUDIO(
        open_stream,
        "AAudioStreamBuilder_openStream",
        open_stream);

    ZCSR_LOAD_AAUDIO(
        delete_builder,
        "AAudioStreamBuilder_delete",
        delete_builder);

    ZCSR_LOAD_AAUDIO(
        request_start,
        "AAudioStream_requestStart",
        request_start);

    ZCSR_LOAD_AAUDIO(
        request_stop,
        "AAudioStream_requestStop",
        request_stop);

    ZCSR_LOAD_AAUDIO(
        close_stream,
        "AAudioStream_close",
        close_stream);

#undef ZCSR_LOAD_AAUDIO

    /*
     * Optional symbol:
     * it is intentionally NOT loaded through ZCSR_LOAD_AAUDIO because
     * absence must not jump to fail or prevent playback.
     */
    b->api.set_error_callback =
        zcsr_mix_aaudio_sym(
            b->lib,
            "AAudioStreamBuilder_setErrorCallback").set_error_callback;

    if (!b->api.set_error_callback) {
        bool already_warned = atomic_exchange_explicit(
            &warned_missing_error_callback,
            true,
            memory_order_relaxed);

        if (!already_warned) {
            (void)fprintf(
                stderr,
                "zcsr audio: AAudioStreamBuilder_setErrorCallback "
                "is unavailable; device-loss notifications are disabled\n");
        }
    }

    result = b->api.create_builder(&builder);
    if (result != AAUDIO_OK || !builder) goto fail;

    b->api.set_direction(builder, AAUDIO_DIRECTION_OUTPUT);
    b->api.set_format(builder, AAUDIO_FORMAT_PCM_I16);
    b->api.set_channel_count(builder, 2);
    b->api.set_sample_rate(builder, ZCSR_MIX_RATE);
    b->api.set_performance_mode(
        builder, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
    b->api.set_sharing_mode(builder, AAUDIO_SHARING_MODE_SHARED);

    /* Existing PCM data callback remains unchanged. */
    b->api.set_data_callback(
        builder, zcsr_mix_aaudio_callback, m);

    /*
     * Register the error callback after set_data_callback and before
     * open_stream, as required by the block-6 contract.
     */
    if (b->api.set_error_callback) {
        b->api.set_error_callback(
            builder,
            zcsr_mix_aaudio_error_callback,
            m);
    }

    result = b->api.open_stream(builder, &b->stream);

    b->api.delete_builder(builder);
    builder = 0;

    if (result != AAUDIO_OK || !b->stream) goto fail;

    b->owner = m;
    b->opened = true;
    return true;

fail:
    if (builder && b->api.delete_builder) {
        b->api.delete_builder(builder);
    }

    if (b->stream && b->api.close_stream) {
        (void)b->api.close_stream(b->stream);
        b->stream = 0;
    }

    if (b->lib) {
        dlclose(b->lib);
    }

    *b = (zcsr_mix_backend){ 0 };
    return false;
}

void zcsr_mix_backend_close(zcsr_mix_backend* b) {
    if (!b) return;
    if (b->stream && b->stream_started && b->api.request_stop)
        (void)b->api.request_stop(b->stream);
    if (b->stream && b->api.close_stream)
        (void)b->api.close_stream(b->stream);
    b->stream = 0;
    if (b->lib) dlclose(b->lib);
    *b = (zcsr_mix_backend){ 0 };
}

void zcsr_mix_backend_open(zcsr_mix_backend* b) {
    /* AAudio stream is opened by zcsr_mix_start before the mixer thread is created. */
    (void)b;
}

void zcsr_mix_backend_write(zcsr_mix_backend* b, const int16_t* pcm, unsigned frames) {
    zcsr_mixer* m;
    unsigned done = 0;
    if (!b || !pcm || !(m = b->owner)) return;
    while (done < frames && atomic_load_explicit(&m->running, memory_order_acquire)) {
        unsigned head = atomic_load_explicit(&m->pcm_head, memory_order_acquire);
        unsigned tail = atomic_load_explicit(&m->pcm_tail, memory_order_relaxed);
        unsigned used = tail - head;
        unsigned free_frames = used < ZCSR_PCM_RING_FRAMES
            ? ZCSR_PCM_RING_FRAMES - used : 0U;
        unsigned chunk = frames - done;
        if (chunk > free_frames) chunk = free_frames;
        if (chunk == 0U) {
            struct timespec ts = { 0, 1000000L };
            (void)nanosleep(&ts, 0);
            continue;
        }
        for (unsigned i = 0; i < chunk; ++i) {
            unsigned slot = (tail + i) & (ZCSR_PCM_RING_FRAMES - 1U);
            m->pcm_ring[(size_t)slot * 2U] = pcm[(size_t)(done + i) * 2U];
            m->pcm_ring[(size_t)slot * 2U + 1U] = pcm[(size_t)(done + i) * 2U + 1U];
        }
        tail += chunk;
        atomic_store_explicit(&m->pcm_tail, tail, memory_order_release);
        done += chunk;
        if (tail - atomic_load_explicit(&m->pcm_head, memory_order_acquire)
                >= ZCSR_PCM_PREFILL_FRAMES) {
            atomic_store_explicit(&m->pcm_prefilled, true, memory_order_release);
        }
    }
}

#elif defined(_WIN32)

/* === Windows IMMNotificationClient (block 4) === */

/* Forward declaration for the already-implemented block 3 state machine. */
static void zcsr_mix_device_set_pending(zcsr_mixer* m, unsigned flags);

enum {
    ZCSR_MIX_NOTIFICATION_ENDPOINT_CAP = 512
};

typedef struct zcsr_mix_notification_client {
    /* COM interface must be the first member. */
    IMMNotificationClient iface;

    /* ULONG-compatible storage for COM AddRef/Release. */
    atomic_ulong ref_count;

    /* Read by notification threads; cleared during backend teardown. */
    _Atomic(zcsr_mixer*) owner;

    /* Protects endpoint_id and endpoint_id_valid. */
    atomic_flag endpoint_id_lock;
    WCHAR endpoint_id[ZCSR_MIX_NOTIFICATION_ENDPOINT_CAP];
    bool endpoint_id_valid;

    /* Prevents mixer destruction while a callback already in progress runs. */
    atomic_uint callbacks_active;

    /* Fixed-pool slot index; immutable while allocated. */
    size_t pool_index;
} zcsr_mix_notification_client;

static zcsr_mix_notification_client
    g_zcsr_mix_notification_pool[ZCSR_MIX_POOL] = {
        { .endpoint_id_lock = ATOMIC_FLAG_INIT }
    };

static atomic_bool
    g_zcsr_mix_notification_pool_used[ZCSR_MIX_POOL] = {
        false
    };

static zcsr_mix_notification_client* zcsr_mix_notification_from_iface(IMMNotificationClient* iface) {
    if (!iface) return NULL;
    return (zcsr_mix_notification_client*)iface;
}

static HRESULT STDMETHODCALLTYPE zcsr_mix_notification_query_interface(
    IMMNotificationClient* This,
    REFIID riid,
    void** ppvObject);
static ULONG STDMETHODCALLTYPE zcsr_mix_notification_add_ref(IMMNotificationClient* This);
static ULONG STDMETHODCALLTYPE zcsr_mix_notification_release(IMMNotificationClient* This);
static HRESULT STDMETHODCALLTYPE zcsr_mix_notification_on_device_state_changed(
    IMMNotificationClient* This,
    LPCWSTR pwstrDeviceId,
    DWORD dwNewState);
static HRESULT STDMETHODCALLTYPE zcsr_mix_notification_on_device_added(
    IMMNotificationClient* This,
    LPCWSTR pwstrDeviceId);
static HRESULT STDMETHODCALLTYPE zcsr_mix_notification_on_device_removed(
    IMMNotificationClient* This,
    LPCWSTR pwstrDeviceId);
static HRESULT STDMETHODCALLTYPE zcsr_mix_notification_on_default_device_changed(
    IMMNotificationClient* This,
    EDataFlow flow,
    ERole role,
    LPCWSTR pwstrDefaultDeviceId);
static HRESULT STDMETHODCALLTYPE zcsr_mix_notification_on_property_value_changed(
    IMMNotificationClient* This,
    LPCWSTR pwstrDeviceId,
    const PROPERTYKEY key);

static IMMNotificationClientVtbl g_zcsr_mix_notification_vtbl = {
    zcsr_mix_notification_query_interface,
    zcsr_mix_notification_add_ref,
    zcsr_mix_notification_release,
    zcsr_mix_notification_on_device_state_changed,
    zcsr_mix_notification_on_device_added,
    zcsr_mix_notification_on_device_removed,
    zcsr_mix_notification_on_default_device_changed,
    zcsr_mix_notification_on_property_value_changed
};

/* ---------- Pool allocation and reference counting ---------- */

static zcsr_mix_notification_client* zcsr_mix_notification_create(zcsr_mixer* owner) {
    if (!owner) return NULL;

    for (size_t i = 0; i < ZCSR_MIX_POOL; ++i) {
        bool expected = false;

        if (!atomic_compare_exchange_strong_explicit(
                &g_zcsr_mix_notification_pool_used[i],
                &expected,
                true,
                memory_order_acq_rel,
                memory_order_acquire)) {
            continue;
        }

        zcsr_mix_notification_client* client =
            &g_zcsr_mix_notification_pool[i];

        client->iface.lpVtbl = &g_zcsr_mix_notification_vtbl;
        atomic_init(&client->ref_count, 1UL);
        atomic_init(&client->owner, owner);
        atomic_init(&client->callbacks_active, 0U);

        atomic_flag_clear_explicit(
            &client->endpoint_id_lock, memory_order_release);

        client->endpoint_id[0] = L'\0';
        client->endpoint_id_valid = false;
        client->pool_index = i;

        return client;
    }

    return NULL;
}

static ULONG STDMETHODCALLTYPE zcsr_mix_notification_add_ref(IMMNotificationClient* This) {
    zcsr_mix_notification_client* client =
        zcsr_mix_notification_from_iface(This);
    unsigned long current;

    if (!client) return 0UL;

    current = atomic_load_explicit(
        &client->ref_count, memory_order_relaxed);

    for (;;) {
        if (current == 0UL) return 0UL;
        if (current == ULONG_MAX) return (ULONG)current;

        if (atomic_compare_exchange_weak_explicit(
                &client->ref_count,
                &current,
                current + 1UL,
                memory_order_relaxed,
                memory_order_relaxed)) {
            return (ULONG)(current + 1UL);
        }
    }
}

static ULONG STDMETHODCALLTYPE zcsr_mix_notification_release(IMMNotificationClient* This) {
    zcsr_mix_notification_client* client =
        zcsr_mix_notification_from_iface(This);
    unsigned long current;
    unsigned long next;

    if (!client) return 0UL;

    current = atomic_load_explicit(
        &client->ref_count, memory_order_acquire);

    for (;;) {
        if (current == 0UL) return 0UL;

        next = current - 1UL;

        if (atomic_compare_exchange_weak_explicit(
                &client->ref_count,
                &current,
                next,
                memory_order_acq_rel,
                memory_order_acquire)) {
            break;
        }
    }

    if (next != 0UL) return (ULONG)next;

    /*
     * Final reference: this slot can only reach zero after successful
     * unregistration or failed registration cleanup.
     */
    atomic_store_explicit(&client->owner, NULL, memory_order_release);
    client->endpoint_id_valid = false;
    client->endpoint_id[0] = L'\0';

    /*
     * Publish the vacant slot only after all object fields above have
     * been finalized. Do not touch client after releasing the slot.
     */
    atomic_store_explicit(
        &g_zcsr_mix_notification_pool_used[client->pool_index],
        false,
        memory_order_release);

    return 0UL;
}

static HRESULT STDMETHODCALLTYPE zcsr_mix_notification_query_interface(
    IMMNotificationClient* This,
    REFIID riid,
    void** ppvObject) {
    zcsr_mix_notification_client* client =
        zcsr_mix_notification_from_iface(This);

    if (!ppvObject) return E_POINTER;
    *ppvObject = NULL;

    if (!client || !riid) return E_POINTER;

    if (IsEqualIID(riid, &IID_IUnknown) ||
        IsEqualIID(riid, &IID_IMMNotificationClient)) {
        *ppvObject = &client->iface;
        (void)zcsr_mix_notification_add_ref(&client->iface);
        return S_OK;
    }

    return E_NOINTERFACE;
}

/* ---------- Callback lifetime and endpoint-ID cache ---------- */

static zcsr_mixer* zcsr_mix_notification_callback_begin(
    zcsr_mix_notification_client* client) {
    if (!client) return NULL;

    atomic_fetch_add_explicit(
        &client->callbacks_active, 1U, memory_order_acq_rel);

    return atomic_load_explicit(
        &client->owner, memory_order_acquire);
}

static void zcsr_mix_notification_callback_end(
    zcsr_mix_notification_client* client) {
    if (!client) return;

    atomic_fetch_sub_explicit(
        &client->callbacks_active, 1U, memory_order_release);
}

static void zcsr_mix_notification_wait_callbacks(
    zcsr_mix_notification_client* client) {
    if (!client) return;

    while (atomic_load_explicit(
                &client->callbacks_active, memory_order_acquire) != 0U) {
        /* Teardown path only; callbacks themselves never wait here. */
        Sleep(0);
    }
}

/*
 * Writer: mixer worker only.
 * It may yield while waiting for the short reader critical section.
 *
 * Policy:
 *   id == NULL       -> invalid cache, empty string
 *   length <= 511    -> complete valid ID
 *   length > 511     -> truncate to 511, but mark invalid
 */
static void zcsr_mix_notification_update_endpoint_id(
    zcsr_mix_notification_client* client,
    LPCWSTR id) {
    size_t length = 0U;
    size_t copy_length = 0U;

    if (!client) return;

    while (atomic_flag_test_and_set_explicit(
                &client->endpoint_id_lock, memory_order_acquire)) {
        Sleep(0);
    }

    if (!id) {
        client->endpoint_id[0] = L'\0';
        client->endpoint_id_valid = false;
        atomic_flag_clear_explicit(
            &client->endpoint_id_lock, memory_order_release);
        return;
    }

    length = wcslen(id);
    copy_length = length;

    if (copy_length >= ZCSR_MIX_NOTIFICATION_ENDPOINT_CAP) {
        copy_length = ZCSR_MIX_NOTIFICATION_ENDPOINT_CAP - 1U;
    }

    wmemcpy(client->endpoint_id, id, copy_length);
    client->endpoint_id[copy_length] = L'\0';

    client->endpoint_id_valid =
        (length < ZCSR_MIX_NOTIFICATION_ENDPOINT_CAP);

    atomic_flag_clear_explicit(
        &client->endpoint_id_lock, memory_order_release);
}

/*
 * Reader: COM notification thread.
 * This is strictly trylock: if another thread owns the lock, return false
 * immediately and let the caller publish CHANGED conservatively.
 */
static bool zcsr_mix_notification_read_endpoint_id(
    zcsr_mix_notification_client* client,
    WCHAR out_id[ZCSR_MIX_NOTIFICATION_ENDPOINT_CAP],
    bool* out_valid) {
    if (!client || !out_id || !out_valid) return false;

    out_id[0] = L'\0';
    *out_valid = false;

    if (atomic_flag_test_and_set_explicit(
            &client->endpoint_id_lock, memory_order_acquire)) {
        return false;
    }

    *out_valid = client->endpoint_id_valid;

    if (*out_valid) {
        wmemcpy(
            out_id,
            client->endpoint_id,
            ZCSR_MIX_NOTIFICATION_ENDPOINT_CAP);
    } else {
        out_id[0] = L'\0';
    }

    atomic_flag_clear_explicit(
        &client->endpoint_id_lock, memory_order_release);

    return true;
}

static bool zcsr_mix_notification_endpoint_matches(
    zcsr_mix_notification_client* client,
    LPCWSTR device_id,
    bool* out_cache_valid) {
    WCHAR cached_id[ZCSR_MIX_NOTIFICATION_ENDPOINT_CAP];
    bool cache_valid = false;
    bool lock_acquired;

    if (out_cache_valid) *out_cache_valid = false;
    if (!client || !device_id) return false;

    lock_acquired = zcsr_mix_notification_read_endpoint_id(
        client, cached_id, &cache_valid);

    if (!lock_acquired || !cache_valid) {
        return false;
    }

    if (out_cache_valid) *out_cache_valid = true;

    /* Endpoint IDs are compared as complete, case-insensitive strings. */
    return _wcsicmp(cached_id, device_id) == 0;
}

/* ---------- Registration and unregistration ---------- */

static bool zcsr_mix_notification_register(
    zcsr_mixer* owner,
    IMMDeviceEnumerator* enumerator) {
    HRESULT hr;
    zcsr_mix_notification_client* client;

    if (!owner || !enumerator) return false;
    if (owner->device_notification_client) return true;

    client = zcsr_mix_notification_create(owner);
    if (!client) return false;

    /*
     * Initial ref_count == 1 is the creator reference.
     *
     * RegisterEndpointNotificationCallback does not AddRef this object.
     * Keep the creator reference across Register so that even an immediate
     * callback cannot observe freed storage.
     */
    hr = enumerator->lpVtbl->RegisterEndpointNotificationCallback(
        enumerator, &client->iface);

    if (FAILED(hr)) {
        atomic_store_explicit(
            &client->owner, NULL, memory_order_release);
        zcsr_mix_notification_wait_callbacks(client);
        (void)client->iface.lpVtbl->Release(&client->iface);
        return false;
    }

    /*
     * The registration reference is distinct from the creator reference.
     * It is released only after UnregisterEndpointNotificationCallback
     * succeeds.
     */
    (void)client->iface.lpVtbl->AddRef(&client->iface);
    owner->device_notification_client = &client->iface;

    return true;
}

/*
 * Returns true for successful/no-op cleanup.
 *
 * If unregister fails, do NOT release the registration reference:
 * pin the static-pool slot, null the owner, and release only the creator
 * reference. This deliberately trades one permanently occupied fixed-pool
 * slot for avoiding use-after-free if the COM system keeps the callback.
 */
static bool zcsr_mix_notification_unregister(
    zcsr_mixer* owner,
    IMMDeviceEnumerator* enumerator) {
    IMMNotificationClient* iface;
    zcsr_mix_notification_client* client;
    HRESULT hr = E_FAIL;
    bool unregistered = false;

    if (!owner || !owner->device_notification_client) return true;

    iface = owner->device_notification_client;
    client = zcsr_mix_notification_from_iface(iface);

    if (!client) {
        owner->device_notification_client = NULL;
        return false;
    }

    if (enumerator) {
        hr = enumerator->lpVtbl->UnregisterEndpointNotificationCallback(
            enumerator, iface);
        unregistered = SUCCEEDED(hr);
    }

    /*
     * From here on, callbacks must see owner == NULL.
     * Wait for callbacks already in progress before the mixer may be reset.
     */
    atomic_store_explicit(
        &client->owner, NULL, memory_order_release);

    owner->device_notification_client = NULL;

    zcsr_mix_notification_wait_callbacks(client);

    if (unregistered) {
        /* Drop the registered-lifetime reference, then creator reference. */
        (void)iface->lpVtbl->Release(iface);
        (void)iface->lpVtbl->Release(iface);
        return true;
    }

    /*
     * Failed/unavailable unregistration: leave the registration reference
     * pinned. The owner's pointer is cleared and future callbacks will
     * return without touching the mixer.
     */
    (void)iface->lpVtbl->Release(iface);
    return false;
}

/* ---------- IMMNotificationClient methods ---------- */

static HRESULT STDMETHODCALLTYPE zcsr_mix_notification_on_device_state_changed(
    IMMNotificationClient* This,
    LPCWSTR pwstrDeviceId,
    DWORD dwNewState) {
    zcsr_mix_notification_client* client =
        zcsr_mix_notification_from_iface(This);
    zcsr_mixer* owner;
    bool cache_valid = false;
    bool matches;

    if (!client) return E_POINTER;

    owner = zcsr_mix_notification_callback_begin(client);
    if (!owner) {
        zcsr_mix_notification_callback_end(client);
        return S_OK;
    }

    if (!pwstrDeviceId) {
        /* Unknown identity: don't assert that the current device was lost. */
        zcsr_mix_device_set_pending(
            owner, ZCSR_MIX_DEVICE_PENDING_CHANGED);
        zcsr_mix_notification_callback_end(client);
        return S_OK;
    }

    matches = zcsr_mix_notification_endpoint_matches(
        client, pwstrDeviceId, &cache_valid);

    if (!cache_valid) {
        /* Invalid/contended cache: conservative rebuild request. */
        zcsr_mix_device_set_pending(
            owner, ZCSR_MIX_DEVICE_PENDING_CHANGED);
    } else if (matches && dwNewState != DEVICE_STATE_ACTIVE) {
        zcsr_mix_device_set_pending(
            owner, ZCSR_MIX_DEVICE_PENDING_LOST);
    } else if (matches && dwNewState == DEVICE_STATE_ACTIVE) {
        zcsr_mix_device_set_pending(
            owner, ZCSR_MIX_DEVICE_PENDING_CHANGED);
    }

    zcsr_mix_notification_callback_end(client);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE zcsr_mix_notification_on_device_added(
    IMMNotificationClient* This,
    LPCWSTR pwstrDeviceId) {
    (void)This;
    (void)pwstrDeviceId;

    /*
     * M3 decision: no CHANGED for every added endpoint.
     * Unopened-backend retry remains the responsibility of block 9.
     */
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE zcsr_mix_notification_on_device_removed(
    IMMNotificationClient* This,
    LPCWSTR pwstrDeviceId) {
    zcsr_mix_notification_client* client =
        zcsr_mix_notification_from_iface(This);
    zcsr_mixer* owner;
    bool cache_valid = false;
    bool matches;

    if (!client) return E_POINTER;

    owner = zcsr_mix_notification_callback_begin(client);
    if (!owner) {
        zcsr_mix_notification_callback_end(client);
        return S_OK;
    }

    if (!pwstrDeviceId) {
        zcsr_mix_device_set_pending(
            owner, ZCSR_MIX_DEVICE_PENDING_CHANGED);
        zcsr_mix_notification_callback_end(client);
        return S_OK;
    }

    matches = zcsr_mix_notification_endpoint_matches(
        client, pwstrDeviceId, &cache_valid);

    if (!cache_valid) {
        /*
         * M4: GetId failure, truncation, or failed trylock means identity
         * cannot be trusted; publish CHANGED instead of asserting LOST.
         */
        zcsr_mix_device_set_pending(
            owner, ZCSR_MIX_DEVICE_PENDING_CHANGED);
    } else if (matches) {
        zcsr_mix_device_set_pending(
            owner, ZCSR_MIX_DEVICE_PENDING_LOST);
    }

    /* A valid cached ID that differs from the removed ID is ignored. */
    zcsr_mix_notification_callback_end(client);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE zcsr_mix_notification_on_default_device_changed(
    IMMNotificationClient* This,
    EDataFlow flow,
    ERole role,
    LPCWSTR pwstrDefaultDeviceId) {
    zcsr_mix_notification_client* client =
        zcsr_mix_notification_from_iface(This);
    zcsr_mixer* owner;

    if (!client) return E_POINTER;

    owner = zcsr_mix_notification_callback_begin(client);
    if (!owner) {
        zcsr_mix_notification_callback_end(client);
        return S_OK;
    }

    /*
     * This backend opens eRender + eConsole. Ignore capture and other roles.
     * A NULL new default ID means no default endpoint is currently available.
     */
    if (flow == eRender && role == eConsole) {
        if (!pwstrDefaultDeviceId) {
            zcsr_mix_device_set_pending(
                owner, ZCSR_MIX_DEVICE_PENDING_LOST);
        } else {
            zcsr_mix_device_set_pending(
                owner, ZCSR_MIX_DEVICE_PENDING_CHANGED);
        }
    }

    zcsr_mix_notification_callback_end(client);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE zcsr_mix_notification_on_property_value_changed(
    IMMNotificationClient* This,
    LPCWSTR pwstrDeviceId,
    const PROPERTYKEY key) {
    (void)This;
    (void)pwstrDeviceId;
    (void)key;

    /* Property changes do not necessarily require rebuilding an audio stream. */
    return S_OK;
}

void zcsr_mix_backend_close(zcsr_mix_backend* b) {
    zcsr_mixer* owner;

    if (!b) return;

    /*
     * M1: preserve owner across the final structure reset. This is required
     * because backend_close_ex may be followed by backend_open_ex to rebuild.
     */
    owner = b->owner;

    /*
     * COM notification cleanup must happen before the enumerator is released.
     * On success: unregister, null owner, wait for in-flight callbacks,
     * release registration reference, then release creator reference.
     *
     * On failure: helper pins the registration reference and pool slot,
     * so an unexpectedly late notification cannot reference freed storage.
     */
    if (owner && owner->device_notification_client) {
        (void)zcsr_mix_notification_unregister(owner, b->enumerator);
    }

    /* Existing backend teardown continues below. */
    if (b->opened && b->audio_client) {
        (void)b->audio_client->lpVtbl->Stop(b->audio_client);
    }

    if (b->render_client) {
        b->render_client->lpVtbl->Release(b->render_client);
        b->render_client = NULL;
    }

    if (b->audio_client) {
        b->audio_client->lpVtbl->Release(b->audio_client);
        b->audio_client = NULL;
    }

    if (b->enumerator) {
        b->enumerator->lpVtbl->Release(b->enumerator);
        b->enumerator = NULL;
    }

    if (b->event) {
        CloseHandle(b->event);
        b->event = NULL;
    }

    if (b->com_initialized) {
        CoUninitialize();
        b->com_initialized = false;
    }

    /* waveOut fallback cleanup */
    if (b->wave_out) {
        waveOutReset(b->wave_out);
        if (b->wave_hdr.lpData) {
            waveOutUnprepareHeader(b->wave_out, &b->wave_hdr, sizeof(WAVEHDR));
        }
        waveOutClose(b->wave_out);
        b->wave_out = NULL;
    }
    if (b->wave_event) {
        CloseHandle(b->wave_event);
        b->wave_event = NULL;
    }
    if (b->wave_buffer) {
        free(b->wave_buffer);
        b->wave_buffer = NULL;
    }

    *b = (zcsr_mix_backend){ 0 };

    /* Keep ownership available for the next open/retry. */
    b->owner = owner;
}

void zcsr_mix_backend_open(zcsr_mix_backend* b) {
    HRESULT hr;
    HRESULT id_hr;
    IMMDevice* device = NULL;
    WAVEFORMATEX wfx = { 0 };
    zcsr_mixer* owner;
    LPWSTR endpoint_id = NULL;

    if (!b) return;

    /*
     * M1: backend_open resets resource fields but must preserve the
     * owning mixer so notification registration and cleanup can find it.
     */
    owner = b->owner;
    *b = (zcsr_mix_backend){ 0 };
    b->owner = owner;

    hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(hr)) {
        b->retry_after_tick = GetTickCount() + (DWORD)1000;
        return;
    }
    b->com_initialized = true;

    hr = CoCreateInstance(
        &CLSID_MMDeviceEnumerator,
        NULL,
        CLSCTX_ALL,
        &IID_IMMDeviceEnumerator,
        (void**)&b->enumerator
    );
    if (FAILED(hr) || !b->enumerator) goto fail;

    /*
     * M2: register as soon as the enumerator exists, before resolving the
     * current default endpoint. Registration is best-effort: if notification
     * registration fails, audio can still open and block 9 can recover from
     * write/open failures. Any later open failure follows the normal close
     * path and unregisters this callback.
     */
    if (owner) {
        (void)zcsr_mix_notification_register(owner, b->enumerator);
    }

    hr = b->enumerator->lpVtbl->GetDefaultAudioEndpoint(
        b->enumerator,
        eRender,
        eConsole,
        &device
    );
    if (FAILED(hr) || !device) goto fail;

    /*
     * Capture the endpoint ID now, but publish it to the notification
     * cache only after every backend initialization step succeeds.
     *
     * GetId's result is allocated by COM and must be freed with
     * CoTaskMemFree, including error/cleanup paths.
     */
    id_hr = device->lpVtbl->GetId(device, &endpoint_id);
    if (FAILED(id_hr) || !endpoint_id) {
        if (endpoint_id) {
            CoTaskMemFree(endpoint_id);
            endpoint_id = NULL;
        }
    }

    /* Use local IID to rule out global symbol conflict */
    static const GUID local_iid_iaudioclient = {0x1CB9AD4C, 0xDBFA, 0x4C32, {0xB1, 0x78, 0x2A, 0x69, 0x72, 0x6B, 0x5C, 0x3F}};
    hr = device->lpVtbl->Activate(
        device,
        &local_iid_iaudioclient,
        CLSCTX_INPROC_SERVER,
        NULL,
        (void**)&b->audio_client
    );

    device->lpVtbl->Release(device);
    device = NULL;

    if (FAILED(hr) || !b->audio_client) {
        /* WASAPI Activate failed — fall back to waveOut (works around FxSound virtual driver) */
        zcsr_mix_backend_close(b);
        b->owner = owner;

        b->wave_event = CreateEventW(NULL, FALSE, FALSE, NULL);
        if (!b->wave_event) goto fail;

        WAVEFORMATEX wfx2 = { 0 };
        wfx2.wFormatTag = WAVE_FORMAT_PCM;
        wfx2.nChannels = 2;
        wfx2.nSamplesPerSec = ZCSR_MIX_RATE;
        wfx2.wBitsPerSample = 16;
        wfx2.nBlockAlign = (WORD)(wfx2.nChannels * wfx2.wBitsPerSample / 8);
        wfx2.nAvgBytesPerSec = wfx2.nSamplesPerSec * wfx2.nBlockAlign;
        wfx2.cbSize = 0;

        /* Find a real physical device, skip virtual drivers like FxSound */
        UINT dev_count = waveOutGetNumDevs();
        UINT chosen_device = WAVE_MAPPER;
        for (UINT i = 0; i < dev_count; i++) {
            WAVEOUTCAPS caps;
            memset(&caps, 0, sizeof(caps));
            if (waveOutGetDevCaps(i, &caps, sizeof(caps)) == MMSYSERR_NOERROR) {
                if (wcsstr(caps.szPname, L"FxSound") == NULL &&
                    wcsstr(caps.szPname, L"Virtual") == NULL) {
                    chosen_device = i;
                    break;
                }
            }
        }

        MMRESULT mr = waveOutOpen(&b->wave_out, chosen_device, &wfx2, (DWORD_PTR)b->wave_event, 0, CALLBACK_EVENT);
        if (mr != MMSYSERR_NOERROR || !b->wave_out) {
            /* Fall back to WAVE_MAPPER if physical device fails */
            if (chosen_device != WAVE_MAPPER) {
                mr = waveOutOpen(&b->wave_out, WAVE_MAPPER, &wfx2, (DWORD_PTR)b->wave_event, 0, CALLBACK_EVENT);
            }
            if (mr != MMSYSERR_NOERROR || !b->wave_out) {
                b->wave_out = NULL;
                goto fail;
            }
        }

        b->wave_buffer_frames = ZCSR_MIX_PERIOD_FRAMES * 4;
        b->wave_buffer = (int16_t*)calloc(b->wave_buffer_frames * 2, sizeof(int16_t));
        if (!b->wave_buffer) goto fail;

        b->use_waveout = true;
        b->opened = true;
        b->timeout_count = 0;
        b->retry_after_tick = 0;
        return;
    }

    wfx.wFormatTag = WAVE_FORMAT_PCM;
    wfx.nChannels = 2;
    wfx.nSamplesPerSec = ZCSR_MIX_RATE;
    wfx.wBitsPerSample = 16;
    wfx.nBlockAlign =
        (WORD)(wfx.nChannels * wfx.wBitsPerSample / 8);
    wfx.nAvgBytesPerSec =
        wfx.nSamplesPerSec * wfx.nBlockAlign;
    wfx.cbSize = 0;

    hr = b->audio_client->lpVtbl->Initialize(
        b->audio_client,
        AUDCLNT_SHAREMODE_SHARED,
        AUDCLNT_STREAMFLAGS_EVENTCALLBACK |
        AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
        AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
        1000000,
        0,
        &wfx,
        NULL
    );
    if (FAILED(hr)) goto fail;

    b->event = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (!b->event) goto fail;

    hr = b->audio_client->lpVtbl->SetEventHandle(
        b->audio_client,
        b->event
    );
    if (FAILED(hr)) goto fail;

    hr = b->audio_client->lpVtbl->GetBufferSize(
        b->audio_client,
        &b->buffer_frame_count
    );
    if (FAILED(hr) || b->buffer_frame_count == 0) goto fail;

    hr = b->audio_client->lpVtbl->GetService(
        b->audio_client,
        &IID_IAudioRenderClient,
        (void**)&b->render_client
    );
    if (FAILED(hr) || !b->render_client) goto fail;

    hr = b->audio_client->lpVtbl->Start(b->audio_client);
    if (FAILED(hr)) goto fail;

    b->opened = true;
    b->timeout_count = 0;
    b->retry_after_tick = 0;

    /*
     * M4: cache only after backend startup succeeds.
     * A NULL endpoint_id invalidates the cache without failing audio open.
     * An overlong ID is truncated to 511 WCHAR and also marked invalid.
     */
    if (owner && owner->device_notification_client) {
        zcsr_mix_notification_update_endpoint_id(
            zcsr_mix_notification_from_iface(
                owner->device_notification_client),
            endpoint_id);
    }

    if (endpoint_id) {
        CoTaskMemFree(endpoint_id);
        endpoint_id = NULL;
    }

    return;

fail:
    if (endpoint_id) {
        CoTaskMemFree(endpoint_id);
        endpoint_id = NULL;
    }

    if (device) {
        device->lpVtbl->Release(device);
        device = NULL;
    }

    zcsr_mix_backend_close(b);

    /*
     * backend_close preserves owner; keep the existing backoff behavior.
     */
    b->retry_after_tick = GetTickCount() + (DWORD)1000;
}

void zcsr_mix_backend_write(
    zcsr_mix_backend* b,
    const int16_t* pcm,
    unsigned frames ) {
    unsigned done = 0U;

    if (!b || !pcm || frames == 0U) {
        zcsr_mix_sleep_period();
        return;
    }

    /*
     * Initial open failures used to call backend_open() directly from the
     * write path. Route that retry through the worker state machine too.
     *
     * CHANGED is used here because no preceding LOST event has necessarily
     * been published. If open later succeeds, the state machine reports
     * CHANGED rather than RECOVERED.
     */
    if (!b->opened) {
        DWORD now = GetTickCount();

        if ((LONG)(now - b->retry_after_tick) >= 0) {
            if (b->owner) {
                zcsr_mix_device_set_pending(
                    b->owner, ZCSR_MIX_DEVICE_PENDING_CHANGED);
                return;
            }
        }

        zcsr_mix_sleep_period();
        return;
    }

    /* waveOut fallback path */
    if (b->use_waveout) {
        unsigned bytes = frames * 2 * sizeof(int16_t);
        if (bytes > b->wave_buffer_frames * 2 * sizeof(int16_t)) {
            bytes = b->wave_buffer_frames * 2 * sizeof(int16_t);
        }
        memcpy(b->wave_buffer, pcm, bytes);

        memset(&b->wave_hdr, 0, sizeof(WAVEHDR));
        b->wave_hdr.lpData = (LPSTR)b->wave_buffer;
        b->wave_hdr.dwBufferLength = bytes;
        b->wave_hdr.dwFlags = 0;

        MMRESULT mr = waveOutPrepareHeader(b->wave_out, &b->wave_hdr, sizeof(WAVEHDR));
        if (mr != MMSYSERR_NOERROR) {
            zcsr_mix_sleep_period();
            return;
        }

        ResetEvent(b->wave_event);
        mr = waveOutWrite(b->wave_out, &b->wave_hdr, sizeof(WAVEHDR));
        if (mr != MMSYSERR_NOERROR) {
            waveOutUnprepareHeader(b->wave_out, &b->wave_hdr, sizeof(WAVEHDR));
            zcsr_mix_sleep_period();
            return;
        }

        /* Wait for playback to complete */
        WaitForSingleObject(b->wave_event, 1000);
        waveOutUnprepareHeader(b->wave_out, &b->wave_hdr, sizeof(WAVEHDR));
        return;
    }

    while (done < frames) {
        DWORD wait_result;
        HRESULT hr;
        UINT32 padding = 0U;
        UINT32 available;
        UINT32 chunk;
        BYTE* dst = NULL;

        wait_result = WaitForSingleObject(b->event, 20);

        if (wait_result == WAIT_TIMEOUT) {
            ++b->timeout_count;

            /*
             * Keep the existing policy: one timeout is not device loss.
             * Five consecutive timeouts trigger the same failure path that
             * previously attempted a backend reopen.
             */
            if (b->timeout_count >= 5U) {
                b->timeout_count = 0U;

                if (b->owner) {
                    zcsr_mix_device_set_pending(
                        b->owner, ZCSR_MIX_DEVICE_PENDING_LOST);
                }

                /*
                 * Whether owner is valid or NULL, stop writing this period.
                 * The next backend operation must not continue here.
                 */
                return;
            }

            continue;
        }

        if (wait_result != WAIT_OBJECT_0) {
            if (b->owner) {
                zcsr_mix_device_set_pending(
                    b->owner, ZCSR_MIX_DEVICE_PENDING_LOST);
            }

            return;
        }

        b->timeout_count = 0U;

        hr = b->audio_client->lpVtbl->GetCurrentPadding(
            b->audio_client,
            &padding
        );

        if (FAILED(hr)) {
            if (b->owner) {
                zcsr_mix_device_set_pending(
                    b->owner, ZCSR_MIX_DEVICE_PENDING_LOST);
            }

            return;
        }

        if (padding >= b->buffer_frame_count) {
            continue;
        }

        available = b->buffer_frame_count - padding;
        chunk = (UINT32)(frames - done);

        if (chunk > available) {
            chunk = available;
        }

        if (chunk == 0U) {
            continue;
        }

        hr = b->render_client->lpVtbl->GetBuffer(
            b->render_client,
            chunk,
            &dst
        );

        if (FAILED(hr) || !dst) {
            if (b->owner) {
                zcsr_mix_device_set_pending(
                    b->owner, ZCSR_MIX_DEVICE_PENDING_LOST);
            }

            return;
        }

        memcpy(
            dst,
            pcm + (size_t)done * 2U,
            (size_t)chunk * 2U * sizeof(int16_t)
        );

        hr = b->render_client->lpVtbl->ReleaseBuffer(
            b->render_client,
            chunk,
            0
        );

        if (FAILED(hr)) {
            if (b->owner) {
                zcsr_mix_device_set_pending(
                    b->owner, ZCSR_MIX_DEVICE_PENDING_LOST);
            }

            return;
        }

        done += (unsigned)chunk;
    }
}

#if defined(__APPLE__)

enum {
    ZCSR_MAC_PCM_RING_FRAMES = 4096U
};

static const AudioObjectPropertyAddress zcsr_mac_default_output_address = {
    kAudioHardwarePropertyDefaultOutputDevice,
    kAudioObjectPropertyScopeGlobal,
    kAudioObjectPropertyElementMain
};

/*
 * AudioUnit's render callback is a real-time audio callback.
 *
 * It must not allocate, lock, sleep, log, call user callbacks, or invoke
 * backend lifecycle functions. It only reads the SPSC PCM ring atomically
 * and fills output buffers.
 */
static OSStatus zcsr_mix_macos_render_callback(
    void* in_ref_con,
    AudioUnitRenderActionFlags* io_action_flags,
    const AudioTimeStamp* in_time_stamp,
    UInt32 in_bus_number,
    UInt32 in_number_frames,
    AudioBufferList* io_data) {
    zcsr_mixer* m = (zcsr_mixer*)in_ref_con;
    AudioBuffer* buffer;
    int16_t* out;
    size_t required_bytes;
    unsigned head;
    bool underrun = false;

    (void)io_action_flags;
    (void)in_time_stamp;
    (void)in_bus_number;

    if (!io_data) return noErr;

    /* Default every supplied output buffer to silence first. */
    for (UInt32 i = 0; i < io_data->mNumberBuffers; ++i) {
        AudioBuffer* current = &io_data->mBuffers[i];
        if (current->mData && current->mDataByteSize > 0U) {
            memset(current->mData, 0, current->mDataByteSize);
        }
    }

    if (!m || in_number_frames == 0U ||
        io_data->mNumberBuffers != 1U) {
        if (m) {
            atomic_fetch_add_explicit(
                &m->pcm_underruns, 1U, memory_order_relaxed);
        }
        return noErr;
    }

    buffer = &io_data->mBuffers[0];

    /*
     * The negotiated format is interleaved signed 16-bit stereo:
     * one buffer, two channels, four bytes per sample frame.
     */
    if (!buffer->mData ||
        buffer->mNumberChannels != 2U) {
        atomic_fetch_add_explicit(
            &m->pcm_underruns, 1U, memory_order_relaxed);
        return noErr;
    }

    required_bytes = (size_t)in_number_frames * 2U * sizeof(int16_t);
    if ((size_t)buffer->mDataByteSize < required_bytes) {
        atomic_fetch_add_explicit(
            &m->pcm_underruns, 1U, memory_order_relaxed);
        return noErr;
    }

    out = (int16_t*)buffer->mData;
    head = atomic_load_explicit(&m->pcm_head, memory_order_relaxed);

    for (UInt32 frame = 0; frame < in_number_frames; ++frame) {
        unsigned tail = atomic_load_explicit(
            &m->pcm_tail, memory_order_acquire);
        unsigned used = tail - head;

        if (used != 0U && used <= ZCSR_MAC_PCM_RING_FRAMES) {
            unsigned slot = head & (ZCSR_MAC_PCM_RING_FRAMES - 1U);

            out[(size_t)frame * 2U] =
                m->pcm_ring[(size_t)slot * 2U];
            out[(size_t)frame * 2U + 1U] =
                m->pcm_ring[(size_t)slot * 2U + 1U];

            ++head;
        } else {
            /* Keep silence for any frames not yet produced. */
            out[(size_t)frame * 2U] = 0;
            out[(size_t)frame * 2U + 1U] = 0;
            underrun = true;
        }
    }

    atomic_store_explicit(&m->pcm_head, head, memory_order_release);

    if (underrun) {
        atomic_fetch_add_explicit(
            &m->pcm_underruns, 1U, memory_order_relaxed);
    }

    return noErr;
}

/*
 * CoreAudio default-output listener.
 *
 * This is a device-property callback, not the real-time AudioUnit render
 * callback. It only reads the current default output identifier and
 * publishes a pending event; it never closes or opens an AudioUnit.
 */
static OSStatus zcsr_mix_macos_default_output_changed(
    AudioObjectID in_object_id,
    UInt32 in_number_addresses,
    const AudioObjectPropertyAddress in_addresses[],
    void* in_client_data) {
    zcsr_mixer* m = (zcsr_mixer*)in_client_data;
    AudioDeviceID current_device = kAudioObjectUnknown;
    UInt32 data_size = sizeof(current_device);
    OSStatus status;

    (void)in_object_id;
    (void)in_number_addresses;
    (void)in_addresses;

    if (!m) return noErr;

    atomic_fetch_add_explicit(
        &m->mac_listener_callbacks_active, 1U, memory_order_acq_rel);

    if (!atomic_load_explicit(
            &m->mac_listener_accepting, memory_order_acquire)) {
        atomic_fetch_sub_explicit(
            &m->mac_listener_callbacks_active, 1U, memory_order_release);
        return noErr;
    }

    status = AudioObjectGetPropertyData(
        kAudioObjectSystemObject,
        &zcsr_mac_default_output_address,
        0U,
        NULL,
        &data_size,
        &current_device);

    if (status == noErr && current_device == kAudioObjectUnknown) {
        /* The system reports that no default output device is available. */
        zcsr_mix_device_set_pending(
            m, ZCSR_MIX_DEVICE_PENDING_LOST);
    } else {
        /*
         * A new default output exists, or the query is temporarily
         * inconclusive. Treat this as a route change, not proven device loss.
         */
        zcsr_mix_device_set_pending(
            m, ZCSR_MIX_DEVICE_PENDING_CHANGED);
    }

    atomic_fetch_sub_explicit(
        &m->mac_listener_callbacks_active, 1U, memory_order_release);

    return noErr;
}

#endif /* defined(__APPLE__) */

#elif defined(__APPLE__)

void zcsr_mix_backend_close(zcsr_mix_backend* b) {
    zcsr_mixer* owner;

    if (!b) return;

    owner = b->owner;

    /*
     * Stop accepting notifications before attempting removal, then wait for
     * in-flight property listener callbacks to finish before mixer teardown.
     */
    if (owner) {
        atomic_store_explicit(
            &owner->mac_listener_accepting, false, memory_order_release);
    }

    if (owner && b->default_output_listener_registered) {
        OSStatus remove_status = AudioObjectRemovePropertyListener(
            kAudioObjectSystemObject,
            &zcsr_mac_default_output_address,
            zcsr_mix_macos_default_output_changed,
            owner);

        if (remove_status != noErr) {
            /*
             * A failed removal must not permit this static mixer slot to be
             * reused. The listener's context is the mixer itself, whose
             * storage is static. Its accepting flag remains false.
             */
            atomic_store_explicit(
                &owner->mac_listener_removal_failed,
                true,
                memory_order_release);

            (void)fprintf(
                stderr,
                "zcsr audiomix macOS: unable to remove default output listener "
                "(OSStatus %d); mixer slot will be quarantined\n",
                (int)remove_status);
        }

        while (atomic_load_explicit(
                    &owner->mac_listener_callbacks_active,
                    memory_order_acquire) != 0U) {
            struct timespec ts = { 0, 1000000L };
            (void)nanosleep(&ts, NULL);
        }
    }

    /*
     * The worker has stopped producing frames before calling backend_close.
     * Stop the AudioUnit so its render callback cannot access the PCM ring
     * after this close operation returns.
     */
    if (b->output_unit_started && b->output_unit) {
        OSStatus status = AudioOutputUnitStop(b->output_unit);
        if (status != noErr) {
            (void)fprintf(
                stderr,
                "zcsr audiomix macOS: AudioOutputUnitStop failed (%d)\n",
                (int)status);
        }
        b->output_unit_started = false;
    }

    if (b->output_unit_initialized && b->output_unit) {
        (void)AudioUnitUninitialize(b->output_unit);
        b->output_unit_initialized = false;
    }

    if (b->output_unit) {
        (void)AudioComponentInstanceDispose(b->output_unit);
        b->output_unit = NULL;
    }

    /*
     * Once the consumer is stopped, discard queued output from the previous
     * device. WAV data and channel playback positions remain untouched.
     */
    if (owner) {
        unsigned tail = atomic_load_explicit(
            &owner->pcm_tail, memory_order_relaxed);

        atomic_store_explicit(
            &owner->pcm_head, tail, memory_order_release);
    }

    *b = (zcsr_mix_backend){ 0 };

    /* M1-style owner preservation across structure reset. */
    b->owner = owner;
}

void zcsr_mix_backend_open(zcsr_mix_backend* b) {
    zcsr_mixer* owner;
    AudioComponentDescription description;
    AudioComponent component;
    AudioStreamBasicDescription format;
    AURenderCallbackStruct render_callback;
    OSStatus status;

    if (!b) return;

    owner = b->owner;
    *b = (zcsr_mix_backend){ 0 };
    b->owner = owner;

    if (!owner) return;

    /*
     * An earlier CoreAudio listener failed to unregister. Do not create a
     * second listener or reactivate its old context; let A3's worker retry
     * logic remain in RECOVERY_PENDING.
     */
    if (atomic_load_explicit(
            &owner->mac_listener_removal_failed, memory_order_acquire)) {
        return;
    }

    memset(&description, 0, sizeof(description));
    description.componentType = kAudioUnitType_Output;
    description.componentSubType = kAudioUnitSubType_DefaultOutput;
    description.componentManufacturer = kAudioUnitManufacturer_Apple;

    component = AudioComponentFindNext(NULL, &description);
    if (!component) goto fail;

    status = AudioComponentInstanceNew(component, &b->output_unit);
    if (status != noErr || !b->output_unit) goto fail;

    /*
     * Request signed 16-bit interleaved stereo PCM at the mixer sample rate.
     * The DefaultOutput unit routes this format to the system's default
     * output device.
     */
    memset(&format, 0, sizeof(format));
    format.mSampleRate = (Float64)ZCSR_MIX_RATE;
    format.mFormatID = kAudioFormatLinearPCM;
    format.mFormatFlags =
        kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked;
    format.mBytesPerPacket = 4U;
    format.mFramesPerPacket = 1U;
    format.mBytesPerFrame = 4U;
    format.mChannelsPerFrame = 2U;
    format.mBitsPerChannel = 16U;
    format.mReserved = 0U;

    status = AudioUnitSetProperty(
        b->output_unit,
        kAudioUnitProperty_StreamFormat,
        kAudioUnitScope_Input,
        0U,
        &format,
        (UInt32)sizeof(format));
    if (status != noErr) goto fail;

    render_callback.inputProc = zcsr_mix_macos_render_callback;
    render_callback.inputProcRefCon = owner;

    status = AudioUnitSetProperty(
        b->output_unit,
        kAudioUnitProperty_SetRenderCallback,
        kAudioUnitScope_Input,
        0U,
        &render_callback,
        (UInt32)sizeof(render_callback));
    if (status != noErr) goto fail;

    status = AudioUnitInitialize(b->output_unit);
    if (status != noErr) goto fail;
    b->output_unit_initialized = true;

    /*
     * Register a listener on the system default output property.
     * AudioUnit creation can still fail if the listener cannot be registered:
     * B1's contract includes device-change detection, not only playback.
     */
    atomic_store_explicit(
        &owner->mac_listener_accepting, true, memory_order_release);

    status = AudioObjectAddPropertyListener(
        kAudioObjectSystemObject,
        &zcsr_mac_default_output_address,
        zcsr_mix_macos_default_output_changed,
        owner);

    if (status != noErr) {
        atomic_store_explicit(
            &owner->mac_listener_accepting, false, memory_order_release);
        goto fail;
    }

    b->default_output_listener_registered = true;

    status = AudioOutputUnitStart(b->output_unit);
    if (status != noErr) goto fail;

    b->output_unit_started = true;
    b->opened = true;
    return;

fail:
    if (owner) {
        atomic_store_explicit(
            &owner->mac_listener_accepting, false, memory_order_release);
    }

    zcsr_mix_backend_close(b);
    b->owner = owner;
    b->opened = false;
}

void zcsr_mix_backend_write(
    zcsr_mix_backend* b,
    const int16_t* pcm,
    unsigned frames) {
    zcsr_mixer* m;
    unsigned done = 0U;

    if (!b || !pcm || frames == 0U) return;

    m = b->owner;
    if (!m) return;

    /*
     * No direct open/reopen from write. A missing backend publishes pending
     * and returns so the mixer worker can perform the next attempt.
     */
    if (!b->opened || !b->output_unit_started) {
        zcsr_mix_device_set_pending(
            m, ZCSR_MIX_DEVICE_PENDING_CHANGED);
        return;
    }

    while (done < frames &&
           atomic_load_explicit(&m->running, memory_order_acquire)) {
        unsigned head = atomic_load_explicit(
            &m->pcm_head, memory_order_acquire);
        unsigned tail = atomic_load_explicit(
            &m->pcm_tail, memory_order_relaxed);
        unsigned used = tail - head;
        unsigned free_frames =
            (used < ZCSR_MAC_PCM_RING_FRAMES)
                ? (ZCSR_MAC_PCM_RING_FRAMES - used)
                : 0U;
        unsigned chunk = frames - done;

        if (chunk > free_frames) chunk = free_frames;

        if (chunk == 0U) {
            /*
             * Producer-side backpressure only. The AudioUnit render callback
             * never sleeps; it emits silence if it has no queued PCM frames.
             */
            struct timespec ts = { 0, 1000000L };
            (void)nanosleep(&ts, NULL);
            continue;
        }

        for (unsigned i = 0; i < chunk; ++i) {
            unsigned slot =
                (tail + i) & (ZCSR_MAC_PCM_RING_FRAMES - 1U);

            m->pcm_ring[(size_t)slot * 2U] =
                pcm[(size_t)(done + i) * 2U];
            m->pcm_ring[(size_t)slot * 2U + 1U] =
                pcm[(size_t)(done + i) * 2U + 1U];
        }

        tail += chunk;
        atomic_store_explicit(
            &m->pcm_tail, tail, memory_order_release);

        done += chunk;
    }
}

#elif !defined(__linux__)

void zcsr_mix_backend_close(zcsr_mix_backend* b) { if (b) *b = (zcsr_mix_backend){ 0 }; }
void zcsr_mix_backend_open(zcsr_mix_backend* b) { if (b) b->opened = false; }
void zcsr_mix_backend_write(zcsr_mix_backend* b, const int16_t* pcm, unsigned frames) {
    (void)b; (void)pcm; (void)frames; zcsr_mix_sleep_period();
}
#endif

/* === Device-recovery backend wrappers (block 7) === */

enum {
    ZCSR_MIX_DEVICE_RETRY_BACKOFF_MS = 1000,
    ZCSR_MIX_DEVICE_RETRY_STEP_MS = 10
};

static bool zcsr_mix_backend_open_ex(zcsr_mixer* m) {
    if (!m) return false;

#if defined(__ANDROID__)
    /* Android stream open/recovery is handled in the Android backend path. */
    return m->backend.opened && m->backend.stream != NULL;

#elif defined(__linux__) && !defined(__ANDROID__)
    m->backend.owner = m;
    zcsr_mix_backend_open(&m->backend);
    return m->backend.opened;

#elif defined(_WIN32)
    m->backend.owner = m;
    zcsr_mix_backend_open(&m->backend);
    return m->backend.opened;

#elif defined(__APPLE__)
    if (atomic_load_explicit(
            &m->mac_listener_removal_failed, memory_order_acquire)) {
        return false;
    }

    m->backend.owner = m;
    zcsr_mix_backend_open(&m->backend);
    return m->backend.opened;

#else
    zcsr_mix_backend_open(&m->backend);
    return m->backend.opened;
#endif
}

static bool zcsr_mix_backend_close_ex(zcsr_mixer* m) {
    if (!m) return false;

    zcsr_mix_backend_close(&m->backend);

#if defined(__ANDROID__)
    return !m->backend.opened &&
           m->backend.stream == NULL &&
           m->backend.lib == NULL;
#elif defined(__linux__) && !defined(__ANDROID__)
#  if defined(ZCSR_AUDIO_BACKEND_PIPEWIRE)
    return !m->backend.opened && m->backend.pipewire_data == NULL;
#  else
    return !m->backend.opened &&
           m->backend.pcm == NULL &&
           m->backend.lib == NULL;
#  endif
#elif defined(_WIN32)
    return !m->backend.opened &&
           m->backend.enumerator == NULL &&
           m->backend.audio_client == NULL &&
           m->backend.render_client == NULL &&
           m->backend.event == NULL &&
           !m->backend.com_initialized;
#elif defined(__APPLE__)
    return !m->backend.opened &&
           m->backend.output_unit == NULL &&
           !m->backend.output_unit_initialized &&
           !m->backend.output_unit_started &&
           !m->backend.default_output_listener_registered;
#else
    return !m->backend.opened;
#endif
}

/*
 * Bounded, interruptible retry delay.
 * Stop is observed at least once per retry step rather than waiting out
 * the entire one-second delay.
 */
static bool zcsr_mix_device_retry_backoff(zcsr_mixer* m) {
    unsigned elapsed_ms = 0U;

    if (!m) return false;

    while (elapsed_ms < ZCSR_MIX_DEVICE_RETRY_BACKOFF_MS) {
        if (!atomic_load_explicit(&m->running, memory_order_acquire)) {
            return false;
        }

#if defined(_WIN32)
        Sleep(ZCSR_MIX_DEVICE_RETRY_STEP_MS);
#else
        {
            struct timespec ts;
            ts.tv_sec = 0;
            ts.tv_nsec =
                (long)ZCSR_MIX_DEVICE_RETRY_STEP_MS * 1000000L;
            (void)nanosleep(&ts, 0);
        }
#endif
        elapsed_ms += ZCSR_MIX_DEVICE_RETRY_STEP_MS;
    }

    return atomic_load_explicit(&m->running, memory_order_acquire);
}

static int16_t zcsr_mix_clamp(int v) {
    if (v > 32767) return 32767;
    if (v < -32768) return -32768;
    return (int16_t)v;
}

static void zcsr_mix_apply_cmd(zcsr_mixer* m, const zcsr_mix_cmd* cmd) {
    if (!m || !cmd) return;
    if (cmd->type == ZCSR_MIX_CMD_PLAY && cmd->channel >= 0 && cmd->channel < ZCSR_MIX_CHANNELS) {
        zcsr_mix_channel* ch = &m->channels[cmd->channel];
        ch->sound = cmd->sound;
        ch->frame = 0;
        ch->volume = cmd->volume;
        ch->volume_q8 = (int32_t)(cmd->volume * 256.0f + 0.5f);  /* Q8 fixed-point for render hot path */
        ch->loop = cmd->loop;
        atomic_store_explicit(&ch->state, ZCSR_MIX_CH_ACTIVE, memory_order_release);
    } else if (cmd->type == ZCSR_MIX_CMD_STOP_CHANNEL && cmd->channel >= 0 && cmd->channel < ZCSR_MIX_CHANNELS) {
        atomic_store_explicit(&m->channels[cmd->channel].state, ZCSR_MIX_CH_FREE, memory_order_release);
        zcsr_mix_finished_push(m, cmd->channel);
    } else if (cmd->type == ZCSR_MIX_CMD_STOP_ID) {
        for (int i = 0; i < ZCSR_MIX_CHANNELS; ++i) {
            int state = atomic_load_explicit(&m->channels[i].state, memory_order_acquire);
            int sound = m->channels[i].sound;
            if (state == ZCSR_MIX_CH_ACTIVE && sound >= 0 && zcsr_mix_eq(m->sounds[sound].id, cmd->id)) {
                atomic_store_explicit(&m->channels[i].state, ZCSR_MIX_CH_FREE, memory_order_release);
                zcsr_mix_finished_push(m, i);
            }
        }
    }
}

static void zcsr_mix_drain_commands(zcsr_mixer* m) {
    zcsr_mix_cmd cmd;
    while (zcsr_mix_ring_pop(m, &cmd)) zcsr_mix_apply_cmd(m, &cmd);
}

static void zcsr_mix_render_period(zcsr_mixer* m) {
    /* Snapshot channel states once (16 atomic loads) instead of 8192 in the inner loop.
     * render_period runs single-threaded after drain_commands; only ACTIVE channels are
     * rendered. Concurrent FREE<->RESERVED transitions from zcsr_mix_play are irrelevant
     * because they never become ACTIVE within this period (that happens in the next drain). */
    int states[ZCSR_MIX_CHANNELS];
    for (int c = 0; c < ZCSR_MIX_CHANNELS; ++c) {
        states[c] = atomic_load_explicit(&m->channels[c].state, memory_order_acquire);
    }
    for (unsigned frame = 0; frame < ZCSR_MIX_PERIOD_FRAMES; ++frame) {
        int left = 0;
        int right = 0;
        for (int c = 0; c < ZCSR_MIX_CHANNELS; ++c) {
            zcsr_mix_channel* ch = &m->channels[c];
            /* ---- streaming PCM channels ---- */
            if (atomic_load_explicit(&ch->is_stream, memory_order_acquire)) {
                unsigned int w, r, avail, idx;
                int vol;
                if (atomic_load_explicit(&ch->stream_paused, memory_order_acquire)) continue;
                w = atomic_load_explicit(&ch->stream_write, memory_order_acquire);
                r = atomic_load_explicit(&ch->stream_read, memory_order_relaxed);
                avail = w - r;
                if (avail == 0) continue;  /* underrun: silence */
                idx = (r % 4096u) * 2u;
                vol = (int)atomic_load_explicit(&ch->stream_volume_q8, memory_order_acquire);
                left += (ch->pcm_ring[idx] * vol) >> 8;
                right += (ch->pcm_ring[idx + 1] * vol) >> 8;
                atomic_store_explicit(&ch->stream_read, r + 1, memory_order_release);
                atomic_fetch_add_explicit(&ch->stream_consumed, 1u, memory_order_relaxed);
                continue;
            }
            /* ---- WAV channels ---- */
            zcsr_mix_sound* s;
            uint32_t sample;
            if (states[c] != ZCSR_MIX_CH_ACTIVE) continue;
            if (ch->sound < 0 || ch->sound >= ZCSR_MIX_MAX_SOUNDS) continue;
            s = &m->sounds[ch->sound];
            if (!s->used || s->frames == 0) {
                states[c] = ZCSR_MIX_CH_FREE;
                atomic_store_explicit(&ch->state, ZCSR_MIX_CH_FREE, memory_order_release);
                continue;
            }
            if (ch->frame >= s->frames) {
                if (ch->loop) {
                    ch->frame = 0;
                } else {
                    states[c] = ZCSR_MIX_CH_FREE;
                    atomic_store_explicit(&ch->state, ZCSR_MIX_CH_FREE, memory_order_release);
                    zcsr_mix_finished_push(m, c);
                    continue;
                }
            }
            sample = (s->offset + ch->frame) * 2U;
            left += (m->samples[sample] * ch->volume_q8) >> 8;
            right += (m->samples[sample + 1U] * ch->volume_q8) >> 8;
            ++ch->frame;
        }
        m->mix_buffer[frame * 2U] = zcsr_mix_clamp(left);
        m->mix_buffer[frame * 2U + 1U] = zcsr_mix_clamp(right);
    }
}

#if defined(__linux__) && !defined(__ANDROID__)

enum {
    ZCSR_LINUX_CARDS_POLL_INTERVAL_MS = 2000
};

static bool zcsr_linux_monotonic_ms(uint64_t* out_ms) {
    struct timespec now;

    if (!out_ms) return false;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) return false;

    *out_ms =
        (uint64_t)now.tv_sec * 1000ULL +
        (uint64_t)now.tv_nsec / 1000000ULL;

    return true;
}

static void zcsr_linux_poll_cards(zcsr_mixer* m) {
    char current[sizeof m->device_cards_snapshot];
    size_t total = 0U;
    bool read_failed = false;
    bool read_complete = false;
    bool too_large = false;
    int fd;

    if (!m) return;

    fd = open("/proc/asound/cards", O_RDONLY);
    if (fd < 0) {
        return;
    }

    for (;;) {
        ssize_t n;

        if (total >= sizeof current) {
            too_large = true;
            break;
        }

        do {
            n = read(fd, current + total, sizeof current - total);
        } while (n < 0 && errno == EINTR);

        if (n < 0) {
            read_failed = true;
            break;
        }

        if (n == 0) {
            read_complete = true;
            break;
        }

        total += (size_t)n;

        if (total >= sizeof current) {
            too_large = true;
            break;
        }
    }

    (void)close(fd);

    if (read_failed) return;

    if (!too_large && !read_complete) return;

    if (too_large) {
        size_t cap = sizeof m->device_cards_snapshot;
        size_t clipped = cap - 1U;

        memcpy(m->device_cards_snapshot, current, clipped);
        m->device_cards_snapshot[clipped] = '\0';
        m->device_cards_snapshot_len = clipped;
        m->device_cards_snapshot_valid = false;

        return;
    }

    current[total] = '\0';

    if (!m->device_cards_snapshot_ever_succeeded) {
        memcpy(m->device_cards_snapshot, current, total + 1U);
        m->device_cards_snapshot_len = total;
        m->device_cards_snapshot_valid = true;
        m->device_cards_snapshot_ever_succeeded = true;
        return;
    }

    if (!m->device_cards_snapshot_valid) {
        memcpy(m->device_cards_snapshot, current, total + 1U);
        m->device_cards_snapshot_len = total;
        m->device_cards_snapshot_valid = true;
        zcsr_mix_device_set_pending(
            m, ZCSR_MIX_DEVICE_PENDING_CHANGED);
        return;
    }

    if (m->device_cards_snapshot_len == total &&
        memcmp(m->device_cards_snapshot, current, total) == 0) {
        return;
    }

    memcpy(m->device_cards_snapshot, current, total + 1U);
    m->device_cards_snapshot_len = total;
    m->device_cards_snapshot_valid = true;
    m->device_cards_snapshot_ever_succeeded = true;

    zcsr_mix_device_set_pending(
        m, ZCSR_MIX_DEVICE_PENDING_CHANGED);
}

#endif /* defined(__linux__) && !defined(__ANDROID__) */

#if defined(_WIN32)
static DWORD WINAPI zcsr_mix_thread_main(LPVOID arg)
#else
static void* zcsr_mix_thread_main(void* arg)
#endif
{
    zcsr_mixer* m = (zcsr_mixer*)arg;

#if defined(__linux__) && !defined(__ANDROID__)
    uint64_t next_cards_poll_ms = 0U;
    bool first_cards_poll_done = false;
    bool cards_poll_deadline_valid = false;
#endif

    if (!m) {
#if defined(_WIN32)
        return 0;
#else
        return 0;
#endif
    }

    (void)zcsr_mix_backend_open_ex(m);

    while (atomic_load_explicit(&m->running, memory_order_acquire)) {
        unsigned pending_flags;
        int state;
        bool retry;
        bool need_rebuild;

        pending_flags = zcsr_mix_device_take_pending(m);
        state = atomic_load_explicit(
            &m->device_state, memory_order_acquire);

        if (pending_flags != 0U &&
            state == ZCSR_MIX_DEVICE_RUNNING) {
            int expected = ZCSR_MIX_DEVICE_RUNNING;

            (void)atomic_compare_exchange_strong_explicit(
                &m->device_state,
                &expected,
                ZCSR_MIX_DEVICE_CHANGE_PENDING,
                memory_order_acq_rel,
                memory_order_acquire);

            state = atomic_load_explicit(
                &m->device_state, memory_order_acquire);
        }

        retry = (state == ZCSR_MIX_DEVICE_RECOVERY_PENDING);
        need_rebuild =
            retry ||
            (pending_flags != 0U &&
             state == ZCSR_MIX_DEVICE_CHANGE_PENDING);

        if (need_rebuild) {
            bool close_ok;
            bool open_ok = false;

            if (retry && !zcsr_mix_device_retry_backoff(m)) {
                break;
            }

            if (!atomic_load_explicit(
                    &m->running, memory_order_acquire)) {
                break;
            }

            if (!zcsr_mix_device_begin_rebuild(
                    m, pending_flags, retry)) {
                continue;
            }

            close_ok = zcsr_mix_backend_close_ex(m);

            if (close_ok &&
                atomic_load_explicit(
                    &m->running, memory_order_acquire)) {
                open_ok = zcsr_mix_backend_open_ex(m);
            }

            if (!atomic_load_explicit(
                    &m->running, memory_order_acquire)) {
                break;
            }

            if (open_ok) {
                zcsr_mix_device_rebuild_succeeded(m);
            } else {
                zcsr_mix_device_rebuild_failed(m);
            }

            continue;
        }

        zcsr_mix_drain_commands(m);
        zcsr_mix_render_period(m);
        zcsr_mix_backend_write(
            &m->backend,
            m->mix_buffer,
            ZCSR_MIX_PERIOD_FRAMES);

#if defined(__linux__) && !defined(__ANDROID__)
        {
            uint64_t now_ms;

            if (!first_cards_poll_done) {
                zcsr_linux_poll_cards(m);
                first_cards_poll_done = true;

                if (zcsr_linux_monotonic_ms(&now_ms)) {
                    next_cards_poll_ms =
                        now_ms + ZCSR_LINUX_CARDS_POLL_INTERVAL_MS;
                    cards_poll_deadline_valid = true;
                }
            } else if (zcsr_linux_monotonic_ms(&now_ms)) {
                if (!cards_poll_deadline_valid) {
                    next_cards_poll_ms =
                        now_ms + ZCSR_LINUX_CARDS_POLL_INTERVAL_MS;
                    cards_poll_deadline_valid = true;
                } else if (now_ms >= next_cards_poll_ms) {
                    zcsr_linux_poll_cards(m);

                    next_cards_poll_ms =
                        now_ms + ZCSR_LINUX_CARDS_POLL_INTERVAL_MS;
                }
            }
        }
#endif
    }

    zcsr_mix_drain_commands(m);

#if !defined(__ANDROID__)
    (void)zcsr_mix_backend_close_ex(m);
#endif

#if defined(_WIN32)
    return 0;
#else
    return 0;
#endif
}

zcsr_mixer* zcsr_mix_start(void) {
    for (int i = 0; i < ZCSR_MIX_POOL; ++i) {
        zcsr_mixer* m = &g_mix_pool[i];

        if (m->in_use) continue;

        memset(m, 0, sizeof(*m));
        m->in_use = true;

        atomic_init(&m->running, true);
        atomic_init(&m->cmd_head, 0U);
        atomic_init(&m->cmd_tail, 0U);
        atomic_init(&m->finished_head, 0U);
        atomic_init(&m->finished_tail, 0U);

        /* New device-state atomics must be explicitly initialized. */
        atomic_init(&m->device_state, ZCSR_MIX_DEVICE_RUNNING);
        atomic_init(&m->device_pending_flags, 0U);
        atomic_flag_clear(&m->device_callback_lock);

        m->device_recovery_after_loss = false;
        m->device_cb = NULL;
        m->device_user = NULL;

#if defined(_WIN32)
        m->device_notification_client = NULL;
#endif

#if defined(__linux__) && !defined(__ANDROID__)
        m->device_cards_snapshot[0] = '\0';
        m->device_cards_snapshot_len = 0U;
        m->device_cards_snapshot_valid = false;
        m->device_cards_snapshot_ever_succeeded = false;
#endif

#if defined(__ANDROID__)
        atomic_init(&m->pcm_head, 0U);
        atomic_init(&m->pcm_tail, 0U);
        atomic_init(&m->pcm_prefilled, false);
        atomic_init(&m->pcm_underruns, 0U);

        for (int c = 0; c < ZCSR_MIX_CHANNELS; ++c) {
            atomic_init(&m->channels[c].state, ZCSR_MIX_CH_FREE);
            m->channels[c].sound = -1;
        }

        if (!atomic_is_lock_free(&m->pcm_head) ||
            !atomic_is_lock_free(&m->pcm_tail) ||
            !atomic_is_lock_free(&m->pcm_prefilled) ||
            !atomic_is_lock_free(&m->pcm_underruns) ||
            !zcsr_mix_android_open(m)) {
            zcsr_mix_backend_close(&m->backend);
            memset(m, 0, sizeof(*m));
            return 0;
        }

#elif defined(__APPLE__)
        atomic_init(&m->pcm_head, 0U);
        atomic_init(&m->pcm_tail, 0U);
        atomic_init(&m->pcm_prefilled, false);
        atomic_init(&m->pcm_underruns, 0U);

        atomic_init(&m->mac_listener_callbacks_active, 0U);
        atomic_init(&m->mac_listener_accepting, false);
        atomic_init(&m->mac_listener_removal_failed, false);

        for (int c = 0; c < ZCSR_MIX_CHANNELS; ++c) {
            atomic_init(&m->channels[c].state, ZCSR_MIX_CH_FREE);
            m->channels[c].sound = -1;
        }

        /*
         * The render callback is real-time and relies on lock-free ring
         * index/statistic atomics. If these guarantees are unavailable,
         * don't start an AudioUnit using potentially blocking atomics.
         */
        if (!atomic_is_lock_free(&m->pcm_head) ||
            !atomic_is_lock_free(&m->pcm_tail) ||
            !atomic_is_lock_free(&m->pcm_underruns)) {
            memset(m, 0, sizeof(*m));
            return 0;
        }

#else
        for (int c = 0; c < ZCSR_MIX_CHANNELS; ++c) {
            atomic_store(&m->channels[c].state, ZCSR_MIX_CH_FREE);
            m->channels[c].sound = -1;
        }
#endif

#if defined(_WIN32)
        m->thread = CreateThread(0, 0, zcsr_mix_thread_main, m, 0, 0);
        m->thread_started = m->thread != 0;
#else
        m->thread_started =
            pthread_create(&m->thread, 0, zcsr_mix_thread_main, m) == 0;
#endif

        if (!m->thread_started) {
#if defined(__ANDROID__)
            atomic_store_explicit(
                &m->running, false, memory_order_release);
            zcsr_mix_backend_close(&m->backend);
#endif
            memset(m, 0, sizeof(*m));
            return 0;
        }

#if defined(__ANDROID__)
        /*
         * Preserve the original startup ordering:
         * thread prefill -> request_start -> mark stream_started.
         */
        {
            unsigned tries = 0;

            while (!atomic_load_explicit(
                        &m->pcm_prefilled, memory_order_acquire) &&
                   atomic_load_explicit(
                       &m->running, memory_order_acquire) &&
                   tries++ < 200U) {
                zcsr_mix_sleep_period();
            }

            if (!atomic_load_explicit(
                    &m->pcm_prefilled, memory_order_acquire) ||
                !m->backend.stream ||
                m->backend.api.request_start(m->backend.stream) != AAUDIO_OK) {
                atomic_store_explicit(
                    &m->running, false, memory_order_release);
                pthread_join(m->thread, 0);
                m->thread_started = false;
                zcsr_mix_backend_close(&m->backend);
                memset(m, 0, sizeof(*m));
                return 0;
            }

            m->backend.stream_started = true;
        }
#endif

        g_active_mixer = m;
        return m;
    }

    return 0;
}

void zcsr_mix_stop(zcsr_mixer* m) {
    if (!m || !m->in_use) return;

    atomic_store_explicit(
        &m->running, false, memory_order_release);

    if (m->thread_started) {
#if defined(_WIN32)
        WaitForSingleObject(m->thread, INFINITE);
        CloseHandle(m->thread);
#else
        pthread_join(m->thread, 0);
#endif
    }

#if defined(__ANDROID__)
    /*
     * Preserve existing lifecycle: the mixer thread is joined before
     * the AAudio stream is closed and the library is unloaded.
     */
    zcsr_mix_backend_close(&m->backend);
#endif

    if (g_active_mixer == m) {
        g_active_mixer = 0;
    }

#if defined(__APPLE__)
    if (atomic_load_explicit(
            &m->mac_listener_removal_failed, memory_order_acquire)) {
        /*
         * A CoreAudio listener could not be unregistered. Do not allow this
         * mixer slot or its atomic callback context to be reused.
         * The slot deliberately remains in_use; the rest of the pool is
         * unaffected if another slot is available.
         */
        m->thread_started = false;
        zcsr_mix_set_device_callback(m, NULL, NULL);
        return;
    }
#endif

    /*
     * The worker has exited, so callback state, pending flags, platform
     * snapshot, and platform notification bookkeeping can now be cleared
     * along with the original mixer state.
     */
    memset(m, 0, sizeof(*m));
}

bool zcsr_mix_register_wav(zcsr_mixer* m, const char* id, const char* wav_path) {
    if (!m || !id || !wav_path || zcsr_mix_find_sound(m, id) >= 0) return false;
    for (int i = 0; i < ZCSR_MIX_MAX_SOUNDS; ++i) {
        zcsr_mix_sound candidate = { 0 };
        if (m->sounds[i].used) continue;
        if (!zcsr_mix_copy(candidate.id, sizeof candidate.id, id)) return false;
        if (!zcsr_mix_load_wav(m, &candidate, wav_path)) return false;
        candidate.used = true;
        m->sounds[i] = candidate;
        return true;
    }
    return false;
}

int zcsr_mix_play(zcsr_mixer* m, const char* id, float volume, bool loop) {
    int sound = zcsr_mix_find_sound(m, id);
    zcsr_mix_cmd cmd;
    if (!m || sound < 0) return -1;
    if (volume < 0.0f) volume = 0.0f;
    if (volume > 1.0f) volume = 1.0f;
    for (int c = 0; c < ZCSR_MIX_CHANNELS; ++c) {
        int expected = ZCSR_MIX_CH_FREE;
        if (!atomic_compare_exchange_strong(&m->channels[c].state, &expected, ZCSR_MIX_CH_RESERVED)) continue;
        cmd = (zcsr_mix_cmd){ ZCSR_MIX_CMD_PLAY, c, sound, volume, loop, { 0 } };
        if (!zcsr_mix_ring_push(m, &cmd)) {
            atomic_store(&m->channels[c].state, ZCSR_MIX_CH_FREE);
            return -1;
        }
        return c;
    }
    return -1;
}

void zcsr_mix_stop_channel(zcsr_mixer* m, int channel) {
    zcsr_mix_cmd cmd;
    if (!m || channel < 0 || channel >= ZCSR_MIX_CHANNELS) return;
    cmd = (zcsr_mix_cmd){ ZCSR_MIX_CMD_STOP_CHANNEL, channel, -1, 0.0f, false, { 0 } };
    (void)zcsr_mix_ring_push(m, &cmd);
}

bool zcsr_mix_slot_play(const char* payload) {
    return g_active_mixer && zcsr_mix_play(g_active_mixer, payload, 1.0f, false) >= 0;
}

bool zcsr_mix_slot_stop(const char* payload) {
    zcsr_mix_cmd cmd;
    if (!g_active_mixer || !zcsr_mix_copy(cmd.id, sizeof cmd.id, payload)) return false;
    cmd.type = ZCSR_MIX_CMD_STOP_ID;
    cmd.channel = -1;
    cmd.sound = -1;
    cmd.volume = 0.0f;
    cmd.loop = false;
    return zcsr_mix_ring_push(g_active_mixer, &cmd);
}

size_t zcsr_mix_poll_finished(zcsr_mixer* m, int* out, size_t cap) {
    size_t n = 0;
    if (!m || !out) return 0;
    while (n < cap) {
        unsigned head = atomic_load_explicit(&m->finished_head, memory_order_relaxed);
        unsigned tail = atomic_load_explicit(&m->finished_tail, memory_order_acquire);
        if (head == tail) break;
        out[n++] = m->finished[head];
        atomic_store_explicit(&m->finished_head, (head + 1U) % ZCSR_MIX_FINISHED_CAP, memory_order_release);
    }
    return n;
}

/* ===========================================================================================
 * Streaming PCM channel API (additive)
 * =========================================================================================== */

int zcsr_mix_attach_stream(zcsr_mixer* m) {
    int c;
    if (!m) return -1;
    for (c = 0; c < ZCSR_MIX_CHANNELS; ++c) {
        zcsr_mix_channel* ch = &m->channels[c];
        int expected = ZCSR_MIX_CH_FREE;
        if (atomic_load_explicit(&ch->is_stream, memory_order_acquire)) continue;
        if (!atomic_compare_exchange_strong_explicit(&ch->state, &expected, ZCSR_MIX_CH_RESERVED,
                                                      memory_order_acq_rel, memory_order_acquire))
            continue;
        atomic_store_explicit(&ch->is_stream, true, memory_order_release);
        atomic_store_explicit(&ch->stream_reserved, true, memory_order_release);
        atomic_store_explicit(&ch->stream_paused, false, memory_order_release);
        atomic_store_explicit(&ch->stream_volume_q8, 256u, memory_order_release);
        atomic_store_explicit(&ch->stream_write, 0u, memory_order_release);
        atomic_store_explicit(&ch->stream_read, 0u, memory_order_release);
        atomic_store_explicit(&ch->stream_consumed, 0u, memory_order_release);
        atomic_store_explicit(&ch->state, ZCSR_MIX_CH_ACTIVE, memory_order_release);
        return c;
    }
    return -1;
}

size_t zcsr_mix_feed_stream(zcsr_mixer* m, int channel,
                             const int16_t* pcm, size_t frames) {
    zcsr_mix_channel* ch;
    unsigned int w, r, space, n, i;
    if (!m || channel < 0 || channel >= ZCSR_MIX_CHANNELS || !pcm) return 0;
    ch = &m->channels[channel];
    if (!atomic_load_explicit(&ch->is_stream, memory_order_acquire)) return 0;
    if (!atomic_load_explicit(&ch->stream_reserved, memory_order_acquire)) return 0;
    w = atomic_load_explicit(&ch->stream_write, memory_order_relaxed);
    r = atomic_load_explicit(&ch->stream_read, memory_order_acquire);
    space = 4096u - (w - r);
    n = (unsigned int)(frames < space ? frames : space);
    for (i = 0; i < n; ++i) {
        unsigned idx = ((w + i) % 4096u) * 2u;
        ch->pcm_ring[idx + 0] = pcm[i * 2 + 0];
        ch->pcm_ring[idx + 1] = pcm[i * 2 + 1];
    }
    atomic_store_explicit(&ch->stream_write, w + n, memory_order_release);
    return n;
}

bool zcsr_mix_stream_set_volume(zcsr_mixer* m, int channel, float volume) {
    zcsr_mix_channel* ch;
    if (!m || channel < 0 || channel >= ZCSR_MIX_CHANNELS) return false;
    ch = &m->channels[channel];
    if (!atomic_load_explicit(&ch->is_stream, memory_order_acquire)) return false;
    if (volume < 0.0f) volume = 0.0f;
    if (volume > 1.0f) volume = 1.0f;
    atomic_store_explicit(&ch->stream_volume_q8,
                           (unsigned int)(int)(volume * 256.0f + 0.5f),
                           memory_order_release);
    return true;
}

bool zcsr_mix_stream_set_paused(zcsr_mixer* m, int channel, bool paused) {
    zcsr_mix_channel* ch;
    if (!m || channel < 0 || channel >= ZCSR_MIX_CHANNELS) return false;
    ch = &m->channels[channel];
    if (!atomic_load_explicit(&ch->is_stream, memory_order_acquire)) return false;
    atomic_store_explicit(&ch->stream_paused, paused, memory_order_release);
    return true;
}

uint64_t zcsr_mix_stream_consumed_frames(const zcsr_mixer* m, int channel) {
    const zcsr_mix_channel* ch;
    if (!m || channel < 0 || channel >= ZCSR_MIX_CHANNELS) return 0;
    ch = &m->channels[channel];
    if (!atomic_load_explicit(&ch->is_stream, memory_order_acquire)) return 0;
    return (uint64_t)atomic_load_explicit(&ch->stream_consumed, memory_order_acquire);
}

bool zcsr_mix_stream_flush(zcsr_mixer* m, int channel) {
    zcsr_mix_channel* ch;
    unsigned int w;
    if (!m || channel < 0 || channel >= ZCSR_MIX_CHANNELS) return false;
    ch = &m->channels[channel];
    if (!atomic_load_explicit(&ch->is_stream, memory_order_acquire)) return false;
    w = atomic_load_explicit(&ch->stream_write, memory_order_acquire);
    atomic_store_explicit(&ch->stream_read, w, memory_order_release);
    return true;
}

bool zcsr_mix_stream_detach(zcsr_mixer* m, int channel) {
    zcsr_mix_channel* ch;
    if (!m || channel < 0 || channel >= ZCSR_MIX_CHANNELS) return false;
    ch = &m->channels[channel];
    if (!atomic_load_explicit(&ch->is_stream, memory_order_acquire)) return false;
    atomic_store_explicit(&ch->is_stream, false, memory_order_release);
    atomic_store_explicit(&ch->stream_reserved, false, memory_order_release);
    atomic_store_explicit(&ch->stream_paused, false, memory_order_release);
    atomic_store_explicit(&ch->state, ZCSR_MIX_CH_FREE, memory_order_release);
    ch->sound = -1;
    return true;
}
