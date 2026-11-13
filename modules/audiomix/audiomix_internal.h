/* audiomix_internal.h — shared types for the audio mixer backend split.
 *
 * zcsr_mix_backend is fully defined here so both audiomix.c (core + ALSA)
 * and audiomix_pipewire.c can access its fields.  Platform-specific headers
 * are included per-branch.
 */
#ifndef ZCSR_AUDIOMIX_INTERNAL_H
#define ZCSR_AUDIOMIX_INTERNAL_H

#include "zcsr/audio_mix.h"

#include <stdbool.h>
#include <stdint.h>

#if defined(__ANDROID__)
#include <aaudio/AAudio.h>
#include <dlfcn.h>
#include <pthread.h>
#include <time.h>

typedef aaudio_result_t (*zcsr_aaudio_create_builder_fn)(AAudioStreamBuilder**);
typedef void (*zcsr_aaudio_builder_direction_fn)(AAudioStreamBuilder*, aaudio_direction_t);
typedef void (*zcsr_aaudio_builder_format_fn)(AAudioStreamBuilder*, aaudio_format_t);
typedef void (*zcsr_aaudio_builder_int_fn)(AAudioStreamBuilder*, int32_t);
typedef void (*zcsr_aaudio_builder_performance_fn)(AAudioStreamBuilder*, aaudio_performance_mode_t);
typedef void (*zcsr_aaudio_builder_sharing_fn)(AAudioStreamBuilder*, aaudio_sharing_mode_t);
typedef void (*zcsr_aaudio_data_callback_set_fn)(
    AAudioStreamBuilder*, AAudioStream_dataCallback, void*);
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
    zcsr_aaudio_error_callback_set_fn set_error_callback;
    zcsr_aaudio_open_stream_fn open_stream;
    zcsr_aaudio_delete_builder_fn delete_builder;
    zcsr_aaudio_stream_action_fn request_start;
    zcsr_aaudio_stream_action_fn request_stop;
    zcsr_aaudio_close_stream_fn close_stream;
} zcsr_aaudio_api;
#elif defined(__APPLE__)
#include <AudioToolbox/AudioToolbox.h>
#include <AudioUnit/AudioUnit.h>
#include <CoreAudio/CoreAudio.h>
#include <pthread.h>
#include <time.h>
#elif defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#define CINTERFACE
#include <initguid.h>
#include <windows.h>
#include <objbase.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <mmsystem.h>
#include <limits.h>
#include <wchar.h>
#elif defined(__linux__) && !defined(__ANDROID__)
#include <pthread.h>
#include <time.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <pipewire/pipewire.h>
#include <spa/utils/ringbuffer.h>

enum {
    ZCSR_PW_RING_FRAMES = 2048,   /* 4 periods of 512 frames */
    ZCSR_PW_RING_SAMPLES = 4096,  /* stereo int32 samples */
};

/* PipeWire backend state — embedded in zcsr_mix_backend (zero heap). */
typedef struct {
    struct pw_thread_loop* loop;
    struct pw_context* context;
    struct pw_core* core;
    struct pw_stream* stream;
    struct spa_hook stream_listener;

    /* SPSC ring: mixer thread writes, PipeWire process thread reads. */
    struct spa_ringbuffer ring;
    int32_t ring_data[ZCSR_PW_RING_SAMPLES];
    int32_t mix_scratch[1024];  /* PERIOD_FRAMES(512) * 2 channels */

    bool stream_connected;
    bool core_connected;
} zcsr_pw_backend;
#endif

struct zcsr_mixer;
typedef struct zcsr_mixer zcsr_mixer;

enum {
    ZCSR_MIX_RATE = 44100,
    ZCSR_MIX_PERIOD_FRAMES = 512,
};

/* Platform output backend.  Exactly one platform branch compiles per build. */
typedef struct {
#if defined(__ANDROID__)
    void* lib;
    AAudioStream* stream;
    zcsr_mixer* owner;
    zcsr_aaudio_api api;
    bool stream_started;
#elif defined(__linux__) && !defined(__ANDROID__)
    /* PipeWire backend state embedded directly (zero heap). */
    zcsr_pw_backend pw;
    zcsr_mixer* owner;
#elif defined(_WIN32)
    IMMDeviceEnumerator* enumerator;
    IAudioClient* audio_client;
    IAudioRenderClient* render_client;
    HANDLE event;
    UINT32 buffer_frame_count;
    bool com_initialized;
    DWORD retry_after_tick;
    unsigned timeout_count;
    HWAVEOUT wave_out;
    WAVEHDR wave_hdr;
    HANDLE wave_event;
    bool use_waveout;
    int16_t* wave_buffer;
    unsigned wave_buffer_frames;
    zcsr_mixer* owner;
#elif defined(__APPLE__)
    AudioComponentInstance output_unit;
    zcsr_mixer* owner;
    bool output_unit_initialized;
    bool output_unit_started;
    bool default_output_listener_registered;
#endif
    bool opened;
} zcsr_mix_backend;

/* Backend entry points — exactly one implementation is linked:
 *   - ALSA:   audiomix.c (Linux, default)
 *   - PipeWire: audiomix_pipewire.c (Linux, ZCSR_AUDIO_BACKEND=pipewire)
 *   - Other:  audiomix.c
 */
void zcsr_mix_backend_open(zcsr_mix_backend* b);
void zcsr_mix_backend_close(zcsr_mix_backend* b);
void zcsr_mix_backend_write(zcsr_mix_backend* b, const int16_t* pcm, unsigned frames);

/* Sleep for one period (~11.6ms at 44100/512).  Implemented in audiomix.c. */
void zcsr_mix_sleep_period(void);

#endif /* ZCSR_AUDIOMIX_INTERNAL_H */
