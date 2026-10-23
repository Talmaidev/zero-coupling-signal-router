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
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

/* ALSA dynamic-loader types. */
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
    void* api;             /* zcsr_aaudio_api */
    bool stream_started;
#elif defined(__linux__) && !defined(__ANDROID__)
#  if defined(ZCSR_AUDIO_BACKEND_PIPEWIRE)
    /* PipeWire backend stores its full state in an internal struct. */
    void* pipewire_data;     /* zcsr_pw_backend* (defined in audiomix_pipewire.c) */
    int32_t* mix_scratch;    /* int32 accumulation buffer, PERIOD_FRAMES*2 */
    zcsr_mixer* owner;
#  else
    /* ALSA dynamic-loader backend. */
    void* lib;
    snd_pcm_t* pcm;
    zcsr_snd_pcm_writei_fn writei;
    zcsr_snd_pcm_prepare_fn prepare;
    zcsr_snd_pcm_close_fn close;
    zcsr_mixer* owner;
#  endif
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
