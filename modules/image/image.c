/* Engine-ext - image module.
 * Renderer-agnostic decode and offline CPU image processing. Includes only shared contracts
 * plus the vendored stb_image implementation. Output pixels are caller-owned; backend scratch
 * stays behind the opaque handle, with no retained per-frame heap growth.
 */
#include "zcsr/image.h"
#include "image_internal.h"

#include <limits.h>
#include <stdint.h>
#include <string.h>

typedef struct {
    unsigned char* base;
    size_t capacity;
    size_t offset;
} zcsr_image_arena;

static zcsr_image_arena* zcsr_image_active_arena;

static size_t zcsr_image_align_up(size_t value, size_t align) {
    size_t mask = align - 1u;
    return (value + mask) & ~mask;
}

static bool zcsr_image_size(int w, int h, size_t* out) {
    size_t sw;
    size_t sh;
    if (!out || w <= 0 || h <= 0) return false;
    sw = (size_t)w;
    sh = (size_t)h;
    if (sw > SIZE_MAX / sh || sw * sh > SIZE_MAX / 4u) return false;
    *out = sw * sh * 4u;
    return true;
}

static void* zcsr_image_alloc(size_t bytes) {
    size_t offset;
    if (!zcsr_image_active_arena || bytes == 0) return 0;
    offset = zcsr_image_align_up(zcsr_image_active_arena->offset, sizeof(void*));
    if (offset > zcsr_image_active_arena->capacity || bytes > zcsr_image_active_arena->capacity - offset) return 0;
    zcsr_image_active_arena->offset = offset + bytes;
    return zcsr_image_active_arena->base + offset;
}

static void zcsr_image_free(void* ptr) {
    (void)ptr;
}

static void* zcsr_image_realloc_sized(void* ptr, size_t old_size, size_t new_size) {
    unsigned char* dst = (unsigned char*)zcsr_image_alloc(new_size);
    unsigned char* src = (unsigned char*)ptr;
    size_t n = old_size < new_size ? old_size : new_size;
    if (!dst) return 0;
    for (size_t i = 0; src && i < n; ++i) dst[i] = src[i];
    return dst;
}

/* T0 image line: only PNG / JPEG / BMP / GIF / HDR routes are compiled out of
 * stb_image. The removed legacy/spectator containers are switched off at the
 * stb compile boundary so their decoders cannot be reached from any dispatch
 * path. HDR decoding stays enabled (STBI_NO_HDR not defined) so
 * stbi_loadf_from_memory is available; the float Radiance RGBE path is
 * tonemapped to RGBA8 here. STBI_NO_LINEAR stays undefined as well: the float
 * interface used by the HDR route is guarded by it in stb_image.h. */
#define STBI_NO_STDIO
#define STBI_NO_TGA
#define STBI_NO_PSD
#define STBI_NO_PIC
#define STBI_NO_PNM
#define STB_IMAGE_STATIC
#define STBI_MALLOC(sz) zcsr_image_alloc(sz)
#define STBI_REALLOC(p, sz) zcsr_image_realloc_sized((p), 0, (sz))
#define STBI_REALLOC_SIZED(p, oldsz, newsz) zcsr_image_realloc_sized((p), (oldsz), (newsz))
#define STBI_FREE(p) zcsr_image_free(p)
#define STB_IMAGE_IMPLEMENTATION
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4244 4267 4189 4389)
#endif
#include "../../third_party/stb_image.h"
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

static uint8_t zcsr_clamp_u8(int v) {
    if (v < 0) return 0;
    if (v > 255) return 255;
    return (uint8_t)v;
}

static uint8_t zcsr_mul_u8(uint8_t a, uint8_t b) {
    return (uint8_t)(((int)a * (int)b + 127) / 255);
}

static uint8_t zcsr_lerp_u8(uint8_t a, uint8_t b, uint8_t amount) {
    int t = (int)amount;
    return (uint8_t)(((int)a * (255 - t) + (int)b * t + 127) / 255);
}

static void zcsr_copy_rgba(uint8_t* dst, const uint8_t* src, size_t bytes) {
    for (size_t i = 0; i < bytes; ++i) dst[i] = src[i];
}

/* ---- Internal decode routes (shared with the ICO/CUR container parser) ---- */

/* stb 8-bit load; assumes the caller has attached the arena. Returns pixels
 * (inside the arena) or NULL. */
static unsigned char* zcsr_stb8_load(const void* encoded, size_t encoded_len,
                                     int* w, int* h, int* channels) {
    return stbi_load_from_memory((const unsigned char*)encoded, (int)encoded_len,
                                 w, h, channels, 4);
}

