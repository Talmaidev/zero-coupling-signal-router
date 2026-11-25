/* video_mf.c — Windows Media Foundation backend (real demux + decode).
 *
 * Opens a media source through the MF source resolver, negotiates an RGB32
 * reader output, and converts each decoded sample to packed RGBA8 in the
 * caller buffer. T0 scope: MP4 container / H.264 codec only. WebM/VP9 and
 * any codec MF cannot decode fall through to the FFmpeg backend (video_ff.c)
 * via the facade candidate list.
 *
 * COM pointers are released manually; no C++/WRL types leak out of this TU.
 * This file is compiled only under _WIN32.
 */
#if defined(_WIN32)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <propvarutil.h>

#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "zcsr/video.h"
#include "video_internal.h"

#define ZCSR_MF_STREAM_ANY (DWORD)-1

typedef struct {
    char                 path[512];
    IMFMediaSource*      source;
    IMFSourceReader*     reader;
    DWORD                stream_index;
    uint32_t             width;
    uint32_t             height;
    uint32_t             fps_num;
    uint32_t             fps_den;
    zcsr_video_pixel_format pixel;
    uint64_t             next_frame_index;
    bool                 eof;
} mf_state;

/* ---- COM helper ---- */

#define MF_RELEASE(p) do { \
        if ((p) != NULL) { (p)->lpVtbl->Release((p)); (p) = NULL; } \
    } while (0)

/* Attribute accessors. In C the MF vtable takes REFGUID = const GUID*; the
 * size/ratio attributes are two UINT32 packed into one UINT64 (low = width /
 * numerator, high = height / denominator). */

static void attr_set_guid(void* attrs, const GUID* key, const GUID* value) {
    IMFAttributes* a = (IMFAttributes*)attrs;
    (void)a->lpVtbl->SetGUID(a, key, value);
}

static void attr_set_size(void* attrs, const GUID* key, UINT32 w, UINT32 h) {
    IMFAttributes* a = (IMFAttributes*)attrs;
    UINT64 packed = (UINT64)w | ((UINT64)h << 32);
    (void)a->lpVtbl->SetUINT64(a, key, packed);
}

static bool attr_get_size(void* attrs, const GUID* key,
                          UINT32* w, UINT32* h) {
    IMFAttributes* a = (IMFAttributes*)attrs;
    UINT64 packed = 0u;
    if (FAILED(a->lpVtbl->GetUINT64(a, key, &packed))) {
        return false;
    }
    *w = (UINT32)(packed & 0xFFFFFFFFu);
    *h = (UINT32)(packed >> 32);
    return true;
}

static bool attr_get_ratio(void* attrs, const GUID* key,
                           UINT32* n, UINT32* d) {
    IMFAttributes* a = (IMFAttributes*)attrs;
    UINT64 packed = 0u;
    if (FAILED(a->lpVtbl->GetUINT64(a, key, &packed))) {
        return false;
    }
    *n = (UINT32)(packed & 0xFFFFFFFFu);
    *d = (UINT32)(packed >> 32);
    return true;
}

static bool attr_get_stride(void* attrs, LONG* stride) {
    IMFAttributes* a = (IMFAttributes*)attrs;
    UINT32 value = 0u;
    if (FAILED(a->lpVtbl->GetUINT32(a, &MF_MT_DEFAULT_STRIDE, &value))) {
        return false;
    }
    *stride = (LONG)value;
    return true;
}

static GUID fourcc_guid(uint32_t fourcc) {
    GUID g;
    g.Data1 = fourcc;
    g.Data2 = 0x0000u;
    g.Data3 = 0x0010u;
    g.Data4[0] = 0x80u; g.Data4[1] = 0x00u; g.Data4[2] = 0x00u; g.Data4[3] = 0xAAu;
    g.Data4[4] = 0x00u; g.Data4[5] = 0x38u; g.Data4[6] = 0x9Bu; g.Data4[7] = 0x71u;
    return g;
}

/* ---- codec / pixel mapping ---- */

