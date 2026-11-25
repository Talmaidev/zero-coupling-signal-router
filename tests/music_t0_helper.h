#ifndef MUSIC_T0_HELPER_H
#define MUSIC_T0_HELPER_H

/* Shared helpers for the music T0 contract tests. The public contract path
 * goes through the real zcsr_music API; the FFmpeg-backed formats are also
 * probed through the exported internal backend to assert decoded PCM is not
 * silence. */

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <zcsr/music.h>

/* Read an entire file into a caller-owned buffer. Sets *out_len; returns NULL
 * on failure (caller frees the returned buffer). */
unsigned char* mt_read_file(const char* path, size_t* out_len);

/* Public contract: detect -> load -> spec sanity -> threadless drain -> seek.
 * Returns 0 on pass, otherwise the number of failed checks. */
int mt_public_contract(const char* data_dir, const char* fixture,
                      zcsr_music_format fmt, uint32_t exp_rate, uint16_t exp_ch);

/* Decode through the FFmpeg backend (AAC/M4A/Opus) and return the count of
 * non-zero float samples; 0 means silence or decode failure. */
long mt_ffmpeg_nonzero(const unsigned char* data, size_t len);

/* Hand-written WAV PCM inspection: return the count of non-zero int16 samples
 * directly parsed from the RIFF payload. 0 means silence / unreadable. */
long mt_wav_nonzero(const unsigned char* data, size_t len);

#endif /* MUSIC_T0_HELPER_H */
