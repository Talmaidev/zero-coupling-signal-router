/* Animation contract implementation: GIF89a animation, animated WebP, and a
 * single-frame passthrough for static images.
 *
 * Memory contract: all canvas frames and the delay table live in the
 * caller-provided scratch buffer; the opaque handle is carved out of that same
 * buffer. Backend scratch for GIF decoding stays in the scratch region during
 * open only; WebP decoder objects are released before open returns. No
 * retained per-frame heap grows with frame count, and zcsr_image_anim_frame()
 * writes into the caller's zcsr_image.rgba.
 *
 * Build contract: ZCSR_HAVE_LIBWEBP is defined by the build when the prebuilt
 * libwebp root (ZCSR_LIBWEBP_ROOT; third_party/prebuilt/<os>/libwebp) is
 * present. Without it, WebP containers fail closed and report unsupported; the
 * build itself does not fail.
 *
 * Portable C only: no platform/Windows types are referenced in this TU.
 */
#include "zcsr/image.h"
#include "zcsr/image_animation.h"
#include "image_internal.h"

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#if defined(ZCSR_HAVE_LIBWEBP)
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4244 4267 4189 4389)
#endif
#include <webp/decode.h>
#include <webp/demux.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
#endif

struct zcsr_image_anim {
    int w;
    int h;
    int frame_count;
    int loop_count;
    int backend; /* 0 = GIF, 1 = animated WebP, 2 = static stb, 3 = static WebP */
    int32_t* delays;
    uint8_t* canvases; /* frame_count * w * h * 4 bytes, caller buffer */
};

enum {
    ZCSR_ANIM_BACKEND_GIF = 0,
    ZCSR_ANIM_BACKEND_WEBP_ANIM = 1,
    ZCSR_ANIM_BACKEND_STB = 2,
    ZCSR_ANIM_BACKEND_WEBP_STATIC = 3
};

static size_t zcsr_align16(size_t v) {
    return (v + 15u) & ~(size_t)15u;
}