static zcsr_video_codec map_codec(const GUID* g) {
    uint32_t f = g->Data1;
    switch (f) {
        case 0x34363248u: /* 'H264' */
        case 0x31637661u: /* 'avc1' */
        case 0x63766168u: /* 'h264' */
            return ZCSR_VIDEO_CODEC_H264;
        default:
            return ZCSR_VIDEO_CODEC_UNKNOWN;
    }
}

static zcsr_video_pixel_format map_pixel(const GUID* g) {
    uint32_t f = g->Data1;
    switch (f) {
        case 0x3231564Eu: /* 'NV12' */
            return ZCSR_VIDEO_PIXEL_NV12;
        case 0x32595559u: /* 'YUY2' */
            return ZCSR_VIDEO_PIXEL_YUY2;
        case 0x41524742u: /* 'BGRA' */
            return ZCSR_VIDEO_PIXEL_BGRA;
        case 0x41475255u: /* 'RGBA' */
            return ZCSR_VIDEO_PIXEL_RGBA;
        default:
            return ZCSR_VIDEO_PIXEL_UNKNOWN;
    }
}

/* ---- YUV -> RGBA (BT.601 limited range) ---- */

static inline uint8_t clamp8(int v) {
    return (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v));
}

static void yuv601_to_rgb(int y, int cb, int cr,
                          uint8_t* r, uint8_t* g, uint8_t* b) {
    int yy = (y - 16) * 255 / 219;
    int c = cb - 128;
    int d = cr - 128;
    if (yy < 0) yy = 0;
    if (yy > 255) yy = 255;
    *r = clamp8(yy + (22986 * d >> 14));
    *g = clamp8(yy - ((5636 * c + 11442 * d) >> 14));
    *b = clamp8(yy + (28214 * c >> 14));
}

/* ---- MF startup (process-wide, lazily once) ---- */

static volatile LONG g_mf_started = 0;

static bool mf_ensure_started(void) {
    LONG prev = InterlockedCompareExchange(&g_mf_started, 1L, 0L);
    if (prev == 1L) {
        return true;
    }
    HRESULT hr = MFStartup(MF_VERSION, MFSTARTUP_LITE);
    if (SUCCEEDED(hr)) {
        return true;
    }
    g_mf_started = 0L;
    return false;
}

/* ---- decoder capability enumeration ---- */

static bool decoder_present(uint32_t fourcc) {
    GUID subtype = fourcc_guid(fourcc);
    MFT_REGISTER_TYPE_INFO filter;
    IMFActivate** activates = NULL;
    UINT32 count = 0u;
    HRESULT hr;
    UINT32 i;

    if (!mf_ensure_started()) {
        return false;
    }
    filter.guidMajorType = MFMediaType_Video;
    filter.guidSubtype = subtype;
    hr = MFTEnumEx(MFT_CATEGORY_VIDEO_DECODER, 0u, &filter, NULL, &activates, &count);
    if (FAILED(hr) || activates == NULL) {
        return false;
    }
    for (i = 0u; i < count; ++i) {
        MF_RELEASE(activates[i]);
    }
    CoTaskMemFree(activates);
    return count > 0u;
}

static void mf_caps(zcsr_video_container_caps* containers,
                    zcsr_video_codec_caps* codecs) {
    if (containers) {
        containers->mp4  = true;   /* MF SourceResolver opens ISO BMFF */
        containers->webm = false;   /* WebM/VP9 is served by the FFmpeg backend */
    }
    if (codecs) {
        codecs->h264 = decoder_present(0x34363248u);
        codecs->vp9  = false;
    }
}

/* ---- open ---- */

static zcsr_video_error map_open_hr(HRESULT hr) {
    if (hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) ||
        hr == HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND)) {
        return ZCSR_VIDEO_ERR_NOT_FOUND;
    }
    if (hr == MF_E_UNSUPPORTED_SCHEME ||
        hr == MF_E_TOPO_CODEC_NOT_FOUND || hr == MF_E_TRANSFORM_TYPE_NOT_SET) {
        return ZCSR_VIDEO_ERR_UNSUPPORTED;
    }
    return ZCSR_VIDEO_ERR_NOT_VIDEO;
}

/* Open the source, build the reader, negotiate an uncompressed output, and
 * fill st plus (optionally) out_info. On success st->source/st->reader are
 * owned by the caller; on failure every partial COM object is released
 * internally. out_info may be NULL on a seek-driven reopen. */