zcsr_image zcsr_image_decode_stb8(const void* encoded, size_t encoded_len,
                                  void* buffer, size_t bytes) {
    zcsr_image_arena arena;
    int w = 0;
    int h = 0;
    int channels = 0;
    unsigned char* pixels;
    size_t image_bytes;
    if (!encoded || encoded_len == 0 || encoded_len > (size_t)INT_MAX || !buffer || bytes == 0) {
        return (zcsr_image){ 0, 0, 0 };
    }
    arena = (zcsr_image_arena){ (unsigned char*)buffer, bytes, 0 };
    zcsr_image_active_arena = &arena;
    pixels = zcsr_stb8_load(encoded, encoded_len, &w, &h, &channels);
    zcsr_image_active_arena = 0;
    (void)channels;
    if (!pixels || !zcsr_image_size(w, h, &image_bytes)) return (zcsr_image){ 0, 0, 0 };
    (void)image_bytes;
    return (zcsr_image){ w, h, pixels };
}

int zcsr_image_stb8_info(const void* encoded, size_t encoded_len, int* w, int* h) {
    int comp = 0;
    if (!encoded || encoded_len == 0 || encoded_len > (size_t)INT_MAX || !w || !h) return 0;
    return stbi_info_from_memory((const unsigned char*)encoded, (int)encoded_len, w, h, &comp);
}

const uint8_t* zcsr_image_load_gif_frames(const void* encoded, size_t encoded_len,
                                          void* buffer, size_t bytes,
                                          int* w, int* h, int* frame_count,
                                          int** delays) {
    zcsr_image_arena arena;
    int channels = 0;
    int* gif_delays = 0;
    const uint8_t* pixels;

    if (!encoded || encoded_len == 0 || encoded_len > (size_t)INT_MAX || !buffer || bytes == 0 ||
        !w || !h || !frame_count || !delays) {
        return 0;
    }
    arena = (zcsr_image_arena){ (unsigned char*)buffer, bytes, 0 };
    zcsr_image_active_arena = &arena;
    pixels = stbi_load_gif_from_memory((const unsigned char*)encoded, (int)encoded_len,
                                       &gif_delays, w, h, frame_count, &channels, 4);
    zcsr_image_active_arena = 0;
    if (!pixels || *frame_count <= 0) return 0;
    *delays = gif_delays;
    return pixels;
}

/* Reinhard filmic-ish compress without a libm dependency: c/(1+c), clamp, round.
 * NaN / negative inputs map to black. */
static uint8_t zcsr_tonemap_channel(float c) {
    float r;
    if (!(c > 0.0f)) return 0;
    r = c / (1.0f + c);
    if (r > 1.0f) r = 1.0f;
    return (uint8_t)(r * 255.0f + 0.5f);
}

static uint8_t zcsr_tonemap_alpha(float a) {
    if (!(a > 0.0f)) return 0;
    if (a > 1.0f) return 255;
    return (uint8_t)(a * 255.0f + 0.5f);
}

/* Radiance RGBE: stb decodes to linear float RGBA; we compress to RGBA8 inside
 * the caller buffer. The float scratch and the packed output both live in the
 * same bump arena. */
static zcsr_image zcsr_image_decode_hdr(const void* encoded, size_t encoded_len,
                                       void* buffer, size_t bytes) {
    zcsr_image_arena arena;
    int w = 0;
    int h = 0;
    int channels = 0;
    float* pixels;
    size_t image_bytes;
    size_t count;
    uint8_t* out;

    if (!encoded || encoded_len == 0 || encoded_len > (size_t)INT_MAX || !buffer || bytes == 0) {
        return (zcsr_image){ 0, 0, 0 };
    }
    arena = (zcsr_image_arena){ (unsigned char*)buffer, bytes, 0 };
    zcsr_image_active_arena = &arena;
    pixels = stbi_loadf_from_memory((const unsigned char*)encoded, (int)encoded_len,
                                    &w, &h, &channels, 4);
    if (!pixels || !zcsr_image_size(w, h, &image_bytes)) {
        zcsr_image_active_arena = 0;
        return (zcsr_image){ 0, 0, 0 };
    }
    out = (uint8_t*)zcsr_image_alloc(image_bytes);
    if (!out) {
        zcsr_image_active_arena = 0;
        return (zcsr_image){ 0, 0, 0 };
    }
    count = (size_t)w * (size_t)h;
    for (size_t i = 0; i < count; ++i) {
        const float* f = &pixels[i * 4];
        uint8_t* o = &out[i * 4];
        o[0] = zcsr_tonemap_channel(f[0]);
        o[1] = zcsr_tonemap_channel(f[1]);
        o[2] = zcsr_tonemap_channel(f[2]);
        o[3] = zcsr_tonemap_alpha(f[3]);
    }
    zcsr_image_active_arena = 0;
    (void)channels;
    return (zcsr_image){ w, h, out };
}

