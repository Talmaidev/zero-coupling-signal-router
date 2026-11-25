/* zcsr font module — TrueType/OpenType (TTF glyf / OTF CFF) rasterizer via stb_truetype.
 * Glyph bitmaps are caller-owned. The opaque handle holds a fixed bump allocator for
 * stb_truetype temporaries; no retained per-glyph heap grows with glyph count. */
#include "zcsr/font.h"
#include "../../third_party/stb_truetype.h"  /* declarations only; implementation in stb_truetype_impl.c */
#include <string.h>
#include <stddef.h>
#include <limits.h>
#include <stdint.h>

/* ---- internal bump allocator for stb_truetype temporaries ---- */
typedef struct {
    uint8_t* base;
    size_t capacity;
    size_t used;
    bool failed;
} zcsr_font_scratch;

struct zcsr_font {
    stbtt_fontinfo info;
    int pixel_height;
    float scale;
    zcsr_font_metrics_info metrics;
    zcsr_font_scratch scratch;
    const uint8_t* ttf_copy;
    size_t ttf_len;
};

void* zcsr_stbtt_malloc(size_t size, void* userdata) {
    zcsr_font_scratch* s = (zcsr_font_scratch*)userdata;
    size_t aligned;
    void* p;
    if (!s || s->failed) return NULL;
    aligned = (size + 7u) & ~(size_t)7u;
    if (aligned > s->capacity - s->used) { s->failed = true; return NULL; }
    p = s->base + s->used;
    s->used += aligned;
    return p;
}

void zcsr_stbtt_free(void* ptr, void* userdata) {
    (void)ptr;
    (void)userdata;
}

/* ---- alignment helpers ---- */
static uint8_t* align_up_ptr(uint8_t* p, size_t align) {
    uintptr_t v = (uintptr_t)p;
    return (uint8_t*)((v + align - 1u) & ~(uintptr_t)(align - 1u));
}

static size_t align_up_sz(size_t s, size_t align) {
    return (s + align - 1u) & ~(align - 1u);
}

static uint32_t be32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] <<  8) |  (uint32_t)p[3];
}

/* Bounds-check the sfnt header and table directory before handing the bytes to
 * stb_truetype. stb's table lookup trusts the directory without validating
 * offsets against the input length; a truncated or corrupted header would
 * otherwise read out of bounds. Only plain TTF (0x00010000) and CFF OTF ('OTTO')
 * are accepted; TTC collections and WOFF/WOFF2 magics are rejected here, before
 * any decode path runs. The magic is also cross-checked against the outline
 * tables: an 'OTTO' file must carry a 'CFF ' table, a TrueType file must carry
 * both 'glyf' and 'loca' (a magic-only rename does not pass). */
static bool zcsr_font_sanity_ok(const uint8_t* data, size_t len) {
    uint32_t magic;
    uint16_t num_tables;
    size_t i;
    bool has_cff = false, has_glyf = false, has_loca = false;
    if (len < 12u) return false;
    magic = be32(data);
    if (magic != 0x00010000u && magic != 0x4F54544Fu) return false;  /* 'OTTO' */
    num_tables = (uint16_t)(((uint16_t)data[4] << 8) | (uint16_t)data[5]);
    if (num_tables == 0) return false;
    if ((size_t)num_tables > (len - 12u) / 16u) return false;
    for (i = 0; i < (size_t)num_tables; ++i) {
        const uint8_t* rec = data + 12u + 16u * i;
        uint32_t off = be32(rec + 8);
        uint32_t slen = be32(rec + 12);
        if (off > len || slen > len - off) return false;
        if (memcmp(rec, "CFF ", 4) == 0) has_cff = true;
        else if (memcmp(rec, "glyf", 4) == 0) has_glyf = true;
        else if (memcmp(rec, "loca", 4) == 0) has_loca = true;
    }
    if (magic == 0x4F54544Fu) {
        if (!has_cff) return false;
    } else {
        if (!has_glyf || !has_loca) return false;
    }
    return true;
}

/* ---- public API ---- */

size_t zcsr_font_buffer_bytes_required(size_t ttf_len) {
    size_t s;
    const size_t SLACK = 15;
    if (ttf_len > SIZE_MAX - 64u * 1024u - 512u - sizeof(struct zcsr_font) - SLACK)
        return 0;
    s = sizeof(struct zcsr_font);
    s = align_up_sz(s, 8) + ttf_len;
    s = align_up_sz(s, 8) + 64u * 1024u;  /* stb_truetype scratch */
    s += 512u + SLACK;
    return s;
}

size_t zcsr_font_glyph_buffer_bytes(const zcsr_font* font) {
    size_t side;
    if (!font) return 0;
    side = (size_t)font->pixel_height * 3u + 16u;
    return side * side;
}

