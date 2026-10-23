/* audiomix_pipewire.c — Linux / PipeWire audio backend (opt-in).
 *
 * Replaces the ALSA dynamic-loader backend when ZCSR_AUDIO_BACKEND=pipewire.
 * Uses pw_stream with a render thread; the mixer thread pushes int16 PCM via
 * zcsr_mix_backend_write(), and the PipeWire process callback pulls from a
 * fixed SPSC ring buffer.
 *
 * Format: 44100 Hz, stereo, 16-bit signed little-endian (SPA_AUDIO_FORMAT_S16_LE).
 * Accumulation: int32 scratch buffer with clamp to int16 range (avoids int16
 * overflow when multiple channels sum).
 *
 * Lifecycle:
 *   backend_open  — pw_init → pw_thread_loop_new/start → pw_context_new →
 *                   pw_context_connect → pw_stream_new → add_listener →
 *                   pw_stream_connect (S16_LE stereo 44100)
 *   backend_close — pw_stream_disconnect → pw_stream_destroy →
 *                   pw_context_disconnect/destroy → pw_thread_loop_stop/destroy
 *   backend_write — lock thread loop, write into SPSC ring, signal process
 */
#if defined(__linux__) && !defined(__ANDROID__)

#include "audiomix_internal.h"

#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>
#include <spa/utils/ringbuffer.h>

#include <errno.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Internal backend state (stored in zcsr_mix_backend.pipewire_data)  */
/* ------------------------------------------------------------------ */

typedef struct {
    struct pw_thread_loop* loop;
    struct pw_context* context;
    struct pw_core* core;
    struct pw_stream* stream;
    struct spa_hook stream_listener;

    /* SPSC ring: mixer thread writes, PipeWire process thread reads. */
    struct spa_ringbuffer ring;
    int32_t* ring_data;          /* bytes = ring_size * sizeof(int32_t) */
    uint32_t ring_size;          /* in int32_t samples (frames * 2) */

    /* int32 accumulation scratch (one period, stereo). */
    int32_t* mix_scratch;

    bool stream_connected;
    bool core_connected;
} zcsr_pw_backend;

/* Ring size: 4 periods of stereo int32 samples. */
enum {
    ZCSR_PW_RING_FRAMES = ZCSR_MIX_PERIOD_FRAMES * 4,
    ZCSR_PW_RING_SAMPLES = ZCSR_PW_RING_FRAMES * 2,
};

/* ------------------------------------------------------------------ */
/* PipeWire stream callbacks                                          */
/* ------------------------------------------------------------------ */

static void zcsr_pw_on_stream_state_changed(void* data,
        enum pw_stream_state old, enum pw_stream_state state, const char* error) {
    zcsr_pw_backend* pw = (zcsr_pw_backend*)data;
    (void)old;
    if (state == PW_STREAM_STATE_STREAMING) {
        pw->stream_connected = true;
    } else if (state == PW_STREAM_STATE_ERROR || state == PW_STREAM_STATE_UNCONNECTED) {
        pw->stream_connected = false;
        if (error) {
            fprintf(stderr, "[zcsr-pw] stream error: %s\n", error);
        }
    }
}

