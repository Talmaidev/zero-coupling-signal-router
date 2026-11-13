#ifndef ZCSR_FONT_H
#define ZCSR_FONT_H
/* TrueType font rasterizer. Loads TTF/OTF from memory, rasterizes glyphs to A8 bitmaps.
 * No heap: caller provides buffer. Implemented in modules/font.
 * Uses stb_truetype (vendored in third_party/), not exposed in this header. */
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

typedef struct zcsr_font zcsr_font;

typedef struct {
    int width;
    int height;
    int bearing_x;
    int bearing_y;
    float advance_x;
    const uint8_t* a8;
} zcsr_font_glyph_info;

typedef struct {
    float ascent;
    float descent;
    float line_gap;
    float line_height;
} zcsr_font_metrics_info;

/* Buffer size required for zcsr_font_load. Includes alignment slack. */
size_t zcsr_font_buffer_bytes_required(size_t ttf_len);

/* Conservative upper bound for glyph rasterization buffer. */
size_t zcsr_font_glyph_buffer_bytes(const zcsr_font* font);

/* Load a TTF/OTF font from memory at the given pixel height.
 * Returns NULL on invalid font or insufficient buffer. */
zcsr_font* zcsr_font_load(const void* ttf, size_t ttf_len,
                          int pixel_height,
                          void* buffer, size_t bytes);

void zcsr_font_destroy(zcsr_font*);

bool zcsr_font_get_metrics(const zcsr_font*, zcsr_font_metrics_info* out);

/* Rasterize a single codepoint to an A8 bitmap.
 * glyph_buffer must be at least zcsr_font_glyph_buffer_bytes(font) bytes.
 * Returns false if codepoint not found or buffer too small.
 * Not thread-safe for concurrent rasterization on the same font. */
bool zcsr_font_rasterize(const zcsr_font*, uint32_t codepoint,
                         void* glyph_buffer, size_t glyph_bytes,
                         zcsr_font_glyph_info* out);

/* Get kerning advance between two codepoints.
 * Returns false if no kerning pair exists. */
bool zcsr_font_get_kerning(const zcsr_font*, uint32_t left, uint32_t right,
                           float* out_advance);

#endif /* ZCSR_FONT_H */
