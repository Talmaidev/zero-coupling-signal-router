/* test_font_otf.c — T0 contract for zcsr_font with a real CFF OTF file fixture.
 *
 * Loads tests/data/t0.otf, a genuine OpenType/CFF font ('OTTO' sfnt version,
 * 'CFF ' table; NOT a renamed TTF). stb_truetype decodes it through its CFF
 * charstring branch. The test asserts real rasterization: sane metrics, non-empty,
 * non-all-zero A8 bitmaps for 'A' / 'H' / 'E-acute' with positive advance, an
 * uncovered codepoint reported absent without crashing, and kerning queries that
 * do not crash. Negative paths: truncated bytes, random bytes, OTF bytes patched to
 * advertise TrueType magic, empty input, and an undersized buffer all fail load
 * without crashing.
 *
 * Fixture directory is passed as argv[1] (see tests/engine/CMakeLists.txt).
 */
#include "zcsr/font.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static unsigned char* read_file(const char* path, size_t* out_len) {
    FILE* f = fopen(path, "rb");
    unsigned char* buf;
    long len;
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    len = ftell(f);
    if (len <= 0) { fclose(f); return NULL; }
    if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return NULL; }
    buf = (unsigned char*)malloc((size_t)len);
    if (!buf) { fclose(f); return NULL; }
    if (fread(buf, 1, (size_t)len, f) != (size_t)len) {
        free(buf);
        fclose(f);
        return NULL;
    }
    fclose(f);
    *out_len = (size_t)len;
    return buf;
}

static bool has_table(const unsigned char* d, size_t len, const char* tag) {
    uint16_t num;
    size_t i;
    if (len < 12) return false;
    num = (uint16_t)(((uint16_t)d[4] << 8) | (uint16_t)d[5]);
    for (i = 0; i < num; ++i) {
        size_t rec = 12u + 16u * i;
        if (rec + 4u > len) return false;
        if (memcmp(d + rec, tag, 4) == 0) return true;
    }
    return false;
}

static size_t count_nonzero(const zcsr_font_glyph_info* g) {
    size_t i, n = 0;
    if (!g->a8 || g->width <= 0 || g->height <= 0) return 0;
    for (i = 0; i < (size_t)g->width * (size_t)g->height; ++i)
        if (g->a8[i] != 0) ++n;
    return n;
}

static void fill_random(unsigned char* buf, size_t len) {
    uint32_t x = 0x9E3779B9u;
    size_t i;
    for (i = 0; i < len; ++i) {
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        buf[i] = (unsigned char)(x & 0xFFu);
    }
}

static zcsr_font* load_bytes(const unsigned char* bytes, size_t len,
                             unsigned char** out_buffer) {
    size_t needed = zcsr_font_buffer_bytes_required(len);
    unsigned char* buffer;
    zcsr_font* f;
    if (needed == 0) return NULL;
    buffer = (unsigned char*)malloc(needed);
    if (!buffer) return NULL;
    f = zcsr_font_load(bytes, len, 24, buffer, needed);
    if (!f) { free(buffer); return NULL; }
    *out_buffer = buffer;
    return f;
}

