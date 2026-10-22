/* zcsr font_atlas module — caches rasterized glyphs in a GL texture atlas.
 * 512 glyph slots, 1024x1024 RGBA atlas, shelf allocator, UTF-8 text drawing. */
#include "zcsr/font_atlas.h"
#include <string.h>
#include <stddef.h>
#include <stdint.h>

#define FG_CACHE_SLOTS 512
#define FG_ATLAS_W 1024
#define FG_ATLAS_H 1024
#define FG_GLYPH_SCRATCH_BYTES 65536
#define FG_SLACK 15
#define FG_FRAME_CODEPOINTS 256

typedef struct {
    uint32_t codepoint;
    int atlas_x, atlas_y;
    int width, height;
    int bearing_x, bearing_y;
    float advance_x;
    bool valid;
} fg_slot;

struct zcsr_font_atlas {
    zcsr_gl_renderer* gl;
    zcsr_font* font;
    zcsr_gl_texture atlas_tex;
    uint8_t* atlas_cpu;
    fg_slot slots[FG_CACHE_SLOTS];
    int shelf_x, shelf_y, shelf_h;
    uint8_t* glyph_scratch;
    size_t glyph_scratch_bytes;
    bool atlas_full;
    uint32_t frame_codepoints[FG_FRAME_CODEPOINTS];
    int frame_codepoint_count;
};

static uint8_t* fg_align_up(uint8_t* p, size_t a) {
    uintptr_t v = (uintptr_t)p;
    return (uint8_t*)((v + a - 1u) & ~(uintptr_t)(a - 1u));
}

static size_t fg_align_up_sz(size_t s, size_t a) {
    return (s + a - 1u) & ~(a - 1u);
}

size_t zcsr_font_atlas_buffer_bytes_required(void) {
    size_t s = sizeof(struct zcsr_font_atlas);
    s = fg_align_up_sz(s, 8) + (size_t)FG_ATLAS_W * FG_ATLAS_H * 4u;
    s = fg_align_up_sz(s, 8) + FG_GLYPH_SCRATCH_BYTES;
    s += 512u + FG_SLACK;
    return s;
}

zcsr_font_atlas* zcsr_font_atlas_create(zcsr_gl_renderer* gl, zcsr_font* font,
                                         void* buffer, size_t bytes) {
    struct zcsr_font_atlas* a;
    uint8_t* p;
    uint8_t* atlas_cpu;
    uint8_t* glyph_scratch;
    size_t needed;
    if (!gl || !font || !buffer) return NULL;
    needed = zcsr_font_atlas_buffer_bytes_required();
    if (bytes < needed) return NULL;
    p = fg_align_up((uint8_t*)buffer, 16);
    if ((size_t)(p - (uint8_t*)buffer) + needed > bytes) return NULL;
    a = (struct zcsr_font_atlas*)p;
    p += fg_align_up_sz(sizeof(*a), 8);
    atlas_cpu = p;
    p += (size_t)FG_ATLAS_W * FG_ATLAS_H * 4u;
    p = fg_align_up(p, 8);
    glyph_scratch = p;
    p += FG_GLYPH_SCRATCH_BYTES;
    if ((size_t)(p - (uint8_t*)buffer) > bytes) return NULL;
    memset(a, 0, sizeof(*a));
    a->gl = gl;
    a->font = font;
    a->atlas_cpu = atlas_cpu;
    a->glyph_scratch = glyph_scratch;
    a->glyph_scratch_bytes = FG_GLYPH_SCRATCH_BYTES;
    memset(atlas_cpu, 0, (size_t)FG_ATLAS_W * FG_ATLAS_H * 4u);
    a->atlas_tex = zcsr_gl_texture_create(gl, atlas_cpu, FG_ATLAS_W, FG_ATLAS_H);
    if (!a->atlas_tex) {
        memset(a, 0, sizeof(*a));
        return NULL;
    }
    return a;
}

void zcsr_font_atlas_destroy(zcsr_font_atlas* a) {
    if (!a) return;
    if (a->atlas_tex) zcsr_gl_texture_destroy(a->gl, a->atlas_tex);
    memset(a, 0, sizeof(*a));
}

static fg_slot* fg_find_slot(zcsr_font_atlas* a, uint32_t cp) {
    int i;
    for (i = 0; i < FG_CACHE_SLOTS; ++i)
        if (a->slots[i].valid && a->slots[i].codepoint == cp) return &a->slots[i];
    return NULL;
}