static uint16_t zcsr_rd16le(const uint8_t* p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t zcsr_rd32le(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void zcsr_wr16le(uint8_t* p, uint16_t v) {
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
}

static void zcsr_wr32le(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)((v >> 24) & 0xFFu);
}

/* Wrap a bare ICO/CUR DIB in a synthetic 14-byte BMP file header so the stb
 * BMP loader can consume it. Icon DIBs store biHeight doubled (image height +
 * AND mask height); we halve it per the icon convention. */
zcsr_image zcsr_image_decode_dib(const void* dib, size_t dib_len,
                                 void* buffer, size_t bytes) {
    zcsr_image_arena arena;
    const uint8_t* d = (const uint8_t*)dib;
    uint32_t bi_size;
    int32_t width;
    int32_t height2;
    uint16_t bpp;
    uint32_t compression;
    uint32_t palette_bytes;
    uint32_t data_offset;
    size_t wrapper_len;
    uint8_t* wrapper;
    zcsr_image img;

    if (!dib || dib_len < 40 || !buffer || bytes == 0) return (zcsr_image){ 0, 0, 0 };
    bi_size = zcsr_rd32le(d + 0);
    if (bi_size != 40 && bi_size != 56 && bi_size != 108 && bi_size != 124) {
        return (zcsr_image){ 0, 0, 0 };
    }
    width = (int32_t)zcsr_rd32le(d + 4);
    height2 = (int32_t)zcsr_rd32le(d + 8);
    bpp = zcsr_rd16le(d + 14);
    compression = zcsr_rd32le(d + 16);
    if (width <= 0 || height2 <= 0) return (zcsr_image){ 0, 0, 0 };
    if (compression != 0) return (zcsr_image){ 0, 0, 0 };           /* BI_RGB only */
    if (bpp != 16 && bpp != 24 && bpp != 32) return (zcsr_image){ 0, 0, 0 };
    palette_bytes = 0;                                              /* no palette for >=16bpp */

    arena = (zcsr_image_arena){ (unsigned char*)buffer, bytes, 0 };
    zcsr_image_active_arena = &arena;

    if (dib_len > SIZE_MAX - 14u) { zcsr_image_active_arena = 0; return (zcsr_image){ 0, 0, 0 }; }
    wrapper_len = 14u + dib_len;
    wrapper = (uint8_t*)zcsr_image_alloc(wrapper_len);
    if (!wrapper) { zcsr_image_active_arena = 0; return (zcsr_image){ 0, 0, 0 }; }

    wrapper[0] = 'B';
    wrapper[1] = 'M';
    zcsr_wr32le(wrapper + 2, (uint32_t)wrapper_len);
    zcsr_wr16le(wrapper + 6, 0);
    zcsr_wr16le(wrapper + 8, 0);
    data_offset = 14u + bi_size + palette_bytes;
    zcsr_wr32le(wrapper + 10, data_offset);
    memcpy(wrapper + 14, d, dib_len);
    /* Halve the doubled icon height in the copied DIB header. */
    zcsr_wr32le(wrapper + 14 + 8, (uint32_t)(height2 / 2));

    {
        int w = 0;
        int h = 0;
        int channels = 0;
        unsigned char* pixels = zcsr_stb8_load(wrapper, wrapper_len, &w, &h, &channels);
        size_t image_bytes = 0;
        zcsr_image_active_arena = 0;
        (void)channels;
        if (!pixels || !zcsr_image_size(w, h, &image_bytes)) return (zcsr_image){ 0, 0, 0 };
        (void)image_bytes;
        img = (zcsr_image){ w, h, pixels };
    }
    return img;
}

zcsr_image zcsr_image_decode(const void* encoded, size_t encoded_len, void* buffer, size_t bytes) {
    zcsr_image_format fmt;

    if (!encoded || encoded_len == 0 || !buffer || bytes == 0) {
        return (zcsr_image){ 0, 0, 0 };
    }
    fmt = zcsr_image_detect_format(encoded, encoded_len);
    switch (fmt) {
        case ZCSR_IMAGE_FMT_HDR:
            return zcsr_image_decode_hdr(encoded, encoded_len, buffer, bytes);
        case ZCSR_IMAGE_FMT_ICO:
        case ZCSR_IMAGE_FMT_CUR:
            return zcsr_image_decode_container(encoded, encoded_len, buffer, bytes);
        case ZCSR_IMAGE_FMT_WEBP:
            return zcsr_image_decode_webp(encoded, encoded_len, buffer, bytes);
        default:
            /* PNG / JPEG / BMP / GIF, and UNKNOWN signatures the stb
             * sniffers may still recognize. */
            return zcsr_image_decode_stb8(encoded, encoded_len, buffer, bytes);
    }
}

bool zcsr_image_chroma_key(zcsr_image* img,
                           uint8_t key_r, uint8_t key_g, uint8_t key_b,
                           uint8_t tolerance, uint8_t softness) {
    size_t pixels;
    size_t bytes;
    if (!img || !img->rgba || !zcsr_image_size(img->w, img->h, &bytes)) return false;
    pixels = bytes / 4u;
    for (size_t i = 0; i < pixels; ++i) {
        uint8_t* p = &img->rgba[i * 4u];
        int dr = p[0] > key_r ? p[0] - key_r : key_r - p[0];
        int dg = p[1] > key_g ? p[1] - key_g : key_g - p[1];
        int db = p[2] > key_b ? p[2] - key_b : key_b - p[2];
        int dist = dr > dg ? dr : dg;
        if (db > dist) dist = db;
        if (dist <= (int)tolerance) {
            p[3] = 0;
        } else if (softness > 0 && dist < (int)tolerance + (int)softness) {
            int edge = dist - (int)tolerance;
            p[3] = (uint8_t)(((int)p[3] * edge + ((int)softness / 2)) / (int)softness);
        }
    }
    return true;
}

zcsr_image zcsr_image_make_variant(const zcsr_image* src, void* dst_buffer, size_t dst_bytes,
                                   zcsr_color_mode mode,
                                   uint8_t cr, uint8_t cg, uint8_t cb, uint8_t ca,
                                   uint8_t amount) {
    size_t bytes;
    uint8_t* dst = (uint8_t*)dst_buffer;
    if (!src || !src->rgba || !dst || !zcsr_image_size(src->w, src->h, &bytes) || dst_bytes < bytes) {
        return (zcsr_image){ 0, 0, 0 };
    }
    zcsr_copy_rgba(dst, src->rgba, bytes);
    for (size_t i = 0; i < bytes; i += 4u) {
        if (mode == ZCSR_MOD_MIX) {
            dst[i + 0u] = zcsr_lerp_u8(dst[i + 0u], cr, amount);
            dst[i + 1u] = zcsr_lerp_u8(dst[i + 1u], cg, amount);
            dst[i + 2u] = zcsr_lerp_u8(dst[i + 2u], cb, amount);
            dst[i + 3u] = zcsr_lerp_u8(dst[i + 3u], ca, amount);
        } else if (mode == ZCSR_MOD_ADD) {
            dst[i + 0u] = zcsr_clamp_u8((int)dst[i + 0u] + ((int)cr * (int)amount + 127) / 255);
            dst[i + 1u] = zcsr_clamp_u8((int)dst[i + 1u] + ((int)cg * (int)amount + 127) / 255);
            dst[i + 2u] = zcsr_clamp_u8((int)dst[i + 2u] + ((int)cb * (int)amount + 127) / 255);
            dst[i + 3u] = zcsr_clamp_u8((int)dst[i + 3u] + ((int)ca * (int)amount + 127) / 255);
        } else {
            dst[i + 0u] = zcsr_mul_u8(dst[i + 0u], cr);
            dst[i + 1u] = zcsr_mul_u8(dst[i + 1u], cg);
            dst[i + 2u] = zcsr_mul_u8(dst[i + 2u], cb);
            dst[i + 3u] = zcsr_mul_u8(dst[i + 3u], ca);
        }
    }
    return (zcsr_image){ src->w, src->h, dst };
}

zcsr_image zcsr_image_palette_remap(const zcsr_image* src, const uint8_t lut[256][4],
                                    void* dst_buffer, size_t dst_bytes) {
    size_t bytes;
    uint8_t* dst = (uint8_t*)dst_buffer;
    if (!src || !src->rgba || !lut || !dst || !zcsr_image_size(src->w, src->h, &bytes) || dst_bytes < bytes) {
        return (zcsr_image){ 0, 0, 0 };
    }
    for (size_t i = 0; i < bytes; i += 4u) {
        const uint8_t* color = lut[src->rgba[i]];
        dst[i + 0u] = color[0];
        dst[i + 1u] = color[1];
        dst[i + 2u] = color[2];
        dst[i + 3u] = color[3];
    }
    return (zcsr_image){ src->w, src->h, dst };
}