static zcsr_video_error mf_attach(const char* path, mf_state* st,
                                  zcsr_video_info* out_info) {
    WCHAR wpath[MAX_PATH];
    int wlen;
    IMFSourceResolver* resolver = NULL;
    IUnknown* unk = NULL;
    MF_OBJECT_TYPE objtype = MF_OBJECT_INVALID;
    IMFMediaSource* source = NULL;
    IMFPresentationDescriptor* pd = NULL;
    IMFSourceReader* reader = NULL;
    IMFMediaType* current = NULL;
    DWORD stream_count = 0u;
    DWORD selected = ZCSR_MF_STREAM_ANY;
    IMFStreamDescriptor* sd = NULL;
    IMFMediaTypeHandler* handler = NULL;
    IMFMediaType* mt = NULL;
    GUID subtype = {0};
    UINT32 width = 0u, height = 0u;
    UINT32 fps_num = 0u, fps_den = 0u;
    zcsr_video_pixel_format negotiated_pixel = ZCSR_VIDEO_PIXEL_UNKNOWN;
    HRESULT hr;
    zcsr_video_error err = ZCSR_VIDEO_ERR_DECODE;

    wlen = MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, MAX_PATH);
    if (wlen <= 0 || wlen >= MAX_PATH) {
        return ZCSR_VIDEO_ERR_INVALID_ARG;
    }

    hr = MFCreateSourceResolver(&resolver);
    if (FAILED(hr)) {
        return ZCSR_VIDEO_ERR_UNSUPPORTED;
    }
    hr = resolver->lpVtbl->CreateObjectFromURL(resolver, wpath,
            MF_RESOLUTION_MEDIASOURCE, NULL, &objtype, &unk);
    if (FAILED(hr)) {
        err = map_open_hr(hr);
        goto cleanup;
    }
    hr = unk->lpVtbl->QueryInterface(unk, &IID_IMFMediaSource, (void**)&source);
    if (FAILED(hr)) {
        err = ZCSR_VIDEO_ERR_NOT_VIDEO;
        goto cleanup;
    }

    hr = source->lpVtbl->CreatePresentationDescriptor(source, &pd);
    if (FAILED(hr)) {
        err = ZCSR_VIDEO_ERR_NOT_VIDEO;
        goto cleanup;
    }
    hr = pd->lpVtbl->GetStreamDescriptorCount(pd, &stream_count);
    if (FAILED(hr) || stream_count == 0u) {
        err = ZCSR_VIDEO_ERR_NOT_VIDEO;
        goto cleanup;
    }

    for (DWORD i = 0u; i < stream_count; ++i) {
        IMFStreamDescriptor* cand_sd = NULL;
        IMFMediaTypeHandler* cand_h = NULL;
        GUID cand_major = {0};
        BOOL stream_selected = TRUE;
        hr = pd->lpVtbl->GetStreamDescriptorByIndex(pd, i, &stream_selected, &cand_sd);
        if (FAILED(hr)) {
            continue;
        }
        hr = cand_sd->lpVtbl->GetMediaTypeHandler(cand_sd, &cand_h);
        if (SUCCEEDED(hr)) {
            hr = cand_h->lpVtbl->GetMajorType(cand_h, &cand_major);
            if (SUCCEEDED(hr) && IsEqualGUID(&cand_major, &MFMediaType_Video)) {
                sd = cand_sd;
                handler = cand_h;
                selected = i;
                break;
            }
        }
        MF_RELEASE(cand_h);
        MF_RELEASE(cand_sd);
    }
    if (selected == ZCSR_MF_STREAM_ANY) {
        err = ZCSR_VIDEO_ERR_UNSUPPORTED; /* container opened, but no video stream */
        goto cleanup;
    }

    hr = handler->lpVtbl->GetCurrentMediaType(handler, &mt);
    if (FAILED(hr)) {
        err = ZCSR_VIDEO_ERR_DECODE;
        goto cleanup;
    }
    (void)attr_get_size(mt, &MF_MT_FRAME_SIZE, &width, &height);
    (void)attr_get_ratio(mt, &MF_MT_FRAME_RATE, &fps_num, &fps_den);
    (void)mt->lpVtbl->GetGUID(mt, &MF_MT_SUBTYPE, &subtype);

    if (width == 0u || height == 0u) {
        err = ZCSR_VIDEO_ERR_DECODE;
        goto cleanup;
    }
    if (map_codec(&subtype) != ZCSR_VIDEO_CODEC_H264) {
        /* Out of T0 MF scope: leave the facade to try the FFmpeg fallback. */
        err = ZCSR_VIDEO_ERR_UNSUPPORTED;
        goto cleanup;
    }

    hr = MFCreateSourceReaderFromMediaSource(source, NULL, &reader);
    if (FAILED(hr)) {
        err = ZCSR_VIDEO_ERR_UNSUPPORTED;
        goto cleanup;
    }

    /* Negotiate an uncompressed output. Some system decoders expose YUY2/NV12
     * but no packed RGB type, so fall back in order and convert in read_frame. */
    {
        static const struct {
            const GUID* subtype;
            zcsr_video_pixel_format fmt;
        } targets[] = {
            { &MFVideoFormat_RGB32, ZCSR_VIDEO_PIXEL_BGRA },
            { &MFVideoFormat_YUY2,   ZCSR_VIDEO_PIXEL_YUY2 },
            { &MFVideoFormat_NV12,   ZCSR_VIDEO_PIXEL_NV12 },
        };
        bool negotiated = false;
        for (size_t t = 0u; t < (sizeof(targets) / sizeof(targets[0])); ++t) {
            IMFMediaType* cand = NULL;
            hr = MFCreateMediaType(&cand);
            if (FAILED(hr)) {
                continue;
            }
            attr_set_guid(cand, &MF_MT_MAJOR_TYPE, &MFMediaType_Video);
            attr_set_guid(cand, &MF_MT_SUBTYPE, targets[t].subtype);
            hr = reader->lpVtbl->SetCurrentMediaType(reader, selected, NULL, cand);
            MF_RELEASE(cand);
            if (FAILED(hr)) {
                continue;
            }
            if (SUCCEEDED(reader->lpVtbl->GetCurrentMediaType(reader, selected, &current))) {
                GUID got_sub = {0};
                (void)current->lpVtbl->GetGUID(current, &MF_MT_SUBTYPE, &got_sub);
                MF_RELEASE(current);
                negotiated_pixel = map_pixel(&got_sub);
                if (negotiated_pixel == ZCSR_VIDEO_PIXEL_UNKNOWN) {
                    negotiated_pixel = targets[t].fmt;
                }
                negotiated = true;
                break;
            }
        }
        if (!negotiated) {
            err = ZCSR_VIDEO_ERR_UNSUPPORTED;
            goto cleanup;
        }
    }

    /* Some sources expose the frame duration (ticks/frame) instead of the
     * frame rate, i.e. numerator/denominator inverted. Normalize a sane
     * inverted ratio back to frames/second. */
    if (fps_num > 0u && fps_den > fps_num) {
        UINT32 inverted = fps_den / fps_num;
        if (inverted >= 1u && inverted <= 240u) {
            UINT32 tmp = fps_num;
            fps_num = fps_den;
            fps_den = tmp;
        }
    }

    st->source = source;
    st->reader = reader;
    st->stream_index = selected;
    st->width = width;
    st->height = height;
    st->fps_num = fps_num;
    st->fps_den = fps_den;
    st->pixel = negotiated_pixel;

    if (out_info) {
        UINT64 hns64 = 0u;
        out_info->width = width;
        out_info->height = height;
        out_info->fps_numerator = fps_num;
        out_info->fps_denominator = fps_den;
        out_info->codec = ZCSR_VIDEO_CODEC_H264;
        out_info->pixel_format = negotiated_pixel;
        if (SUCCEEDED(pd->lpVtbl->GetUINT64(pd, &MF_PD_DURATION, &hns64)) &&
            hns64 > 0u) {
            out_info->duration_ms = (uint32_t)(hns64 / 10000u);
        }
        if (fps_den > 0u && fps_num > 0u && out_info->duration_ms > 0u) {
            uint64_t frames = ((uint64_t)out_info->duration_ms * (uint64_t)fps_num) /
                              ((uint64_t)fps_den * 1000u);
            out_info->frame_count = (frames > 0u) ? (uint32_t)frames : 1u;
        }
    }

    err = ZCSR_VIDEO_OK;