zcsr_font* zcsr_font_load(const void* ttf, size_t ttf_len,
                          int pixel_height, void* buffer, size_t bytes) {
    struct zcsr_font* f;
    uint8_t* p;
    uint8_t* ttf_dst;
    uint8_t* scratch_base;
    size_t needed;
    if (!ttf || !ttf_len || !buffer || pixel_height <= 0) return NULL;
    if (ttf_len > (size_t)INT_MAX) return NULL;
    needed = zcsr_font_buffer_bytes_required(ttf_len);
    if (needed == 0 || bytes < needed) return NULL;
    p = align_up_ptr((uint8_t*)buffer, 16);
    if ((size_t)(p - (uint8_t*)buffer) + needed > bytes) return NULL;
    f = (struct zcsr_font*)p;
    p += align_up_sz(sizeof(*f), 8);
    ttf_dst = p;
    p += ttf_len;
    p = align_up_ptr(p, 8);
    scratch_base = p;
    p += 64u * 1024u;
    if ((size_t)(p - (uint8_t*)buffer) > bytes) return NULL;
    memset(f, 0, sizeof(*f));
    memcpy(ttf_dst, ttf, ttf_len);
    f->ttf_copy = ttf_dst;
    f->ttf_len = ttf_len;
    f->pixel_height = pixel_height;
    f->scratch.base = scratch_base;
    f->scratch.capacity = 64u * 1024u;
    f->scratch.used = 0;
    f->scratch.failed = false;
    if (!zcsr_font_sanity_ok(f->ttf_copy, f->ttf_len)) {
        memset(f, 0, sizeof(*f));
        return NULL;
    }
    if (!stbtt_InitFont(&f->info, f->ttf_copy, 0)) {
        memset(f, 0, sizeof(*f));
        return NULL;
    }
    f->info.userdata = &f->scratch;
    f->scale = stbtt_ScaleForPixelHeight(&f->info, (float)pixel_height);
    {
        int a, d, g;
        stbtt_GetFontVMetrics(&f->info, &a, &d, &g);
        f->metrics.ascent = (float)a * f->scale;
        f->metrics.descent = (float)(-d) * f->scale;
        f->metrics.line_gap = (float)g * f->scale;
        f->metrics.line_height = f->metrics.ascent + f->metrics.descent + f->metrics.line_gap;
    }
    return f;
}

void zcsr_font_destroy(zcsr_font* f) {
    if (f) memset(f, 0, sizeof(*f));
}

bool zcsr_font_get_metrics(const zcsr_font* f, zcsr_font_metrics_info* out) {
    if (!f || !out) return false;
    *out = f->metrics;
    return true;
}

bool zcsr_font_rasterize(const zcsr_font* font, uint32_t codepoint,
                          void* glyph_buffer, size_t glyph_bytes,
                          zcsr_font_glyph_info* out) {
    int advance, lsb, x0, y0, x1, y1, w, h;
    zcsr_font_scratch* s;
    if (!font || !glyph_buffer || !out) return false;
    memset(out, 0, sizeof(*out));
    if (stbtt_FindGlyphIndex(&font->info, (int)codepoint) == 0) return false;
    stbtt_GetCodepointHMetrics(&font->info, (int)codepoint, &advance, &lsb);
    stbtt_GetCodepointBitmapBox(&font->info, (int)codepoint,
                                 font->scale, font->scale, &x0, &y0, &x1, &y1);
    w = x1 - x0;
    h = y1 - y0;
    out->advance_x = (float)advance * font->scale;
    if (w <= 0 || h <= 0) {
        out->bearing_x = x0;
        out->bearing_y = -y0;
        out->a8 = NULL;
        return true;
    }
    if ((size_t)w > SIZE_MAX / (size_t)h) return false;
    if (glyph_bytes < (size_t)w * (size_t)h) return false;
    s = (zcsr_font_scratch*)font->info.userdata;
    s->used = 0;
    s->failed = false;
    stbtt_MakeCodepointBitmap(&font->info, glyph_buffer,
                                w, h, w, font->scale, font->scale,
                                (int)codepoint);
    if (s->failed) return false;
    out->width = w;
    out->height = h;
    out->bearing_x = x0;
    out->bearing_y = -y0;
    out->a8 = (const uint8_t*)glyph_buffer;
    return true;
}

bool zcsr_font_get_kerning(const zcsr_font* font, uint32_t left, uint32_t right,
                            float* out_advance) {
    int kern;
    if (!font || !out_advance) return false;
    kern = stbtt_GetCodepointKernAdvance(&font->info, (int)left, (int)right);
    if (kern == 0) return false;
    *out_advance = (float)kern * font->scale;
    return true;
}
