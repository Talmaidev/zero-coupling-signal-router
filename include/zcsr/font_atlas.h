#ifndef ZCSR_FONT_ATLAS_H
#define ZCSR_FONT_ATLAS_H
/* Font atlas: caches rasterized glyphs in a GL texture atlas and queues text draws.
 * Depends on zcsr/font.h and zcsr/gl_render.h. Implemented in modules/font.
 * First version: 512 glyph slots, 1024x1024 atlas, no LRU rebuild (new codepoints
 * beyond slot capacity are skipped). UTF-8 invalid sequences are skipped. */
#include "font.h"
#include "gl_render.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

typedef struct zcsr_font_atlas zcsr_font_atlas;

/* Buffer size required for zcsr_font_atlas_create. Includes alignment slack. */
size_t zcsr_font_atlas_buffer_bytes_required(void);

/* Create a font atlas backed by the given renderer and font.
 * The font must outlive the atlas. */
zcsr_font_atlas* zcsr_font_atlas_create(zcsr_gl_renderer* gl, zcsr_font* font,
                                         void* buffer, size_t bytes);

void zcsr_font_atlas_destroy(zcsr_font_atlas*);

/* Call at the start of each frame before any draw_text. Resets the per-frame
 * codepoint set used for atlas rebuild-on-overflow. */
void zcsr_font_atlas_begin_frame(zcsr_font_atlas*);

/* Enqueue a UTF-8 text string for drawing. Origin is the baseline position.
 * Enqueue-only: actual draw happens at zcsr_gl_flush.
 * New glyphs are rasterized and uploaded to the atlas on demand. */
void zcsr_font_draw_text(zcsr_font_atlas*, const char* utf8,
                          float x, float y, float scale,
                          float r, float g, float b, float a);

#endif /* ZCSR_FONT_ATLAS_H */
