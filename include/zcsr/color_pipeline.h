#ifndef ZCSR_COLOR_PIPELINE_H
#define ZCSR_COLOR_PIPELINE_H

/* zcsr/color_pipeline.h — unified frame display-transfer contract.
 *
 * Path A (sprites/UI/instances) and Path B (Light2D) both render into the
 * renderer's RGBA16F linear frame. This 1D LUT is applied exactly once when
 * the frame is presented or read back; no path encodes display pixels itself.
 *
 * Texture decode is a closed input contract: DISPLAY textures are stored as
 * GL_SRGB8_ALPHA8 and hardware-decoded; SRGB uses the sRGB sampler; LINEAR is
 * sampled verbatim; DISPLAY tints/flat colors are decoded by the shader or
 * CPU default pipeline. The transfer curve (today sRGB) is generated into a
 * LUT by this module; the shader samples that LUT. A future algorithm can
 * replace the descriptor without editing shader source. This is a per-channel
 * transfer, not a 3D LUT or a per-object 3x3 matrix.
 *
 * Renderer boundary: zcsr_gl_l2d_set_color_pipeline accepts only descriptors
 * whose `to_linear` matches the default sRGB decoder; a custom `to_display` is
 * allowed. Pure-CPU callers may still use arbitrary inverse pairs for their
 * own data.
 *
 * This module is pure CPU logic. It owns no GPU object, no window, no UIM
 * page. The renderer consumes it; the editor/runtime may also call it.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "zcsr/render_tier.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Number of entries in the default 1D display-encode LUT. 1024 keeps the
 * dark end of the sRGB curve stable while remaining a 4 KiB RGBA8 texture. */
#define ZCSR_COLOR_LUT_ENTRIES 1024

typedef struct zcsr_color_pipeline_desc {
    /* Stable key for project/diagnostic identity; never a localized label. */
    const char* stable_key;
    /* linear-light [0,1] -> display-encoded [0,1]. */
    float (*to_display)(float linear_value);
    /* display-encoded [0,1] -> linear-light [0,1]. */
    float (*to_linear)(float display_value);
} zcsr_color_pipeline_desc;

/* Tier capability descriptor (裁决 8). This is an assembly-time / compile-time
 * description: which working domain and which shader-side stages a tier
 * compiles in. It is NOT a per-frame bool switch (裁决 5) — nothing on the hot
 * path reads these fields per draw.
 *
 *   LINEAR_HDR   : RGBA16F linear working frame, one LUT encode pass at
 *                  present/readback; recolor, ramp, OKLab and chroma stages
 *                  are compiled in.
 *   DIRECT_SRGB  : write the default framebuffer directly; no encode pass,
 *                  no linear frame, and the okLab/recolor/i_ramp/chroma stages
 *                  are compiled out.
 */
typedef struct zcsr_color_tier_desc {
    zcsr_color_tier tier;
    /* Stable key for project/diagnostic identity; never a localized label. */
    const char* stable_key;
    /* Transfer pair for decode/encode; NULL for DIRECT_SRGB (direct write). */
    const zcsr_color_pipeline_desc* transfer;
    bool linear_frame; /* RGBA16F linear frame + single LUT encode pass */
    bool recolor;      /* selective hue-family remap stage (i_recolor) */
    bool ramp;         /* 1D gradient atlas stage (i_ramp) */
    bool oklab;        /* OKLab chroma preserve-L ramp mode */
    bool chroma;       /* saturation/value conditioning stage */
} zcsr_color_tier_desc;

/* Descriptor for a color tier. Never NULL; unknown tiers resolve to the
 * LINEAR_HDR default. The descriptor is static storage and outlives the caller. */
const zcsr_color_tier_desc* zcsr_color_pipeline_tier(zcsr_color_tier tier);

/* Convenience: true when the tier compiles an encode/linear-frame pass. */
bool zcsr_color_pipeline_tier_encodes(const zcsr_color_tier_desc* desc);

/* Default sRGB transfer pair used by the unified frame encode. Both Path A
 * and Path B render linear and pass through this pipeline once. */
const zcsr_color_pipeline_desc* zcsr_color_pipeline_default(void);

/* Build a horizontal RGBA8 encode LUT. `rgba` must hold entries*4 bytes.
 * RGB receive the encoded value, A is 255. Returns false on invalid input. */
bool zcsr_color_pipeline_build_encode_lut(const zcsr_color_pipeline_desc* pipeline,
                                          uint8_t* rgba, int entries);

/* Convenience CPU transfer helpers. */
float zcsr_color_pipeline_to_display(const zcsr_color_pipeline_desc* pipeline,
                                     float linear_value);
float zcsr_color_pipeline_to_linear(const zcsr_color_pipeline_desc* pipeline,
                                    float display_value);

#ifdef __cplusplus
}
#endif

#endif /* ZCSR_COLOR_PIPELINE_H */