cleanup:
    MF_RELEASE(mt);
    MF_RELEASE(handler);
    MF_RELEASE(sd);
    MF_RELEASE(pd);
    MF_RELEASE(unk);
    MF_RELEASE(resolver);
    if (err != ZCSR_VIDEO_OK) {
        MF_RELEASE(reader);
        MF_RELEASE(source);
    }
    return err;
}

static zcsr_video_error mf_open(const char* path, void** out_state,
                                 zcsr_video_info* out_info) {
    mf_state* st = NULL;
    zcsr_video_error err;

    if (!path || !out_state || !out_info) {
        return ZCSR_VIDEO_ERR_INVALID_ARG;
    }
    *out_state = NULL;

    if (!mf_ensure_started()) {
        return ZCSR_VIDEO_ERR_UNSUPPORTED;
    }

    st = (mf_state*)calloc(1u, sizeof(*st));
    if (!st) {
        return ZCSR_VIDEO_ERR_NO_MEMORY;
    }
    (void)strncpy(st->path, path, sizeof(st->path) - 1u);

    err = mf_attach(path, st, out_info);
    if (err != ZCSR_VIDEO_OK) {
        free(st);
        return err;
    }
    st->next_frame_index = 0u;
    st->eof = false;
    *out_state = st;
    return ZCSR_VIDEO_OK;
}

