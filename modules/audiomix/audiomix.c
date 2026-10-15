/* Engine-ext Agent 2 - multi-channel audio mixer.
 * Fixed pools only, one mixer thread, command/finished rings, no cross-module includes.
 * Linux attempts ALSA dynamically (no CMake find_package); if no device exists, the mixer
 * still runs its logic path so headless CI can validate threading/state.
 */
#include "zcsr/audio_mix.h"

#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#if defined(__ANDROID__)
#include <aaudio/AAudio.h>
#include <dlfcn.h>
#include <pthread.h>
#include <time.h>
#elif defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <initguid.h>
#include <windows.h>
#include <objbase.h>
#include <mmdeviceapi.h>
#include <audioclient.h>

DEFINE_GUID(CLSID_MMDeviceEnumerator, 0xBCDE0395, 0xE52F, 0x467C, 0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E);
DEFINE_GUID(IID_IMMDeviceEnumerator, 0xA95664D2, 0x9614, 0x4F35, 0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6);
DEFINE_GUID(IID_IAudioClient, 0x1CB9AD4C, 0xDBFA, 0x4C32, 0xB1, 0x78, 0x2A, 0x69, 0x72, 0x6B, 0x5C, 0x3F);
DEFINE_GUID(IID_IAudioRenderClient, 0xF294ACFC, 0x3146, 0x4483, 0xA7, 0xBF, 0xAD, 0xDC, 0xA7, 0xC2, 0x60, 0xE2);
#else
#include <pthread.h>
#include <time.h>
#endif

#if defined(__linux__) && !defined(__ANDROID__)
#include <dlfcn.h>
typedef struct _snd_pcm snd_pcm_t;
typedef long snd_pcm_sframes_t;
typedef int (*zcsr_snd_pcm_open_fn)(snd_pcm_t**, const char*, int, int);
typedef int (*zcsr_snd_pcm_set_params_fn)(snd_pcm_t*, int, int, unsigned int, unsigned int, int, unsigned int);
typedef snd_pcm_sframes_t (*zcsr_snd_pcm_writei_fn)(snd_pcm_t*, const void*, unsigned long);
typedef int (*zcsr_snd_pcm_prepare_fn)(snd_pcm_t*);
typedef int (*zcsr_snd_pcm_close_fn)(snd_pcm_t*);
typedef void (*zcsr_snd_config_update_free_global_fn)(void);
typedef union {
    void* obj;
    zcsr_snd_pcm_open_fn open_fn;
    zcsr_snd_pcm_set_params_fn set_params;
    zcsr_snd_pcm_writei_fn writei;
    zcsr_snd_pcm_prepare_fn prepare;
    zcsr_snd_pcm_close_fn close;
    zcsr_snd_config_update_free_global_fn config_free_global;
} zcsr_alsa_sym;
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
    ZCSR_MIX_RATE = 44100,
    ZCSR_MIX_PERIOD_FRAMES = 512,
    ZCSR_MIX_SAMPLE_FRAMES = ZCSR_MIX_RATE * 20
};

enum {
    ZCSR_MIX_CH_FREE = 0,
    ZCSR_MIX_CH_RESERVED = 1,
    ZCSR_MIX_CH_ACTIVE = 2
};

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
    float volume;
    bool loop;
} zcsr_mix_channel;

typedef struct {
    zcsr_mix_cmd_type type;
    int channel;
    int sound;
    float volume;
    bool loop;
    char id[ZCSR_MIX_ID_CAP];
} zcsr_mix_cmd;

