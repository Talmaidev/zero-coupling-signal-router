/* video_ff.c — FFmpeg backend: avformat demux + avcodec decode + swscale RGBA8.
 *
 * Compiled on every host that links the FFmpeg prebuilt. When
 * ZCSR_VIDEO_HAVE_FFMPEG is not defined (dependency absent), this TU exports
 * an honest all-false backend: open reports UNSUPPORTED and every capability
 * flag stays false — it never fakes success.
 *
 * FFmpeg types stay inside this TU; the public contract sees only the opaque
 * zcsr_video handle. Demuxer/decoder/scaler contexts are held behind the
 * handle; the per-frame output buffer belongs to the caller. No allocation
 * happens per frame on the output path (the scaler context is created once).
 */
#include "zcsr/video.h"
#include "video_internal.h"

#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#if defined(ZCSR_VIDEO_HAVE_FFMPEG)

#define __STDC_CONSTANT_MACROS

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4828 4201 4100 4204 4221 4244 4267)
#endif

#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>

#if defined(_MSC_VER)
#pragma warning(pop)
#endif

typedef struct {
    char                path[512];
    AVFormatContext*    fmt;
    AVCodecContext*     avctx;
    AVFrame*            frame;
    AVPacket*           pkt;
    struct SwsContext*  sws;
    int                 stream_idx;
    uint32_t            width;
    uint32_t            height;
    uint32_t            fps_num;
    uint32_t            fps_den;
    uint32_t            frame_count;
    uint32_t            duration_ms;
    zcsr_video_codec    codec;
    uint64_t            next_index;
    bool                eof;
} ff_state;

/* ---- helpers ---- */

static zcsr_video_codec map_codec_id(enum AVCodecID id) {
    switch (id) {
        case AV_CODEC_ID_H264: return ZCSR_VIDEO_CODEC_H264;
        case AV_CODEC_ID_VP9:  return ZCSR_VIDEO_CODEC_VP9;
        default:               return ZCSR_VIDEO_CODEC_UNKNOWN;
    }
}

static zcsr_video_error map_open_err(int ret) {
    if (ret == AVERROR(ENOENT)) {
        return ZCSR_VIDEO_ERR_NOT_FOUND;
    }
    if (ret == AVERROR(EINVAL) || ret == AVERROR(EILSEQ) || ret == AVERROR(EPROTONOSUPPORT)) {
        return ZCSR_VIDEO_ERR_NOT_VIDEO;
    }
    return ZCSR_VIDEO_ERR_UNSUPPORTED;
}

static void ff_free_state(ff_state* s) {
    if (!s) {
        return;
    }
    if (s->sws) {
        sws_freeContext(s->sws);
        s->sws = NULL;
    }
    if (s->frame) {
        av_frame_free(&s->frame);
    }
    if (s->pkt) {
        av_packet_free(&s->pkt);
    }
    if (s->avctx) {
        avcodec_free_context(&s->avctx);
    }
    if (s->fmt) {
        avformat_close_input(&s->fmt);
    }
    free(s);
}

/* Pull the next decoded frame. Returns 0 and stores the frame, AVERROR_EOF
 * at end of stream, or another negative FFmpeg error code. */
static int ff_pull(ff_state* s, AVFrame** out) {
    for (;;) {
        int ret;

        if (!s->eof) {
            ret = av_read_frame(s->fmt, s->pkt);
            if (ret == AVERROR_EOF) {
                s->eof = true;
                /* Flush the decoder: send a NULL packet to drain queued frames. */
                (void)avcodec_send_packet(s->avctx, NULL);
            } else if (ret < 0) {
                return ret;
            } else {
                if (s->pkt->stream_index != s->stream_idx) {
                    av_packet_unref(s->pkt);
                    continue;
                }
                ret = avcodec_send_packet(s->avctx, s->pkt);
                av_packet_unref(s->pkt);
                if (ret < 0 && ret != AVERROR(EAGAIN)) {
                    return ret;
                }
            }
        }

        ret = avcodec_receive_frame(s->avctx, s->frame);
        if (ret == 0) {
            *out = s->frame;
            return 0;
        }
        if (ret == AVERROR_EOF) {
            return AVERROR_EOF;
        }
        if (ret != AVERROR(EAGAIN)) {
            return ret;
        }
        /* EAGAIN: feed more packets. */
    }
}

