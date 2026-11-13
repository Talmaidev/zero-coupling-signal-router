#ifndef ZCSR_MUSIC_H
#define ZCSR_MUSIC_H
/* Streaming music player (OGG Vorbis / MP3). Decodes in a background thread,
 * feeds PCM into an audiomix stream channel. No heap: caller provides buffer.
 * Implemented in modules/music. Depends on zcsr/audio_mix.h. */
#include "audio_mix.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

typedef struct zcsr_music zcsr_music;

typedef enum {
    ZCSR_MUSIC_OGG = 0,
    ZCSR_MUSIC_MP3 = 1
} zcsr_music_format;

typedef enum {
    ZCSR_MUSIC_STOPPED = 0,
    ZCSR_MUSIC_PLAYING,
    ZCSR_MUSIC_PAUSED,
    ZCSR_MUSIC_ENDED,
    ZCSR_MUSIC_ERROR
} zcsr_music_state;

/* Buffer size required for zcsr_music_load. Includes alignment slack.
 * Uses safe_add to avoid overflow. */
size_t zcsr_music_buffer_bytes_required(size_t encoded_len);

/* Load a music file from memory. Format must match the encoded data.
 * Returns NULL on invalid data or insufficient buffer. */
zcsr_music* zcsr_music_load(const void* encoded, size_t len,
                            zcsr_music_format fmt,
                            void* buffer, size_t bytes);

void zcsr_music_destroy(zcsr_music*);

/* Attach to a mixer (allocates a stream channel). Must be called before play. */
bool zcsr_music_attach_mixer(zcsr_music*, zcsr_mixer*);

bool zcsr_music_play(zcsr_music*);
void zcsr_music_stop(zcsr_music*);
void zcsr_music_pause(zcsr_music*);   /* also pauses mixer channel */
void zcsr_music_resume(zcsr_music*);  /* also resumes mixer channel */
void zcsr_music_set_volume(zcsr_music*, float volume);
void zcsr_music_set_loop(zcsr_music*, bool loop);

zcsr_music_state zcsr_music_get_state(const zcsr_music*);

/* PCM milliseconds consumed by the mixer since the current play() session.
 * Monotonic; not reset on loop. New play() resets to 0.
 * Note: internal consumed_frames is 32-bit; wraps after ~27.3 hours at 44100Hz. */
uint64_t zcsr_music_position_ms(const zcsr_music*);

#endif /* ZCSR_MUSIC_H */