/* ---- frame decode ---- */

static zcsr_video_error mf_read_frame(void* state, void* buffer, size_t capacity,
                                     zcsr_video_frame* out_frame) {
    mf_state* st = (mf_state*)state;
    IMFSample* sample = NULL;
    IMFMediaBuffer* mb = NULL;
    IMFMediaType* cur_mt = NULL;
    BYTE* pixels = NULL;
    DWORD max_len = 0u;
    DWORD cur_len = 0u;
    LONG stride = 0;
    uint32_t w, h, x, y;
    const BYTE* src;
    uint8_t* dst;
    HRESULT hr;
    DWORD flags = 0u;
    LONGLONG ts = 0i64;

    if (!st || !buffer || !out_frame) {
        return ZCSR_VIDEO_ERR_INVALID_ARG;
    }
    if (st->eof) {
        return ZCSR_VIDEO_ERR_EOF;
    }

    for (;;) {
        sample = NULL;
        flags = 0u;
        ts = 0i64;
        hr = st->reader->lpVtbl->ReadSample(st->reader, st->stream_index, 0u,
                                            NULL, &flags, &ts, &sample);
        if (FAILED(hr)) {
            return ZCSR_VIDEO_ERR_DECODE;
        }
        if ((flags & MF_SOURCE_READERF_ENDOFSTREAM) != 0u) {
            st->eof = true;
            MF_RELEASE(sample);
            return ZCSR_VIDEO_ERR_EOF;
        }
        if (sample != NULL) {
            break;
        }
        /* Dropped sample: read next. */
    }

    hr = sample->lpVtbl->ConvertToContiguousBuffer(sample, &mb);
    if (FAILED(hr)) {
        MF_RELEASE(sample);
        return ZCSR_VIDEO_ERR_DECODE;
    }

    hr = st->reader->lpVtbl->GetCurrentMediaType(st->reader, st->stream_index, &cur_mt);
    if (SUCCEEDED(hr)) {
        (void)attr_get_stride(cur_mt, &stride);
    }
    MF_RELEASE(cur_mt);

    hr = mb->lpVtbl->Lock(mb, &pixels, &max_len, &cur_len);
    if (FAILED(hr)) {
        MF_RELEASE(mb);
        MF_RELEASE(sample);
        return ZCSR_VIDEO_ERR_DECODE;
    }

    w = st->width;
    h = st->height;
    {
        LONG default_stride;
        uint64_t plane_lines;
        switch (st->pixel) {
            case ZCSR_VIDEO_PIXEL_YUY2: default_stride = (LONG)(w * 2u); break;
            case ZCSR_VIDEO_PIXEL_NV12: default_stride = (LONG)w; break;
            default:                     default_stride = (LONG)(w * 4u); break;
        }
        if (stride <= 0) {
            stride = default_stride;
        }
        /* Some decoders report a padded/unreliable DEFAULT_STRIDE that does
         * not match the actual buffer length. Derive the row stride from the
         * locked buffer when the advertised stride overshoots the plane. */
        plane_lines = (st->pixel == ZCSR_VIDEO_PIXEL_NV12) ? (uint64_t)h * 3u / 2u
                                                           : (uint64_t)h;
        if ((uint64_t)stride * plane_lines > (uint64_t)max_len || max_len == 0u) {
            stride = (LONG)(max_len / plane_lines);
        }
        {
            uint64_t src_bytes = (uint64_t)stride * plane_lines;
            if (src_bytes > (uint64_t)max_len ||
                capacity < (size_t)w * (size_t)h * 4u) {
                mb->lpVtbl->Unlock(mb);
                MF_RELEASE(mb);
                MF_RELEASE(sample);
                return ZCSR_VIDEO_ERR_INVALID_ARG;
            }
        }
    }

    dst = (uint8_t*)buffer;
    if (st->pixel == ZCSR_VIDEO_PIXEL_YUY2) {
        for (y = 0u; y < h; ++y) {
            const BYTE* row = pixels + (size_t)y * (size_t)stride;
            for (x = 0u; x < w; x += 2u) {
                int k = (int)x * 2;
                uint8_t r, g, b;
                yuv601_to_rgb(row[k], row[k + 1], row[k + 3], &r, &g, &b);
                dst[(size_t)y * w * 4u + x * 4u + 0u] = r;
                dst[(size_t)y * w * 4u + x * 4u + 1u] = g;
                dst[(size_t)y * w * 4u + x * 4u + 2u] = b;
                dst[(size_t)y * w * 4u + x * 4u + 3u] = 0xFFu;
                if (x + 1u < w) {
                    yuv601_to_rgb(row[k + 2], row[k + 1], row[k + 3], &r, &g, &b);
                    dst[(size_t)y * w * 4u + (x + 1u) * 4u + 0u] = r;
                    dst[(size_t)y * w * 4u + (x + 1u) * 4u + 1u] = g;
                    dst[(size_t)y * w * 4u + (x + 1u) * 4u + 2u] = b;
                    dst[(size_t)y * w * 4u + (x + 1u) * 4u + 3u] = 0xFFu;
                }
            }
        }
    } else if (st->pixel == ZCSR_VIDEO_PIXEL_NV12) {
        const BYTE* yplane = pixels;
        const BYTE* uvplane = pixels + (size_t)stride * (size_t)h;
        for (y = 0u; y < h; ++y) {
            uint32_t cy = y >> 1;
            for (x = 0u; x < w; ++x) {
                uint32_t cx = x >> 1;
                uint8_t r, g, b;
                int yy = yplane[(size_t)y * (size_t)stride + x];
                int cb = uvplane[(size_t)cy * (size_t)stride + cx * 2u];
                int cr = uvplane[(size_t)cy * (size_t)stride + cx * 2u + 1u];
                yuv601_to_rgb(yy, cb, cr, &r, &g, &b);
                dst[(size_t)y * w * 4u + x * 4u + 0u] = r;
                dst[(size_t)y * w * 4u + x * 4u + 1u] = g;
                dst[(size_t)y * w * 4u + x * 4u + 2u] = b;
                dst[(size_t)y * w * 4u + x * 4u + 3u] = 0xFFu;
            }
        }
    } else {
        for (y = 0u; y < h; ++y) {
            src = pixels + (size_t)y * (size_t)stride;
            for (x = 0u; x < w; ++x) {
                dst[(size_t)y * w * 4u + x * 4u + 0u] = src[x * 4u + 2u]; /* R */
                dst[(size_t)y * w * 4u + x * 4u + 1u] = src[x * 4u + 1u]; /* G */
                dst[(size_t)y * w * 4u + x * 4u + 2u] = src[x * 4u + 0u]; /* B */
                dst[(size_t)y * w * 4u + x * 4u + 3u] = 0xFFu;            /* A */
            }
        }
    }
    mb->lpVtbl->Unlock(mb);

    out_frame->width = w;
    out_frame->height = h;
    out_frame->frame_index = st->next_frame_index++;
    out_frame->time_ms = (ts > 0i64) ? (uint64_t)(ts / 10000i64) : 0u;
    out_frame->rgba = (const uint8_t*)buffer;

    MF_RELEASE(mb);
    MF_RELEASE(sample);
    return ZCSR_VIDEO_OK;
}

