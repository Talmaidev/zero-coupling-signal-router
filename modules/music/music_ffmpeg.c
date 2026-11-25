/* FFmpeg backend for the music module: AAC (ADTS), AAC-in-MP4 (M4A), Ogg Opus.
 *
 * FFmpeg types are confined to this translation unit; the public music header
 * and the rest of the module see only the opaque zcsr_ffmpeg_backend handle.
 *
 * Memory contract: decoder context, the reusable packet/frame and the
 * resampler scratch are held behind the handle for the instance lifetime.
 * Per-frame output writes into the caller's source PCM scratch; the backend
 * reuses its packet/frame/frame buffer, so there is no retained per-frame heap
 * growth.
 *
 * The backend is compiled only when ZCSR_HAVE_FFMPEG is defined by the build.
 * Without it, zcsr_ffmpeg_available() reports false and the other entry points
 * are safe no-ops / NULL returns.
 */
#include "music_ffmpeg.h"

#if defined(ZCSR_HAVE_FFMPEG)

#include <stdlib.h>
#include <string.h>

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4244 4245 4267 4310 4389 4456 4457 4701 4702 4189 4505 4100)
#endif

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/mem.h>
#include <libavutil/rational.h>
#include <libswresample/swresample.h>

#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#define FF_AVIO_BUF_SIZE (32u * 1024u)
#define FF_SRC_MAX_FRAMES 4096u

struct zcsr_ffmpeg_backend {
    AVFormatContext* fmt;
    AVCodecContext*  codec;
    AVStream*        stream;
    AVPacket*        pkt;
    AVFrame*         frame;
    SwrContext*      swr;
    AVIOContext*     avio;

    const unsigned char* mem;
    size_t mem_len;
    size_t mem_pos;

    int  audio_stream;
    int  src_rate;
    int  src_channels;
    bool eof;
    bool flushed;
};

static int ff_read(void* opaque, uint8_t* buf, int buf_size) {
    zcsr_ffmpeg_backend* b = (zcsr_ffmpeg_backend*)opaque;
    size_t avail;
    int n;
    if (!b || buf_size <= 0) return AVERROR(EIO);
    if (b->mem_pos >= b->mem_len) return AVERROR_EOF;
    avail = b->mem_len - b->mem_pos;
    n = (int)(avail < (size_t)buf_size ? avail : (size_t)buf_size);
    memcpy(buf, b->mem + b->mem_pos, (size_t)n);
    b->mem_pos += (size_t)n;
    return n;
}

static int64_t ff_seek(void* opaque, int64_t offset, int whence) {
    zcsr_ffmpeg_backend* b = (zcsr_ffmpeg_backend*)opaque;
    int64_t pos;
    if (!b) return -1;
    switch (whence) {
        case SEEK_SET: pos = offset; break;
        case SEEK_CUR: pos = (int64_t)b->mem_pos + offset; break;
        case SEEK_END: pos = (int64_t)b->mem_len + offset; break;
        default: return -1;
    }
    if (pos < 0 || pos > (int64_t)b->mem_len) return -1;
    b->mem_pos = (size_t)pos;
    return pos;
}

int zcsr_ffmpeg_available(void) { return 1; }

