#ifndef ZCSR_VIDEO_H
#define ZCSR_VIDEO_H
/* Video stream opening, probing and frame decoding to 8-bit RGBA (RGBA32).
 *
 * SEPARATED from rendering: this module ONLY produces pixel buffers, the video
 * counterpart of `image`. It is offline/stream decode, NOT a runtime player and
 * NOT a fallback when a GPU path is unavailable. The renderer consumes produced
 * bytes through the texture upload path; `video` never includes a renderer.
 *
 * Contract rules:
 *  - Pure C11. This header exposes NO platform types (no COM pointers, no
 *    Windows handles, no MF/WIC/FFmpeg types). Platform backends live in
 *    modules/video and are hidden behind the opaque zcsr_video handle.
 *  - Honest failure: every entry point returns an explicit error code. A
 *    container the backend cannot demux, or a codec the platform has no
 *    decoder for, is reported as ZCSR_VIDEO_ERR_UNSUPPORTED / NOT_VIDEO; the
 *    contract never returns a "success" with an empty or zeroed frame.
 *
 * Memory contract: output pixels are caller-owned. Backends may hold scratch
 * memory (decoder context, frames, scaling context) behind the opaque handle;
 * no retained per-frame heap grows with frame count, and no allocation happens
 * per frame on the output path. read_frame() writes RGBA8 into a caller-provided
 * buffer.
 *
 * T0 container/codec matrix (frozen):
 *  - containers: MP4 (ISO BMFF), WebM (EBML WebM profile)
 *  - codecs:     H.264 (AVC), VP9
 * Which ones actually decode on the current host is reported by
 * zcsr_video_platform_caps() and enforced at open.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque handle to one opened video stream. */
typedef struct zcsr_video zcsr_video;

/* Container family, detected from the file content (magic bytes), not from
 * the file extension. */
typedef enum {
    ZCSR_VIDEO_CONTAINER_UNKNOWN = 0,
    ZCSR_VIDEO_CONTAINER_MP4,      /* ISO BMFF: .mp4 / .mov / .m4v */
    ZCSR_VIDEO_CONTAINER_WEBM,     /* EBML + WebM profile */
} zcsr_video_container;

/* Video codec reported by the opened stream. */
typedef enum {
    ZCSR_VIDEO_CODEC_UNKNOWN = 0,
    ZCSR_VIDEO_CODEC_H264,
    ZCSR_VIDEO_CODEC_VP9,
} zcsr_video_codec;

/* Source pixel format of the decoded stream. read_frame() always converts to
 * RGBA8; this enum reports the source layout for diagnostics only. */
typedef enum {
    ZCSR_VIDEO_PIXEL_UNKNOWN = 0,
    ZCSR_VIDEO_PIXEL_NV12,
    ZCSR_VIDEO_PIXEL_YUY2,
    ZCSR_VIDEO_PIXEL_BGRA,
    ZCSR_VIDEO_PIXEL_RGBA,
} zcsr_video_pixel_format;

/* Explicit failure reason. There is no fuzzy "always succeeds" path. */
typedef enum {
    ZCSR_VIDEO_OK = 0,
    ZCSR_VIDEO_ERR_INVALID_ARG,   /* NULL handle/buffer, contradictory parameters */
    ZCSR_VIDEO_ERR_NOT_FOUND,    /* file does not exist or cannot be opened */
    ZCSR_VIDEO_ERR_NOT_VIDEO,    /* bytes do not match any recognized container */
    ZCSR_VIDEO_ERR_UNSUPPORTED,  /* container recognized, but codec/pixel layout
                                   * has no decoder on this platform */
    ZCSR_VIDEO_ERR_DECODE,        /* demuxed, but frame decoding failed mid-stream */
    ZCSR_VIDEO_ERR_EOF,           /* no further frame available */
    ZCSR_VIDEO_ERR_OUT_OF_RANGE,  /* seek target outside the stream bounds */
    ZCSR_VIDEO_ERR_NO_MEMORY,     /* backend allocation failed */
} zcsr_video_error;

/* Stream metadata. Unknown fields stay zero; a zero-initialized struct is
 * never reported as a valid stream. */