/* ---- backend ops ---- */

static zcsr_video_error ff_open(const char* path, void** out_state,
                                 zcsr_video_info* out_info) {
    ff_state* s = NULL;
    const AVCodec* decoder = NULL;
    AVStream* st;
    int ret;
    int stream_idx;
    AVRational fps;

    if (!path || !out_state || !out_info) {
        return ZCSR_VIDEO_ERR_INVALID_ARG;
    }
    *out_state = NULL;

    s = (ff_state*)calloc(1u, sizeof(*s));
    if (!s) {
        return ZCSR_VIDEO_ERR_NO_MEMORY;
    }
    strncpy(s->path, path, sizeof(s->path) - 1u);

    ret = avformat_open_input(&s->fmt, path, NULL, NULL);
    if (ret < 0) {
        goto fail;
    }
    ret = avformat_find_stream_info(s->fmt, NULL);
    if (ret < 0) {
        goto fail;
    }

    stream_idx = av_find_best_stream(s->fmt, AVMEDIA_TYPE_VIDEO, -1, -1, &decoder, 0);
    if (stream_idx < 0) {
        ret = stream_idx;
        goto fail;
    }
    s->stream_idx = stream_idx;
    st = s->fmt->streams[stream_idx];

    s->codec = map_codec_id(st->codecpar->codec_id);
    if (s->codec == ZCSR_VIDEO_CODEC_UNKNOWN) {
        ret = AVERROR(EINVAL);
        goto fail;
    }

    s->avctx = avcodec_alloc_context3(decoder);
    if (!s->avctx) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }
    ret = avcodec_parameters_to_context(s->avctx, st->codecpar);
    if (ret < 0) {
        goto fail;
    }
    ret = avcodec_open2(s->avctx, decoder, NULL);
    if (ret < 0) {
        goto fail;
    }

    s->width  = (uint32_t)s->avctx->width;
    s->height = (uint32_t)s->avctx->height;
    if (s->width == 0u || s->height == 0u) {
        ret = AVERROR(EINVAL);
        goto fail;
    }

    fps = st->avg_frame_rate;
    if (fps.den <= 0 || fps.num <= 0) {
        fps = st->r_frame_rate;
    }
    /* Some MP4 muxers expose the frame duration (seconds/frame) instead of the
     * frame rate; normalize an inverted ratio back to frames/second. */
    if (fps.num > 0 && fps.den > 0 && fps.num < fps.den) {
        uint32_t inverted = (uint32_t)fps.den / (uint32_t)fps.num;
        if (inverted >= 1u && inverted <= 240u) {
            uint32_t tmp = (uint32_t)fps.num;
            fps.num = fps.den;
            fps.den = tmp;
        }
    }
    s->fps_num = (uint32_t)fps.num;
    s->fps_den = (uint32_t)fps.den;

    if (st->duration != AV_NOPTS_VALUE && st->time_base.den > 0) {
        s->duration_ms = (uint32_t)av_rescale_q(st->duration, st->time_base,
                                                (AVRational){ 1, 1000 });
    } else if (s->fmt->duration != AV_NOPTS_VALUE) {
        /* AVFormatContext.duration is in AV_TIME_BASE microseconds. */
        s->duration_ms = (uint32_t)(s->fmt->duration / 1000);
    }

    if (st->nb_frames > 0) {
        s->frame_count = (uint32_t)st->nb_frames;
    } else if (s->fps_den > 0u && s->fps_num > 0u && s->duration_ms > 0u) {
        s->frame_count = (uint32_t)((uint64_t)s->duration_ms * s->fps_num /
                                    ((uint64_t)s->fps_den * 1000u));
    }

    s->frame = av_frame_alloc();
    s->pkt = av_packet_alloc();
    if (!s->frame || !s->pkt) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    out_info->width = s->width;
    out_info->height = s->height;
    out_info->frame_count = s->frame_count;
    out_info->duration_ms = s->duration_ms;
    out_info->fps_numerator = s->fps_num;
    out_info->fps_denominator = s->fps_den;
    out_info->codec = s->codec;
    out_info->pixel_format = ZCSR_VIDEO_PIXEL_UNKNOWN;

    *out_state = s;
    return ZCSR_VIDEO_OK;