zcsr_ffmpeg_backend* zcsr_ffmpeg_open(const void* data, size_t len,
                                      int* out_rate, int* out_channels,
                                      uint64_t* out_frames,
                                      uint64_t* out_duration_ms) {
    zcsr_ffmpeg_backend* b;
    const AVCodec* decoder = NULL;
    int ret;
    int audio_stream;
    int64_t duration_us;
    uint64_t duration_ms = 0;
    uint64_t frames = 0;

    if (!data || len == 0u) return NULL;

    b = (zcsr_ffmpeg_backend*)av_mallocz(sizeof(*b));
    if (!b) return NULL;
    b->mem = (const unsigned char*)data;
    b->mem_len = len;
    b->audio_stream = -1;

    {
        unsigned char* abuf = (unsigned char*)av_malloc(FF_AVIO_BUF_SIZE);
        if (!abuf) goto fail;
        b->avio = avio_alloc_context(abuf, FF_AVIO_BUF_SIZE, 0, b,
                                     ff_read, NULL, ff_seek);
        if (!b->avio) { av_free(abuf); goto fail; }
    }

    b->fmt = avformat_alloc_context();
    if (!b->fmt) goto fail;
    b->fmt->pb = b->avio;
    b->fmt->flags |= AVFMT_FLAG_CUSTOM_IO;

    ret = avformat_open_input(&b->fmt, NULL, NULL, NULL);
    if (ret < 0) goto fail;
    ret = avformat_find_stream_info(b->fmt, NULL);
    if (ret < 0) goto fail;

    audio_stream = av_find_best_stream(b->fmt, AVMEDIA_TYPE_AUDIO, -1, -1,
                                       &decoder, 0);
    if (audio_stream < 0 || !decoder) goto fail;
    b->audio_stream = audio_stream;
    b->stream = b->fmt->streams[audio_stream];

    b->codec = avcodec_alloc_context3(decoder);
    if (!b->codec) goto fail;
    /* Copy container-side extradata/parameters (e.g. the MP4 AAC
     * AudioSpecificConfig), which alloc_context3 does not do itself. */
    ret = avcodec_parameters_to_context(b->codec, b->stream->codecpar);
    if (ret < 0) goto fail;
    ret = avcodec_open2(b->codec, decoder, NULL);
    if (ret < 0) goto fail;

    {
        AVCodecParameters* par = b->stream->codecpar;
        AVChannelLayout out_ch, in_ch;
        int in_rate = par->sample_rate > 0 ? par->sample_rate
                                            : b->codec->sample_rate;
        int in_ch_count = par->ch_layout.nb_channels > 0
                              ? par->ch_layout.nb_channels
                              : b->codec->ch_layout.nb_channels;
        enum AVSampleFormat in_fmt = par->format != AV_SAMPLE_FMT_NONE
                                          ? par->format : b->codec->sample_fmt;
        if (in_ch_count <= 0) in_ch_count = 2;
        if (in_rate <= 0) goto fail;
        av_channel_layout_default(&out_ch, 2);
        av_channel_layout_default(&in_ch, in_ch_count);
        ret = swr_alloc_set_opts2(&b->swr, &out_ch, AV_SAMPLE_FMT_FLT, in_rate,
                                   &in_ch, in_fmt, in_rate, 0, NULL);
        av_channel_layout_uninit(&out_ch);
        av_channel_layout_uninit(&in_ch);
        if (ret < 0) goto fail;
    }
    if (swr_init(b->swr) < 0) goto fail;

    b->pkt = av_packet_alloc();
    b->frame = av_frame_alloc();
    if (!b->pkt || !b->frame) goto fail;

    {
        AVCodecParameters* par = b->stream->codecpar;
        b->src_rate = par->sample_rate > 0 ? par->sample_rate
                                           : b->codec->sample_rate;
        b->src_channels = par->ch_layout.nb_channels > 0
                              ? par->ch_layout.nb_channels
                              : b->codec->ch_layout.nb_channels;
    }
    if (b->src_rate <= 0 || b->src_channels <= 0 || b->src_channels > 2) goto fail;

    duration_us = b->fmt->duration;
    if (duration_us <= 0 && b->stream) {
        duration_us = av_rescale_q(b->stream->duration, b->stream->time_base,
                                   AV_TIME_BASE_Q);
    }
    if (duration_us > 0) {
        duration_ms = (uint64_t)(duration_us / 1000);
        frames = (uint64_t)((duration_us * (int64_t)b->src_rate) / AV_TIME_BASE);
    }

    if (out_rate) *out_rate = b->src_rate;
    if (out_channels) *out_channels = b->src_channels;
    if (out_frames) *out_frames = frames;
    if (out_duration_ms) *out_duration_ms = duration_ms;
    return b;

fail:
    zcsr_ffmpeg_close(b);
    return NULL;
}

