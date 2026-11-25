#ifndef ZCSR_IMAGE_H
#define ZCSR_IMAGE_H
/* image loading, decoding, and offline CPU processing to 8-bit RGBA (RGBA32).
 *
 * SEPARATED from rendering: this module ONLY produces pixel buffers. It is
 * offline baking, NOT a runtime renderer and NOT a fallback when GL is
 * unavailable. The GPU path consumes produced bytes via zcsr_gl_texture_create
 * / _create_ex. `image` never includes a renderer; renderers never include
 * `image`. Decouples decode/processing from upload/draw.
 *
 * Memory contract: output pixels are caller-owned. Backends may hold scratch
 * memory behind the opaque animation handle; there is no retained per-frame
 * heap that grows with frame count, and no allocation happens per frame on the
 * output path. The caller sizes the scratch+output buffer to cover decode
 * scratch plus the packed RGBA output.
 */
#include "color.h"   /* zcsr_color_mode — shared with the glrender runtime path */
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/* Decoded / processed image: tightly packed 8-bit RGBA, top-left origin, row stride = w*4.
 * Pixels live inside the caller-provided buffer (do NOT free). w==0 || h==0 || !rgba => invalid. */
typedef struct {
    int            w, h;
    uint8_t*       rgba; /* w*h*4 bytes, caller-buffer-owned */
} zcsr_image;

/* ---- Format identification & capability reporting ---- */

/* Container/encoding identifiers. Values are part of the ABI: append new
 * entries at the end, never renumber existing ones. `UNKNOWN` means the bytes
 * carry no recognizable signature. */
typedef enum {
    ZCSR_IMAGE_FMT_UNKNOWN = 0,
    ZCSR_IMAGE_FMT_PNG,        /* Portable Network Graphics                  */
    ZCSR_IMAGE_FMT_JPEG,        /* JFIF / Exif JPEG                           */
    ZCSR_IMAGE_FMT_BMP,         /* Windows BMP (DIB file)                      */
    ZCSR_IMAGE_FMT_GIF,         /* GIF87a / GIF89a (static first frame)      */
    ZCSR_IMAGE_FMT_WEBP,        /* WebP (static and animated)                   */
    ZCSR_IMAGE_FMT_HDR,         /* Radiance RGBE `.hdr` (legacy, tonemapped)  */
    ZCSR_IMAGE_FMT_ICO,          /* Windows icon container (legacy)            */
    ZCSR_IMAGE_FMT_CUR,          /* Windows cursor container (legacy)           */
    ZCSR_IMAGE_FMT_COUNT
} zcsr_image_format;

/* Decode capability snapshot for the current build/backend. The snapshot is
 * static (no per-call probing): a false `decode` flag means
 * zcsr_image_decode fails closed for that format; it never fabricates pixels.
 * `animated` reports whether multi-frame sequences (GIF/WebP) decode as more
 * than their first frame through the animation contract. */
typedef struct {
    bool decode[ZCSR_IMAGE_FMT_COUNT]; /* indexed by zcsr_image_format */
    bool animated;                     /* true if GIF/WebP sequences decode multi-frame */
} zcsr_image_caps_info;

/* Sniff encoded bytes for a container signature. Returns
 * ZCSR_IMAGE_FMT_UNKNOWN when the signature is absent or ambiguous. Pure:
 * does not decode and does not touch the caller buffer. */
zcsr_image_format zcsr_image_detect_format(const void* encoded, size_t encoded_len);

/* Static capability snapshot; never NULL. */
const zcsr_image_caps_info* zcsr_image_caps(void);

/* Short stable lowercase name for a format enum (e.g. "png", "hdr", "ico").
 * Returns "unknown" for out-of-range values. */
const char* zcsr_image_format_name(zcsr_image_format format);

/* ---- Decode (multi-format) ---- */

/* Decode an encoded image from memory into RGBA32. Format is auto-detected; supported
 * encodings are PNG / JPEG / BMP / GIF / WebP, with legacy HDR / ICO / CUR retained.
 * `buffer`/`bytes` is caller scratch+output; the returned rgba points inside it.
 * Returns {0,0,NULL} on decode failure, on an unsupported encoding, or if the
 * buffer is too small to hold scratch + output. Animated sequences return their
 * first frame only; use the animation contract for multi-frame playback. */
zcsr_image zcsr_image_decode(const void* encoded, size_t encoded_len, void* buffer, size_t bytes);

/* ---- Offline CPU processing (bake once, then upload as an ordinary static texture) ---- */

/* Color-key transparency (chroma-key / "green screen"). Texels within `tolerance` (per-channel
 * max distance, 0..255) of (key_r,key_g,key_b) become fully transparent (alpha=0); a `softness`
 * band (0..255) feathers alpha just outside the keyed range so edges are not hard-cut. Operates
 * IN PLACE on `img`'s buffer. Returns false on NULL / invalid image. */
bool zcsr_image_chroma_key(zcsr_image* img,
                           uint8_t key_r, uint8_t key_g, uint8_t key_b,
                           uint8_t tolerance, uint8_t softness);

/* Variant texture: copy `src` into `dst_buffer` and modulate every texel by ONE RGBA color
 * (full RGBA, no per-channel control), producing a recolored variant (e.g. team color).
 *   mode: MULTIPLY = texel*color, MIX = lerp(texel,color,amount), ADD = texel + color*amount.
 *   (cr,cg,cb,ca) 0..255 = modulation color; amount 0..255 = blend strength for MIX/ADD.
 * Returns the new image in dst_buffer, or {0,0,NULL} on failure / buffer too small. */
zcsr_image zcsr_image_make_variant(const zcsr_image* src, void* dst_buffer, size_t dst_bytes,
                                   zcsr_color_mode mode,
                                   uint8_t cr, uint8_t cg, uint8_t cb, uint8_t ca,
                                   uint8_t amount);

/* Palette remap: map each source texel to an RGBA32 entry via a 256-entry LUT, keyed by the
 * texel's RED channel (index/palette images) — for dynamic recolors (hair, faction palettes).
 * Runs on the CPU once; cache the result as a static texture. Writes the remapped image into
 * `dst_buffer`. Returns {0,0,NULL} on failure / buffer too small. */
zcsr_image zcsr_image_palette_remap(const zcsr_image* src, const uint8_t lut[256][4],
                                    void* dst_buffer, size_t dst_bytes);

#endif /* ZCSR_IMAGE_H */
