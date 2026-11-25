/* T0 contract test: BMP static decode (t0.bmp).
 * Fixture: 32x32, TL red / TR green / BL blue / BR white. BMP is exact.
 * Reads ZCSR_TEST_DATA_DIR, falling back to "data". */
#include "zcsr/image.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static int fails = 0;
#define CK(label, cond) do { if (!(cond)) { printf("FAIL: %s\n", label); fails++; } } while (0)

static const char* data_dir(void) {
    const char* e = getenv("ZCSR_TEST_DATA_DIR");
    return (e && *e) ? e : "data";
}

static unsigned char* read_file(const char* name, size_t* out_len) {
    char path[1024];
    FILE* f;
    long sz;
    unsigned char* buf;

    snprintf(path, sizeof path, "%s/%s", data_dir(), name);
    f = fopen(path, "rb");
    if (!f) return 0;
    if (fseek(f, 0, SEEK_END) != 0 || (sz = ftell(f)) < 0) { fclose(f); return 0; }
    rewind(f);
    buf = (unsigned char*)malloc((size_t)sz + 1u);
    if (!buf) { fclose(f); return 0; }
    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) { free(buf); fclose(f); return 0; }
    fclose(f);
    *out_len = (size_t)sz;
    return buf;
}

static int px_match(const uint8_t* rgba, int w, int x, int y,
                    int r, int g, int b, int tol) {
    const uint8_t* p = rgba + (size_t)(y * w + x) * 4;
    return abs((int)p[0] - r) <= tol && abs((int)p[1] - g) <= tol &&
           abs((int)p[2] - b) <= tol && p[3] > 200;
}

int main(void) {
    static unsigned char arena[256 * 1024];
    size_t len = 0;
    unsigned char* data = read_file("t0.bmp", &len);
    zcsr_image img;

    CK("fixture loaded", data != 0 && len > 0);
    if (!data) return 1;

    img = zcsr_image_decode(data, len, arena, sizeof arena);
    CK("bmp width 32", img.w == 32);
    CK("bmp height 32", img.h == 32);
    CK("bmp rgba", img.rgba != 0);
    if (img.rgba) {
        CK("bmp TL red", px_match(img.rgba, img.w, 4, 4, 255, 0, 0, 0));
        CK("bmp TR green", px_match(img.rgba, img.w, 20, 4, 0, 255, 0, 0));
        CK("bmp BL blue", px_match(img.rgba, img.w, 4, 20, 0, 0, 255, 0));
        CK("bmp BR white", px_match(img.rgba, img.w, 20, 20, 255, 255, 255, 0));
    }

    /* Disguised garbage: BM signature but a corrupt DIB header. Must fail
     * closed, not crash. */
    {
        static const unsigned char fake_bmp[] = {
            'B', 'M', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
        };
        CK("garbage bmp invalid",
           zcsr_image_decode(fake_bmp, sizeof fake_bmp, arena, sizeof arena).w == 0);
    }
    free(data);
    if (!fails) printf("PASS: image_bmp\n");
    return fails ? 1 : 0;
}