fail:
    ff_free_state(s);
    return map_open_err(ret);
}

static zcsr_video_error ff_read_frame(void* state, void* buffer, size_t capacity,
                                      zcsr_video_frame* out_frame) {
    ff_state* s = (ff_state*)state;
    AVFrame* src = NULL;
    const uint8_t* src_slice[4] = { NULL, NULL, NULL, NULL };
    uint8_t* dst[4] = { NULL, NULL, NULL, NULL };
    int dst_stride[4] = { 0, 0, 0, 0 };
    int ret;
    int plane;
    AVStream* st;

    if (!s || !buffer || !out_frame) {
        return ZCSR_VIDEO_ERR_INVALID_ARG;
    }
    (void)capacity;

    ret = ff_pull(s, &src);
    if (ret == AVERROR_EOF) {
        return ZCSR_VIDEO_ERR_EOF;
    }
    if (ret < 0) {
        return ZCSR_VIDEO_ERR_DECODE;
    }

    if (!s->sws) {
        s->sws = sws_getContext((int)s->width, (int)s->height, src->format,
                                (int)s->width, (int)s->height, AV_PIX_FMT_RGBA,
                                SWS_BILINEAR, NULL, NULL, NULL);
        if (!s->sws) {
            return ZCSR_VIDEO_ERR_DECODE;
        }
    }

    dst[0] = (uint8_t*)buffer;
    dst_stride[0] = (int)(s->width * 4u);
    for (plane = 0; plane < 4; ++plane) {
        src_slice[plane] = src->data[plane];
    }
    if (sws_scale(s->sws, src_slice, src->linesize, 0, (int)s->height,
                  dst, dst_stride) < 0) {
        return ZCSR_VIDEO_ERR_DECODE;
    }

    out_frame->width = s->width;
    out_frame->height = s->height;
    out_frame->frame_index = s->next_index++;
    if (src->pts != AV_NOPTS_VALUE) {
        st = s->fmt->streams[s->stream_idx];
        out_frame->time_ms = (uint64_t)av_rescale_q(src->pts, st->time_base,
                                                    (AVRational){ 1, 1000 });
    } else if (s->fps_den > 0u && s->fps_num > 0u) {
        out_frame->time_ms = out_frame->frame_index * 1000u * s->fps_den / s->fps_num;
    } else {
        out_frame->time_ms = 0u;
    }
    out_frame->rgba = (const uint8_t*)buffer;
    return ZCSR_VIDEO_OK;
}

static zcsr_video_error ff_seek_ms(void* state, uint64_t ms) {
    ff_state* s = (ff_state*)state;
    AVStream* st;
    int64_t ts;
    int ret;

    if (!s) {
        return ZCSR_VIDEO_ERR_INVALID_ARG;
    }
    st = s->fmt->streams[s->stream_idx];
    ts = av_rescale_q((int64_t)ms, (AVRational){ 1, 1000 }, st->time_base);
    ret = avformat_seek_file(s->fmt, s->stream_idx, ts, ts, ts, AVSEEK_FLAG_BACKWARD);
    if (ret < 0) {
        return ZCSR_VIDEO_ERR_DECODE;
    }
    avcodec_flush_buffers(s->avctx);
    s->eof = false;
    if (s->fps_den > 0u && s->fps_num > 0u) {
        s->next_index = (uint64_t)ms * s->fps_num / ((uint64_t)s->fps_den * 1000u);
    } else {
        s->next_index = 0u;
    }
    return ZCSR_VIDEO_OK;
}

