/* video_internal.h — video backend dispatch table (not part of the public contract).
 *
 * Each platform backend defines one static const zcsr_video_backend_ops and
 * exports it under a backend-specific symbol; the facade in video.c routes
 * every public call through the backend that successfully opened the stream.
 * Backend state is an opaque void* owned by the backend.
 *
 * Backend selection (host order, #if only picks candidates, never replaces a
 * decode implementation):
 *  - Windows: Media Foundation (`zcsr_video_backend_mf`) is tried first for
 *    MP4/H.264; the FFmpeg backend (`zcsr_video_backend_ff`) is the fallback
 *    and the primary path for WebM/VP9.
 *  - Other hosts: FFmpeg only.
 */
#ifndef ZCSR_VIDEO_INTERNAL_H
#define ZCSR_VIDEO_INTERNAL_H

#include "zcsr/video.h"

typedef struct zcsr_video_backend_ops {
    /* Open `path`, allocate backend state into *out_state, and fill out_info
     * (dimensions, codec, pixel format, rate, duration). Container family is
     * filled by the facade from magic-byte sniffing. */
    zcsr_video_error (*open)(const char* path, void** out_state,
                             zcsr_video_info* out_info);

    /* Decode the next frame into the caller buffer; fill out_frame. */
    zcsr_video_error (*read_frame)(void* state, void* buffer, size_t capacity,
                                   zcsr_video_frame* out_frame);

    zcsr_video_error (*seek_ms)(void* state, uint64_t ms);
    zcsr_video_error (*seek_frame)(void* state, uint64_t frame_index);

    /* Release all backend state; the handle itself is freed by the facade. */
    void (*close)(void* state);

    /* Fill platform capability snapshots; either pointer may be NULL. */
    void (*caps)(zcsr_video_container_caps* containers,
                 zcsr_video_codec_caps* codecs);
} zcsr_video_backend_ops;

/* FFmpeg backend: compiled on every host that links the FFmpeg prebuilt. When
 * ZCSR_VIDEO_HAVE_FFMPEG is not defined (dependency absent), this TU exports
 * an honest all-false backend that reports UNSUPPORTED on every open. */
extern const zcsr_video_backend_ops zcsr_video_backend_ff;

#if defined(_WIN32)
/* Media Foundation backend, Windows only. */
extern const zcsr_video_backend_ops zcsr_video_backend_mf;
#endif

/* Ordered backend candidates for this host; the facade tries them in order on
 * open and keeps the first that succeeds. *out_count receives the number of
 * entries; the returned array is static and never NULL. */
const zcsr_video_backend_ops* const* zcsr_video_backend_list(size_t* out_count);

/* Magic-byte container sniff shared by the facade and the tests. Reads the
 * leading bytes of the file; returns UNKNOWN when the file cannot be read or
 * matches no known family. */
zcsr_video_container zcsr_video_sniff_container(const char* path);

#endif /* ZCSR_VIDEO_INTERNAL_H */