static fg_slot* fg_find_free_slot(zcsr_font_atlas* a) {
    int i;
    for (i = 0; i < FG_CACHE_SLOTS; ++i)
        if (!a->slots[i].valid) return &a->slots[i];
    return NULL;
}

static bool fg_alloc_atlas_rect(zcsr_font_atlas* a, int w, int h, int* ox, int* oy) {
    if (w <= 0 || h <= 0) return false;
    if (w > FG_ATLAS_W || h > FG_ATLAS_H) return false;
    if (a->shelf_x + w > FG_ATLAS_W) {
        a->shelf_y += a->shelf_h;
        a->shelf_x = 0;
        a->shelf_h = 0;
    }
    if (a->shelf_y + h > FG_ATLAS_H) {
        a->atlas_full = true;
        return false;
    }
    *ox = a->shelf_x;
    *oy = a->shelf_y;
    a->shelf_x += w + 1;  /* 1px padding */
    if (h > a->shelf_h) a->shelf_h = h;
    return true;
}

static fg_slot* fg_rasterize_to_atlas_internal(zcsr_font_atlas* a, uint32_t cp) {
    zcsr_font_glyph_info info;
    fg_slot* s;
    int ax, ay, row, col;
    if (a->atlas_full) return NULL;
    s = fg_find_free_slot(a);
    if (!s) {
        a->atlas_full = true;
        return NULL;
    }
    if (!zcsr_font_rasterize(a->font, cp, a->glyph_scratch,
                             a->glyph_scratch_bytes, &info))
        return NULL;
    if (info.width == 0 || info.height == 0) {
        s->codepoint = cp;
        s->valid = true;
        s->width = 0;
        s->height = 0;
        s->advance_x = info.advance_x;
        s->bearing_x = info.bearing_x;
        s->bearing_y = info.bearing_y;
        return s;
    }
    if (!fg_alloc_atlas_rect(a, info.width, info.height, &ax, &ay)) return NULL;
    for (row = 0; row < info.height; ++row) {
        for (col = 0; col < info.width; ++col) {
            uint8_t alpha = info.a8[(size_t)row * info.width + col];
            uint8_t* dst = a->atlas_cpu
                         + (((size_t)(ay + row) * FG_ATLAS_W) + (ax + col)) * 4u;
            dst[0] = 255;
            dst[1] = 255;
            dst[2] = 255;
            dst[3] = alpha;
        }
    }
    if (!zcsr_gl_texture_upload_region(a->gl, a->atlas_tex,
                                        ax, ay, info.width, info.height,
                                        FG_ATLAS_W * 4,
                                        a->atlas_cpu
                                        + (((size_t)ay * FG_ATLAS_W) + ax) * 4u))
        return NULL;
    s->codepoint = cp;
    s->valid = true;
    s->atlas_x = ax;
    s->atlas_y = ay;
    s->width = info.width;
    s->height = info.height;
    s->bearing_x = info.bearing_x;
    s->bearing_y = info.bearing_y;
    s->advance_x = info.advance_x;
    return s;
}

/* Rebuild atlas from the per-frame codepoint set. Called when the atlas fills
 * up: flush queued draws, clear atlas, re-rasterize all codepoints used this
 * frame, re-upload texture. After rebuild, callers re-find their slot. */
static void fg_rebuild_atlas(zcsr_font_atlas* a) {
    int i;
    zcsr_gl_flush(a->gl);
    memset(a->atlas_cpu, 0, (size_t)FG_ATLAS_W * FG_ATLAS_H * 4u);
    a->shelf_x = 0;
    a->shelf_y = 0;
    a->shelf_h = 0;
    a->atlas_full = false;
    for (i = 0; i < FG_CACHE_SLOTS; ++i) a->slots[i].valid = false;
    for (i = 0; i < a->frame_codepoint_count; ++i) {
        fg_rasterize_to_atlas_internal(a, a->frame_codepoints[i]);
    }
    zcsr_gl_texture_upload_region(a->gl, a->atlas_tex,
                                  0, 0, FG_ATLAS_W, FG_ATLAS_H,
                                  FG_ATLAS_W * 4, a->atlas_cpu);
}