/* ---- seek ---- */

static zcsr_video_error mf_seek_ms(void* state, uint64_t ms) {
    mf_state* st = (mf_state*)state;
    PROPVARIANT var;
    HRESULT hr;
    zcsr_video_error err;

    if (!st) {
        return ZCSR_VIDEO_ERR_INVALID_ARG;
    }

    /* The SourceReader latches its end-of-stream flag and does not recover
     * after a source Start, so release and reopen the source/reader, then
     * Start at the target position on the fresh instance. */
    MF_RELEASE(st->reader);
    MF_RELEASE(st->source);
    err = mf_attach(st->path, st, NULL);
    if (err != ZCSR_VIDEO_OK) {
        return err;
    }

    PropVariantInit(&var);
    var.vt = VT_I8;
    var.hVal.QuadPart = (LONGLONG)ms * 10000i64;
    hr = st->source->lpVtbl->Start(st->source, NULL, NULL, &var);
    PropVariantClear(&var);
    if (FAILED(hr)) {
        return ZCSR_VIDEO_ERR_DECODE;
    }
    st->eof = false;
    if (st->fps_den > 0u && st->fps_num > 0u) {
        st->next_frame_index =
            (ms * (uint64_t)st->fps_num) / ((uint64_t)st->fps_den * 1000u);
    }
    return ZCSR_VIDEO_OK;
}