size_t zcsr_ffmpeg_decode(zcsr_ffmpeg_backend* b, float* dst, size_t cap_frames,
                          int* out_status) {
    size_t out = 0u;
    if (!b || !dst || cap_frames == 0u) {
        if (out_status) *out_status = ZFF_ERROR;
        return 0u;
    }

    while (out < cap_frames) {
        int ret;
        if (!b->eof) {
            ret = av_read_frame(b->fmt, b->pkt);
            if (ret == AVERROR_EOF) {
                b->eof = true;
            } else if (ret < 0) {
                *out_status = ZFF_ERROR;
                return out;
            } else {
                ret = avcodec_send_packet(b->codec, b->pkt);
                av_packet_unref(b->pkt);
                if (ret < 0 && ret != AVERROR(EAGAIN)) {
                    *out_status = ZFF_ERROR;
                    return out;
                }
            }
        } else {
            if (!b->flushed) {
                avcodec_send_packet(b->codec, NULL);
                b->flushed = true;
            }
        }

        ret = avcodec_receive_frame(b->codec, b->frame);
        if (ret == AVERROR(EAGAIN)) {
            if (b->eof) { *out_status = ZFF_EOF; return out; }
            continue;
        }
        if (ret == AVERROR_EOF) {
            *out_status = ZFF_EOF;
            return out;
        }
        if (ret < 0) {
            *out_status = ZFF_ERROR;
            return out;
        }

        {
            int nb = b->frame->nb_samples;
            int want = (int)(cap_frames - out);
            int conv = nb < want ? nb : want;
            uint8_t* out_arr[2] = { NULL, NULL };
            int got;
            if (nb <= 0 || conv <= 0) { av_frame_unref(b->frame); continue; }
            out_arr[0] = (uint8_t*)(dst + out * 2u);
            got = swr_convert(b->swr, out_arr, conv,
                              (const uint8_t* const*)b->frame->data, nb);
            av_frame_unref(b->frame);
            if (got < 0) { *out_status = ZFF_ERROR; return out; }
            out += (size_t)got;
        }
        *out_status = ZFF_OK;
        return out;
    }

    *out_status = ZFF_OK;
    return out;
}

bool zcsr_ffmpeg_seek_ms(zcsr_ffmpeg_backend* b, uint64_t ms) {
    int64_t target;
    int ret;
    if (!b || !b->stream) return false;
    target = av_rescale_q((int64_t)ms, AV_TIME_BASE_Q, b->stream->time_base);
    ret = av_seek_frame(b->fmt, b->audio_stream, target, AVSEEK_FLAG_BACKWARD);
    if (ret < 0) return false;
    avcodec_flush_buffers(b->codec);
    swr_close(b->swr);
    if (swr_init(b->swr) < 0) return false;
    b->eof = false;
    b->flushed = false;
    return true;
}

void zcsr_ffmpeg_rewind(zcsr_ffmpeg_backend* b) {
    if (b) (void)zcsr_ffmpeg_seek_ms(b, 0u);
}

void zcsr_ffmpeg_close(zcsr_ffmpeg_backend* b) {
    if (!b) return;
    av_packet_free(&b->pkt);
    av_frame_free(&b->frame);
    swr_free(&b->swr);
    avcodec_free_context(&b->codec);
    if (b->fmt) {
        avformat_close_input(&b->fmt);
    }
    if (b->avio) {
        /* The AVIO buffer may have been replaced by libavformat; free the
         * current buffer held by the context, not a saved copy. */
        if (b->avio->buffer) av_free(b->avio->buffer);
        avio_context_free(&b->avio);
    }
    av_free(b);
}

#else /* !ZCSR_HAVE_FFMPEG */

int zcsr_ffmpeg_available(void) { return 0; }

zcsr_ffmpeg_backend* zcsr_ffmpeg_open(const void* data, size_t len,
                                      int* out_rate, int* out_channels,
                                      uint64_t* out_frames,
                                      uint64_t* out_duration_ms) {
    (void)data; (void)len;
    if (out_rate) *out_rate = 0;
    if (out_channels) *out_channels = 0;
    if (out_frames) *out_frames = 0;
    if (out_duration_ms) *out_duration_ms = 0;
    return NULL;
}

size_t zcsr_ffmpeg_decode(zcsr_ffmpeg_backend* b, float* dst, size_t cap_frames,
                          int* out_status) {
    (void)b; (void)dst; (void)cap_frames;
    if (out_status) *out_status = ZFF_ERROR;
    return 0u;
}

bool zcsr_ffmpeg_seek_ms(zcsr_ffmpeg_backend* b, uint64_t ms) {
    (void)b; (void)ms; return false;
}

void zcsr_ffmpeg_rewind(zcsr_ffmpeg_backend* b) { (void)b; }

void zcsr_ffmpeg_close(zcsr_ffmpeg_backend* b) { (void)b; }

#endif /* ZCSR_HAVE_FFMPEG */