static uint16_t zcsr_rd16le(const uint8_t* p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

/* Rough peak for the GIF stb loader's internal per-frame buffers
 * (current frame canvas, background copy, history mask) plus slack. Held in
 * the caller buffer only during open(). */
static size_t zcsr_gif_scratch_bytes(int w, int h) {
    size_t px = (size_t)w * (size_t)h;
    return px * 4u * 3u + px + 32u * 1024u;
}

static size_t zcsr_layout_offsets(const zcsr_image_anim_info* info,
                                  size_t* delays_off, size_t* canvases_off,
                                  size_t* handle_off);

/* ---- Lightweight GIF structure scan (no pixel decode) ---- */

static int zcsr_gif_parse_info(const uint8_t* b, size_t len,
                               zcsr_image_anim_info* out) {
    size_t p = 13;
    int frames = 0;
    int loop = 1;
    uint8_t flags;

    if (len < 13) return 0;
    if (memcmp(b, "GIF8", 4) != 0) return 0;
    if (b[4] != '7' && b[4] != '9') return 0;

    out->w = zcsr_rd16le(b + 6);
    out->h = zcsr_rd16le(b + 8);
    flags = b[10];
    if (out->w <= 0 || out->h <= 0) return 0;

    if (flags & 0x80) p += (size_t)(2u << (flags & 7)) * 3u;

    while (p < len) {
        uint8_t tag = b[p++];
        if (tag == 0x3B) break; /* trailer */
        if (tag == 0x21) {      /* extension */
            uint8_t label;
            uint8_t blen;
            if (p >= len) return 0;
            label = b[p++];
            if (label == 0xFF) { /* application extension: NETSCAPE loop count */
                if (p >= len) return 0;
                blen = b[p++];
                if (blen == 11 && p + 11 <= len &&
                    memcmp(b + p, "NETSCAPE2.0", 11) == 0) {
                    p += 11;
                    if (p >= len) return 0;
                    blen = b[p++];
                    if (blen == 3 && p + 3 <= len) {
                        loop = (int)b[p + 1] | ((int)b[p + 2] << 8);
                        p += 3;
                    }
                } else {
                    p += blen;
                }
                while (p < len && (blen = b[p++]) != 0) p += blen;
            } else {
                while (p < len && (blen = b[p++]) != 0) p += blen;
            }
        } else if (tag == 0x2C) { /* image descriptor */
            uint8_t iflags;
            uint8_t blen;
            if (p + 9 > len) return 0;
            iflags = b[p + 8];
            p += 9;
            if (iflags & 0x80) p += (size_t)(2u << (iflags & 7)) * 3u;
            if (p >= len) return 0;
            p++; /* LZW minimum code size */
            while (p < len && (blen = b[p++]) != 0) p += blen;
            frames++;
        } else {
            return 0; /* unknown block: corrupt */
        }
    }

    if (frames <= 0) frames = 1;
    out->frame_count = frames;
    out->loop_count = loop;
    return 1;
}

static int zcsr_is_gif(const uint8_t* b, size_t len) {
    return len >= 6 && memcmp(b, "GIF8", 4) == 0 && (b[4] == '7' || b[4] == '9');
}

#if defined(ZCSR_HAVE_LIBWEBP)
static int zcsr_is_webp(const uint8_t* b, size_t len) {
    return len >= 12 && memcmp(b, "RIFF", 4) == 0 && memcmp(b + 8, "WEBP", 4) == 0;
}
#endif

#if defined(ZCSR_HAVE_LIBWEBP)
static int zcsr_webp_parse_info(const uint8_t* b, size_t len,
                                zcsr_image_anim_info* out) {
    WebPData data = { b, len };
    WebPDemuxer* dm = WebPDemux(&data);
    uint32_t frames;
    if (!dm) return 0;
    out->w = (int)WebPDemuxGetI(dm, WEBP_FF_CANVAS_WIDTH);
    out->h = (int)WebPDemuxGetI(dm, WEBP_FF_CANVAS_HEIGHT);
    out->loop_count = (int)WebPDemuxGetI(dm, WEBP_FF_LOOP_COUNT);
    frames = WebPDemuxGetI(dm, WEBP_FF_FRAME_COUNT);
    WebPDemuxDelete(dm);
    if (out->w <= 0 || out->h <= 0) return 0;
    out->frame_count = frames == 0 ? 1 : (int)frames;
    return 1;
}
#endif

bool zcsr_image_anim_info_of(const void* encoded, size_t len,
                             zcsr_image_anim_info* out) {
    const uint8_t* b;
    zcsr_image_anim_info info;
    int w = 0, h = 0;

    if (!encoded || len == 0 || !out) return false;
    b = (const uint8_t*)encoded;

    if (zcsr_is_gif(b, len)) {
        return zcsr_gif_parse_info(b, len, out);
    }
#if defined(ZCSR_HAVE_LIBWEBP)
    if (zcsr_is_webp(b, len)) {
        return zcsr_webp_parse_info(b, len, out);
    }
#endif
    /* Static stb-supported containers: single frame, play once. */
    if (zcsr_image_stb8_info(b, len, &w, &h) && w > 0 && h > 0) {
        info.w = w;
        info.h = h;
        info.frame_count = 1;
        info.loop_count = 1;
        *out = info;
        return true;
    }
#if defined(ZCSR_HAVE_LIBWEBP)
    if (zcsr_is_webp(b, len) && WebPGetInfo(b, len, &w, &h) && w > 0 && h > 0) {
        info.w = w;
        info.h = h;
        info.frame_count = 1;
        info.loop_count = 1;
        *out = info;
        return true;
    }
#endif
    return false;
}

size_t zcsr_image_anim_buffer_bytes(const void* encoded, size_t len) {
    zcsr_image_anim_info info;
    size_t stride;
    size_t delays_off, canvases_off, handle_off, total;

    if (!zcsr_image_anim_info_of(encoded, len, &info)) return 0;
    if (info.w <= 0 || info.h <= 0 || info.frame_count <= 0) return 0;
    stride = (size_t)info.w * (size_t)info.h * 4u;
    if (stride > SIZE_MAX / (size_t)info.frame_count) return 0;

    total = zcsr_layout_offsets(&info, &delays_off, &canvases_off, &handle_off);
    if (total < stride) return 0;
    return total;
}

/* ---- Open / frame / delay / close ---- */

static size_t zcsr_layout_offsets(const zcsr_image_anim_info* info,
                                  size_t* delays_off, size_t* canvases_off,
                                  size_t* handle_off) {
    size_t stride = (size_t)info->w * (size_t)info->h * 4u;
    size_t scratch = zcsr_gif_scratch_bytes(info->w, info->h);
    size_t p = scratch;
    *delays_off = p;
    p += (size_t)info->frame_count * sizeof(int32_t);
    p = zcsr_align16(p);
    *canvases_off = p;
    p += (size_t)info->frame_count * stride;
    p = zcsr_align16(p);
    *handle_off = p;
    p += zcsr_align16(sizeof(struct zcsr_image_anim));
    return p;
}

#if defined(ZCSR_HAVE_LIBWEBP)
static int zcsr_anim_open_webp(zcsr_image_anim* anim, const uint8_t* b, size_t len,
                               uint8_t* base, size_t delays_off, size_t canvases_off) {
    WebPData data = { b, len };
    WebPAnimDecoderOptions opts;
    WebPAnimDecoder* dec;
    WebPAnimInfo anim_info;
    size_t stride = (size_t)anim->w * (size_t)anim->h * 4u;
    int frame = 0;
    int prev_ts = 0;
    int ok = 0;

    if (!WebPAnimDecoderOptionsInit(&opts)) return 0;
    opts.color_mode = MODE_RGBA;
    opts.use_threads = 0;
    dec = WebPAnimDecoderNew(&data, &opts);
    if (!dec) return 0;
    if (!WebPAnimDecoderGetInfo(dec, &anim_info)) {
        WebPAnimDecoderDelete(dec);
        return 0;
    }
    anim->frame_count = (int)anim_info.frame_count;
    anim->loop_count = (int)anim_info.loop_count;
    if (anim->frame_count <= 0) {
        WebPAnimDecoderDelete(dec);
        return 0;
    }

    WebPAnimDecoderReset(dec);
    while (WebPAnimDecoderHasMoreFrames(dec) && frame < anim->frame_count) {
        uint8_t* buf = 0;
        int ts = 0;
        size_t dst_off = canvases_off + (size_t)frame * stride;
        if (!WebPAnimDecoderGetNext(dec, &buf, &ts) || !buf) break;
        memcpy(base + dst_off, buf, stride);
        if (frame > 0) anim->delays[frame - 1] = (int32_t)(ts - prev_ts);
        prev_ts = ts;
        frame++;
    }
    WebPAnimDecoderDelete(dec);

    if (frame >= 2) {
        anim->delays[frame - 1] = anim->delays[frame - 2]; /* last repeats prior */
        ok = 1;
    } else if (frame == 1) {
        /* Static WebP: anim decoder produced a single canvas. */
        anim->frame_count = 1;
        anim->loop_count = 1;
        anim->delays[0] = 0;
        ok = 1;
    }
    (void)delays_off;
    return ok;
}
#endif

zcsr_image_anim* zcsr_image_anim_open(const void* encoded, size_t len,
                                     void* buffer, size_t bytes) {
    zcsr_image_anim_info info;
    zcsr_image_anim* anim;
    uint8_t* base;
    size_t delays_off, canvases_off, handle_off, total;
    const uint8_t* b;

    if (!encoded || len == 0 || !buffer || bytes == 0) return 0;
    if (!zcsr_image_anim_info_of(encoded, len, &info)) return 0;
    total = zcsr_layout_offsets(&info, &delays_off, &canvases_off, &handle_off);
    if (bytes < total) return 0;

    base = (uint8_t*)buffer;
    b = (const uint8_t*)encoded;
    /* Handle sits after the canvases so it never overlaps decode scratch. */
    anim = (zcsr_image_anim*)(base + handle_off);
    anim->w = info.w;
    anim->h = info.h;
    anim->frame_count = info.frame_count;
    anim->loop_count = info.loop_count;
    anim->delays = (int32_t*)(base + delays_off);
    anim->canvases = base + canvases_off;

    if (zcsr_is_gif(b, len)) {
        const uint8_t* frames;
        int w = 0, h = 0, count = 0;
        int* gif_delays = 0;
        size_t stride = (size_t)info.w * (size_t)info.h * 4u;
        int i;
        anim->backend = ZCSR_ANIM_BACKEND_GIF;
        frames = zcsr_image_load_gif_frames(b, len, base, bytes,
                                             &w, &h, &count, &gif_delays);
        if (!frames || count <= 0 || w != info.w || h != info.h) return 0;
        if (count > anim->frame_count) count = anim->frame_count;
        for (i = 0; i < count; ++i) {
            memcpy(anim->canvases + (size_t)i * stride,
                   frames + (size_t)i * stride, stride);
            anim->delays[i] = gif_delays ? (int32_t)gif_delays[i] : 0;
        }
        anim->frame_count = count;
        return anim;
    }

#if defined(ZCSR_HAVE_LIBWEBP)
    if (zcsr_is_webp(b, len)) {
        zcsr_image img;
        anim->backend = ZCSR_ANIM_BACKEND_WEBP_ANIM;
        if (zcsr_anim_open_webp(anim, b, len, base, delays_off, canvases_off)) {
            return anim;
        }
        /* Fallback: static WebP single frame. */
        img = zcsr_image_decode_webp(b, len, base, bytes);
        if (!img.rgba || img.w != info.w || img.h != info.h) return 0;
        memcpy(anim->canvases, img.rgba, (size_t)img.w * img.h * 4u);
        anim->backend = ZCSR_ANIM_BACKEND_WEBP_STATIC;
        anim->frame_count = 1;
        anim->loop_count = 1;
        anim->delays[0] = 0;
        return anim;
    }
#endif

    /* Static stb-supported container: single frame passthrough. */
    {
        zcsr_image img = zcsr_image_decode_stb8(b, len, base, bytes);
        if (!img.rgba || img.w != info.w || img.h != info.h) return 0;
        memcpy(anim->canvases, img.rgba, (size_t)img.w * img.h * 4u);
        anim->backend = ZCSR_ANIM_BACKEND_STB;
        anim->frame_count = 1;
        anim->loop_count = 1;
        anim->delays[0] = 0;
        return anim;
    }
}

bool zcsr_image_anim_frame(zcsr_image_anim* anim, int index, zcsr_image* out) {
    size_t stride;
    if (!anim || !out || !out->rgba) return false;
    if (index < 0 || index >= anim->frame_count) return false;
    stride = (size_t)anim->w * (size_t)anim->h * 4u;
    out->w = anim->w;
    out->h = anim->h;
    memcpy(out->rgba, anim->canvases + (size_t)index * stride, stride);
    return true;
}

int zcsr_image_anim_delay_ms(const zcsr_image_anim* anim, int index) {
    if (!anim || index < 0 || index >= anim->frame_count) return 0;
    return (int)anim->delays[index];
}

void zcsr_image_anim_close(zcsr_image_anim* anim) {
    /* All state lives in the caller buffer; no backend decoder is retained
     * behind the handle. Nothing to release here. */
    (void)anim;
}
