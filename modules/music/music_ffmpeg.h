#ifndef ZCSR_MUSIC_FFMPEG_H
#define ZCSR_MUSIC_FFMPEG_H

/* Internal FFmpeg backend for the music module. The FFmpeg types are kept
 * behind this opaque handle; none appear in include/zcsr/music.h.
 *
 * Memory contract: the backend holds its decoder context, reusable packet/frame
 * and resampler scratch behind the handle. The per-frame output writes into
 * the caller's source PCM scratch; there is no retained per-frame heap growth.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct zcsr_ffmpeg_backend zcsr_ffmpeg_backend;

enum { ZFF_OK = 0, ZFF_EOF, ZFF_ERROR };

/* Nonzero when the FFmpeg backend is compiled into this build. */
int zcsr_ffmpeg_available(void);

/* Open an in-memory encoded stream. The encoded bytes must outlive the
 * backend. Returns NULL on failure. On success the out_* attributes are
 * filled; out_frames may be 0 when the container does not state a frame count
 * up front. */
zcsr_ffmpeg_backend* zcsr_ffmpeg_open(const void* data, size_t len,
                                      int* out_rate, int* out_channels,
                                      uint64_t* out_frames,
                                      uint64_t* out_duration_ms);

/* Decode up to cap_frames frames into dst (interleaved float stereo,
 * [-1,1]). Returns the number of frames written; *out_status receives a
 * ZFF_* code. */
size_t zcsr_ffmpeg_decode(zcsr_ffmpeg_backend* b, float* dst, size_t cap_frames,
                          int* out_status);

bool zcsr_ffmpeg_seek_ms(zcsr_ffmpeg_backend* b, uint64_t ms);
void zcsr_ffmpeg_rewind(zcsr_ffmpeg_backend* b);
void zcsr_ffmpeg_close(zcsr_ffmpeg_backend* b);

#endif /* ZCSR_MUSIC_FFMPEG_H */
