#ifndef ZCSR_TEXTURE_H
#define ZCSR_TEXTURE_H
/* PNG 解码后的 CPU 像素描述。
 *
 * zcsr_texture_cache_init / zcsr_texture_load 已移除：它们由 modules/render 实现，
 * 该模块已整体删除；这两个 API 没有跨平台实现，调用方只有那份孤儿测试。
 * 需要真纹理请走 glrender（zcsr_gl_texture）。 */
#include <stddef.h>
#include <stdint.h>

/* Legacy CPU-side image descriptor. No runtime renderer consumes it anymore:
 * the GPU path uses the opaque zcsr_gl_texture handle, and image is offline
 * baking only. Keep this type for source compatibility with old tools; do not
 * build new runtime code around it. */
typedef struct {
    int            width;
    int            height;
    const uint8_t* rgba; /* 8-bit RGBA, cache-owned (do not free) */
} zcsr_texture;

/* Texture encodings are explicit because one RGBA8 byte pattern can mean
 * display-encoded color or linear data. The GPU upload entry point uses this
 * to choose the internal format; it is not a hint that shaders may ignore.
 *
 * DISPLAY  -> GL_SRGB8_ALPHA8; bytes are display-encoded and the sampler
 *             decodes them to linear (path A's default working space).
 * SRGB     -> GL_SRGB8_ALPHA8; identical storage/sampling to DISPLAY, kept
 *             as a semantic label for path B authoring.
 * LINEAR   -> GL_RGBA8; bytes are linear data, sampled as-is.
 *
 * Path A (screen-space sprites/UI) accepts DISPLAY only. Its shader decodes
 * the tint/flat color to linear and writes the unified RGBA16F frame; present
 * applies the final color-pipeline LUT. Feeding it SRGB or LINEAR would
 * double-decode or skip a decode. Path B's G-buffer is RGBA16F linear and
 * accepts any of the three; its sampler/geometry boundary handles the decode. */
typedef enum {
    ZCSR_COLOR_SPACE_DISPLAY = 0,
    ZCSR_COLOR_SPACE_SRGB,
    ZCSR_COLOR_SPACE_LINEAR
} zcsr_texture_color_space;

#endif /* ZCSR_TEXTURE_H */
