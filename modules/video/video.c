/* zcsr video module — facade over the platform video backends.
 *
 * This TU owns the opaque handle, argument validation, frame-buffer sizing,
 * magic-byte container sniffing and the error-code table. Actual demux/decode
 * lives in the active backend TUs:
 *  - video_mf.c:    Windows Media Foundation (MP4/H.264 primary on Windows)
 *  - video_ff.c:    FFmpeg (H.264/VP9 on Linux; fallback + WebM/VP9 on Windows)
 *
 * Backend selection is a host-ordered candidate list (see video_internal.h);
 * `#if` only decides which candidates exist, never substitutes a decode
 * implementation. On open the facade tries candidates in order and keeps the
 * first that succeeds; capability flags are the OR of every candidate.
 *
 * Zero-coupling: this TU includes only the public zcsr/video.h contract and
 * the module-local internal dispatch header.
 */
#include "zcsr/video.h"
#include "video_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct zcsr_video {
    const zcsr_video_backend_ops* backend;
    void*                     state;
    zcsr_video_error         last_error;
    zcsr_video_info           info;
};

/* ---- ordered backend candidates ---- */

#if defined(_WIN32)
static const zcsr_video_backend_ops* const zcsr_video_candidates[] = {
    &zcsr_video_backend_mf,
    &zcsr_video_backend_ff,
};
#else
static const zcsr_video_backend_ops* const zcsr_video_candidates[] = {
    &zcsr_video_backend_ff,
};
#endif

const zcsr_video_backend_ops* const* zcsr_video_backend_list(size_t* out_count) {
    if (out_count) {
        *out_count = sizeof(zcsr_video_candidates) / sizeof(zcsr_video_candidates[0]);
    }
    return zcsr_video_candidates;
}

/* ---- magic-byte container sniffing ---- */

static zcsr_video_container sniff_bytes(const uint8_t* b, size_t n) {
    if (n >= 8u && memcmp(b + 4u, "ftyp", 4u) == 0) {
        return ZCSR_VIDEO_CONTAINER_MP4;
    }
    if (n >= 4u && b[0] == 0x1Au && b[1] == 0x45u && b[2] == 0xDFu && b[3] == 0xA3u) {
        /* EBML: T0 accepts the WebM profile only; Matroska is out of scope. */
        return ZCSR_VIDEO_CONTAINER_WEBM;
    }
    return ZCSR_VIDEO_CONTAINER_UNKNOWN;
}

zcsr_video_container zcsr_video_sniff_container(const char* path) {
    uint8_t header[192];
    size_t  n = 0u;
    FILE* f;
    if (!path || !*path) {
        return ZCSR_VIDEO_CONTAINER_UNKNOWN;
    }
    f = fopen(path, "rb");
    if (!f) {
        return ZCSR_VIDEO_CONTAINER_UNKNOWN;
    }
    n = fread(header, 1u, sizeof(header), f);
    fclose(f);
    return sniff_bytes(header, n);
}

/* ---- internal helpers ---- */

static zcsr_video_error set_err(zcsr_video* video, zcsr_video_error err) {
    if (video) {
        video->last_error = err;
    }
    return err;
}

/* ---- public contract ---- */

void zcsr_video_platform_caps(zcsr_video_container_caps* containers,
                              zcsr_video_codec_caps* codecs) {
    size_t count, i;
    const zcsr_video_backend_ops* const* list = zcsr_video_backend_list(&count);

    if (containers) {
        memset(containers, 0, sizeof(*containers));
    }
    if (codecs) {
        memset(codecs, 0, sizeof(*codecs));
    }
    for (i = 0u; i < count; ++i) {
        if (list[i]->caps) {
            zcsr_video_container_caps cc;
            zcsr_video_codec_caps kc;
            memset(&cc, 0, sizeof(cc));
            memset(&kc, 0, sizeof(kc));
            list[i]->caps(&cc, &kc);
            if (containers) {
                containers->mp4  = containers->mp4  || cc.mp4;
                containers->webm = containers->webm || cc.webm;
            }
            if (codecs) {
                codecs->h264 = codecs->h264 || kc.h264;
                codecs->vp9  = codecs->vp9  || kc.vp9;
            }
        }
    }
}

