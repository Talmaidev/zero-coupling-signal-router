/* zcsr music module — streaming OGG/MP3/FLAC/WAV/AAC/M4A decoder with
 * background thread. Feeds decoded/resampled PCM into an audiomix stream
 * channel. Native codecs use a caller buffer + fixed internal scratch. */
#include "zcsr/music.h"
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
#pragma warning(disable: 4244 4245 4456 4457 4701 4702 4706 4996)
#endif
#define MINIMP3_FLOAT_OUTPUT
#include "../../third_party/minimp3.h"
#define STB_VORBIS_HEADER_ONLY
#include "../../third_party/stb_vorbis.c"
#undef STB_VORBIS_HEADER_ONLY
#define DR_FLAC_IMPLEMENTATION
#include "../../third_party/dr_flac.h"
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#  include <mfapi.h>
#  include <mfidl.h>
#  include <mfreadwrite.h>
#  include <mferror.h>
#  include <mftransform.h>
#  include <propvarutil.h>
#else
#  include <time.h>
#  include <pthread.h>
#endif

#define MUSIC_VORBIS_ALLOC_BYTES (256u * 1024u)
#define MUSIC_FLAC_ALLOC_BYTES   (1024u * 1024u)
#define MUSIC_SRC_MAX_FRAMES 4096
#define MUSIC_OUT_MAX_FRAMES 4096
#define MUSIC_SLACK 15

typedef enum { DEC_OK, DEC_EOF, DEC_SKIP_NO_AUDIO, DEC_ERROR } decode_status;

/* Bounded arena used by dr_flac. dr_flac may grow its seek table with realloc;
 * each block stores its own size so realloc can copy the old bytes without
 * introducing an unbounded global heap dependency. */
typedef struct {
    size_t size;
} music_arena_header;

typedef struct {
    uint8_t* base;
    size_t cap;
    size_t used;
} music_arena;

static void* music_arena_alloc(size_t size, void* user) {
    music_arena* a = (music_arena*)user;
    size_t need;
    uint8_t* p;
    music_arena_header* h;
    if (!a || !a->base || size > SIZE_MAX - sizeof(*h) - 16u) return NULL;
    need = (size + sizeof(*h) + 15u) & ~(size_t)15u;
    if (a->used > a->cap || need > a->cap - a->used) return NULL;
    p = a->base + a->used;
    h = (music_arena_header*)p;
    h->size = size;
    a->used += need;
    return (void*)(h + 1);
}

static void* music_arena_realloc(void* ptr, size_t size, void* user) {
    music_arena_header* old;
    void* fresh;
    if (!ptr) return music_arena_alloc(size, user);
    old = ((music_arena_header*)ptr) - 1;
    fresh = music_arena_alloc(size, user);
    if (!fresh) return NULL;
    if (old->size > 0) {
        size_t copy = old->size < size ? old->size : size;
        memcpy(fresh, ptr, copy);
    }
    return fresh;
}

static void music_arena_free(void* ptr, void* user) {
    (void)ptr;
    (void)user;
}