typedef struct {
    uint32_t width;                /* pixels, decoded frame width */
    uint32_t height;               /* pixels, decoded frame height */
    uint32_t frame_count;          /* total frames; 0 when the container does not state it */
    uint32_t duration_ms;          /* stream duration; 0 when unknown */
    uint32_t fps_numerator;        /* frame rate = fps_numerator / fps_denominator; */
    uint32_t fps_denominator;      /* both zero when the rate is unknown */
    zcsr_video_container container;
    zcsr_video_codec codec;
    zcsr_video_pixel_format pixel_format;
} zcsr_video_info;

/* What the current platform backend can attempt to demux. These flags describe
 * platform-level reachability; a specific file may still fail at open when its
 * codec is missing. */
typedef struct {
    bool mp4;
    bool webm;
} zcsr_video_container_caps;

/* What decoders the current platform actually has installed. Populated from a
 * real backend query (registered decoder MFTs / FFmpeg decoder lookup); a host
 * without a video backend reports every flag false. */
typedef struct {
    bool h264;
    bool vp9;
} zcsr_video_codec_caps;

/* Snapshot of this host's video decode reachability. Never claims a decoder
 * that is not installed. `containers` / `codecs` may be NULL. */
void zcsr_video_platform_caps(zcsr_video_container_caps* containers,
                               zcsr_video_codec_caps* codecs);

/* Open a video file by UTF-8 path. On success returns an owned handle and
 * fills *out_info (when non-NULL). On failure returns NULL and writes the
 * detailed reason to *out_error (when non-NULL); the reason is never
 * ZCSR_VIDEO_OK for a NULL handle. */
zcsr_video* zcsr_video_open(const char* path, zcsr_video_error* out_error);

/* Copy stream metadata into out_info. Returns ZCSR_VIDEO_ERR_INVALID_ARG for
 * NULL arguments; does not change decode state. */
zcsr_video_error zcsr_video_get_info(const zcsr_video* video,
                                     zcsr_video_info* out_info);

/* Bytes required for one decoded RGBA8 frame: width * height * 4, with an
 * overflow guard. Returns 0 for an invalid handle or a stream that cannot
 * report dimensions. */
size_t zcsr_video_frame_bytes(const zcsr_video* video);

/* One decoded frame: tightly packed RGBA8, top-left origin, row stride =
 * width * 4, opaque (alpha = 255). `rgba` points inside the caller buffer;
 * the caller does not free it. */
typedef struct {
    uint32_t width;
    uint32_t height;
    uint64_t frame_index;  /* 0-based index of this frame in presentation order */
    uint64_t time_ms;      /* presentation timestamp in milliseconds */
    const uint8_t* rgba;   /* width * height * 4 bytes, caller-buffer-owned */
} zcsr_video_frame;

/* Read the next frame in presentation order. `buffer`/`capacity` must hold at
 * least zcsr_video_frame_bytes(video) bytes. Returns:
 *  - ZCSR_VIDEO_OK with *out_frame filled on a decoded frame;
 *  - ZCSR_VIDEO_ERR_EOF when the stream has no further frame;
 *  - an explicit error code otherwise. Does not allocate the output frame. */
zcsr_video_error zcsr_video_read_frame(zcsr_video* video,
                                       void* buffer, size_t capacity,
                                       zcsr_video_frame* out_frame);

/* Seek to the presentation time closest to `ms` from the start. Targets past
 * the duration clamp to the last frame; a negative-style underflow is rejected
 * as out of range. The next read_frame() returns the frame at/after the seek
 * point. */
zcsr_video_error zcsr_video_seek_ms(zcsr_video* video, uint64_t ms);

/* Seek to a 0-based frame index. Out-of-range indices return
 * ZCSR_VIDEO_ERR_OUT_OF_RANGE and leave the stream position unchanged. */
zcsr_video_error zcsr_video_seek_frame(zcsr_video* video, uint64_t frame_index);

/* Close the stream and release backend state. NULL is ignored. */
void zcsr_video_close(zcsr_video* video);

/* Last detailed error recorded for this instance. NULL handle maps to
 * ZCSR_VIDEO_ERR_INVALID_ARG. Does not alter state. */
zcsr_video_error zcsr_video_last_error(const zcsr_video* video);

/* Static ASCII human-readable name for an error code (never NULL). */
const char* zcsr_video_error_string(zcsr_video_error error);

#ifdef __cplusplus
}
#endif

#endif /* ZCSR_VIDEO_H */