static zcsr_video_error mf_seek_frame(void* state, uint64_t frame_index) {
    mf_state* st = (mf_state*)state;
    zcsr_video_error err;
    uint64_t i;

    if (!st) {
        return ZCSR_VIDEO_ERR_INVALID_ARG;
    }
    if (st->fps_den == 0u || st->fps_num == 0u) {
        return ZCSR_VIDEO_ERR_UNSUPPORTED;
    }

    /* Reopen a fresh source (see mf_seek_ms for the EOF-latch reason), leave
     * it at time 0, and discard leading samples so the next decoded frame is
     * exactly the requested index. */
    MF_RELEASE(st->reader);
    MF_RELEASE(st->source);
    err = mf_attach(st->path, st, NULL);
    if (err != ZCSR_VIDEO_OK) {
        return err;
    }
    st->eof = false;
    for (i = 0u; i < frame_index; ++i) {
        IMFSample* sample = NULL;
        DWORD flags = 0u;
        LONGLONG ts = 0i64;
        HRESULT hr = st->reader->lpVtbl->ReadSample(st->reader, st->stream_index,
                                                    0u, NULL, &flags, &ts, &sample);
        if (FAILED(hr)) {
            return ZCSR_VIDEO_ERR_DECODE;
        }
        if (sample) {
            sample->lpVtbl->Release(sample);
        }
        if ((flags & MF_SOURCE_READERF_ENDOFSTREAM) != 0u) {
            return ZCSR_VIDEO_ERR_OUT_OF_RANGE;
        }
    }
    st->next_frame_index = frame_index;
    return ZCSR_VIDEO_OK;
}

static void mf_close(void* state) {
    mf_state* st = (mf_state*)state;
    if (!st) {
        return;
    }
    MF_RELEASE(st->reader);
    MF_RELEASE(st->source);
    free(st);
}

const zcsr_video_backend_ops zcsr_video_backend_mf = {
    mf_open,
    mf_read_frame,
    mf_seek_ms,
    mf_seek_frame,
    mf_close,
    mf_caps,
};

#else
/* Non-Windows: the MF backend TU is inactive; the FFmpeg backend owns the
 * dispatch. Keep this TU non-empty to avoid the empty-translation-unit warning. */
typedef int zcsr_video_mf_backend_empty_tu;
#endif /* defined(_WIN32) */
