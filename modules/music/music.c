/* zcsr music module — streaming decoder with a background thread.
 * Feeds decoded/resampled PCM into an audiomix stream channel.
 *
 * Memory contract: output PCM is caller-owned. The vendored decoders keep
 * scratch in the caller buffer; the FFmpeg backend (AAC/M4A/Opus) holds its
 * decoder context and reused frames behind the opaque handle. There is no
 * retained per-frame heap growth and no per-frame output allocation. */
#include "zcsr/music.h"
#include "music_ffmpeg.h"
#include <string.h>
#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>
#include <limits.h>
#include <math.h>

/* Third-party single-file decoders: their warnings are not ours to fix; scope
 * the suppression to these includes only (MSVC /WX would otherwise fail). */
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4244 4245 4456 4457 4701)
#endif
#define MINIMP3_FLOAT_OUTPUT
#include "../../third_party/minimp3.h"
#define STB_VORBIS_HEADER_ONLY
#include "../../third_party/stb_vorbis.c"
#undef STB_VORBIS_HEADER_ONLY
/* dr_flac declarations only; the implementation lives in drflac_impl.c.
 * NO_STDIO drops the file-open entry points (we decode from memory); NO_SIMD
 * keeps the portable scalar path so no SSE2/NEON feature detection is needed. */
#define DR_FLAC_NO_STDIO
#define DR_FLAC_NO_SIMD
#include "third_party/dr_flac.h"
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#if defined(_WIN32)
#  include <windows.h>
#else
#  include <time.h>
#  include <pthread.h>
#endif

#define MUSIC_VORBIS_ALLOC_BYTES (256u * 1024u)
#define MUSIC_FLAC_ALLOC_BYTES (256u * 1024u)
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
        struct {
            size_t data_offset;   /* byte offset of PCM data in encoded_copy */
            size_t data_bytes;    /* PCM payload bytes */
            size_t data_pos;      /* byte offset within PCM data */
            uint16_t format;      /* 1 = PCM, 3 = IEEE float */
            uint16_t channels;
            uint16_t bits;
            uint32_t frame_bytes; /* block align */
        } wav;
        struct { drflac* handle; } flac;
        struct { zcsr_ffmpeg_backend* handle; } ffmpeg;
    } decoder;
    uint8_t* vorbis_alloc_buf;
    size_t vorbis_alloc_bytes;
    uint8_t* flac_arena_buf;
    size_t flac_arena_bytes;
    size_t flac_arena_offset;
    uint16_t spec_bits;
    uint64_t spec_frames;
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
    uint64_t duration_ms;
    zcsr_music_info info;
    const uint8_t* cover_src;   /* encoded cover bytes, or base64 text for OGG */
    size_t cover_src_bytes;
    size_t cover_bytes;         /* decoded image bytes; 0 = no cover */
    size_t cover_skip;          /* decoded bytes to skip (OGG picture block header) */
    bool cover_base64;
    bool threaded;              /* true = decoder thread (desktop default) */
    uint64_t play_start_ms;
    _Atomic uint64_t position_base_ms;
    _Atomic uint64_t position_base_frames;
    _Atomic uint64_t seek_target_ms;
    _Atomic bool seek_requested;
    _Atomic int error_detail;
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

static uint64_t samples_to_ms(uint64_t samples, int rate) {
    uint64_t r;
    if (rate <= 0) return 0;
    r = (uint64_t)rate;
    if (samples > UINT64_MAX / 1000u) return 0; /* implausible container length */
    return (samples / r) * 1000u + ((samples % r) * 1000u) / r;
}