typedef struct {
#if defined(__ANDROID__)
    void* lib;
    AAudioStream* stream;
    zcsr_mixer* owner;
    zcsr_aaudio_api api;
    bool stream_started;
#elif defined(__linux__) && !defined(__ANDROID__)
    void* lib;
    snd_pcm_t* pcm;
    zcsr_snd_pcm_writei_fn writei;
    zcsr_snd_pcm_prepare_fn prepare;
    zcsr_snd_pcm_close_fn close;
#elif defined(_WIN32)
    IMMDeviceEnumerator* enumerator;
    IAudioClient* audio_client;
    IAudioRenderClient* render_client;
    HANDLE event;
    UINT32 buffer_frame_count;
    bool com_initialized;
    DWORD retry_after_tick;
    unsigned timeout_count;
#endif
    bool opened;
} zcsr_mix_backend;

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
#if defined(__ANDROID__)
    /* SPSC PCM ring: mixer thread is producer, AAudio callback is consumer. */
    atomic_uint pcm_head;
    atomic_uint pcm_tail;
    atomic_bool pcm_prefilled;
    atomic_uint pcm_underruns;
    int16_t pcm_ring[4096U * 2U];
#endif
    zcsr_mix_backend backend;
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

static void zcsr_mix_sleep_period(void) {
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

static bool zcsr_mix_android_open(zcsr_mixer* m) {
    zcsr_mix_backend* b;
    AAudioStreamBuilder* builder = 0;
    aaudio_result_t result;
    if (!m) return false;
    b = &m->backend;
    *b = (zcsr_mix_backend){ 0 };
    b->lib = dlopen("libaaudio.so", RTLD_NOW | RTLD_LOCAL);
    if (!b->lib) return false;

#define ZCSR_LOAD_AAUDIO(field, symbol, member) \
    do { b->api.field = zcsr_mix_aaudio_sym(b->lib, symbol).member; \
         if (!b->api.field) goto fail; } while (0)
    ZCSR_LOAD_AAUDIO(create_builder, "AAudio_createStreamBuilder", create_builder);
    ZCSR_LOAD_AAUDIO(set_direction, "AAudioStreamBuilder_setDirection", set_direction);
    ZCSR_LOAD_AAUDIO(set_format, "AAudioStreamBuilder_setFormat", set_format);
    ZCSR_LOAD_AAUDIO(set_channel_count, "AAudioStreamBuilder_setChannelCount", set_channel_count);
    ZCSR_LOAD_AAUDIO(set_sample_rate, "AAudioStreamBuilder_setSampleRate", set_sample_rate);
    ZCSR_LOAD_AAUDIO(set_performance_mode, "AAudioStreamBuilder_setPerformanceMode", set_performance_mode);
    ZCSR_LOAD_AAUDIO(set_sharing_mode, "AAudioStreamBuilder_setSharingMode", set_sharing_mode);
    ZCSR_LOAD_AAUDIO(set_data_callback, "AAudioStreamBuilder_setDataCallback", set_data_callback);
    ZCSR_LOAD_AAUDIO(open_stream, "AAudioStreamBuilder_openStream", open_stream);
    ZCSR_LOAD_AAUDIO(delete_builder, "AAudioStreamBuilder_delete", delete_builder);
    ZCSR_LOAD_AAUDIO(request_start, "AAudioStream_requestStart", request_start);
    ZCSR_LOAD_AAUDIO(request_stop, "AAudioStream_requestStop", request_stop);
    ZCSR_LOAD_AAUDIO(close_stream, "AAudioStream_close", close_stream);
#undef ZCSR_LOAD_AAUDIO

    result = b->api.create_builder(&builder);
    if (result != AAUDIO_OK || !builder) goto fail;
    b->api.set_direction(builder, AAUDIO_DIRECTION_OUTPUT);
    b->api.set_format(builder, AAUDIO_FORMAT_PCM_I16);
    b->api.set_channel_count(builder, 2);
    b->api.set_sample_rate(builder, ZCSR_MIX_RATE);
    b->api.set_performance_mode(builder, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
    b->api.set_sharing_mode(builder, AAUDIO_SHARING_MODE_SHARED);
    b->api.set_data_callback(builder, zcsr_mix_aaudio_callback, m);
    result = b->api.open_stream(builder, &b->stream);
    b->api.delete_builder(builder);
    builder = 0;
    if (result != AAUDIO_OK || !b->stream) goto fail;
    b->owner = m;
    b->opened = true;
    return true;

fail:
    if (builder && b->api.delete_builder) b->api.delete_builder(builder);
    if (b->stream && b->api.close_stream) {
        (void)b->api.close_stream(b->stream);
        b->stream = 0;
    }
    if (b->lib) dlclose(b->lib);
    *b = (zcsr_mix_backend){ 0 };
    return false;
}

static void zcsr_mix_backend_close(zcsr_mix_backend* b) {
    if (!b) return;
    if (b->stream && b->stream_started && b->api.request_stop)
        (void)b->api.request_stop(b->stream);
    if (b->stream && b->api.close_stream)
        (void)b->api.close_stream(b->stream);
    b->stream = 0;
    if (b->lib) dlclose(b->lib);
    *b = (zcsr_mix_backend){ 0 };
}

static void zcsr_mix_backend_open(zcsr_mix_backend* b) {
    /* AAudio stream is opened by zcsr_mix_start before the mixer thread is created. */
    (void)b;
}

static void zcsr_mix_backend_write(zcsr_mix_backend* b, const int16_t* pcm, unsigned frames) {
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

#elif defined(__linux__) && !defined(__ANDROID__)
static void* zcsr_mix_sym(void* lib, const char* name) {
    return lib ? dlsym(lib, name) : 0;
}

static zcsr_alsa_sym zcsr_mix_alsa_sym(void* lib, const char* name) {
    zcsr_alsa_sym s;
    s.obj = zcsr_mix_sym(lib, name);
    return s;
}

static void zcsr_mix_backend_close(zcsr_mix_backend* b) {
    zcsr_snd_config_update_free_global_fn config_free_global;
    if (!b) return;
    if (b->opened && b->pcm && b->close) b->close(b->pcm);
    config_free_global = b->lib ? zcsr_mix_alsa_sym(b->lib, "snd_config_update_free_global").config_free_global : 0;
    if (config_free_global) config_free_global();
    if (b->lib) dlclose(b->lib);
    *b = (zcsr_mix_backend){ 0 };
}

static void zcsr_mix_backend_open(zcsr_mix_backend* b) {
    zcsr_snd_pcm_open_fn open_fn;
    zcsr_snd_pcm_set_params_fn set_params;
    if (!b) return;
    b->lib = dlopen("libasound.so.2", RTLD_LAZY | RTLD_LOCAL);
    if (!b->lib) return;
    open_fn = zcsr_mix_alsa_sym(b->lib, "snd_pcm_open").open_fn;
    set_params = zcsr_mix_alsa_sym(b->lib, "snd_pcm_set_params").set_params;
    b->writei = zcsr_mix_alsa_sym(b->lib, "snd_pcm_writei").writei;
    b->prepare = zcsr_mix_alsa_sym(b->lib, "snd_pcm_prepare").prepare;
    b->close = zcsr_mix_alsa_sym(b->lib, "snd_pcm_close").close;
    if (!open_fn || !set_params || !b->writei || !b->prepare || !b->close ||
        open_fn(&b->pcm, "default", 0, 0) < 0 ||
        set_params(b->pcm, 2, 3, 2U, ZCSR_MIX_RATE, 1, 25000U) < 0) {
        zcsr_mix_backend_close(b);
        return;
    }
    b->opened = true;
}

static void zcsr_mix_backend_write(zcsr_mix_backend* b, const int16_t* pcm, unsigned frames) {
    if (!b || !b->opened || !pcm) {
        zcsr_mix_sleep_period();
        return;
    }
    while (frames > 0) {
        snd_pcm_sframes_t n = b->writei(b->pcm, pcm, frames);
        if (n < 0) {
            (void)b->prepare(b->pcm);
            break;
        }
        if (n == 0) break;
        pcm += (unsigned)n * 2U;
        frames -= (unsigned)n;
    }
}
#elif defined(_WIN32)

static void zcsr_mix_backend_close(zcsr_mix_backend* b) {
    if (!b) return;

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

    *b = (zcsr_mix_backend){ 0 };
}

static void zcsr_mix_backend_open(zcsr_mix_backend* b) {
    HRESULT hr;
    IMMDevice* device = NULL;
    WAVEFORMATEX wfx = { 0 };

    if (!b) return;

    *b = (zcsr_mix_backend){ 0 };

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

    hr = b->enumerator->lpVtbl->GetDefaultAudioEndpoint(
        b->enumerator,
        eRender,
        eConsole,
        &device
    );
    if (FAILED(hr) || !device) goto fail;

    hr = device->lpVtbl->Activate(
        device,
        &IID_IAudioClient,
        CLSCTX_ALL,
        NULL,
        (void**)&b->audio_client
    );

    device->lpVtbl->Release(device);
    device = NULL;

    if (FAILED(hr) || !b->audio_client) goto fail;

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
    return;

fail:
    if (device) {
        device->lpVtbl->Release(device);
        device = NULL;
    }

    zcsr_mix_backend_close(b);

    b->retry_after_tick = GetTickCount() + (DWORD)1000;
}

static bool zcsr_mix_backend_reopen(zcsr_mix_backend* b) {
    if (!b) return false;

    zcsr_mix_backend_close(b);
    zcsr_mix_backend_open(b);

    return b->opened;
}

static void zcsr_mix_backend_write(
    zcsr_mix_backend* b,
    const int16_t* pcm,
    unsigned frames ) {
    unsigned done = 0;
    bool retried = false;

    if (!b || !pcm || frames == 0) {
        zcsr_mix_sleep_period();
        return;
    }

    if (!b->opened) {
        DWORD now = GetTickCount();

        if ((LONG)(now - b->retry_after_tick) >= 0) {
            zcsr_mix_backend_open(b);
        }

        if (!b->opened) {
            zcsr_mix_sleep_period();
            return;
        }
    }

    while (done < frames) {
        DWORD wait_result;
        HRESULT hr;
        UINT32 padding = 0;
        UINT32 available;
        UINT32 chunk;
        BYTE* dst = NULL;

        wait_result = WaitForSingleObject(b->event, 20);

        if (wait_result == WAIT_TIMEOUT) {
            ++b->timeout_count;

            if (b->timeout_count >= 5U) {
                b->timeout_count = 0;

                if (!retried && zcsr_mix_backend_reopen(b)) {
                    retried = true;
                    continue;
                }

                break;
            }

            continue;
        }

        if (wait_result != WAIT_OBJECT_0) {
            if (!retried && zcsr_mix_backend_reopen(b)) {
                retried = true;
                continue;
            }

            break;
        }

        b->timeout_count = 0;

        hr = b->audio_client->lpVtbl->GetCurrentPadding(
            b->audio_client,
            &padding
        );

        if (FAILED(hr)) {
            if (!retried && zcsr_mix_backend_reopen(b)) {
                retried = true;
                continue;
            }

            break;
        }

        if (padding >= b->buffer_frame_count) {
            continue;
        }

        available = b->buffer_frame_count - padding;
        chunk = (UINT32)(frames - done);

        if (chunk > available) {
            chunk = available;
        }

        if (chunk == 0) {
            continue;
        }

        hr = b->render_client->lpVtbl->GetBuffer(
            b->render_client,
            chunk,
            &dst
        );

        if (FAILED(hr) || !dst) {
            if (!retried && zcsr_mix_backend_reopen(b)) {
                retried = true;
                continue;
            }

            break;
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
            if (!retried && zcsr_mix_backend_reopen(b)) {
                retried = true;
                continue;
            }

            break;
        }

        done += (unsigned)chunk;
    }
}

#else

static void zcsr_mix_backend_close(zcsr_mix_backend* b) { if (b) *b = (zcsr_mix_backend){ 0 }; }
static void zcsr_mix_backend_open(zcsr_mix_backend* b) { if (b) b->opened = false; }
static void zcsr_mix_backend_write(zcsr_mix_backend* b, const int16_t* pcm, unsigned frames) {
    (void)b; (void)pcm; (void)frames; zcsr_mix_sleep_period();
}
#endif

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
    for (unsigned frame = 0; frame < ZCSR_MIX_PERIOD_FRAMES; ++frame) {
        int left = 0;
        int right = 0;
        for (int c = 0; c < ZCSR_MIX_CHANNELS; ++c) {
            zcsr_mix_channel* ch = &m->channels[c];
            int state = atomic_load_explicit(&ch->state, memory_order_acquire);
            if (state != ZCSR_MIX_CH_ACTIVE || ch->sound < 0 || ch->sound >= ZCSR_MIX_MAX_SOUNDS) continue;
            zcsr_mix_sound* s = &m->sounds[ch->sound];
            if (!s->used || s->frames == 0) {
                atomic_store_explicit(&ch->state, ZCSR_MIX_CH_FREE, memory_order_release);
                continue;
            }
            if (ch->frame >= s->frames) {
                if (ch->loop) ch->frame = 0;
                else {
                    atomic_store_explicit(&ch->state, ZCSR_MIX_CH_FREE, memory_order_release);
                    zcsr_mix_finished_push(m, c);
                    continue;
                }
            }
            uint32_t sample = (s->offset + ch->frame) * 2U;
            left += (int)((float)m->samples[sample] * ch->volume);
            right += (int)((float)m->samples[sample + 1U] * ch->volume);
            ++ch->frame;
        }
        m->mix_buffer[frame * 2U] = zcsr_mix_clamp(left);
        m->mix_buffer[frame * 2U + 1U] = zcsr_mix_clamp(right);
    }
}

#if defined(_WIN32)
static DWORD WINAPI zcsr_mix_thread_main(LPVOID arg)
#else
static void* zcsr_mix_thread_main(void* arg)
#endif
{
    zcsr_mixer* m = (zcsr_mixer*)arg;
    zcsr_mix_backend_open(&m->backend);
    while (atomic_load_explicit(&m->running, memory_order_acquire)) {
        zcsr_mix_drain_commands(m);
        zcsr_mix_render_period(m);
        zcsr_mix_backend_write(&m->backend, m->mix_buffer, ZCSR_MIX_PERIOD_FRAMES);
    }
    zcsr_mix_drain_commands(m);
#if !defined(__ANDROID__)
    zcsr_mix_backend_close(&m->backend);
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
        m->thread_started = pthread_create(&m->thread, 0, zcsr_mix_thread_main, m) == 0;
#endif
        if (!m->thread_started) {
#if defined(__ANDROID__)
            atomic_store_explicit(&m->running, false, memory_order_release);
            zcsr_mix_backend_close(&m->backend);
#endif
            memset(m, 0, sizeof(*m));
            return 0;
        }
#if defined(__ANDROID__)
        {
            unsigned tries = 0;
            while (!atomic_load_explicit(&m->pcm_prefilled, memory_order_acquire) &&
                   atomic_load_explicit(&m->running, memory_order_acquire) &&
                   tries++ < 200U) {
                zcsr_mix_sleep_period();
            }
            if (!atomic_load_explicit(&m->pcm_prefilled, memory_order_acquire) ||
                !m->backend.stream ||
                m->backend.api.request_start(m->backend.stream) != AAUDIO_OK) {
                atomic_store_explicit(&m->running, false, memory_order_release);
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
    atomic_store_explicit(&m->running, false, memory_order_release);
    if (m->thread_started) {
#if defined(_WIN32)
        WaitForSingleObject(m->thread, INFINITE);
        CloseHandle(m->thread);
#else
        pthread_join(m->thread, 0);
#endif
    }
#if defined(__ANDROID__)
    /* Thread is joined before stream teardown and dlclose. */
    zcsr_mix_backend_close(&m->backend);
#endif
    if (g_active_mixer == m) g_active_mixer = 0;
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
