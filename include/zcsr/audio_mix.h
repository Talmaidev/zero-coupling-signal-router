#ifndef ZCSR_AUDIO_MIX_H
#define ZCSR_AUDIO_MIX_H
/* Engine-ext Agent 2 — multi-channel WAV mixer. NEW module; COEXISTS with the single-stream
 * zcsr/audio.h (does not change/break it). 16 software channels, one dedicated audio thread,
 * lock-free command/PCM rings. Backend: Win WASAPI / Linux ALSA / Android AAudio (API 26+
 * dynamically loaded at runtime). Other unsupported platforms use the stub backend.
 * Implementer: modules/audiomix. Signals-friendly: audio_play / audio_finished.
 * Fixed pools; no runtime heap for mixer/sample/PCM storage. */
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

enum { ZCSR_MIX_CHANNELS = 16 };

typedef struct zcsr_mixer zcsr_mixer; /* opaque; owns the audio thread + device */

zcsr_mixer* zcsr_mix_start(void);       /* spawn audio thread + open output device. NULL on fail. */
void        zcsr_mix_stop(zcsr_mixer*); /* stop playback + join thread + close device. */

/*
 * Audio output-device lifecycle events.
 *
 * Notifications are dispatched by the mixer worker, not by a platform
 * notification callback or a real-time audio callback.
 */
typedef enum {
    ZCSR_AUDIO_DEVICE_LOST = 0,
    ZCSR_AUDIO_DEVICE_CHANGED = 1,
    ZCSR_AUDIO_DEVICE_RECOVERED = 2
} zcsr_audio_device_event;

/*
 * Called on the mixer worker thread.
 *
 * LOST:
 *   The current output device/stream has been determined to be unavailable.
 *
 * CHANGED:
 *   The output-device configuration changed and the backend was reopened
 *   successfully, without an outstanding LOST recovery cycle.
 *
 * RECOVERED:
 *   The backend reopened successfully after a LOST event.
 *
 * The callback must not synchronously stop or destroy the same mixer,
 * because doing so may attempt to join the current worker thread.
 */
typedef void (*zcsr_audio_device_fn)(
    zcsr_mixer *mixer,
    zcsr_audio_device_event event,
    void *user
);

/*
 * Register, replace, or unregister the output-device callback.
 *
 * A later registration replaces the previous callback and user pointer.
 * Passing callback == NULL unregisters the callback and clears its user
 * pointer. Registration does not invoke the callback immediately.
 *
 * Events are dispatched synchronously on the mixer worker thread.
 */
void zcsr_mix_set_device_callback(
    zcsr_mixer *mixer,
    zcsr_audio_device_fn callback,
    void *user
);

/* Register a WAV (PCM 16-bit 44100 Hz mono/stereo) under an id (no path in play calls/signals). */
bool        zcsr_mix_register_wav(zcsr_mixer*, const char* id, const char* wav_path);

/* Play a registered sound on a free channel. Returns channel 0..15, or -1 (no free channel /
 * unknown id). volume 0..1; loop repeats until stopped. Non-blocking (posts to the audio thread). */
int         zcsr_mix_play(zcsr_mixer*, const char* id, float volume, bool loop);
void        zcsr_mix_stop_channel(zcsr_mixer*, int channel);

/* Slots to connect to a zcsr/signal.h router (zcsr_slot_fn shape). payload = sound id.
 * Operate on the module-owned active mixer (like zcsr_audio_slot_*). */
bool        zcsr_mix_slot_play(const char* payload);   /* connect to "PLAY_SOUND" */
bool        zcsr_mix_slot_stop(const char* payload);   /* connect to "STOP_SOUND" (by id) */

/* Drain channels that finished since the last call (the audio thread marks them). Returns the
 * count written into out[] (capped at cap) — lets the caller emit audio_finished(channel). */
size_t      zcsr_mix_poll_finished(zcsr_mixer*, int* out, size_t cap);

/* ---- Streaming PCM channels (additive) ----
 * A stream channel accepts raw int16 stereo PCM from the caller (e.g. a music decoder thread)
 * and mixes it alongside WAV channels. Stream and WAV modes are mutually exclusive per channel. */

/* Reserve a free channel for streaming. Returns channel 0..15, or -1 (none free). */
int         zcsr_mix_attach_stream(zcsr_mixer*);

/* Feed PCM into a stream channel (non-blocking). Returns actual frames accepted.
 * pcm is interleaved stereo int16 (frames * 2 samples). */
size_t      zcsr_mix_feed_stream(zcsr_mixer*, int channel,
                                 const int16_t* pcm, size_t frames);

bool        zcsr_mix_stream_set_volume(zcsr_mixer*, int channel, float volume);
bool        zcsr_mix_stream_set_paused(zcsr_mixer*, int channel, bool paused);

/* Frames consumed by the mixer since attach (monotonic, 32-bit; wraps at 2^32). */
uint64_t    zcsr_mix_stream_consumed_frames(const zcsr_mixer*, int channel);

/* Discard all buffered PCM in a stream channel. */
bool        zcsr_mix_stream_flush(zcsr_mixer*, int channel);

/* Release a stream channel back to the free pool. */
bool        zcsr_mix_stream_detach(zcsr_mixer*, int channel);

#endif /* ZCSR_AUDIO_MIX_H */