static uint16_t le16(const uint8_t* p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t le32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint32_t be32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

/* Parse a RIFF/WAVE PCM or IEEE-float stream. The music module owns only the
 * byte copy and a fixed source PCM scratch, so this is a streaming parser: no
 * per-file PCM allocation. Compressed WAV codecs are intentionally rejected
 * instead of silently returning a fake duration. */
static bool wav_parse(struct zcsr_music* m) {
    const uint8_t* p = (const uint8_t*)m->encoded_copy;
    size_t n = m->encoded_len;
    size_t pos = 12u;
    size_t data_offset = 0u;
    size_t data_bytes = 0u;
    uint16_t format = 0u;
    uint16_t channels = 0u;
    uint32_t rate = 0u;
    uint16_t bits = 0u;
    uint32_t block_align = 0u;
    bool have_fmt = false;
    bool have_data = false;

    if (!p || n < 12u || memcmp(p, "RIFF", 4u) != 0 ||
        memcmp(p + 8u, "WAVE", 4u) != 0) {
        return false;
    }

    while (pos <= n - 8u) {
        const uint8_t* id = p + pos;
        uint32_t chunk = le32(p + pos + 4u);
        size_t body = pos + 8u;
        if ((size_t)chunk > n - body) break;
        if (memcmp(id, "fmt ", 4u) == 0 && chunk >= 16u) {
            format = le16(p + body + 0u);
            channels = le16(p + body + 2u);
            rate = le32(p + body + 4u);
            block_align = le16(p + body + 12u);
            bits = le16(p + body + 14u);
            if (format == 0xFFFEu && chunk >= 40u) {
                /* WAVE_FORMAT_EXTENSIBLE: the first two GUID bytes are the
                 * actual format tag; the rest is a fixed subtype GUID. */
                format = le16(p + body + 24u);
            }
            have_fmt = true;
        } else if (memcmp(id, "data", 4u) == 0) {
            data_offset = body;
            data_bytes = (size_t)chunk;
            if (data_bytes > n - body) data_bytes = n - body;
            have_data = true;
        }
        pos = body + (size_t)chunk;
        if ((chunk & 1u) != 0u) {
            if (pos == SIZE_MAX) break;
            ++pos;
        }
    }

    if (!have_fmt || !have_data || channels == 0u || channels > 2u ||
        rate == 0u || block_align == 0u || bits == 0u) {
        return false;
    }
    if (format != 1u && format != 3u) return false;

    size_t bytes_per_sample = (size_t)((bits + 7u) / 8u);
    if (bytes_per_sample == 0u ||
        (size_t)channels > SIZE_MAX / bytes_per_sample ||
        (size_t)channels * bytes_per_sample != (size_t)block_align) {
        return false;
    }
    if (format == 1u &&
        bits != 8u && bits != 16u && bits != 24u && bits != 32u) {
        return false;
    }
    if (format == 3u && bits != 32u && bits != 64u) return false;

    data_bytes -= data_bytes % (size_t)block_align;
    if (data_bytes == 0u) return false;

    m->decoder.wav.data_offset = data_offset;
    m->decoder.wav.data_bytes = data_bytes;
    m->decoder.wav.data_pos = 0u;
    m->decoder.wav.format = format;
    m->decoder.wav.channels = channels;
    m->decoder.wav.bits = bits;
    m->decoder.wav.frame_bytes = block_align;
    m->src_sample_rate = (int)rate;
    m->src_channels = (int)channels;
    m->duration_ms = samples_to_ms(data_bytes / (size_t)block_align, (int)rate);
    m->spec_bits = bits;
    m->spec_frames = (uint64_t)(data_bytes / (size_t)block_align);
    return true;
}

/* FLAC caller-buffer arena: bump-allocated from the instance's fixed scratch.
 * free() is a no-op (the arena lives for the whole instance); realloc is left
 * NULL so dr_flac's own fallback allocator owns any growth, released when the
 * decoder closes. No per-frame retained heap is held by the music instance. */
static void* flac_arena_malloc(size_t sz, void* userData) {
    struct zcsr_music* m = (struct zcsr_music*)userData;
    size_t aligned;
    if (!m || sz == 0u || sz > m->flac_arena_bytes) return NULL;
    aligned = (m->flac_arena_offset + 15u) & ~(size_t)15u;
    if (aligned > m->flac_arena_bytes || sz > m->flac_arena_bytes - aligned) {
        return NULL;
    }
    m->flac_arena_offset = aligned + sz;
    return m->flac_arena_buf + aligned;
}

static void flac_arena_free(void* p, void* userData) {
    (void)p;
    (void)userData;
}

static void music_set_error(zcsr_music* m, zcsr_music_error error) {
    if (m) atomic_store(&m->error_detail, (int)error);
}

/* Scan MP3 frames once at load. This is O(file size), but duration is then O(1).
 * Decoding is used because minimp3 has no header-only sample counter; frames with
 * no audio (ID3/metadata) still advance frame_offset. */
static uint64_t mp3_scan_duration(const unsigned char* data, size_t len, int* out_rate) {
    mp3dec_t dec;
    mp3dec_frame_info_t info;
    float pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
    size_t offset = 0;
    uint64_t total_samples = 0;
    int rate = 0;

    if (out_rate) *out_rate = 0;
    mp3dec_init(&dec);
    while (offset < len) {
        size_t remaining = len - offset;
        int samples;
        if (remaining > (size_t)INT_MAX) break;
        memset(&info, 0, sizeof(info));
        samples = mp3dec_decode_frame(&dec, data + offset, (int)remaining, pcm, &info);
        if (info.frame_bytes == 0) break;
        offset += (size_t)info.frame_bytes;
        if (samples > 0) {
            if (rate == 0) rate = info.hz;
            total_samples += (uint64_t)samples;
        }
    }
    if (out_rate) *out_rate = rate;
    return samples_to_ms(total_samples, rate);
}

/* ---- metadata / embedded cover ---- */

static bool ascii_key_equal(const char* s, size_t len, const char* key) {
    size_t i;
    size_t key_len = strlen(key);
    if (len != key_len) return false;
    for (i = 0; i < len; ++i) {
        char a = s[i];
        char b = key[i];
        if (a >= 'a' && a <= 'z') a = (char)(a - ('a' - 'A'));
        if (b >= 'a' && b <= 'z') b = (char)(b - ('a' - 'A'));
        if (a != b) return false;
    }
    return true;
}

static void copy_trimmed(char* dst, size_t cap, const char* src, size_t len) {
    size_t end;
    size_t out;
    if (!dst || cap == 0u) return;
    if (!src) { dst[0] = '\0'; return; }
    end = len;
    while (end > 0u && (src[end - 1u] == ' ' || src[end - 1u] == '\t' ||
                        src[end - 1u] == '\r' || src[end - 1u] == '\n' ||
                        src[end - 1u] == '\0')) {
        --end;
    }
    out = end;
    if (out >= cap) out = cap - 1u;
    if (out > 0u && src != dst) memcpy(dst, src, out);
    dst[out] = '\0';
}

static uint32_t parse_uint_prefix(const char* s, size_t len) {
    uint32_t value = 0u;
    size_t i = 0u;
    bool have = false;
    while (i < len && (s[i] == ' ' || s[i] == '\t')) ++i;
    while (i < len && s[i] >= '0' && s[i] <= '9') {
        have = true;
        if (value <= (UINT32_MAX - 9u) / 10u) value = value * 10u + (uint32_t)(s[i] - '0');
        ++i;
    }
    return have ? value : 0u;
}

static uint32_t syncsafe32(const uint8_t* p) {
    return ((uint32_t)p[0] << 21) | ((uint32_t)p[1] << 14) |
           ((uint32_t)p[2] << 7) | (uint32_t)p[3];
}

static int b64_value(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

/* Decode base64, skipping `skip` decoded bytes and writing at most `want`
 * bytes (also bounded by capacity). Writes only into the caller buffer. */
static size_t base64_decode_skip(const char* src, size_t len,
                                 uint8_t* dst, size_t capacity,
                                 size_t skip, size_t want) {
    unsigned int acc = 0u;
    int bits = 0;
    size_t out_index = 0u;
    size_t written = 0u;
    size_t i;
    if (!src || !dst || capacity == 0u || want == 0u) return 0u;
    for (i = 0u; i < len; ++i) {
        int value;
        char c = src[i];
        if (c == '=') break;
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        value = b64_value(c);
        if (value < 0) continue;
        acc = (acc << 6) | (unsigned int)value;
        bits += 6;
        while (bits >= 8) {
            uint8_t byte;
            bits -= 8;
            byte = (uint8_t)((acc >> (unsigned int)bits) & 0xFFu);
            if (out_index >= skip && written < capacity) {
                dst[written++] = byte;
                if (written >= want) return written;
            }
            ++out_index;
        }
    }
    return written;
}

static size_t utf8_put(char* dst, size_t cap, size_t pos, uint32_t cp) {
    if (cp < 0x80u) {
        if (pos + 1u >= cap) return pos;
        dst[pos++] = (char)cp;
    } else if (cp < 0x800u) {
        if (pos + 2u >= cap) return pos;
        dst[pos++] = (char)(0xC0u | (cp >> 6));
        dst[pos++] = (char)(0x80u | (cp & 0x3Fu));
    } else if (cp < 0x10000u) {
        if (pos + 3u >= cap) return pos;
        dst[pos++] = (char)(0xE0u | (cp >> 12));
        dst[pos++] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
        dst[pos++] = (char)(0x80u | (cp & 0x3Fu));
    } else {
        if (pos + 4u >= cap) return pos;
        dst[pos++] = (char)(0xF0u | (cp >> 18));
        dst[pos++] = (char)(0x80u | ((cp >> 12) & 0x3Fu));
        dst[pos++] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
        dst[pos++] = (char)(0x80u | (cp & 0x3Fu));
    }
    return pos;
}

static void copy_utf16(char* dst, size_t cap, const uint8_t* src, size_t len, bool big_endian) {
    size_t i = 0u;
    size_t out = 0u;
    if (!dst || cap == 0u) return;
    if (len >= 2u && src[0] == 0xFFu && src[1] == 0xFEu) { big_endian = false; i = 2u; }
    else if (len >= 2u && src[0] == 0xFEu && src[1] == 0xFFu) { big_endian = true; i = 2u; }
    while (i + 1u < len) {
        uint32_t cp;
        uint16_t unit = big_endian
            ? (uint16_t)(((uint16_t)src[i] << 8) | src[i + 1u])
            : (uint16_t)(((uint16_t)src[i + 1u] << 8) | src[i]);
        i += 2u;
        if (unit >= 0xD800u && unit <= 0xDBFFu && i + 1u < len) {
            uint16_t low = big_endian
                ? (uint16_t)(((uint16_t)src[i] << 8) | src[i + 1u])
                : (uint16_t)(((uint16_t)src[i + 1u] << 8) | src[i]);
            if (low >= 0xDC00u && low <= 0xDFFFu) {
                cp = 0x10000u + (((uint32_t)unit - 0xD800u) << 10) + ((uint32_t)low - 0xDC00u);
                i += 2u;
            } else {
                cp = unit;
            }
        } else {
            cp = unit;
        }
        out = utf8_put(dst, cap, out, cp);
        if (out + 1u >= cap) break;
    }
    dst[out] = '\0';
}

static void copy_tag_text(char* dst, size_t cap, const uint8_t* src, size_t len, uint8_t encoding) {
    size_t i;
    size_t out = 0u;
    if (!dst || cap == 0u) return;
    dst[0] = '\0';
    if (!src || len == 0u) return;
    if (encoding == 1u || encoding == 2u) {
        copy_utf16(dst, cap, src, len, encoding == 2u);
        return;
    }
    if (encoding == 0u) {
        for (i = 0u; i < len && out + 1u < cap; ++i) {
            out = utf8_put(dst, cap, out, (uint32_t)src[i]);
        }
        dst[out] = '\0';
        copy_trimmed(dst, cap, dst, out);
        return;
    }
    /* Encoding 3 (UTF-8): copy bytes verbatim, bounded. */
    if (len > cap - 1u) len = cap - 1u;
    memcpy(dst, src, len);
    dst[len] = '\0';
    copy_trimmed(dst, cap, dst, len);
}

static bool parse_ogg_picture(const char* b64, size_t b64_len,
                              size_t* out_skip, size_t* out_bytes) {
    uint8_t hdr[512];
    size_t got;
    uint32_t mime_len;
    uint32_t desc_len;
    uint32_t data_len;
    size_t dim_off;
    size_t data_off;
    if (!b64 || !out_skip || !out_bytes) return false;
    got = base64_decode_skip(b64, b64_len, hdr, sizeof(hdr), 0u, sizeof(hdr));
    if (got < 8u) return false;
    mime_len = be32(hdr + 4u);
    if ((size_t)mime_len + 8u + 4u > got) return false;
    desc_len = be32(hdr + 8u + mime_len);
    dim_off = 8u + (size_t)mime_len + 4u + (size_t)desc_len;
    if (dim_off + 20u > got) return false;
    data_len = be32(hdr + dim_off + 16u);
    if (data_len == 0u) return false;
    data_off = dim_off + 20u;
    *out_skip = data_off;
    *out_bytes = (size_t)data_len;
    return true;
}

static void parse_ogg_metadata(zcsr_music* m) {
    stb_vorbis_comment comments;
    int i;
    if (!m || !m->decoder.ogg.handle) return;
    comments = stb_vorbis_get_comment(m->decoder.ogg.handle);
    for (i = 0; i < comments.comment_list_length; ++i) {
        const char* item = comments.comment_list[i];
        const char* eq;
        size_t key_len;
        size_t value_len;
        if (!item) continue;
        eq = strchr(item, '=');
        if (!eq) continue;
        key_len = (size_t)(eq - item);
        value_len = strlen(eq + 1);
        if (ascii_key_equal(item, key_len, "TITLE")) {
            copy_trimmed(m->info.title, ZCSR_MUSIC_TEXT_CAP, eq + 1, value_len);
        } else if (ascii_key_equal(item, key_len, "ARTIST")) {
            copy_trimmed(m->info.artist, ZCSR_MUSIC_TEXT_CAP, eq + 1, value_len);
        } else if (ascii_key_equal(item, key_len, "ALBUM")) {
            copy_trimmed(m->info.album, ZCSR_MUSIC_TEXT_CAP, eq + 1, value_len);
        } else if (ascii_key_equal(item, key_len, "DATE")) {
            if (m->info.year == 0u) m->info.year = parse_uint_prefix(eq + 1, value_len);
        } else if (ascii_key_equal(item, key_len, "TRACKNUMBER")) {
            if (m->info.track == 0u) m->info.track = parse_uint_prefix(eq + 1, value_len);
        } else if (ascii_key_equal(item, key_len, "METADATA_BLOCK_PICTURE") &&
                   m->cover_bytes == 0u) {
            size_t skip = 0u;
            size_t bytes = 0u;
            if (parse_ogg_picture(eq + 1, value_len, &skip, &bytes)) {
                m->cover_src = (const uint8_t*)(eq + 1);
                m->cover_src_bytes = value_len;
                m->cover_bytes = bytes;
                m->cover_skip = skip;
                m->cover_base64 = true;
            }
        }
    }
}

/* FLAC Vorbis-comment metadata callback. Runs during drflac_open; comment
 * bytes belong to the decoder's arena, so every field is copied out immediately. */
static void flac_meta_proc(void* userData, drflac_metadata* meta) {
    struct zcsr_music* m = (struct zcsr_music*)userData;
    drflac_vorbis_comment_iterator it;
    const char* comment;
    uint32_t clen;
    if (!m || !meta) return;
    if (meta->type != DR_FLAC_METADATA_BLOCK_TYPE_VORBIS_COMMENT) return;
    drflac_init_vorbis_comment_iterator(&it,
        meta->data.vorbis_comment.commentCount,
        meta->data.vorbis_comment.pComments);
    while ((comment = drflac_next_vorbis_comment(&it, &clen)) != NULL) {
        const char* eq = NULL;
        uint32_t i;
        size_t key_len;
        size_t val_len;
        for (i = 0u; i < clen; ++i) {
            if (comment[i] == '=') { eq = comment + (size_t)i; break; }
        }
        if (!eq) continue;
        key_len = (size_t)(eq - comment);
        val_len = clen - key_len - 1u;
        if (ascii_key_equal(comment, key_len, "TITLE")) {
            copy_trimmed(m->info.title, ZCSR_MUSIC_TEXT_CAP, eq + 1, val_len);
        } else if (ascii_key_equal(comment, key_len, "ARTIST")) {
            copy_trimmed(m->info.artist, ZCSR_MUSIC_TEXT_CAP, eq + 1, val_len);
        } else if (ascii_key_equal(comment, key_len, "ALBUM")) {
            copy_trimmed(m->info.album, ZCSR_MUSIC_TEXT_CAP, eq + 1, val_len);
        } else if (ascii_key_equal(comment, key_len, "DATE")) {
            if (m->info.year == 0u) m->info.year = parse_uint_prefix(eq + 1, val_len);
        } else if (ascii_key_equal(comment, key_len, "TRACKNUMBER")) {
            if (m->info.track == 0u) m->info.track = parse_uint_prefix(eq + 1, val_len);
        }
    }
}

static void parse_apic(zcsr_music* m, const uint8_t* payload, size_t len) {
    uint8_t encoding;
    size_t i = 1u;
    if (!m || !payload || len < 2u || m->cover_bytes != 0u) return;
    encoding = payload[0];
    while (i < len && payload[i] != 0u) ++i;  /* MIME type */
    if (i >= len) return;
    ++i;
    ++i;  /* picture type */
    if (i >= len) return;
    if (encoding == 1u || encoding == 2u) {
        while (i + 1u < len && !(payload[i] == 0u && payload[i + 1u] == 0u)) i += 2u;
        if (i + 1u >= len) return;
        i += 2u;
    } else {
        while (i < len && payload[i] != 0u) ++i;
        if (i >= len) return;
        ++i;
    }
    if (i >= len) return;
    m->cover_src = payload + i;
    m->cover_src_bytes = len - i;
    m->cover_bytes = len - i;
    m->cover_skip = 0u;
    m->cover_base64 = false;
}

static void parse_id3_metadata(zcsr_music* m) {
    const uint8_t* data;
    size_t len;
    size_t tag_size;
    size_t end;
    size_t pos;
    uint8_t version;
    uint8_t flags;
    if (!m || !m->encoded_copy) return;
    data = (const uint8_t*)m->encoded_copy;
    len = m->encoded_len;
    if (len < 10u || memcmp(data, "ID3", 3u) != 0) return;
    version = data[3];
    if (version < 3u || version > 4u) return;
    flags = data[5];
    tag_size = (size_t)syncsafe32(data + 6u);
    end = 10u + tag_size;
    if (end > len) end = len;
    pos = 10u;
    if ((flags & 0x40u) != 0u && pos + 4u <= end) {
        size_t ext = (version == 4u)
            ? (size_t)syncsafe32(data + pos)
            : (size_t)be32(data + pos);
        pos += 4u + ext;
        if (pos > end) return;
    }
    while (pos + 10u <= end) {
        const uint8_t* frame = data + pos;
        uint32_t frame_size;
        size_t payload_off;
        if (frame[0] == 0u || frame[1] == 0u) break;
        frame_size = (version == 4u)
            ? syncsafe32(frame + 4u)
            : be32(frame + 4u);
        payload_off = pos + 10u;
        if (frame_size == 0u || payload_off + (size_t)frame_size > end) break;
        if (memcmp(frame, "TIT2", 4u) == 0) {
            copy_tag_text(m->info.title, ZCSR_MUSIC_TEXT_CAP,
                         data + payload_off + 1u, (size_t)frame_size - 1u, data[payload_off]);
        } else if (memcmp(frame, "TPE1", 4u) == 0) {
            copy_tag_text(m->info.artist, ZCSR_MUSIC_TEXT_CAP,
                         data + payload_off + 1u, (size_t)frame_size - 1u, data[payload_off]);
        } else if (memcmp(frame, "TALB", 4u) == 0) {
            copy_tag_text(m->info.album, ZCSR_MUSIC_TEXT_CAP,
                         data + payload_off + 1u, (size_t)frame_size - 1u, data[payload_off]);
        } else if (memcmp(frame, "TYER", 4u) == 0) {
            char year_text[ZCSR_MUSIC_TEXT_CAP];
            copy_tag_text(year_text, sizeof(year_text), data + payload_off + 1u,
                          (size_t)frame_size - 1u, data[payload_off]);
            m->info.year = parse_uint_prefix(year_text, strlen(year_text));
        } else if (memcmp(frame, "TRCK", 4u) == 0) {
            char track_text[ZCSR_MUSIC_TEXT_CAP];
            copy_tag_text(track_text, sizeof(track_text), data + payload_off + 1u,
                          (size_t)frame_size - 1u, data[payload_off]);
            m->info.track = parse_uint_prefix(track_text, strlen(track_text));
        } else if (memcmp(frame, "APIC", 4u) == 0) {
            parse_apic(m, data + payload_off, (size_t)frame_size);
        }
        pos = payload_off + (size_t)frame_size;
    }
}

/* ---- capability / spec / detection ---- */

bool zcsr_music_format_supported(zcsr_music_format fmt) {
    switch (fmt) {
        case ZCSR_MUSIC_OGG:
        case ZCSR_MUSIC_MP3:
        case ZCSR_MUSIC_WAV:
        case ZCSR_MUSIC_FLAC:
            return true;
        case ZCSR_MUSIC_AAC:
        case ZCSR_MUSIC_M4A:
        case ZCSR_MUSIC_OPUS:
            return zcsr_ffmpeg_available() != 0;
        default:
            return false;
    }
}

bool zcsr_music_get_spec(const zcsr_music* music, zcsr_music_spec* out_spec) {
    const struct zcsr_music* m = (const struct zcsr_music*)music;
    if (!m || !out_spec) return false;
    memset(out_spec, 0, sizeof(*out_spec));
    out_spec->sample_rate = (uint32_t)m->src_sample_rate;
    out_spec->channels = (uint16_t)m->src_channels;
    out_spec->bits_per_sample = m->spec_bits;
    out_spec->total_frames = m->spec_frames;
    return true;
}

bool zcsr_music_detect_format(const void* encoded, size_t len,
                               zcsr_music_format* out_fmt) {
    const uint8_t* p;
    if (!encoded || len == 0u || !out_fmt) return false;
    p = (const uint8_t*)encoded;

    if (len >= 4u && memcmp(p, "fLaC", 4u) == 0) {
        *out_fmt = ZCSR_MUSIC_FLAC;
        return true;
    }
    if (len >= 12u && memcmp(p, "RIFF", 4u) == 0 && memcmp(p + 8u, "WAVE", 4u) == 0) {
        *out_fmt = ZCSR_MUSIC_WAV;
        return true;
    }
    if (len >= 4u && memcmp(p, "OggS", 4u) == 0) {
        /* Opus streams carry an "OpusHead" identifier in their first header
         * packet; Vorbis does not. Search only the first page worth of bytes. */
        size_t scan = len < 4096u ? len : 4096u;
        size_t i;
        for (i = 0u; i + 8u <= scan; ++i) {
            if (memcmp(p + i, "OpusHead", 8u) == 0) {
                *out_fmt = ZCSR_MUSIC_OPUS;
                return true;
            }
        }
        *out_fmt = ZCSR_MUSIC_OGG;
        return true;
    }
    if (len >= 12u && memcmp(p + 4u, "ftyp", 4u) == 0) {
        /* ISO BMFF: AAC in MP4/M4A. */
        *out_fmt = ZCSR_MUSIC_M4A;
        return true;
    }
    if (len >= 3u && memcmp(p, "ID3", 3u) == 0) {
        *out_fmt = ZCSR_MUSIC_MP3;
        return true;
    }
    /* ADTS AAC: 12-bit frame sync 0xFFF; the ADTS layer field is 00, which
     * distinguishes it from an MP3 frame sync. */
    if (len >= 2u && p[0] == 0xFFu && (p[1] & 0xF6u) == 0xF0u) {
        *out_fmt = ZCSR_MUSIC_AAC;
        return true;
    }
    if (len >= 2u && p[0] == 0xFFu && (p[1] & 0xE0u) == 0xE0u) {
        *out_fmt = ZCSR_MUSIC_MP3;
        return true;
    }
    return false;
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
    s = safe_add(s, MUSIC_FLAC_ALLOC_BYTES, &ok);
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
    if (!zcsr_music_format_supported(fmt)) return NULL;
    needed = zcsr_music_buffer_bytes_required(len);
    if (needed == 0 || bytes < needed) return NULL;
    p = align_up((uint8_t*)buffer, 16);
    if ((size_t)(p - (uint8_t*)buffer) + needed > bytes) return NULL;
    m = (struct zcsr_music*)p;
    p += align_up_sz(sizeof(*m), 8);
    memset(m, 0, sizeof(*m));
    enc_dst = p;
    p += len;
    p = align_up(p, 8);
    m->vorbis_alloc_buf = p;
    p += MUSIC_VORBIS_ALLOC_BYTES;
    p = align_up(p, 8);
    m->flac_arena_buf = p;
    p += MUSIC_FLAC_ALLOC_BYTES;
    p = align_up(p, 8);
    m->src_pcm_f = (float*)p;
    p += MUSIC_SRC_MAX_FRAMES * 2u * sizeof(float);
    p = align_up(p, 8);
    m->out_pcm = (int16_t*)p;
    p += MUSIC_OUT_MAX_FRAMES * 2u * sizeof(int16_t);
    if ((size_t)(p - (uint8_t*)buffer) > bytes) return NULL;
    memcpy(enc_dst, encoded, len);
    m->fmt = fmt;
    m->encoded_copy = enc_dst;
    m->encoded_len = len;
    m->vorbis_alloc_bytes = MUSIC_VORBIS_ALLOC_BYTES;
    m->flac_arena_bytes = MUSIC_FLAC_ALLOC_BYTES;
    m->flac_arena_offset = 0u;
    atomic_store(&m->state, ZCSR_MUSIC_STOPPED);
    m->stream_channel = -1;
    m->threaded = true;
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
            unsigned samples;
            m->src_sample_rate = info.sample_rate;
            m->src_channels = info.channels;
            samples = stb_vorbis_stream_length_in_samples(m->decoder.ogg.handle);
            m->spec_bits = 0u;
            m->spec_frames = (uint64_t)samples;
            m->duration_ms = samples_to_ms((uint64_t)samples, info.sample_rate);
            parse_ogg_metadata(m);
        }
    } else if (fmt == ZCSR_MUSIC_MP3) {
        int scan_rate = 0;
        m->duration_ms = mp3_scan_duration(m->encoded_copy, m->encoded_len, &scan_rate);
        if (scan_rate > 0) m->src_sample_rate = scan_rate;
        mp3dec_init(&m->decoder.mp3.dec);
        m->decoder.mp3.frame_offset = 0;
        parse_id3_metadata(m);
        /* sample rate / channels filled on first successful decode */
        m->spec_bits = 0u;
        m->spec_frames = 0u;
    } else if (fmt == ZCSR_MUSIC_WAV) {
        if (!wav_parse(m)) {
            memset(m, 0, sizeof(*m));
            return NULL;
        }
    } else if (fmt == ZCSR_MUSIC_FLAC) {
        ma_allocation_callbacks cb;
        drflac* handle;
        memset(&cb, 0, sizeof(cb));
        cb.pUserData = m;
        cb.onMalloc = flac_arena_malloc;
        cb.onRealloc = NULL;
        cb.onFree = flac_arena_free;
        handle = drflac_open_memory_with_metadata(
            m->encoded_copy, m->encoded_len, flac_meta_proc, m, &cb);
        if (!handle) {
            memset(m, 0, sizeof(*m));
            return NULL;
        }
        if (handle->channels == 0u || handle->channels > 2u) {
            drflac_close(handle);
            memset(m, 0, sizeof(*m));
            return NULL;
        }
        m->decoder.flac.handle = handle;
        m->src_sample_rate = (int)handle->sampleRate;
        m->src_channels = (int)handle->channels;
        m->spec_bits = handle->bitsPerSample;
        m->spec_frames = handle->totalPCMFrameCount;
        m->duration_ms = samples_to_ms(handle->totalPCMFrameCount,
                                       (int)handle->sampleRate);
    } else if (fmt == ZCSR_MUSIC_AAC || fmt == ZCSR_MUSIC_M4A ||
               fmt == ZCSR_MUSIC_OPUS) {
        zcsr_ffmpeg_backend* be = zcsr_ffmpeg_open(
            m->encoded_copy, m->encoded_len,
            &m->src_sample_rate, &m->src_channels,
            &m->spec_frames, &m->duration_ms);
        if (!be) {
            memset(m, 0, sizeof(*m));
            return NULL;
        }
        if (m->src_channels == 0 || m->src_channels > 2) {
            zcsr_ffmpeg_close(be);
            memset(m, 0, sizeof(*m));
            return NULL;
        }
        m->decoder.ffmpeg.handle = be;
        m->spec_bits = 0u;
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

static float wav_read_sample(const uint8_t* p, uint16_t format, uint16_t bits) {
    if (format == 3u) {
        if (bits == 32u) {
            uint32_t raw = le32(p);
            float v;
            memcpy(&v, &raw, sizeof(v));
            return clampf(v, -1.0f, 1.0f);
        } else {
            uint64_t raw = (uint64_t)le32(p) | ((uint64_t)le32(p + 4u) << 32);
            double v;
            memcpy(&v, &raw, sizeof(v));
            return clampf((float)v, -1.0f, 1.0f);
        }
    }
    if (bits == 8u) {
        return ((int)p[0] - 128) / 128.0f;
    }
    if (bits == 16u) {
        int16_t v = (int16_t)le16(p);
        return (float)v / 32768.0f;
    }
    if (bits == 24u) {
        uint32_t u = (uint32_t)p[0] |
                     ((uint32_t)p[1] << 8) |
                     ((uint32_t)p[2] << 16);
        int32_t v;
        if ((u & 0x00800000u) != 0u) u |= 0xFF000000u;
        v = (int32_t)u;
        return (float)v / 8388608.0f;
    }
    if (bits == 32u) {
        int32_t v = (int32_t)le32(p);
        return (float)((double)v / 2147483648.0);
    }
    return 0.0f;
}

static size_t decode_block_wav(zcsr_music* m, decode_status* st) {
    size_t frame;
    size_t frames_left;
    size_t want;
    size_t out = 0u;
    size_t bytes_per_sample = (size_t)((m->decoder.wav.bits + 7u) / 8u);

    if (!m->decoder.wav.frame_bytes || !bytes_per_sample) {
        *st = DEC_ERROR;
        return 0u;
    }
    if (m->decoder.wav.data_pos >= m->decoder.wav.data_bytes) {
        *st = DEC_EOF;
        return 0u;
    }
    frames_left = (m->decoder.wav.data_bytes - m->decoder.wav.data_pos) /
                  (size_t)m->decoder.wav.frame_bytes;
    want = frames_left < MUSIC_SRC_MAX_FRAMES ? frames_left : MUSIC_SRC_MAX_FRAMES;
    for (frame = 0u; frame < want; ++frame) {
        const uint8_t* src = (const uint8_t*)m->encoded_copy +
            m->decoder.wav.data_offset + m->decoder.wav.data_pos;
        float left = wav_read_sample(src, m->decoder.wav.format,
                                     m->decoder.wav.bits);
        float right = left;
        if (m->decoder.wav.channels == 2u) {
            right = wav_read_sample(src + bytes_per_sample,
                                    m->decoder.wav.format,
                                    m->decoder.wav.bits);
        }
        m->src_pcm_f[out * 2u + 0u] = left;
        m->src_pcm_f[out * 2u + 1u] = right;
        ++out;
        m->decoder.wav.data_pos += (size_t)m->decoder.wav.frame_bytes;
    }
    *st = DEC_OK;
    return out;
}

static size_t decode_block_flac(zcsr_music* m, decode_status* st) {
    int ch = m->src_channels > 0 ? m->src_channels : 2;
    uint64_t got;
    if (ch > 2) { *st = DEC_ERROR; return 0u; }
    got = drflac_read_pcm_frames_f32(m->decoder.flac.handle,
            (uint64_t)MUSIC_SRC_MAX_FRAMES, m->src_pcm_f);
    if (got == 0u) { *st = DEC_EOF; return 0u; }
    if (ch == 1) {
        size_t i;
        for (i = (size_t)got; i-- > 0u; ) {
            float v = m->src_pcm_f[i];
            m->src_pcm_f[i * 2u + 0u] = v;
            m->src_pcm_f[i * 2u + 1u] = v;
        }
    }
    *st = DEC_OK;
    return (size_t)got;
}

static size_t decode_block_ffmpeg(zcsr_music* m, decode_status* st) {
    int zff = ZFF_ERROR;
    size_t n = zcsr_ffmpeg_decode(m->decoder.ffmpeg.handle, m->src_pcm_f,
                                   MUSIC_SRC_MAX_FRAMES, &zff);
    if (zff == ZFF_EOF) { *st = DEC_EOF; return 0u; }
    if (zff != ZFF_OK) { *st = DEC_ERROR; return 0u; }
    *st = DEC_OK;
    return n;
}

static size_t decode_block(zcsr_music* m, decode_status* st) {
    if (m->fmt == ZCSR_MUSIC_OGG) return decode_block_ogg(m, st);
    if (m->fmt == ZCSR_MUSIC_MP3) return decode_block_mp3(m, st);
    if (m->fmt == ZCSR_MUSIC_WAV) return decode_block_wav(m, st);
    if (m->fmt == ZCSR_MUSIC_FLAC) return decode_block_flac(m, st);
    if (m->fmt == ZCSR_MUSIC_AAC || m->fmt == ZCSR_MUSIC_M4A ||
        m->fmt == ZCSR_MUSIC_OPUS) return decode_block_ffmpeg(m, st);
    *st = DEC_ERROR;
    return 0u;
}

/* Position the decoder at the target. OGG uses stb_vorbis's sample index;
 * MP3 re-initializes and decodes forward because no sparse index is allocated. */
static void decoder_rewind(zcsr_music* m) {
    if (!m) return;
    if (m->fmt == ZCSR_MUSIC_OGG) {
        stb_vorbis_seek_start(m->decoder.ogg.handle);
    } else if (m->fmt == ZCSR_MUSIC_MP3) {
        mp3dec_init(&m->decoder.mp3.dec);
        m->decoder.mp3.frame_offset = 0;
    } else if (m->fmt == ZCSR_MUSIC_WAV) {
        m->decoder.wav.data_pos = 0;
    } else if (m->fmt == ZCSR_MUSIC_FLAC) {
        drflac_seek_to_pcm_frame(m->decoder.flac.handle, 0u);
    } else if (m->fmt == ZCSR_MUSIC_AAC || m->fmt == ZCSR_MUSIC_M4A ||
               m->fmt == ZCSR_MUSIC_OPUS) {
        zcsr_ffmpeg_rewind(m->decoder.ffmpeg.handle);
    }
}

static bool decoder_seek_to_ms(zcsr_music* m, uint64_t ms) {
    if (!m || m->src_sample_rate <= 0) return false;
    if (m->fmt == ZCSR_MUSIC_WAV) {
        uint64_t rate = (uint64_t)m->src_sample_rate;
        uint64_t frame;
        uint64_t byte;
        if (ms > UINT64_MAX / rate) return false;
        frame = (ms * rate) / 1000u;
        if (frame > (uint64_t)(m->decoder.wav.data_bytes /
                              (size_t)m->decoder.wav.frame_bytes)) {
            frame = (uint64_t)(m->decoder.wav.data_bytes /
                               (size_t)m->decoder.wav.frame_bytes);
        }
        if (frame > (uint64_t)SIZE_MAX / (uint64_t)m->decoder.wav.frame_bytes) {
            return false;
        }
        byte = frame * (uint64_t)m->decoder.wav.frame_bytes;
        m->decoder.wav.data_pos = (size_t)byte;
        m->src_pcm_frames = 0u;
        return true;
    }
    if (m->fmt == ZCSR_MUSIC_FLAC) {
        uint64_t rate = (uint64_t)m->src_sample_rate;
        uint64_t frame;
        if (ms > UINT64_MAX / rate) return false;
        frame = (ms * rate) / 1000u;
        if (frame > m->decoder.flac.handle->totalPCMFrameCount) {
            frame = m->decoder.flac.handle->totalPCMFrameCount;
        }
        return drflac_seek_to_pcm_frame(m->decoder.flac.handle, frame) != 0;
    }
    if (m->fmt == ZCSR_MUSIC_OGG) {
        uint64_t sample = (ms * (uint64_t)m->src_sample_rate) / 1000u;
        if (sample > (uint64_t)UINT_MAX) return false;
        return stb_vorbis_seek(m->decoder.ogg.handle, (unsigned)sample) != 0;
    }
    if (m->fmt == ZCSR_MUSIC_MP3) {
        size_t offset = 0;
        uint64_t decoded_ms = 0;
        mp3dec_t dec;
        mp3dec_frame_info_t info;
        float pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];

        mp3dec_init(&dec);
        while (offset < m->encoded_len && decoded_ms < ms) {
            size_t remaining = m->encoded_len - offset;
            int samples;
            if (remaining > (size_t)INT_MAX) return false;
            memset(&info, 0, sizeof(info));
            samples = mp3dec_decode_frame(&dec,
                (const uint8_t*)m->encoded_copy + offset,
                (int)remaining, pcm, &info);
            if (info.frame_bytes == 0) return false;
            offset += (size_t)info.frame_bytes;
            if (samples > 0) {
                int rate = info.hz > 0 ? info.hz : m->src_sample_rate;
                if (rate <= 0) return false;
                decoded_ms += ((uint64_t)samples * 1000u) / (uint64_t)rate;
                m->src_sample_rate = rate;
                m->src_channels = info.channels > 2 ? 2 : info.channels;
            }
        }
        m->decoder.mp3.dec = dec;
        m->decoder.mp3.frame_offset = offset;
        m->src_pcm_frames = 0;
        return true;
    }
    if (m->fmt == ZCSR_MUSIC_AAC || m->fmt == ZCSR_MUSIC_M4A ||
        m->fmt == ZCSR_MUSIC_OPUS) {
        bool ok = zcsr_ffmpeg_seek_ms(m->decoder.ffmpeg.handle, ms);
        m->src_pcm_frames = 0u;
        return ok;
    }
    return false;
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

/* Apply a pending seek on the thread that owns the decoder (decoder thread or
 * the host's pump loop). Flushes mixer PCM and rebases position. */
static bool music_apply_seek(zcsr_music* m) {
    uint64_t target;
    uint64_t consumed;
    if (!m) return false;
    target = atomic_load(&m->seek_target_ms);
    atomic_store(&m->seek_requested, false);
    (void)zcsr_mix_stream_flush(m->mixer, m->stream_channel);
    if (!decoder_seek_to_ms(m, target)) {
        music_set_error(m, ZCSR_MUSIC_ERR_DECODE);
        atomic_store(&m->state, ZCSR_MUSIC_ERROR);
        return false;
    }
    m->src_pcm_frames = 0;
    m->resample_phase = 0.0f;
    m->out_pending_frames = 0;
    m->out_pending_offset = 0;
    m->frames_sent = 0;
    consumed = zcsr_mix_stream_consumed_frames(m->mixer, m->stream_channel);
    atomic_store(&m->session_base_frames, (unsigned int)consumed);
    atomic_store(&m->position_base_frames, consumed);
    atomic_store(&m->position_base_ms, target);
    return true;
}

static void music_thread_main(zcsr_music* m) {
    while (!atomic_load(&m->stop_requested)) {
        if (atomic_load(&m->seek_requested)) {
            if (!music_apply_seek(m)) break;
            continue;
        }
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
                music_set_error(m, ZCSR_MUSIC_ERR_DECODE);
                atomic_store(&m->state, ZCSR_MUSIC_ERROR);
                break;
            }
            if (st == DEC_EOF) {
                if (atomic_load(&m->loop)) {
                    decoder_rewind(m);
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
    if (m->stream_channel < 0) {
        music_set_error(m, ZCSR_MUSIC_ERR_NO_DEVICE);
        return false;
    }
    m->mixer = mixer;
    music_set_error(m, ZCSR_MUSIC_ERR_NONE);
    return true;
}

bool zcsr_music_play(zcsr_music* m) {
    uint64_t start_ms;
    uint64_t start_frames;
    if (!m || !m->mixer) {
        music_set_error(m, ZCSR_MUSIC_ERR_NO_DEVICE);
        return false;
    }
    start_ms = m->play_start_ms;
    if (m->started) zcsr_music_stop(m);
    if (!decoder_seek_to_ms(m, start_ms)) {
        music_set_error(m, ZCSR_MUSIC_ERR_DECODE);
        atomic_store(&m->state, ZCSR_MUSIC_ERROR);
        return false;
    }
    m->resample_phase = 0.0f;
    m->out_pending_frames = 0;
    m->out_pending_offset = 0;
    m->frames_sent = 0;
    m->play_start_ms = start_ms;
    atomic_store(&m->position_base_ms, start_ms);
    atomic_store(&m->seek_requested, false);
    atomic_store(&m->stop_requested, false);
    atomic_store(&m->paused, false);
    start_frames = zcsr_mix_stream_consumed_frames(m->mixer, m->stream_channel);
    atomic_store(&m->session_base_frames, (unsigned int)start_frames);
    atomic_store(&m->position_base_frames, start_frames);
    music_set_error(m, ZCSR_MUSIC_ERR_NONE);
    /* set PLAYING before creating thread */
    atomic_store(&m->state, ZCSR_MUSIC_PLAYING);
    if (!m->threaded) {
        /* Host pumps on its own thread; no OS thread is created. */
        m->started = false;
        return true;
    }
#if defined(_WIN32)
    m->thread_handle = CreateThread(NULL, 0, music_thread_proc, m, 0, NULL);
    if (!m->thread_handle) {
        music_set_error(m, ZCSR_MUSIC_ERR_NO_DEVICE);
        atomic_store(&m->state, ZCSR_MUSIC_ERROR);
        return false;
    }
#else
    if (pthread_create(&m->thread_handle, NULL, music_thread_proc, m) != 0) {
        music_set_error(m, ZCSR_MUSIC_ERR_NO_DEVICE);
        atomic_store(&m->state, ZCSR_MUSIC_ERROR);
        return false;
    }
#endif
    m->started = true;
    return true;
}

void zcsr_music_stop(zcsr_music* m) {
    if (!m) return;
    if (m->started) {
        atomic_store(&m->stop_requested, true);
#if defined(_WIN32)
        WaitForSingleObject(m->thread_handle, INFINITE);
        CloseHandle(m->thread_handle);
        m->thread_handle = NULL;
#else
        pthread_join(m->thread_handle, NULL);
#endif
        m->started = false;
    } else {
        atomic_store(&m->stop_requested, true);
    }
    if (m->mixer) zcsr_mix_stream_flush(m->mixer, m->stream_channel);
    decoder_rewind(m);
    m->src_pcm_frames = 0;
    m->resample_phase = 0.0f;
    m->out_pending_frames = 0;
    m->out_pending_offset = 0;
    m->play_start_ms = 0;
    atomic_store(&m->position_base_ms, 0);
    atomic_store(&m->position_base_frames, 0);
    atomic_store(&m->seek_requested, false);
    atomic_store(&m->state, ZCSR_MUSIC_STOPPED);
}

void zcsr_music_destroy(zcsr_music* m) {
    if (!m) return;
    if (m->started) zcsr_music_stop(m);
    if (m->mixer && m->stream_channel >= 0)
        zcsr_mix_stream_detach(m->mixer, m->stream_channel);
    if (m->fmt == ZCSR_MUSIC_OGG && m->decoder.ogg.handle)
        stb_vorbis_close(m->decoder.ogg.handle);
    if (m->fmt == ZCSR_MUSIC_FLAC && m->decoder.flac.handle)
        drflac_close(m->decoder.flac.handle);
    if ((m->fmt == ZCSR_MUSIC_AAC || m->fmt == ZCSR_MUSIC_M4A ||
         m->fmt == ZCSR_MUSIC_OPUS) && m->decoder.ffmpeg.handle)
        zcsr_ffmpeg_close(m->decoder.ffmpeg.handle);
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
    uint64_t now, base, base_ms;
    if (!m) return 0;
    base_ms = atomic_load(&((zcsr_music*)m)->position_base_ms);
    if (!m->mixer) return base_ms;
    now = zcsr_mix_stream_consumed_frames(m->mixer, m->stream_channel);
    base = atomic_load(&((zcsr_music*)m)->position_base_frames);
    if (now <= base) return base_ms;
    return base_ms + (now - base) * 1000u / 44100u;
}

bool zcsr_music_duration_ms(const zcsr_music* m, uint64_t* out_ms) {
    if (!m || !out_ms || m->duration_ms == 0) return false;
    *out_ms = m->duration_ms;
    return true;
}

bool zcsr_music_seek_ms(zcsr_music* m, uint64_t ms) {
    uint64_t target;
    zcsr_music_state state;

    if (!m || m->duration_ms == 0) return false;
    state = zcsr_music_get_state(m);
    if (state == ZCSR_MUSIC_ERROR) return false;
    target = (ms > m->duration_ms) ? m->duration_ms : ms;
    atomic_store(&m->position_base_ms, target);
    if (state == ZCSR_MUSIC_PLAYING || state == ZCSR_MUSIC_PAUSED) {
        /* The decoder thread owns the decoder; it consumes this request at the
         * top of its loop, flushes the mixer stream, and reseeks. */
        if (m->mixer) {
            atomic_store(&m->position_base_frames,
                zcsr_mix_stream_consumed_frames(m->mixer, m->stream_channel));
        }
        atomic_store(&m->seek_target_ms, target);
        atomic_store(&m->seek_requested, true);
    } else {
        /* STOPPED/ENDED: record the next play start. The decoder is not touched
         * until play(), which joins any old thread first. */
        m->play_start_ms = target;
        if (m->mixer) {
            atomic_store(&m->position_base_frames,
                zcsr_mix_stream_consumed_frames(m->mixer, m->stream_channel));
        }
        atomic_store(&m->seek_requested, false);
    }
    music_set_error(m, ZCSR_MUSIC_ERR_NONE);
    return true;
}

zcsr_music_error zcsr_music_get_error(const zcsr_music* m) {
    if (!m) return ZCSR_MUSIC_ERR_NONE;
    return (zcsr_music_error)atomic_load(&((zcsr_music*)m)->error_detail);
}

bool zcsr_music_get_info(const zcsr_music* m, zcsr_music_info* out_info) {
    if (!m || !out_info) return false;
    *out_info = m->info;
    return true;
}

size_t zcsr_music_cover_bytes_required(const zcsr_music* m) {
    return m ? m->cover_bytes : 0u;
}

bool zcsr_music_get_cover(const zcsr_music* m, void* buffer, size_t capacity) {
    if (!m || !buffer || m->cover_bytes == 0u || capacity < m->cover_bytes) return false;
    if (m->cover_base64) {
        size_t decoded = base64_decode_skip((const char*)m->cover_src,
                                            m->cover_src_bytes,
                                            (uint8_t*)buffer, capacity,
                                            m->cover_skip, m->cover_bytes);
        return decoded == m->cover_bytes;
    }
    if (!m->cover_src || m->cover_src_bytes < m->cover_bytes) return false;
    memcpy(buffer, m->cover_src, m->cover_bytes);
    return true;
}

bool zcsr_music_set_threaded(zcsr_music* m, bool threaded) {
    if (!m || m->started) return false;
    if (zcsr_music_get_state(m) != ZCSR_MUSIC_STOPPED) return false;
    m->threaded = threaded;
    return true;
}

bool zcsr_music_pump(zcsr_music* m, uint32_t max_frames) {
    zcsr_music_state state;
    if (!m || !m->mixer || m->threaded) return false;
    state = zcsr_music_get_state(m);
    if (state == ZCSR_MUSIC_ERROR || state == ZCSR_MUSIC_STOPPED ||
        state == ZCSR_MUSIC_ENDED) {
        return false;
    }
    if (max_frames == 0u) return true;
    if (atomic_load(&m->seek_requested)) {
        return music_apply_seek(m);
    }
    if (atomic_load(&m->paused)) return true;

    if (m->out_pending_frames > 0u) {
        size_t take = m->out_pending_frames;
        size_t sent;
        if (take > (size_t)max_frames) take = (size_t)max_frames;
        sent = zcsr_mix_feed_stream(m->mixer, m->stream_channel,
            m->out_pcm + m->out_pending_offset * 2, take);
        m->out_pending_offset += sent;
        m->out_pending_frames -= sent;
        m->frames_sent += sent;
        return true;
    }
    if (m->src_pcm_frames >= 2u) {
        m->out_pending_frames = resample_to_scratch(m);
        m->out_pending_offset = 0;
        if (m->out_pending_frames > 0u) return true;
    }

    {
        decode_status st;
        size_t src_n = decode_block(m, &st);
        if (st == DEC_SKIP_NO_AUDIO) return true;
        if (st == DEC_ERROR) {
            music_set_error(m, ZCSR_MUSIC_ERR_DECODE);
            atomic_store(&m->state, ZCSR_MUSIC_ERROR);
            return false;
        }
        if (st == DEC_EOF) {
            if (atomic_load(&m->loop)) {
                decoder_rewind(m);
                m->src_pcm_frames = 0u;
                m->resample_phase = 0.0f;
                return true;
            }
            atomic_store(&m->state, ZCSR_MUSIC_ENDED);
            return true;
        }
        m->src_pcm_frames = src_n;
        m->resample_phase = 0.0f;
    }
    return true;
}
