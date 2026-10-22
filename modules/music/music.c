/* zcsr music module — streaming OGG/MP3 decoder with background thread.
 * Feeds decoded/resampled PCM into an audiomix stream channel.
 * No heap: caller buffer + fixed internal scratch. */
#include "zcsr/music.h"
#include <string.h>
#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>
#include <limits.h>
#include <math.h>

#define MINIMP3_FLOAT_OUTPUT
#include "../../third_party/minimp3.h"
#define STB_VORBIS_HEADER_ONLY
#include "../../third_party/stb_vorbis.c"
#undef STB_VORBIS_HEADER_ONLY

#if defined(_WIN32)
#  include <windows.h>
#else
#  include <time.h>
#  include <pthread.h>
#endif

#define MUSIC_VORBIS_ALLOC_BYTES (256u * 1024u)
#define MUSIC_SRC_MAX_FRAMES 4096
#define MUSIC_OUT_MAX_FRAMES 4096
#define MUSIC_SLACK 15

typedef enum { DEC_OK, DEC_EOF, DEC_SKIP_NO_AUDIO, DEC_ERROR } decode_status;

struct zcsr_music {
    zcsr_music_format fmt;
    void* encoded_copy;
    size_t encoded_len;
    union {
        struct { stb_vorbis* handle; } ogg;
        struct { mp3dec_t dec; size_t frame_offset; } mp3;
    } decoder;
    uint8_t* vorbis_alloc_buf;
    size_t vorbis_alloc_bytes;
    int src_sample_rate;
    int src_channels;
    /* resample source PCM (float interleaved stereo); unconsumed frames stay at [0..src_pcm_frames) */
    float* src_pcm_f;
    size_t src_pcm_frames;
    float resample_phase;
    /* output PCM (44100 stereo int16) */
    int16_t* out_pcm;
    size_t out_pending_frames;
    size_t out_pending_offset;
    zcsr_mixer* mixer;
    int stream_channel;
    _Atomic bool stop_requested;
    _Atomic bool paused;
    _Atomic bool loop;
    _Atomic int state;
    _Atomic unsigned int session_base_frames;
    uint64_t frames_sent;
#if defined(_WIN32)
    HANDLE thread_handle;
#else
    pthread_t thread_handle;
#endif
    bool started;
};

/* ---- utilities ---- */

static size_t safe_add(size_t a, size_t b, bool* ok) {
    if (!*ok) return 0;
    if (a > SIZE_MAX - b) { *ok = false; return 0; }
    return a + b;
}

static uint8_t* align_up(uint8_t* p, size_t a) {
    uintptr_t v = (uintptr_t)p;
    return (uint8_t*)((v + a - 1u) & ~(uintptr_t)(a - 1u));
}

static size_t align_up_sz(size_t s, size_t a) {
    return (s + a - 1u) & ~(a - 1u);
}