struct zcsr_music {
    zcsr_music_format fmt;
    void* encoded_copy;
    size_t encoded_len;
    union {
        struct { stb_vorbis* handle; } ogg;
        struct { mp3dec_t dec; size_t frame_offset; } mp3;
        struct { drflac* handle; } flac;
        struct {
            size_t data_offset;
            size_t data_end;
            size_t frame_offset;
            unsigned block_align;
            unsigned bits;
            bool is_float;
        } wav;
#if defined(_WIN32)
        struct {
            IMFSourceReader* reader;
            IMFSample* sample;
            IMFMediaBuffer* buffer;
            uint8_t* buffer_data;
            size_t buffer_offset;
            size_t buffer_bytes;
            bool eof;
            wchar_t temp_path[MAX_PATH];
        } aac;
#endif
    } decoder;
    uint8_t* vorbis_alloc_buf;
    size_t vorbis_alloc_bytes;
    uint8_t* flac_alloc_buf;
    size_t flac_alloc_bytes;
    music_arena flac_arena;
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

static uint32_t be32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static uint32_t be24(const uint8_t* p) {
    return ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | (uint32_t)p[2];
}

static uint16_t be16(const uint8_t* p) {
    return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

static uint32_t le32(const uint8_t* p) {
    return ((uint32_t)p[3] << 24) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[1] << 8) | (uint32_t)p[0];
}

static uint16_t le16(const uint8_t* p) {
    return (uint16_t)(((uint16_t)p[1] << 8) | (uint16_t)p[0]);
}

static uint64_t be64(const uint8_t* p) {
    return ((uint64_t)be32(p) << 32) | (uint64_t)be32(p + 4);
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
 * bytes (also bounded by capacity). No heap. */
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

static void parse_vorbis_entry(zcsr_music* m, const uint8_t* text, size_t len) {
    const uint8_t* eq;
    size_t key_len;
    size_t value_len;
    if (!m || !text || len == 0u) return;
    eq = (const uint8_t*)memchr(text, '=', len);
    if (!eq) return;
    key_len = (size_t)(eq - text);
    value_len = len - key_len - 1u;
    if (ascii_key_equal((const char*)text, key_len, "TITLE")) {
        copy_trimmed(m->info.title, ZCSR_MUSIC_TEXT_CAP, (const char*)eq + 1, value_len);
    } else if (ascii_key_equal((const char*)text, key_len, "ARTIST")) {
        copy_trimmed(m->info.artist, ZCSR_MUSIC_TEXT_CAP, (const char*)eq + 1, value_len);
    } else if (ascii_key_equal((const char*)text, key_len, "ALBUM")) {
        copy_trimmed(m->info.album, ZCSR_MUSIC_TEXT_CAP, (const char*)eq + 1, value_len);
    } else if (ascii_key_equal((const char*)text, key_len, "DATE")) {
        if (m->info.year == 0u) m->info.year = parse_uint_prefix((const char*)eq + 1, value_len);
    } else if (ascii_key_equal((const char*)text, key_len, "TRACKNUMBER") ||
               ascii_key_equal((const char*)text, key_len, "TRACK")) {
        if (m->info.track == 0u) m->info.track = parse_uint_prefix((const char*)eq + 1, value_len);
    }
}

static void parse_vorbis_comment_block(zcsr_music* m, const uint8_t* p, size_t len) {
    uint32_t vendor_len;
    uint32_t count;
    size_t pos;
    size_t i;
    if (!m || !p || len < 8u) return;
    vendor_len = le32(p);
    pos = 4u + (size_t)vendor_len;
    if (pos > len || len - pos < 4u) return;
    count = le32(p + pos);
    pos += 4u;
    for (i = 0; i < count && pos + 4u <= len; ++i) {
        uint32_t entry_len = le32(p + pos);
        pos += 4u;
        if ((size_t)entry_len > len - pos) break;
        parse_vorbis_entry(m, p + pos, (size_t)entry_len);
        pos += (size_t)entry_len;
    }
}

static void parse_flac_picture(zcsr_music* m, const uint8_t* p, size_t len) {
    uint32_t mime_len;
    uint32_t desc_len;
    uint32_t data_len;
    size_t pos;
    if (!m || !p || m->cover_bytes != 0u || len < 32u) return;
    mime_len = be32(p + 4u);
    pos = 8u + (size_t)mime_len;
    if (pos > len || len - pos < 4u) return;
    desc_len = be32(p + pos);
    pos += 4u + (size_t)desc_len;
    if (pos > len || len - pos < 24u) return;
    /* width, height, depth, colors, then the encoded image length. */
    data_len = be32(p + pos + 16u);
    pos += 20u;
    if ((size_t)data_len > len - pos || data_len == 0u) return;
    m->cover_src = p + pos;
    m->cover_src_bytes = (size_t)data_len;
    m->cover_bytes = (size_t)data_len;
    m->cover_skip = 0u;
    m->cover_base64 = false;
}

static void parse_flac_metadata(zcsr_music* m) {
    const uint8_t* data;
    size_t len;
    size_t pos;
    if (!m || !m->encoded_copy) return;
    data = (const uint8_t*)m->encoded_copy;
    len = m->encoded_len;
    if (len < 4u || memcmp(data, "fLaC", 4u) != 0) return;
    pos = 4u;
    while (pos + 4u <= len) {
        uint8_t header = data[pos];
        uint32_t size = be24(data + pos + 1u);
        pos += 4u;
        if ((size_t)size > len - pos) break;
        if ((header & 0x7fu) == 4u) {
            parse_vorbis_comment_block(m, data + pos, (size_t)size);
        } else if ((header & 0x7fu) == 6u) {
            parse_flac_picture(m, data + pos, (size_t)size);
        }
        pos += (size_t)size;
        if ((header & 0x80u) != 0u) break;
    }
}

static bool mp4_box_at(const uint8_t* data, size_t pos, size_t end,
                       const char* type, size_t* payload, size_t* payload_end,
                       size_t* next) {
    uint64_t box_size;
    size_t header = 8u;
    if (!data || pos > end || end - pos < 8u) return false;
    box_size = be32(data + pos);
    if (box_size == 1u) {
        if (end - pos < 16u) return false;
        box_size = be64(data + pos + 8u);
        header = 16u;
    } else if (box_size == 0u) {
        box_size = (uint64_t)(end - pos);
    }
    if (box_size < header || box_size > (uint64_t)(end - pos)) return false;
    if (payload) *payload = pos + header;
    if (payload_end) *payload_end = pos + (size_t)box_size;
    if (next) *next = pos + (size_t)box_size;
    if (type && memcmp(data + pos + 4u, type, 4u) != 0) return false;
    return true;
}

static bool mp4_find_child(const uint8_t* data, size_t start, size_t end,
                           const char* type, size_t* payload, size_t* payload_end) {
    size_t pos = start;
    size_t next = 0;
    while (pos + 8u <= end) {
        if (mp4_box_at(data, pos, end, type, payload, payload_end, &next)) return true;
        if (next <= pos) break;
        pos = next;
    }
    return false;
}

static void parse_m4a_item(zcsr_music* m, const uint8_t* data, size_t start,
                           size_t end, const char* type) {
    size_t payload = 0;
    size_t payload_end = 0;
    uint32_t data_type;
    const uint8_t* value;
    size_t value_len;
    if (!m || !mp4_find_child(data, start, end, "data", &payload, &payload_end)) return;
    if (payload_end - payload < 8u) return;
    data_type = be32(data + payload);
    value = data + payload + 8u;
    value_len = payload_end - payload - 8u;
    if (memcmp(type, "\xA9" "nam", 4u) == 0) {
        copy_trimmed(m->info.title, ZCSR_MUSIC_TEXT_CAP, (const char*)value, value_len);
    } else if (memcmp(type, "\xA9" "ART", 4u) == 0) {
        copy_trimmed(m->info.artist, ZCSR_MUSIC_TEXT_CAP, (const char*)value, value_len);
    } else if (memcmp(type, "\xA9" "alb", 4u) == 0) {
        copy_trimmed(m->info.album, ZCSR_MUSIC_TEXT_CAP, (const char*)value, value_len);
    } else if (memcmp(type, "\xA9" "day", 4u) == 0) {
        m->info.year = parse_uint_prefix((const char*)value, value_len);
    } else if (memcmp(type, "trkn", 4u) == 0) {
        if (value_len >= 4u) m->info.track = be16(value + 2u);
    } else if (memcmp(type, "covr", 4u) == 0) {
        if (m->cover_bytes == 0u && value_len > 0u &&
            (data_type == 13u || data_type == 14u || data_type == 0u)) {
            m->cover_src = value;
            m->cover_src_bytes = value_len;
            m->cover_bytes = value_len;
            m->cover_skip = 0u;
            m->cover_base64 = false;
        }
    }
}

static void parse_m4a_metadata(zcsr_music* m) {
    const uint8_t* data;
    size_t len;
    size_t moov_start = 0;
    size_t moov_end = 0;
    size_t udta_start = 0;
    size_t udta_end = 0;
    size_t meta_start = 0;
    size_t meta_end = 0;
    size_t ilst_start = 0;
    size_t ilst_end = 0;
    size_t pos;
    size_t next;
    if (!m || !m->encoded_copy) return;
    data = (const uint8_t*)m->encoded_copy;
    len = m->encoded_len;
    pos = 0;
    while (pos + 8u <= len) {
        if (mp4_box_at(data, pos, len, "moov", &moov_start, &moov_end, &next)) break;
        if (next <= pos) return;
        pos = next;
    }
    if (!moov_start || !mp4_find_child(data, moov_start, moov_end, "udta",
                                       &udta_start, &udta_end)) return;
    if (!mp4_find_child(data, udta_start, udta_end, "meta", &meta_start, &meta_end)) return;
    if (meta_start + 4u >= meta_end) return;
    if (!mp4_find_child(data, meta_start + 4u, meta_end, "ilst", &ilst_start, &ilst_end)) return;
    pos = ilst_start;
    while (pos + 8u <= ilst_end) {
        size_t item_start = 0;
        size_t item_end = 0;
        if (!mp4_box_at(data, pos, ilst_end, NULL, &item_start, &item_end, &next)) break;
        if (item_end - item_start >= 8u) {
            const uint8_t* type = data + pos + 4u;
            parse_m4a_item(m, data, item_start, item_end, (const char*)type);
        }
        if (next <= pos) break;
        pos = next;
    }
}

static void parse_wav_info(zcsr_music* m, const uint8_t* p, size_t len) {
    size_t pos;
    if (!m || !p || len < 4u || memcmp(p, "INFO", 4u) != 0) return;
    pos = 4u;
    while (pos + 8u <= len) {
        const uint8_t* id = p + pos;
        uint32_t size = le32(p + pos + 4u);
        pos += 8u;
        if ((size_t)size > len - pos) break;
        if (memcmp(id, "INAM", 4u) == 0) {
            copy_trimmed(m->info.title, ZCSR_MUSIC_TEXT_CAP, (const char*)p + pos, (size_t)size);
        } else if (memcmp(id, "IART", 4u) == 0) {
            copy_trimmed(m->info.artist, ZCSR_MUSIC_TEXT_CAP, (const char*)p + pos, (size_t)size);
        } else if (memcmp(id, "IPRD", 4u) == 0) {
            copy_trimmed(m->info.album, ZCSR_MUSIC_TEXT_CAP, (const char*)p + pos, (size_t)size);
        } else if (memcmp(id, "ICRD", 4u) == 0) {
            m->info.year = parse_uint_prefix((const char*)p + pos, (size_t)size);
        } else if (memcmp(id, "ITRK", 4u) == 0) {
            m->info.track = parse_uint_prefix((const char*)p + pos, (size_t)size);
        }
        pos += (size_t)size;
        if ((size & 1u) != 0u && pos < len) ++pos; /* RIFF chunks are word aligned. */
    }
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

/* ---- WAV parser ---- */

static bool parse_wav(zcsr_music* m) {
    const uint8_t* data;
    size_t len;
    size_t pos;
    bool have_fmt = false;
    bool have_data = false;
    uint16_t format_tag = 0u;
    uint16_t channels = 0u;
    uint32_t sample_rate = 0u;
    uint16_t block_align = 0u;
    uint16_t bits = 0u;
    if (!m || !m->encoded_copy) return false;
    data = (const uint8_t*)m->encoded_copy;
    len = m->encoded_len;
    if (len < 12u || memcmp(data, "RIFF", 4u) != 0 || memcmp(data + 8u, "WAVE", 4u) != 0) {
        return false;
    }
    pos = 12u;
    while (pos + 8u <= len) {
        const uint8_t* id = data + pos;
        uint32_t size = le32(data + pos + 4u);
        size_t payload = pos + 8u;
        if ((size_t)size > len - payload) break;
        if (memcmp(id, "fmt ", 4u) == 0 && size >= 16u) {
            format_tag = le16(data + payload);
            channels = le16(data + payload + 2u);
            sample_rate = le32(data + payload + 4u);
            block_align = le16(data + payload + 12u);
            bits = le16(data + payload + 14u);
            if (format_tag == 0xFFFEu && size >= 40u) {
                format_tag = le16(data + payload + 24u);
            }
            have_fmt = true;
        } else if (memcmp(id, "data", 4u) == 0) {
            m->decoder.wav.data_offset = payload;
            m->decoder.wav.data_end = payload + (size_t)size;
            have_data = true;
        } else if (memcmp(id, "LIST", 4u) == 0) {
            parse_wav_info(m, data + payload, (size_t)size);
        }
        pos = payload + (size_t)size;
        if ((size & 1u) != 0u && pos < len) ++pos;
    }
    if (!have_fmt || !have_data) return false;
    if (format_tag != 1u && format_tag != 3u) return false;
    if (channels == 0u || channels > 2u || sample_rate == 0u) return false;
    if (bits != 8u && bits != 16u && bits != 24u && bits != 32u) return false;
    if (block_align < (uint16_t)(channels * ((bits + 7u) / 8u))) return false;
    if (m->decoder.wav.data_end > len) m->decoder.wav.data_end = len;
    if (m->decoder.wav.data_end < m->decoder.wav.data_offset) return false;
    m->decoder.wav.block_align = block_align;
    m->decoder.wav.bits = bits;
    m->decoder.wav.is_float = (format_tag == 3u);
    m->decoder.wav.frame_offset = m->decoder.wav.data_offset;
    m->src_sample_rate = (int)sample_rate;
    m->src_channels = (int)channels;
    {
        size_t frames = (m->decoder.wav.data_end - m->decoder.wav.data_offset) / block_align;
        m->duration_ms = samples_to_ms((uint64_t)frames, (int)sample_rate);
    }
    return true;
}

/* ---- Windows Media Foundation AAC/M4A backend ---- */
#if defined(_WIN32)

static INIT_ONCE g_mf_once = INIT_ONCE_STATIC_INIT;

static BOOL CALLBACK music_mf_init_once(PINIT_ONCE once, PVOID param, PVOID* ctx) {
    HRESULT co;
    (void)once;
    (void)param;
    (void)ctx;
    co = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(co) && co != RPC_E_CHANGED_MODE) return FALSE;
    return SUCCEEDED(MFStartup(MF_VERSION, MFSTARTUP_FULL)) ? TRUE : FALSE;
}

static bool music_mf_init(void) {
    return InitOnceExecuteOnce(&g_mf_once, music_mf_init_once, NULL, NULL) != FALSE;
}

static bool music_make_temp_file(const void* data, size_t len, const wchar_t* ext,
                                 wchar_t* path, size_t path_cap) {
    static LONG counter = 0;
    WCHAR dir[MAX_PATH];
    HANDLE file;
    const uint8_t* src = (const uint8_t*)data;
    if (!data || !path || path_cap < 16u || !ext) return false;
    if (!GetTempPathW(MAX_PATH, dir)) return false;
    if (swprintf(path, path_cap, L"%s\\zcsr_music_%lu_%ld.%s",
                 dir, GetCurrentProcessId(), (long)InterlockedIncrement(&counter), ext) < 0) {
        return false;
    }
    file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_NEW,
                       FILE_ATTRIBUTE_TEMPORARY, NULL);
    if (file == INVALID_HANDLE_VALUE) return false;
    while (len > 0u) {
        DWORD chunk = (DWORD)(len > (size_t)(1u << 20) ? (1u << 20) : len);
        DWORD written = 0;
        if (!WriteFile(file, src, chunk, &written, NULL) || written != chunk) {
            CloseHandle(file);
            DeleteFileW(path);
            return false;
        }
        src += chunk;
        len -= chunk;
    }
    CloseHandle(file);
    return true;
}

static void music_aac_release_buffer(zcsr_music* m) {
    if (!m) return;
    if (m->decoder.aac.buffer) {
        if (m->decoder.aac.buffer_data) {
            m->decoder.aac.buffer->lpVtbl->Unlock(m->decoder.aac.buffer);
            m->decoder.aac.buffer_data = NULL;
        }
        m->decoder.aac.buffer->lpVtbl->Release(m->decoder.aac.buffer);
        m->decoder.aac.buffer = NULL;
    }
    if (m->decoder.aac.sample) {
        m->decoder.aac.sample->lpVtbl->Release(m->decoder.aac.sample);
        m->decoder.aac.sample = NULL;
    }
    m->decoder.aac.buffer_offset = 0u;
    m->decoder.aac.buffer_bytes = 0u;
}

static bool music_aac_open(zcsr_music* m, const wchar_t* ext) {
    wchar_t path[MAX_PATH];
    IMFSourceReader* reader = NULL;
    IMFMediaType* pcm = NULL;
    IMFMediaType* actual = NULL;
    PROPVARIANT duration;
    HRESULT hr;
    UINT32 channels = 2u;
    UINT32 rate = 44100u;
    UINT32 bits = 16u;
    if (!m || !ext || !music_mf_init()) return false;
    if (!music_make_temp_file(m->encoded_copy, m->encoded_len, ext, path, MAX_PATH)) {
        return false;
    }
    hr = MFCreateSourceReaderFromURL(path, NULL, &reader);
    if (FAILED(hr) || !reader) goto fail;
    reader->lpVtbl->SetStreamSelection(reader, (DWORD)MF_SOURCE_READER_ALL_STREAMS, FALSE);
    reader->lpVtbl->SetStreamSelection(reader, (DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, TRUE);
    hr = MFCreateMediaType(&pcm);
    if (FAILED(hr) || !pcm) goto fail;
    pcm->lpVtbl->SetGUID(pcm, &MF_MT_MAJOR_TYPE, &MFMediaType_Audio);
    pcm->lpVtbl->SetGUID(pcm, &MF_MT_SUBTYPE, &MFAudioFormat_PCM);
    pcm->lpVtbl->SetUINT32(pcm, &MF_MT_AUDIO_BITS_PER_SAMPLE, 16u);
    pcm->lpVtbl->SetUINT32(pcm, &MF_MT_AUDIO_NUM_CHANNELS, 2u);
    pcm->lpVtbl->SetUINT32(pcm, &MF_MT_AUDIO_SAMPLES_PER_SECOND, 44100u);
    pcm->lpVtbl->SetUINT32(pcm, &MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);
    hr = reader->lpVtbl->SetCurrentMediaType(reader,
        (DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, NULL, pcm);
    pcm->lpVtbl->Release(pcm);
    pcm = NULL;
    if (FAILED(hr)) goto fail;

    hr = reader->lpVtbl->GetCurrentMediaType(reader,
        (DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, &actual);
    if (SUCCEEDED(hr) && actual) {
        actual->lpVtbl->GetUINT32(actual, &MF_MT_AUDIO_NUM_CHANNELS, &channels);
        actual->lpVtbl->GetUINT32(actual, &MF_MT_AUDIO_SAMPLES_PER_SECOND, &rate);
        actual->lpVtbl->GetUINT32(actual, &MF_MT_AUDIO_BITS_PER_SAMPLE, &bits);
        actual->lpVtbl->Release(actual);
        actual = NULL;
    }
    if (channels == 0u || channels > 2u || rate == 0u || bits != 16u) goto fail;

    PropVariantInit(&duration);
    hr = reader->lpVtbl->GetPresentationAttribute(reader,
        (DWORD)MF_SOURCE_READER_MEDIASOURCE, &MF_PD_DURATION, &duration);
    if (SUCCEEDED(hr)) {
        if (duration.vt == VT_UI8 && duration.uhVal.QuadPart > 0u) {
            m->duration_ms = (uint64_t)(duration.uhVal.QuadPart / 10000u);
        } else if (duration.vt == VT_I8 && duration.hVal.QuadPart > 0) {
            m->duration_ms = (uint64_t)(duration.hVal.QuadPart / 10000u);
        }
    }
    PropVariantClear(&duration);

    m->decoder.aac.reader = reader;
    m->decoder.aac.sample = NULL;
    m->decoder.aac.buffer = NULL;
    m->decoder.aac.buffer_data = NULL;
    m->decoder.aac.buffer_offset = 0u;
    m->decoder.aac.buffer_bytes = 0u;
    m->decoder.aac.eof = false;
    memcpy(m->decoder.aac.temp_path, path, sizeof(path));
    m->src_sample_rate = (int)rate;
    m->src_channels = (int)channels;
    return true;

fail:
    if (actual) actual->lpVtbl->Release(actual);
    if (pcm) pcm->lpVtbl->Release(pcm);
    if (reader) reader->lpVtbl->Release(reader);
    DeleteFileW(path);
    return false;
}

static bool music_aac_seek(zcsr_music* m, uint64_t ms) {
    PROPVARIANT pos;
    HRESULT hr;
    if (!m || !m->decoder.aac.reader) return false;
    music_aac_release_buffer(m);
    PropVariantInit(&pos);
    pos.vt = VT_I8;
    pos.hVal.QuadPart = (LONGLONG)(ms * 10000u);
    hr = m->decoder.aac.reader->lpVtbl->SetCurrentPosition(m->decoder.aac.reader,
                                                           &GUID_NULL, &pos);
    PropVariantClear(&pos);
    m->decoder.aac.eof = false;
    return SUCCEEDED(hr);
}

static size_t decode_block_aac(zcsr_music* m, decode_status* st) {
    size_t frame_size;
    size_t frames;
    size_t i;
    int ch = m->src_channels > 0 ? m->src_channels : 2;
    if (!m || !m->decoder.aac.reader) { *st = DEC_ERROR; return 0u; }
    if (!m->decoder.aac.sample) {
        DWORD flags = 0;
        LONGLONG timestamp = 0;
        HRESULT hr = m->decoder.aac.reader->lpVtbl->ReadSample(
            m->decoder.aac.reader, (DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0,
            NULL, &flags, &timestamp, &m->decoder.aac.sample);
        if (FAILED(hr)) { *st = DEC_ERROR; return 0u; }
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) m->decoder.aac.eof = true;
        if (!m->decoder.aac.sample) {
            *st = m->decoder.aac.eof ? DEC_EOF : DEC_SKIP_NO_AUDIO;
            return 0u;
        }
        m->decoder.aac.buffer = NULL;
        m->decoder.aac.buffer_data = NULL;
        m->decoder.aac.buffer_offset = 0u;
        m->decoder.aac.buffer_bytes = 0u;
    }
    if (!m->decoder.aac.buffer) {
        IMFMediaBuffer* buffer = NULL;
        DWORD length = 0;
        HRESULT hr = m->decoder.aac.sample->lpVtbl->ConvertToContiguousBuffer(
            m->decoder.aac.sample, &buffer);
        if (FAILED(hr) || !buffer) { *st = DEC_ERROR; return 0u; }
        hr = buffer->lpVtbl->GetCurrentLength(buffer, &length);
        if (FAILED(hr)) { buffer->lpVtbl->Release(buffer); *st = DEC_ERROR; return 0u; }
        hr = buffer->lpVtbl->Lock(buffer, &m->decoder.aac.buffer_data, NULL, NULL);
        if (FAILED(hr)) { buffer->lpVtbl->Release(buffer); *st = DEC_ERROR; return 0u; }
        m->decoder.aac.buffer = buffer;
        m->decoder.aac.buffer_bytes = (size_t)length;
        m->decoder.aac.buffer_offset = 0u;
    }
    frame_size = (size_t)ch * 2u;
    if (m->decoder.aac.buffer_bytes - m->decoder.aac.buffer_offset < frame_size) {
        music_aac_release_buffer(m);
        *st = DEC_SKIP_NO_AUDIO;
        return 0u;
    }
    frames = (m->decoder.aac.buffer_bytes - m->decoder.aac.buffer_offset) / frame_size;
    if (frames > MUSIC_SRC_MAX_FRAMES) frames = MUSIC_SRC_MAX_FRAMES;
    {
        const uint8_t* p = m->decoder.aac.buffer_data + m->decoder.aac.buffer_offset;
        if (ch == 1) {
            for (i = 0; i < frames; ++i) {
                int16_t v = (int16_t)((uint16_t)p[i * 2u] | ((uint16_t)p[i * 2u + 1u] << 8));
                m->src_pcm_f[i * 2u] = (float)v / 32768.0f;
                m->src_pcm_f[i * 2u + 1u] = m->src_pcm_f[i * 2u];
            }
        } else {
            for (i = 0; i < frames * 2u; ++i) {
                int16_t v = (int16_t)((uint16_t)p[i * 2u] | ((uint16_t)p[i * 2u + 1u] << 8));
                m->src_pcm_f[i] = (float)v / 32768.0f;
            }
        }
    }
    m->decoder.aac.buffer_offset += frames * frame_size;
    if (m->decoder.aac.buffer_offset >= m->decoder.aac.buffer_bytes) {
        music_aac_release_buffer(m);
    }
    *st = DEC_OK;
    return frames;
}

#else

static bool music_aac_open(zcsr_music* m, const wchar_t* ext) {
    (void)m; (void)ext;
    return false;
}

static size_t decode_block_aac(zcsr_music* m, decode_status* st) {
    (void)m;
    *st = DEC_ERROR;
    return 0u;
}

static bool music_aac_seek(zcsr_music* m, uint64_t ms) {
    (void)m; (void)ms;
    return false;
}

#endif

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
    m->flac_alloc_buf = p;
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
    m->flac_alloc_bytes = MUSIC_FLAC_ALLOC_BYTES;
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
    } else if (fmt == ZCSR_MUSIC_FLAC) {
        drflac_allocation_callbacks callbacks;
        m->flac_arena.base = m->flac_alloc_buf;
        m->flac_arena.cap = m->flac_alloc_bytes;
        m->flac_arena.used = 0u;
        callbacks.pUserData = &m->flac_arena;
        callbacks.onMalloc = music_arena_alloc;
        callbacks.onRealloc = music_arena_realloc;
        callbacks.onFree = music_arena_free;
        m->decoder.flac.handle = drflac_open_memory(m->encoded_copy, m->encoded_len, &callbacks);
        if (!m->decoder.flac.handle) { memset(m, 0, sizeof(*m)); return NULL; }
        if (m->decoder.flac.handle->channels > 2u || m->decoder.flac.handle->sampleRate == 0u) {
            drflac_close(m->decoder.flac.handle);
            memset(m, 0, sizeof(*m));
            return NULL;
        }
        m->src_sample_rate = (int)m->decoder.flac.handle->sampleRate;
        m->src_channels = (int)m->decoder.flac.handle->channels;
        m->duration_ms = samples_to_ms(m->decoder.flac.handle->totalPCMFrameCount,
                                       m->src_sample_rate);
        parse_flac_metadata(m);
    } else if (fmt == ZCSR_MUSIC_WAV) {
        if (!parse_wav(m)) { memset(m, 0, sizeof(*m)); return NULL; }
    } else if (fmt == ZCSR_MUSIC_AAC || fmt == ZCSR_MUSIC_M4A) {
#if defined(_WIN32)
        if (fmt == ZCSR_MUSIC_M4A) parse_m4a_metadata(m);
        if (!music_aac_open(m, fmt == ZCSR_MUSIC_M4A ? L"m4a" : L"aac")) {
            memset(m, 0, sizeof(*m));
            return NULL;
        }
#else
        memset(m, 0, sizeof(*m));
        return NULL;
#endif
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

static size_t decode_block_flac(zcsr_music* m, decode_status* st) {
    unsigned ch = m->src_channels > 0 ? (unsigned)m->src_channels : 2u;
    drflac_uint64 got;
    if (!m->decoder.flac.handle) { *st = DEC_ERROR; return 0u; }
    got = drflac_read_pcm_frames_f32(m->decoder.flac.handle,
                                     MUSIC_SRC_MAX_FRAMES, m->src_pcm_f);
    if (got == 0u) { *st = DEC_EOF; return 0u; }
    if (ch == 1u) {
        size_t i;
        for (i = (size_t)got - 1u; i > 0u; --i) {
            float v = m->src_pcm_f[i];
            m->src_pcm_f[i * 2u] = v;
            m->src_pcm_f[i * 2u + 1u] = v;
        }
        m->src_pcm_f[1u] = m->src_pcm_f[0u];
    }
    *st = DEC_OK;
    return (size_t)got;
}

static size_t decode_block_wav(zcsr_music* m, decode_status* st) {
    size_t frame_size;
    size_t frames;
    size_t i;
    size_t bytes;
    unsigned ch = m->src_channels > 0 ? (unsigned)m->src_channels : 2u;
    if (!m->encoded_copy) { *st = DEC_ERROR; return 0u; }
    if (m->decoder.wav.frame_offset >= m->decoder.wav.data_end) {
        *st = DEC_EOF;
        return 0u;
    }
    frame_size = m->decoder.wav.block_align;
    bytes = (m->decoder.wav.bits + 7u) / 8u;
    if (frame_size == 0u || bytes == 0u || ch > 2u) { *st = DEC_ERROR; return 0u; }
    frames = (m->decoder.wav.data_end - m->decoder.wav.frame_offset) / frame_size;
    if (frames > MUSIC_SRC_MAX_FRAMES) frames = MUSIC_SRC_MAX_FRAMES;
    if (m->decoder.wav.is_float && m->decoder.wav.bits != 32u) {
        *st = DEC_ERROR;
        return 0u;
    }
    for (i = 0; i < frames; ++i) {
        const uint8_t* base = (const uint8_t*)m->encoded_copy +
                              m->decoder.wav.frame_offset + i * frame_size;
        unsigned c;
        for (c = 0; c < ch; ++c) {
            const uint8_t* p = base + c * bytes;
            float v;
            if (m->decoder.wav.is_float) {
                float f;
                memcpy(&f, p, sizeof(f));
                v = f;
            } else if (m->decoder.wav.bits == 8u) {
                v = ((float)p[0] - 128.0f) / 128.0f;
            } else if (m->decoder.wav.bits == 16u) {
                int16_t s = (int16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
                v = (float)s / 32768.0f;
            } else if (m->decoder.wav.bits == 24u) {
                int32_t s = (int32_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8) |
                                      ((uint32_t)p[2] << 16));
                if (s & 0x800000) s |= (int32_t)0xFF000000;
                v = (float)s / 8388608.0f;
            } else {
                uint32_t u = (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
                             ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
                v = (float)(int32_t)u / 2147483648.0f;
            }
            m->src_pcm_f[(i * 2u) + c] = clampf(v, -1.0f, 1.0f);
        }
        if (ch == 1u) {
            m->src_pcm_f[i * 2u + 1u] = m->src_pcm_f[i * 2u];
        }
    }
    m->decoder.wav.frame_offset += frames * frame_size;
    *st = DEC_OK;
    return frames;
}

static size_t decode_block(zcsr_music* m, decode_status* st) {
    switch (m->fmt) {
    case ZCSR_MUSIC_OGG:
        return decode_block_ogg(m, st);
    case ZCSR_MUSIC_MP3:
        return decode_block_mp3(m, st);
    case ZCSR_MUSIC_FLAC:
        return decode_block_flac(m, st);
    case ZCSR_MUSIC_WAV:
        return decode_block_wav(m, st);
    case ZCSR_MUSIC_AAC:
    case ZCSR_MUSIC_M4A:
        return decode_block_aac(m, st);
    default:
        *st = DEC_ERROR;
        return 0u;
    }
}

/* Position the decoder at the target. OGG uses stb_vorbis's sample index;
 * MP3 re-initializes and decodes forward because no sparse index is allocated. */
static bool decoder_seek_to_ms(zcsr_music* m, uint64_t ms) {
    if (!m || m->src_sample_rate <= 0) return false;
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
    if (m->fmt == ZCSR_MUSIC_FLAC) {
        uint64_t sample = (ms * (uint64_t)m->src_sample_rate) / 1000u;
        return drflac_seek_to_pcm_frame(m->decoder.flac.handle, sample) != 0;
    }
    if (m->fmt == ZCSR_MUSIC_WAV) {
        uint64_t sample = (ms * (uint64_t)m->src_sample_rate) / 1000u;
        size_t frame = (size_t)sample;
        size_t offset;
        if (m->decoder.wav.block_align == 0u) return false;
        offset = m->decoder.wav.data_offset + frame * m->decoder.wav.block_align;
        if (offset > m->decoder.wav.data_end) offset = m->decoder.wav.data_end;
        m->decoder.wav.frame_offset = offset;
        m->src_pcm_frames = 0;
        return true;
    }
    if (m->fmt == ZCSR_MUSIC_AAC || m->fmt == ZCSR_MUSIC_M4A) {
        return music_aac_seek(m, ms);
    }
    return false;
}

/* Rewind a decoder to its first frame. Used by stop, loop and pump. */
static bool decoder_restart(zcsr_music* m) {
    if (!m) return false;
    switch (m->fmt) {
    case ZCSR_MUSIC_OGG:
        return stb_vorbis_seek_start(m->decoder.ogg.handle) != 0;
    case ZCSR_MUSIC_MP3:
        mp3dec_init(&m->decoder.mp3.dec);
        m->decoder.mp3.frame_offset = 0;
        return true;
    case ZCSR_MUSIC_FLAC:
        return drflac_seek_to_pcm_frame(m->decoder.flac.handle, 0u) != 0;
    case ZCSR_MUSIC_WAV:
        m->decoder.wav.frame_offset = m->decoder.wav.data_offset;
        return true;
    case ZCSR_MUSIC_AAC:
    case ZCSR_MUSIC_M4A:
        return music_aac_seek(m, 0u);
    default:
        return false;
    }
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
                    decoder_restart(m);
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
    decoder_restart(m);
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
#if defined(_WIN32)
    if ((m->fmt == ZCSR_MUSIC_AAC || m->fmt == ZCSR_MUSIC_M4A) &&
        m->decoder.aac.reader) {
        music_aac_release_buffer(m);
        m->decoder.aac.reader->lpVtbl->Release(m->decoder.aac.reader);
        m->decoder.aac.reader = NULL;
        if (m->decoder.aac.temp_path[0] != L'\0') {
            DeleteFileW(m->decoder.aac.temp_path);
            m->decoder.aac.temp_path[0] = L'\0';
        }
    }
#endif
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
                    decoder_restart(m);
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