zcsr_video* zcsr_video_open(const char* path, zcsr_video_error* out_error) {
    zcsr_video_error err = ZCSR_VIDEO_OK;
    zcsr_video_error last = ZCSR_VIDEO_ERR_UNSUPPORTED;
    zcsr_video* video;
    zcsr_video_container container;
    zcsr_video_info info;
    void* state = NULL;
    size_t count, i;
    const zcsr_video_backend_ops* const* list;
    FILE* f;

    if (!path || !*path) {
        err = ZCSR_VIDEO_ERR_INVALID_ARG;
        goto fail;
    }

    f = fopen(path, "rb");
    if (!f) {
        err = ZCSR_VIDEO_ERR_NOT_FOUND;
        goto fail;
    }
    fclose(f);

    container = zcsr_video_sniff_container(path);
    if (container == ZCSR_VIDEO_CONTAINER_UNKNOWN) {
        err = ZCSR_VIDEO_ERR_NOT_VIDEO;
        goto fail;
    }

    list = zcsr_video_backend_list(&count);
    video = (zcsr_video*)calloc(1u, sizeof(*video));
    if (!video) {
        err = ZCSR_VIDEO_ERR_NO_MEMORY;
        goto fail;
    }

    for (i = 0u; i < count; ++i) {
        memset(&info, 0, sizeof(info));
        state = NULL;
        err = list[i]->open(path, &state, &info);
        if (err == ZCSR_VIDEO_OK) {
            video->backend = list[i];
            video->state = state;
            video->last_error = ZCSR_VIDEO_OK;
            info.container = container;
            video->info = info;
            if (out_error) {
                *out_error = ZCSR_VIDEO_OK;
            }
            return video;
        }
        last = err;
        /* Backend refused this file: try the next candidate (e.g. MF fails
         * on WebM/VP9, FFmpeg fallback takes over). */
    }

    free(video);
    err = last;

fail:
    if (out_error) {
        *out_error = err;
    }
    return NULL;
}

zcsr_video_error zcsr_video_get_info(const zcsr_video* video,
                                      zcsr_video_info* out_info) {
    if (!video || !out_info) {
        return ZCSR_VIDEO_ERR_INVALID_ARG;
    }
    *out_info = video->info;
    return ZCSR_VIDEO_OK;
}

size_t zcsr_video_frame_bytes(const zcsr_video* video) {
    size_t pixels;
    if (!video || video->info.width == 0u || video->info.height == 0u) {
        return 0u;
    }
    pixels = (size_t)video->info.width * (size_t)video->info.height;
    if (pixels > SIZE_MAX / 4u) {
        return 0u;
    }
    return pixels * 4u;
}

zcsr_video_error zcsr_video_read_frame(zcsr_video* video,
                                       void* buffer, size_t capacity,
                                       zcsr_video_frame* out_frame) {
    size_t needed;
    zcsr_video_error err;

    if (!video || !buffer || !out_frame) {
        return set_err(video, ZCSR_VIDEO_ERR_INVALID_ARG);
    }
    needed = zcsr_video_frame_bytes(video);
    if (needed == 0u || capacity < needed) {
        return set_err(video, ZCSR_VIDEO_ERR_INVALID_ARG);
    }
    err = video->backend->read_frame(video->state, buffer, capacity, out_frame);
    return set_err(video, err);
}

zcsr_video_error zcsr_video_seek_ms(zcsr_video* video, uint64_t ms) {
    zcsr_video_error err;
    if (!video) {
        return ZCSR_VIDEO_ERR_INVALID_ARG;
    }
    if (video->info.duration_ms > 0u && ms > video->info.duration_ms) {
        ms = video->info.duration_ms; /* clamp past-duration targets to the last frame */
    }
    err = video->backend->seek_ms(video->state, ms);
    return set_err(video, err);
}

zcsr_video_error zcsr_video_seek_frame(zcsr_video* video, uint64_t frame_index) {
    zcsr_video_error err;
    if (!video) {
        return ZCSR_VIDEO_ERR_INVALID_ARG;
    }
    if (video->info.frame_count > 0u && frame_index >= video->info.frame_count) {
        return set_err(video, ZCSR_VIDEO_ERR_OUT_OF_RANGE);
    }
    err = video->backend->seek_frame(video->state, frame_index);
    return set_err(video, err);
}

void zcsr_video_close(zcsr_video* video) {
    if (!video) {
        return;
    }
    if (video->backend->close) {
        video->backend->close(video->state);
    }
    free(video);
}

zcsr_video_error zcsr_video_last_error(const zcsr_video* video) {
    if (!video) {
        return ZCSR_VIDEO_ERR_INVALID_ARG;
    }
    return video->last_error;
}

const char* zcsr_video_error_string(zcsr_video_error error) {
    switch (error) {
        case ZCSR_VIDEO_OK:              return "ok";
        case ZCSR_VIDEO_ERR_INVALID_ARG: return "invalid argument";
        case ZCSR_VIDEO_ERR_NOT_FOUND:   return "file not found";
        case ZCSR_VIDEO_ERR_NOT_VIDEO:   return "not a recognized video container";
        case ZCSR_VIDEO_ERR_UNSUPPORTED: return "container/codec unsupported on this platform";
        case ZCSR_VIDEO_ERR_DECODE:      return "decode failure";
        case ZCSR_VIDEO_ERR_EOF:         return "end of stream";
        case ZCSR_VIDEO_ERR_OUT_OF_RANGE: return "seek target out of range";
        case ZCSR_VIDEO_ERR_NO_MEMORY:   return "out of memory";
    }
    return "unknown error";
}