static zcsr_video_error ff_seek_frame(void* state, uint64_t frame_index) {
    ff_state* s = (ff_state*)state;
    AVFrame* drop = NULL;
    uint64_t i;
    int ret;

    if (!s) {
        return ZCSR_VIDEO_ERR_INVALID_ARG;
    }

    /* Seek back to the start, flush, then discard leading frames so the next
     * decoded frame is exactly the requested index. */
    ret = av_seek_frame(s->fmt, s->stream_idx, 0, AVSEEK_FLAG_BACKWARD);
    if (ret < 0) {
        return ZCSR_VIDEO_ERR_DECODE;
    }
    avcodec_flush_buffers(s->avctx);
    s->eof = false;

    for (i = 0u; i < frame_index; ++i) {
        ret = ff_pull(s, &drop);
        if (ret == AVERROR_EOF) {
            return ZCSR_VIDEO_ERR_OUT_OF_RANGE;
        }
        if (ret < 0) {
            return ZCSR_VIDEO_ERR_DECODE;
        }
    }
    s->next_index = frame_index;
    return ZCSR_VIDEO_OK;
}

static void ff_close(void* state) {
    ff_free_state((ff_state*)state);
}

static void ff_caps(zcsr_video_container_caps* containers,
                    zcsr_video_codec_caps* codecs) {
    if (containers) {
        containers->mp4  = av_find_input_format("mov,mp4,m4a,3gp,3g2,mj2") != NULL;
        containers->webm = av_find_input_format("webm") != NULL;
    }
    if (codecs) {
        codecs->h264 = avcodec_find_decoder(AV_CODEC_ID_H264) != NULL;
        codecs->vp9  = avcodec_find_decoder(AV_CODEC_ID_VP9) != NULL;
    }
}

const zcsr_video_backend_ops zcsr_video_backend_ff = {
    ff_open,
    ff_read_frame,
    ff_seek_ms,
    ff_seek_frame,
    ff_close,
    ff_caps,
};

#else /* !ZCSR_VIDEO_HAVE_FFMPEG — honest absence, never a fake success */

static zcsr_video_error ff_unsupported_open(const char* path, void** out_state,
                                            zcsr_video_info* out_info) {
    (void)path;
    (void)out_state;
    (void)out_info;
    return ZCSR_VIDEO_ERR_UNSUPPORTED;
}

static zcsr_video_error ff_unsupported_frame(void* state, void* buffer, size_t capacity,
                                             zcsr_video_frame* out_frame) {
    (void)state;
    (void)buffer;
    (void)capacity;
    (void)out_frame;
    return ZCSR_VIDEO_ERR_UNSUPPORTED;
}

static zcsr_video_error ff_unsupported_seek_ms(void* state, uint64_t ms) {
    (void)state;
    (void)ms;
    return ZCSR_VIDEO_ERR_UNSUPPORTED;
}

static zcsr_video_error ff_unsupported_seek_frame(void* state, uint64_t frame_index) {
    (void)state;
    (void)frame_index;
    return ZCSR_VIDEO_ERR_UNSUPPORTED;
}

static void ff_unsupported_close(void* state) {
    (void)state;
}

static void ff_unsupported_caps(zcsr_video_container_caps* containers,
                                zcsr_video_codec_caps* codecs) {
    if (containers) {
        memset(containers, 0, sizeof(*containers));
    }
    if (codecs) {
        memset(codecs, 0, sizeof(*codecs));
    }
}

const zcsr_video_backend_ops zcsr_video_backend_ff = {
    ff_unsupported_open,
    ff_unsupported_frame,
    ff_unsupported_seek_ms,
    ff_unsupported_seek_frame,
    ff_unsupported_close,
    ff_unsupported_caps,
};

#endif /* ZCSR_VIDEO_HAVE_FFMPEG */