static void zcsr_pw_on_stream_process(void* data) {
    zcsr_pw_backend* pw = (zcsr_pw_backend*)data;
    struct pw_buffer* buf;
    uint32_t filled, size;
    int32_t* dst;
    uint32_t rindex;

    if (!pw || !pw->stream) return;

    buf = pw_stream_dequeue_buffer(pw->stream);
    if (!buf) return;

    if (!buf->buffer || !buf->buffer->datas) {
        pw_stream_queue_buffer(pw->stream, buf);
        return;
    }

    /* Requested frames in this buffer. */
    size = buf->requested ? (uint32_t)buf->requested : ZCSR_MIX_PERIOD_FRAMES;
    if (size > ZCSR_MIX_PERIOD_FRAMES * 2) size = ZCSR_MIX_PERIOD_FRAMES * 2;

    dst = (int32_t*)buf->buffer->datas[0].data;
    if (!dst) {
        pw_stream_queue_buffer(pw->stream, buf);
        return;
    }

    /* Pull from ring buffer.  SPA ringbuffer indices are monotonic byte offsets. */
    spa_ringbuffer_get_read_index(&pw->ring, &rindex);
    {
        uint32_t windex;
        spa_ringbuffer_get_write_index(&pw->ring, &windex);
        filled = windex - rindex;  /* bytes available to read */
    }
    if (filled > size * 2 * sizeof(int32_t))
        filled = size * 2 * sizeof(int32_t);

    if (filled > 0) {
        spa_ringbuffer_read_data(&pw->ring, pw->ring_data,
            pw->ring_size * sizeof(int32_t),
            rindex % (pw->ring_size * sizeof(int32_t)),
            dst, filled);
        rindex += filled;
    }

    /* Zero-fill remaining samples (underrun). */
    {
        uint32_t want_bytes = size * 2 * sizeof(int32_t);
        if (filled < want_bytes) {
            memset((uint8_t*)dst + filled, 0, want_bytes - filled);
        }
    }

    spa_ringbuffer_read_update(&pw->ring, rindex);

    buf->buffer->datas[0].chunk->offset = 0;
    buf->buffer->datas[0].chunk->stride = (uint32_t)(2 * sizeof(int32_t));
    buf->buffer->datas[0].chunk->size = size * 2 * sizeof(int32_t);

    pw_stream_queue_buffer(pw->stream, buf);
}

static const struct pw_stream_events zcsr_pw_stream_events = {
    PW_VERSION_STREAM_EVENTS,
    .state_changed = zcsr_pw_on_stream_state_changed,
    .process = zcsr_pw_on_stream_process,
};

/* ------------------------------------------------------------------ */
/* Backend entry points                                               */
/* ------------------------------------------------------------------ */

void zcsr_mix_backend_open(zcsr_mix_backend* b) {
    zcsr_pw_backend* pw;
    struct pw_properties* props;
    const struct spa_pod* params[1];
    uint8_t buffer[1024];
    struct spa_pod_builder pod_builder = SPA_POD_BUILDER_INIT(buffer, sizeof buffer);
    static const struct spa_audio_info_raw raw_info = {
        .format = SPA_AUDIO_FORMAT_S16_LE,
        .rate = ZCSR_MIX_RATE,
        .channels = 2,
        .position = { SPA_AUDIO_CHANNEL_FL, SPA_AUDIO_CHANNEL_FR },
    };

    if (!b) return;

    pw = (zcsr_pw_backend*)calloc(1, sizeof(zcsr_pw_backend));
    if (!pw) return;

    pw->ring_size = ZCSR_PW_RING_SAMPLES;
    pw->ring_data = (int32_t*)calloc(pw->ring_size, sizeof(int32_t));
    pw->mix_scratch = (int32_t*)calloc(ZCSR_MIX_PERIOD_FRAMES * 2, sizeof(int32_t));
    if (!pw->ring_data || !pw->mix_scratch) {
        free(pw->ring_data);
        free(pw->mix_scratch);
        free(pw);
        return;
    }
    spa_ringbuffer_init(&pw->ring);

    pw_init(NULL, NULL);

    pw->loop = pw_thread_loop_new("zcsr-audio", NULL);
    if (!pw->loop) goto fail_loop;

    if (pw_thread_loop_start(pw->loop) < 0) goto fail_thread;

    pw_thread_loop_lock(pw->loop);

    pw->context = pw_context_new(pw_thread_loop_get_loop(pw->loop), NULL, 0);
    if (!pw->context) goto fail_context;

    pw->core = pw_context_connect(pw->context, NULL, 0);
    if (!pw->core) goto fail_core;
    pw->core_connected = true;

    props = pw_properties_new(
        PW_KEY_MEDIA_TYPE, "Audio",
        PW_KEY_MEDIA_CATEGORY, "Playback",
        PW_KEY_MEDIA_ROLE, "Game",
        PW_KEY_APP_NAME, "zcsr",
        NULL);
    if (!props) goto fail_props;

    pw->stream = pw_stream_new(pw->core, "zcsr-playback", props);
    if (!pw->stream) goto fail_stream;

    pw_stream_add_listener(pw->stream, &pw->stream_listener,
        &zcsr_pw_stream_events, pw);

    params[0] = spa_format_audio_raw_build(&pod_builder,
        SPA_PARAM_EnumFormat, &raw_info);

    if (pw_stream_connect(pw->stream,
            PW_DIRECTION_OUTPUT,
            PW_ID_ANY,
            (enum pw_stream_flags)(PW_STREAM_FLAG_AUTOCONNECT |
                                   PW_STREAM_FLAG_INACTIVE |
                                   PW_STREAM_FLAG_MAP_BUFFERS |
                                   PW_STREAM_FLAG_RT_PROCESS),
            params, 1) < 0) {
        goto fail_connect;
    }

    pw_stream_set_active(pw->stream, true);

    pw_thread_loop_unlock(pw->loop);

    b->pipewire_data = pw;
    b->mix_scratch = pw->mix_scratch;
    b->opened = true;
    return;

fail_connect:
fail_stream:
fail_props:
    if (pw->core) pw_core_disconnect(pw->core);
fail_core:
    if (pw->context) pw_context_destroy(pw->context);
fail_context:
    pw_thread_loop_unlock(pw->loop);
    pw_thread_loop_stop(pw->loop);
fail_thread:
    pw_thread_loop_destroy(pw->loop);
fail_loop:
    free(pw->ring_data);
    free(pw->mix_scratch);
    free(pw);
}