int main(int argc, char** argv) {
    const char* data_dir = (argc > 1) ? argv[1] : "data";
    char path[1024];
    unsigned char* bytes;
    size_t len;
    unsigned char* buffer = NULL;
    zcsr_font* f;
    zcsr_font_metrics_info m;
    zcsr_font_glyph_info g;
    unsigned char* glyph = NULL;
    size_t glyph_bytes;
    size_t nz;
    float kern;
    bool has_kern;

    snprintf(path, sizeof(path), "%s/%s", data_dir, "t0.otf");
    bytes = read_file(path, &len);
    assert(bytes && len > 0);
    printf("[otf] fixture: %s (%zu bytes)\n", path, len);

    /* Real CFF OTF: 'OTTO' sfnt version and a 'CFF ' table present. A renamed TTF
     * would carry glyf/loca and fail this structural check. */
    assert(bytes[0] == 'O' && bytes[1] == 'T' && bytes[2] == 'T' && bytes[3] == 'O');
    assert(has_table(bytes, len, "CFF "));
    assert(!has_table(bytes, len, "glyf"));
    printf("[otf] structure: OTTO magic, CFF table present, no glyf table\n");

    /* --- positive: load (stb CFF charstring branch) --- */
    f = load_bytes(bytes, len, &buffer);
    assert(f != NULL);
    printf("[otf] load: OK (CFF path)\n");

    /* --- metrics --- */
    assert(zcsr_font_get_metrics(f, &m));
    assert(m.ascent > 0.0f);
    assert(m.descent >= 0.0f);
    assert(m.line_gap >= 0.0f);
    assert(m.line_height > 0.0f);
    printf("[otf] metrics: ascent=%.2f descent=%.2f line_gap=%.2f line_height=%.2f\n",
           m.ascent, m.descent, m.line_gap, m.line_height);

    glyph_bytes = zcsr_font_glyph_buffer_bytes(f);
    assert(glyph_bytes > 0);
    glyph = (unsigned char*)malloc(glyph_bytes);
    assert(glyph != NULL);

    /* --- 'A' rasterized to a real A8 bitmap via CFF outlines --- */
    assert(zcsr_font_rasterize(f, 'A', glyph, glyph_bytes, &g));
    assert(g.width > 0 && g.height > 0);
    assert(g.a8 != NULL);
    assert(g.advance_x > 0.0f);
    nz = count_nonzero(&g);
    assert(nz > 0);
    printf("[otf] glyph 'A' (U+0041): %dx%d, nonzero=%zu, advance=%.2f\n",
           g.width, g.height, nz, g.advance_x);

    /* --- 'H' sanity, same engine --- */
    assert(zcsr_font_rasterize(f, 'H', glyph, glyph_bytes, &g));
    assert(g.width > 0 && g.height > 0);
    assert(g.advance_x > 0.0f);
    nz = count_nonzero(&g);
    assert(nz > 0);
    printf("[otf] glyph 'H' (U+0048): %dx%d, nonzero=%zu, advance=%.2f\n",
           g.width, g.height, nz, g.advance_x);

    /* --- non-ASCII: 'É' U+00C9, covered by this font --- */
    assert(zcsr_font_rasterize(f, 0x00C9, glyph, glyph_bytes, &g));
    assert(g.width > 0 && g.height > 0);
    assert(g.advance_x > 0.0f);
    nz = count_nonzero(&g);
    assert(nz > 0);
    printf("[otf] glyph 'É' (U+00C9): %dx%d, nonzero=%zu, advance=%.2f\n",
           g.width, g.height, nz, g.advance_x);

    /* --- uncovered codepoint: must report false, not crash --- */
    assert(!zcsr_font_rasterize(f, 0x4E2D, glyph, glyph_bytes, &g));
    printf("[otf] glyph U+4E2D: absent (rasterize=false), no crash\n");
    assert(!zcsr_font_rasterize(f, 0x10FFFF, glyph, glyph_bytes, &g));

    /* --- kerning query: false when no pair exists is still a pass --- */
    has_kern = zcsr_font_get_kerning(f, 'A', 'V', &kern);
    printf("[otf] kerning 'AV': %s (%+.2f)\n", has_kern ? "present" : "absent",
           has_kern ? kern : 0.0f);

    zcsr_font_destroy(f);
    free(buffer);
    free(glyph);

    /* --- negative: truncated font (first half; table records run past EOF) --- */
    {
        size_t half = len / 2;
        unsigned char* bad = NULL;
        zcsr_font* nf = load_bytes(bytes, half, &bad);
        assert(nf == NULL);
        printf("[otf] negative: truncated %zu/%zu bytes -> load=NULL\n", half, len);
    }

    /* --- negative: truncated to a header-sized prefix (directory past EOF) --- */
    {
        size_t tiny = len < 64u ? len : 64u;
        unsigned char* bad = NULL;
        zcsr_font* nf = load_bytes(bytes, tiny, &bad);
        assert(nf == NULL);
        printf("[otf] negative: truncated %zu-byte prefix -> load=NULL\n", tiny);
    }

    /* --- negative: random bytes --- */
    {
        unsigned char* junk = (unsigned char*)malloc(len);
        zcsr_font* nf;
        unsigned char* bad = NULL;
        assert(junk != NULL);
        fill_random(junk, len);
        nf = load_bytes(junk, len, &bad);
        assert(nf == NULL);
        free(junk);
        printf("[otf] negative: random bytes -> load=NULL\n");
    }

    /* --- negative: OTF bytes patched to advertise TrueType magic --- */
    {
        unsigned char* fake = (unsigned char*)malloc(len);
        zcsr_font* nf;
        unsigned char* bad = NULL;
        assert(fake != NULL);
        memcpy(fake, bytes, len);
        fake[0] = 0x00; fake[1] = 0x01; fake[2] = 0x00; fake[3] = 0x00;
        nf = load_bytes(fake, len, &bad);
        assert(nf == NULL);
        free(fake);
        printf("[otf] negative: OTF bytes with TrueType magic -> load=NULL\n");
    }

    /* --- negative: empty input / undersized buffer --- */
    {
        size_t needed = zcsr_font_buffer_bytes_required(len);
        unsigned char* small = (unsigned char*)malloc(needed - 1);
        assert(small != NULL);
        assert(zcsr_font_load(bytes, 0, 24, small, needed - 1) == NULL);
        assert(zcsr_font_load(bytes, len, 24, small, needed - 1) == NULL);
        free(small);
        printf("[otf] negative: empty input / undersized buffer -> load=NULL\n");
    }

    free(bytes);
    printf("test_font_otf PASS\n");
    return 0;
}
