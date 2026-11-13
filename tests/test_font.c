/* test_font.c — headless tests for zcsr_font (TrueType loader/rasterizer).
 * Uses embedded Inter.ttf subset from tests/data/test_ttf.h. */
#include "zcsr/font.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "data/test_ttf.h"

int main(void) {
    static unsigned char buf[2 * 1024 * 1024];
    static unsigned char glyph[128 * 128];
    zcsr_font* f;
    zcsr_font_metrics_info m;
    zcsr_font_glyph_info g;

    /* load */
    f = zcsr_font_load(test_ttf, test_ttf_len, 24, buf, sizeof(buf));
    assert(f);

    /* metrics */
    assert(zcsr_font_get_metrics(f, &m));
    assert(m.line_height > 0.0f);

    /* rasterize 'A' */
    assert(zcsr_font_rasterize(f, 'A', glyph, sizeof(glyph), &g));
    assert(g.width > 0 && g.height > 0);
    assert(g.a8 != NULL);
    assert(g.advance_x > 0.0f);

    /* same glyph twice — consistent */
    {
        zcsr_font_glyph_info g2;
        assert(zcsr_font_rasterize(f, 'A', glyph, sizeof(glyph), &g2));
        assert(g2.width == g.width && g2.height == g.height);
    }

    /* space: zero bitmap but valid advance */
    assert(zcsr_font_rasterize(f, ' ', glyph, sizeof(glyph), &g));
    assert(g.width == 0);
    assert(g.advance_x > 0.0f);

    /* invalid codepoint */
    assert(!zcsr_font_rasterize(f, 0x10FFFF, glyph, sizeof(glyph), &g));

    /* buffer too small */
    assert(!zcsr_font_rasterize(f, 'A', glyph, 1, &g));

    /* buffer_bytes_required sanity */
    {
        size_t need = zcsr_font_buffer_bytes_required(test_ttf_len);
        assert(need > 0);
        assert(need <= sizeof(buf));
    }

    zcsr_font_destroy(f);
    printf("test_font PASS\n");
    return 0;
}
