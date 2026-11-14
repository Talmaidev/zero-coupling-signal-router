#ifndef ZCSR_MUSIC_H
#define ZCSR_MUSIC_H
/* Streaming music player (OGG/MP3/FLAC/WAV; AAC/M4A via Windows Media
 * Foundation). Decodes in a background thread, feeds PCM into an audiomix
 * stream channel. No heap: caller provides buffer. Implemented in modules/music.
 * Depends on zcsr/audio_mix.h. */
#include "audio_mix.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

typedef struct zcsr_music zcsr_music;

typedef enum {
    ZCSR_MUSIC_OGG = 0,
    ZCSR_MUSIC_MP3 = 1,
    ZCSR_MUSIC_FLAC = 2,
    ZCSR_MUSIC_WAV = 3,
    ZCSR_MUSIC_AAC = 4,
    ZCSR_MUSIC_M4A = 5
} zcsr_music_format;

typedef enum {
    ZCSR_MUSIC_STOPPED = 0,
    ZCSR_MUSIC_PLAYING,
    ZCSR_MUSIC_PAUSED,
    ZCSR_MUSIC_ENDED,
    ZCSR_MUSIC_ERROR
} zcsr_music_state;

/* Detailed failure reason for instances in ZCSR_MUSIC_ERROR (or a failed play/attach).
 * ZCSR_MUSIC_ERR_NONE is also returned for a null instance. */
typedef enum {
    ZCSR_MUSIC_ERR_NONE = 0,
    ZCSR_MUSIC_ERR_INVALID_DATA,
    ZCSR_MUSIC_ERR_UNSUPPORTED,
    ZCSR_MUSIC_ERR_DECODE,
    ZCSR_MUSIC_ERR_NO_DEVICE
} zcsr_music_error;

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

/* Current playback position in milliseconds, measured from the beginning of the
 * track. seek_ms() changes the baseline; loop does not reset it. Monotonic between
 * seeks. Note: internal consumed_frames is 32-bit; wraps after ~27.3 hours at 44100Hz. */
uint64_t zcsr_music_position_ms(const zcsr_music*);

/* Container duration in milliseconds. Returns false when the duration is unknown
 * (for example a malformed MP3 with no decodable frames) or when out_ms is NULL.
 * Does not change playback state and does not decode. O(1) after load. */
bool zcsr_music_duration_ms(const zcsr_music* music, uint64_t* out_ms);

/* Seek to the target position. The target is clamped to [0, duration].
 * - PLAYING: the decoder thread discards buffered PCM, seeks, and keeps playing.
 * - PAUSED: seeks while staying paused; no audio is produced.
 * - STOPPED/ENDED: records the start point; the next play() begins there.
 * - ERROR or unknown duration: returns false.
 * Does not restart or join the decoder thread. Returns true when the request is
 * accepted; position_ms() is based on the target immediately after acceptance.
 * MP3 seek re-decodes from the first frame because no index buffer is allocated;
 * OGG/FLAC use their sample indexes; WAV seeks by PCM frame offset; AAC/M4A
 * use Media Foundation SetCurrentPosition (Windows only). */
bool zcsr_music_seek_ms(zcsr_music* music, uint64_t ms);

#define ZCSR_MUSIC_TEXT_CAP 128u

/* Container metadata. All strings are UTF-8 and NUL-terminated; a missing tag
 * is an empty string / zero integer, never uninitialized memory. */
typedef struct zcsr_music_info {
    char     title[ZCSR_MUSIC_TEXT_CAP];
    char     artist[ZCSR_MUSIC_TEXT_CAP];
    char     album[ZCSR_MUSIC_TEXT_CAP];
    uint32_t year;
    uint32_t track;
} zcsr_music_info;

/* Last detailed error for this instance. Does not alter state. */
zcsr_music_error zcsr_music_get_error(const zcsr_music* music);

/* Read container metadata parsed at load. Bad/missing tags never fail load;
 * this simply reports empty fields. Returns false only for NULL arguments. */
bool zcsr_music_get_info(const zcsr_music* music, zcsr_music_info* out_info);

/* Embedded cover bytes in their original encoded form (JPEG/PNG/etc.).
 * `bytes_required` returns 0 when there is no cover. `get_cover` copies into
 * the caller buffer only when cap is large enough; it does not decode the
 * image. OGG METADATA_BLOCK_PICTURE is base64-decoded on the fly, MP3 APIC,
 * FLAC PICTURE and M4A covr are copied directly. */
size_t zcsr_music_cover_bytes_required(const zcsr_music* music);
bool zcsr_music_get_cover(const zcsr_music* music, void* buffer, size_t capacity);

/* Thread model. Desktop default is true (decoder thread). Set false only while
 * STOPPED; then play() does not create an OS thread and the host must call
 * pump() from its main loop. Returns false when the state forbids the switch. */
bool zcsr_music_set_threaded(zcsr_music* music, bool threaded);

/* Decode/feed up to max_frames mixer frames without blocking. Only meaningful
 * when threaded == false. Returns false for an invalid instance, a threaded
 * instance, no attached mixer, or terminal state. */
bool zcsr_music_pump(zcsr_music* music, uint32_t max_frames);

#endif /* ZCSR_MUSIC_H */