static void sleep_ms(int ms) {
#if defined(_WIN32)
    Sleep((DWORD)ms);
#else
    struct timespec ts = { ms / 1000, (long)(ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
#endif
}

static float clampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

/* ---- buffer size ---- */

size_t zcsr_music_buffer_bytes_required(size_t encoded_len) {
    bool ok = true;
    size_t s = 0;
    s = safe_add(s, sizeof(struct zcsr_music), &ok);
    s = safe_add(s, 7, &ok);
    s = safe_add(s, encoded_len, &ok);
    s = safe_add(s, 7, &ok);
    s = safe_add(s, MUSIC_VORBIS_ALLOC_BYTES, &ok);
    s = safe_add(s, MUSIC_SRC_MAX_FRAMES * 2u * sizeof(float), &ok);
    s = safe_add(s, 7, &ok);
    s = safe_add(s, MUSIC_OUT_MAX_FRAMES * 2u * sizeof(int16_t), &ok);
    s = safe_add(s, 512, &ok);
    s = safe_add(s, MUSIC_SLACK, &ok);
    return ok ? s : 0;
}

/* ---- load ---- */

zcsr_music* zcsr_music_load(const void* encoded, size_t len,
                            zcsr_music_format fmt,
                            void* buffer, size_t bytes) {
    struct zcsr_music* m;
    uint8_t* p;
    uint8_t* enc_dst;
    size_t needed;
    if (!encoded || !len || !buffer) return NULL;
    if (len > (size_t)INT_MAX) return NULL;
    needed = zcsr_music_buffer_bytes_required(len);
    if (needed == 0 || bytes < needed) return NULL;
    p = align_up((uint8_t*)buffer, 16);
    if ((size_t)(p - (uint8_t*)buffer) + needed > bytes) return NULL;
    m = (struct zcsr_music*)p;
    p += align_up_sz(sizeof(*m), 8);
    enc_dst = p;
    p += len;
    p = align_up(p, 8);
    m->vorbis_alloc_buf = p;
    p += MUSIC_VORBIS_ALLOC_BYTES;
    p = align_up(p, 8);
    m->src_pcm_f = (float*)p;
    p += MUSIC_SRC_MAX_FRAMES * 2u * sizeof(float);
    p = align_up(p, 8);
    m->out_pcm = (int16_t*)p;
    p += MUSIC_OUT_MAX_FRAMES * 2u * sizeof(int16_t);
    if ((size_t)(p - (uint8_t*)buffer) > bytes) return NULL;
    memset(m, 0, sizeof(*m));
    memcpy(enc_dst, encoded, len);
    m->fmt = fmt;
    m->encoded_copy = enc_dst;
    m->encoded_len = len;
    m->vorbis_alloc_bytes = MUSIC_VORBIS_ALLOC_BYTES;
    atomic_store(&m->state, ZCSR_MUSIC_STOPPED);
    m->stream_channel = -1;
    if (fmt == ZCSR_MUSIC_OGG) {
        int err = 0;
        stb_vorbis_alloc al;
        al.alloc_buffer = (char*)m->vorbis_alloc_buf;
        al.alloc_buffer_length_in_bytes = (int)m->vorbis_alloc_bytes;
        m->decoder.ogg.handle = stb_vorbis_open_memory(
            (const unsigned char*)m->encoded_copy,
            (int)m->encoded_len, &err, &al);
        if (!m->decoder.ogg.handle) { memset(m, 0, sizeof(*m)); return NULL; }
        {
            stb_vorbis_info info = stb_vorbis_get_info(m->decoder.ogg.handle);
            m->src_sample_rate = info.sample_rate;
            m->src_channels = info.channels;
        }
    } else if (fmt == ZCSR_MUSIC_MP3) {
        mp3dec_init(&m->decoder.mp3.dec);
        m->decoder.mp3.frame_offset = 0;
        /* sample rate / channels filled on first successful decode */
    } else {
        memset(m, 0, sizeof(*m));
        return NULL;
    }
    return m;
}

/* ---- decode ---- */

static size_t decode_block_ogg(zcsr_music* m, decode_status* st) {
    int ch = m->src_channels > 0 ? m->src_channels : 2;
    int n;
    if (ch > 2) { *st = DEC_ERROR; return 0; }
    n = stb_vorbis_get_samples_float_interleaved(
        m->decoder.ogg.handle, ch, m->src_pcm_f,
        (int)(MUSIC_SRC_MAX_FRAMES * ch));
    if (n <= 0) { *st = DEC_EOF; return 0; }
    if (ch == 1) {
        int i;
        for (i = n - 1; i >= 0; --i) {
            float v = m->src_pcm_f[i];
            m->src_pcm_f[i * 2 + 0] = v;
            m->src_pcm_f[i * 2 + 1] = v;
        }
    }
    *st = DEC_OK;
    return (size_t)n;
}

static size_t decode_block_mp3(zcsr_music* m, decode_status* st) {
    mp3dec_frame_info_t info;
    float pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
    int samples;
    size_t remaining;
    bool decoded_any = false;
    int ch;
    while (m->decoder.mp3.frame_offset < m->encoded_len) {
        memset(&info, 0, sizeof(info));
        remaining = m->encoded_len - m->decoder.mp3.frame_offset;
        if (remaining > (size_t)INT_MAX) { *st = DEC_ERROR; return 0; }
        samples = mp3dec_decode_frame(&m->decoder.mp3.dec,
            (const uint8_t*)m->encoded_copy + m->decoder.mp3.frame_offset,
            (int)remaining, pcm, &info);
        if (info.frame_bytes == 0) {
            *st = decoded_any ? DEC_EOF : DEC_ERROR;
            return 0;
        }
        m->decoder.mp3.frame_offset += info.frame_bytes;
        decoded_any = true;
        if (samples == 0) { *st = DEC_SKIP_NO_AUDIO; continue; }
        ch = info.channels;
        if (ch > 2) { *st = DEC_ERROR; return 0; }
        m->src_sample_rate = info.hz;
        m->src_channels = ch;
        if ((size_t)samples > MUSIC_SRC_MAX_FRAMES) { *st = DEC_ERROR; return 0; }
        if (ch == 1) {
            int i;
            for (i = samples - 1; i >= 0; --i) {
                float v = pcm[i];
                m->src_pcm_f[i * 2 + 0] = v;
                m->src_pcm_f[i * 2 + 1] = v;
            }
        } else {
            memcpy(m->src_pcm_f, pcm, (size_t)samples * 2u * sizeof(float));
        }
        *st = DEC_OK;
        return (size_t)samples;
    }
    *st = DEC_EOF;
    return 0;
}

static size_t decode_block(zcsr_music* m, decode_status* st) {
    return (m->fmt == ZCSR_MUSIC_OGG)
        ? decode_block_ogg(m, st)
        : decode_block_mp3(m, st);
}

/* ---- resample (linear interpolation, source_step = src_rate / 44100) ---- */

static size_t resample_to_scratch(zcsr_music* m) {
    float source_step, phase, frac;
    size_t out = 0, i = 0, consumed;
    if (m->src_sample_rate <= 0) return 0;
    if (m->src_pcm_frames < 2) return 0;
    source_step = (float)m->src_sample_rate / 44100.0f;
    phase = m->resample_phase;
    while (i + 1 < m->src_pcm_frames && out < MUSIC_OUT_MAX_FRAMES) {
        size_t j = i + (size_t)phase;
        size_t j1 = j + 1;
        if (j1 >= m->src_pcm_frames) break;
        frac = phase - (float)(size_t)phase;
        {
            float s0l = m->src_pcm_f[j * 2 + 0];
            float s0r = m->src_pcm_f[j * 2 + 1];
            float s1l = m->src_pcm_f[j1 * 2 + 0];
            float s1r = m->src_pcm_f[j1 * 2 + 1];
            float l = clampf(s0l + (s1l - s0l) * frac, -1.0f, 1.0f);
            float r = clampf(s0r + (s1r - s0r) * frac, -1.0f, 1.0f);
            m->out_pcm[out * 2 + 0] = (int16_t)(l * 32767.0f);
            m->out_pcm[out * 2 + 1] = (int16_t)(r * 32767.0f);
            ++out;
        }
        phase += source_step;
        while (phase >= 1.0f) { phase -= 1.0f; ++i; }
        if (i + 1 >= m->src_pcm_frames) break;
    }
    consumed = i;
    if (consumed > m->src_pcm_frames) consumed = m->src_pcm_frames;
    if (consumed > 0) {
        size_t left = m->src_pcm_frames - consumed;
        if (left > 0)
            memmove(m->src_pcm_f, m->src_pcm_f + consumed * 2,
                    left * 2u * sizeof(float));
        m->src_pcm_frames = left;
    }
    m->resample_phase = phase;
    return out;
}

/* ---- decoder thread ---- */

static void music_thread_main(zcsr_music* m) {
    while (!atomic_load(&m->stop_requested)) {
        if (atomic_load(&m->paused)) { sleep_ms(1); continue; }
        /* feed pending PCM */
        if (m->out_pending_frames > 0) {
            size_t sent = zcsr_mix_feed_stream(m->mixer, m->stream_channel,
                m->out_pcm + m->out_pending_offset * 2,
                m->out_pending_frames);
            m->out_pending_offset += sent;
            m->out_pending_frames -= sent;
            m->frames_sent += sent;
            if (sent == 0) sleep_ms(1);
            continue;
        }
        /* consume remaining source frames */
        if (m->src_pcm_frames >= 2) {
            m->out_pending_frames = resample_to_scratch(m);
            m->out_pending_offset = 0;
            if (m->out_pending_frames > 0) continue;
        }
        /* decode new block */
        {
            decode_status st;
            size_t src_n = decode_block(m, &st);
            if (st == DEC_SKIP_NO_AUDIO) continue;
            if (st == DEC_ERROR) {
                atomic_store(&m->state, ZCSR_MUSIC_ERROR);
                break;
            }
            if (st == DEC_EOF) {
                if (atomic_load(&m->loop)) {
                    if (m->fmt == ZCSR_MUSIC_OGG) {
                        stb_vorbis_seek_start(m->decoder.ogg.handle);
                    } else {
                        mp3dec_init(&m->decoder.mp3.dec);
                        m->decoder.mp3.frame_offset = 0;
                    }
                    m->src_pcm_frames = 0;
                    m->resample_phase = 0.0f;
                    continue;
                }
                /* natural end: drain mixer */
                while (!atomic_load(&m->stop_requested)) {
                    uint64_t base = (uint64_t)atomic_load(&m->session_base_frames);
                    uint64_t now = zcsr_mix_stream_consumed_frames(m->mixer, m->stream_channel);
                    if (now - base >= m->frames_sent) break;
                    sleep_ms(1);
                }
                atomic_store(&m->state, ZCSR_MUSIC_ENDED);
                break;
            }
            m->src_pcm_frames = src_n;
            m->resample_phase = 0.0f;
        }
    }
}

#if defined(_WIN32)
static DWORD WINAPI music_thread_proc(LPVOID arg) {
    music_thread_main((zcsr_music*)arg);
    return 0;
}
#else
static void* music_thread_proc(void* arg) {
    music_thread_main((zcsr_music*)arg);
    return NULL;
}
#endif

/* ---- lifecycle ---- */

bool zcsr_music_attach_mixer(zcsr_music* m, zcsr_mixer* mixer) {
    if (!m || !mixer || m->mixer) return false;
    m->stream_channel = zcsr_mix_attach_stream(mixer);
    if (m->stream_channel < 0) return false;
    m->mixer = mixer;
    return true;
}

bool zcsr_music_play(zcsr_music* m) {
    if (!m || !m->mixer) return false;
    if (m->started) zcsr_music_stop(m);
    if (m->fmt == ZCSR_MUSIC_OGG) {
        stb_vorbis_seek_start(m->decoder.ogg.handle);
    } else {
        mp3dec_init(&m->decoder.mp3.dec);
        m->decoder.mp3.frame_offset = 0;
    }
    m->src_pcm_frames = 0;
    m->resample_phase = 0.0f;
    m->out_pending_frames = 0;
    m->out_pending_offset = 0;
    m->frames_sent = 0;
    atomic_store(&m->stop_requested, false);
    atomic_store(&m->paused, false);
    atomic_store(&m->session_base_frames,
        (unsigned int)zcsr_mix_stream_consumed_frames(m->mixer, m->stream_channel));
    /* set PLAYING before creating thread */
    atomic_store(&m->state, ZCSR_MUSIC_PLAYING);
#if defined(_WIN32)
    m->thread_handle = CreateThread(NULL, 0, music_thread_proc, m, 0, NULL);
    if (!m->thread_handle) { atomic_store(&m->state, ZCSR_MUSIC_ERROR); return false; }
#else
    if (pthread_create(&m->thread_handle, NULL, music_thread_proc, m) != 0) {
        atomic_store(&m->state, ZCSR_MUSIC_ERROR);
        return false;
    }
#endif
    m->started = true;
    return true;
}

void zcsr_music_stop(zcsr_music* m) {
    if (!m || !m->started) return;
    atomic_store(&m->stop_requested, true);
#if defined(_WIN32)
    WaitForSingleObject(m->thread_handle, INFINITE);
    CloseHandle(m->thread_handle);
    m->thread_handle = NULL;
#else
    pthread_join(m->thread_handle, NULL);
#endif
    m->started = false;
    if (m->mixer) zcsr_mix_stream_flush(m->mixer, m->stream_channel);
    if (m->fmt == ZCSR_MUSIC_OGG) {
        stb_vorbis_seek_start(m->decoder.ogg.handle);
    } else {
        mp3dec_init(&m->decoder.mp3.dec);
        m->decoder.mp3.frame_offset = 0;
    }
    m->src_pcm_frames = 0;
    m->resample_phase = 0.0f;
    m->out_pending_frames = 0;
    atomic_store(&m->state, ZCSR_MUSIC_STOPPED);
}

void zcsr_music_destroy(zcsr_music* m) {
    if (!m) return;
    if (m->started) zcsr_music_stop(m);
    if (m->mixer && m->stream_channel >= 0)
        zcsr_mix_stream_detach(m->mixer, m->stream_channel);
    if (m->fmt == ZCSR_MUSIC_OGG && m->decoder.ogg.handle)
        stb_vorbis_close(m->decoder.ogg.handle);
    memset(m, 0, sizeof(*m));
}

void zcsr_music_pause(zcsr_music* m) {
    if (!m) return;
    atomic_store(&m->paused, true);
    if (m->mixer) zcsr_mix_stream_set_paused(m->mixer, m->stream_channel, true);
    atomic_store(&m->state, ZCSR_MUSIC_PAUSED);
}

void zcsr_music_resume(zcsr_music* m) {
    if (!m) return;
    atomic_store(&m->paused, false);
    if (m->mixer) zcsr_mix_stream_set_paused(m->mixer, m->stream_channel, false);
    atomic_store(&m->state, ZCSR_MUSIC_PLAYING);
}

void zcsr_music_set_volume(zcsr_music* m, float v) {
    if (!m || !m->mixer) return;
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;
    zcsr_mix_stream_set_volume(m->mixer, m->stream_channel, v);
}

void zcsr_music_set_loop(zcsr_music* m, bool loop) {
    if (m) atomic_store(&m->loop, loop);
}

zcsr_music_state zcsr_music_get_state(const zcsr_music* m) {
    return m ? (zcsr_music_state)atomic_load(&m->state) : ZCSR_MUSIC_ERROR;
}

uint64_t zcsr_music_position_ms(const zcsr_music* m) {
    uint64_t now, base;
    if (!m || !m->mixer) return 0;
    now = zcsr_mix_stream_consumed_frames(m->mixer, m->stream_channel);
    base = (uint64_t)atomic_load(&m->session_base_frames);
    return (now - base) * 1000u / 44100u;
}