static fg_slot* fg_rasterize_to_atlas(zcsr_font_atlas* a, uint32_t cp) {
    fg_slot* s = fg_rasterize_to_atlas_internal(a, cp);
    if (!s) {
        fg_rebuild_atlas(a);
        s = fg_find_slot(a, cp);
    }
    return s;
}

/* Add a codepoint to the per-frame set (deduplicated). Used by atlas rebuild
 * to re-rasterize all glyphs used in the current frame. */
static void fg_add_frame_codepoint(zcsr_font_atlas* a, uint32_t cp) {
    int i;
    if (cp == 0 || cp == '\n') return;
    for (i = 0; i < a->frame_codepoint_count; ++i) {
        if (a->frame_codepoints[i] == cp) return;
    }
    if (a->frame_codepoint_count < FG_FRAME_CODEPOINTS) {
        a->frame_codepoints[a->frame_codepoint_count++] = cp;
    }
}

void zcsr_font_atlas_begin_frame(zcsr_font_atlas* a) {
    if (!a) return;
    a->frame_codepoint_count = 0;
}

/* UTF-8 decode: all failure branches advance *p to avoid infinite loops. */
static bool fg_utf8_next(const uint8_t** pp, uint32_t* out) {
    const uint8_t* s = *pp;
    uint32_t cp;
    int n, i;
    if (s[0] == 0) return false;
    if (s[0] < 0x80) {
        *out = s[0];
        *pp = s + 1;
        return true;
    }
    if ((s[0] & 0xE0) == 0xC0) {
        cp = s[0] & 0x1F;
        n = 1;
        if (cp < 0x02) { *pp = s + 1; return false; }  /* overlong */
    } else if ((s[0] & 0xF0) == 0xE0) {
        cp = s[0] & 0x0F;
        n = 2;
    } else if ((s[0] & 0xF8) == 0xF0) {
        cp = s[0] & 0x07;
        n = 3;
        if (cp > 0x04) { *pp = s + 1; return false; }  /* out of range */
    } else {
        *pp = s + 1;
        return false;  /* invalid lead byte */
    }
    for (i = 1; i <= n; ++i) {
        if ((s[i] & 0xC0) != 0x80) { *pp = s + i; return false; }
        cp = (cp << 6) | (s[i] & 0x3F);
    }
    if (cp >= 0xD800 && cp <= 0xDFFF) { *pp = s + n + 1; return false; }  /* surrogate */
    if (cp > 0x10FFFF) { *pp = s + n + 1; return false; }
    *out = cp;
    *pp = s + n + 1;
    return true;
}

void zcsr_font_draw_text(zcsr_font_atlas* a, const char* utf8,
                          float x, float y, float scale,
                          float r, float g, float b, float a_) {
    float pen = x;
    const uint8_t* p = (const uint8_t*)utf8;
    uint32_t prev_cp = 0;
    zcsr_font_metrics_info m;
    if (!a || !utf8) return;
    zcsr_font_get_metrics(a->font, &m);
    while (*p) {
        uint32_t cp;
        fg_slot* s;
        if (!fg_utf8_next(&p, &cp)) {
            prev_cp = 0;
            continue;
        }
        if (cp == '\n') {
            pen = x;
            y += m.line_height * scale;
            prev_cp = 0;
            continue;
        }
        if (prev_cp) {
            float k;
            if (zcsr_font_get_kerning(a->font, prev_cp, cp, &k))
                pen += k * scale;
        }
        fg_add_frame_codepoint(a, cp);
        s = fg_find_slot(a, cp);
        if (!s) s = fg_rasterize_to_atlas(a, cp);
        if (s && s->width > 0) {
            float gx = pen + s->bearing_x * scale;
            float gy = y - s->bearing_y * scale;
            float gw = s->width * scale;
            float gh = s->height * scale;
            float u0 = (float)s->atlas_x / FG_ATLAS_W;
            float v0 = (float)s->atlas_y / FG_ATLAS_H;
            float u1 = (float)(s->atlas_x + s->width) / FG_ATLAS_W;
            float v1 = (float)(s->atlas_y + s->height) / FG_ATLAS_H;
            zcsr_gl_queue_atlas_sprite(a->gl, a->atlas_tex,
                                        gx, gy, gw, gh,
                                        u0, v0, u1, v1, r, g, b, a_);
        }
        if (s) pen += s->advance_x * scale;
        prev_cp = cp;
    }
}