void zcsr_mix_backend_close(zcsr_mix_backend* b) {
    zcsr_pw_backend* pw;
    if (!b || !b->pipewire_data) return;

    pw = (zcsr_pw_backend*)b->pipewire_data;

    pw_thread_loop_lock(pw->loop);
    if (pw->stream) {
        pw_stream_set_active(pw->stream, false);
        pw_stream_disconnect(pw->stream);
        pw_stream_destroy(pw->stream);
        pw->stream = NULL;
    }
    if (pw->core) {
        pw_core_disconnect(pw->core);
        pw->core = NULL;
    }
    if (pw->context) {
        pw_context_destroy(pw->context);
        pw->context = NULL;
    }
    pw_thread_loop_unlock(pw->loop);

    pw_thread_loop_stop(pw->loop);
    pw_thread_loop_destroy(pw->loop);

    free(pw->ring_data);
    free(pw->mix_scratch);
    free(pw);

    b->pipewire_data = NULL;
    b->mix_scratch = NULL;
    b->opened = false;
}

void zcsr_mix_backend_write(zcsr_mix_backend* b, const int16_t* pcm, unsigned frames) {
    zcsr_pw_backend* pw;
    uint32_t windex, filled, available, i;
    int32_t* scratch;

    if (!b || !b->opened || !b->pipewire_data || !pcm) {
        zcsr_mix_sleep_period();
        return;
    }

    pw = (zcsr_pw_backend*)b->pipewire_data;
    scratch = pw->mix_scratch;

    /* Convert int16 -> int32 with saturation (input is already clamped,
     * but guard against malformed data). */
    for (i = 0; i < frames * 2; ++i) {
        int32_t v = pcm[i];
        if (v < -32768) v = -32768;
        if (v > 32767) v = 32767;
        scratch[i] = v;
    }

    pw_thread_loop_lock(pw->loop);

    spa_ringbuffer_get_write_index(&pw->ring, &windex);
    {
        uint32_t rindex;
        uint32_t ring_bytes = pw->ring_size * sizeof(int32_t);
        spa_ringbuffer_get_read_index(&pw->ring, &rindex);
        filled = windex - rindex;          /* bytes already in ring */
        available = ring_bytes - filled;   /* bytes free */
    }

    if (available >= frames * 2 * sizeof(int32_t)) {
        spa_ringbuffer_write_data(&pw->ring, pw->ring_data,
            pw->ring_size * sizeof(int32_t),
            windex % (pw->ring_size * sizeof(int32_t)),
            scratch, frames * 2 * sizeof(int32_t));
        windex += frames * 2 * sizeof(int32_t);
        spa_ringbuffer_write_update(&pw->ring, windex);
    }
    /* else: ring full, drop this period (xruns handled by process callback zero-fill). */

    pw_thread_loop_unlock(pw->loop);
}

#endif /* defined(__linux__) && !defined(__ANDROID__) */
