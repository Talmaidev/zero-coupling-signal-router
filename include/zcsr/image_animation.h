#ifndef ZCSR_IMAGE_ANIMATION_H
#define ZCSR_IMAGE_ANIMATION_H
/* Multi-frame image contract (GIF89a animation, animated WebP; static images
 * open with frame_count == 1 so callers can use one playback path).
 *
 * Memory contract: output pixels are caller-owned. Backend scratch (decoder
 * state, canvas history) lives behind the opaque handle; there is no retained
 * per-frame heap that grows with frame count, and no allocation happens per
 * frame on the output path. zcsr_image_anim_frame() writes into the caller's
 * zcsr_image.rgba; it never returns internally allocated pixels.
 */
#include "zcsr/image.h"

#include <stdbool.h>
#include <stddef.h>

/* Opaque animated-image handle. Backed by the caller-provided scratch buffer
 * passed to zcsr_image_anim_open(); do not free the handle itself. */
typedef struct zcsr_image_anim zcsr_image_anim;

/* Playback metadata. */
typedef struct {
    int w, h;            /* canvas size (logical screen) */
    int frame_count;     /* >= 1 */
    int loop_count;      /* 0 = infinite, 1 = play once, n = repeat n times */
} zcsr_image_anim_info;

/* Parse header-only metadata without opening a playback handle. Returns false
 * for an unsupported or unrecognised container. Pure: no decode, no allocation. */
bool zcsr_image_anim_info_of(const void* encoded, size_t len, zcsr_image_anim_info* out);

/* Real caller-buffer requirement for open(): opaque handle + per-frame delay
 * table + cached canvas frames + backend decode scratch. Caller passes a
 * buffer at least this large to zcsr_image_anim_open(). Returns 0 on an
 * unsupported container or on size overflow. */
size_t zcsr_image_anim_buffer_bytes(const void* encoded, size_t len);

/* Open an animation over the encoded bytes. `buffer`/`bytes` is the caller
 * scratch+output arena for the lifetime of the handle. Returns NULL on an
 * unsupported container, truncated/corrupt bytes, or an undersized buffer.
 * The encoded bytes must remain unchanged for the lifetime of the handle. */
zcsr_image_anim* zcsr_image_anim_open(const void* encoded, size_t len,
                                      void* buffer, size_t bytes);

/* Replay from frame 0 to `index` and write the composited canvas into
 * `out->rgba` (caller-owned). Canvas compositing (GIF disposal methods 0/1/2
 * and the transparent index; WebP disposal/blend handled by libwebp) is
 * already applied. Returns false on an out-of-range index or invalid handle. */
bool zcsr_image_anim_frame(zcsr_image_anim* anim, int index, zcsr_image* out);

/* Display duration of frame `index` in milliseconds. Returns 0 for an
 * out-of-range index. */
int zcsr_image_anim_delay_ms(const zcsr_image_anim* anim, int index);

/* Release the handle. Frees any backend scratch held behind the handle; the
 * caller buffer itself is owned by the caller. */
void zcsr_image_anim_close(zcsr_image_anim* anim);

#endif /* ZCSR_IMAGE_ANIMATION_H */
