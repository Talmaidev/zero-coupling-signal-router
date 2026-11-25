/* T0 contract test: animated WebP (t0_anim.webp).
 * Two frames, 500 ms each; disposal/blend compositing is handled by libwebp.
 *   frame 0: TL red / TR green / BL blue
 *   frame 1: TL blue / TR red  / BL green
 * Reads ZCSR_TEST_DATA_DIR, falling back to "data". */
#include "zcsr/image.h"
#include "zcsr/image_animation.h"

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
    static unsigned char arena[512 * 1024];
    static uint8_t fbuf[32 * 32 * 4];
    size_t len = 0;
    unsigned char* data = read_file("t0_anim.webp", &len);
    zcsr_image_anim_info info;
    zcsr_image_anim* anim;
    zcsr_image frame = { 0, 0, fbuf };
    size_t need;

    CK("fixture loaded", data != 0 && len > 0);
    if (!data) return 1;

    CK("info_of ok", zcsr_image_anim_info_of(data, len, &info));
    CK("anim width 32", info.w == 32);
    CK("anim height 32", info.h == 32);
    CK("anim frames >= 2", info.frame_count >= 2);
    CK("anim loop count valid", info.loop_count >= 0);

    need = zcsr_image_anim_buffer_bytes(data, len);
    CK("buffer_bytes positive", need > 0);

    anim = zcsr_image_anim_open(data, len, arena, sizeof arena);
    CK("open ok", anim != 0);
    if (!anim) { free(data); return 1; }

    CK("delay frame0 > 0", zcsr_image_anim_delay_ms(anim, 0) > 0);
    CK("delay frame1 > 0", zcsr_image_anim_delay_ms(anim, 1) > 0);

    CK("frame0 ok", zcsr_image_anim_frame(anim, 0, &frame));
    CK("frame0 TL red", px_match(frame.rgba, frame.w, 4, 4, 255, 0, 0, 8));
    CK("frame0 TR green", px_match(frame.rgba, frame.w, 20, 4, 0, 255, 0, 8));
    CK("frame0 BL blue", px_match(frame.rgba, frame.w, 4, 20, 0, 0, 255, 8));

    CK("frame1 ok", zcsr_image_anim_frame(anim, 1, &frame));
    CK("frame1 TL blue", px_match(frame.rgba, frame.w, 4, 4, 0, 0, 255, 8));
    CK("frame1 TR red", px_match(frame.rgba, frame.w, 20, 4, 255, 0, 0, 8));
    CK("frame1 BL green", px_match(frame.rgba, frame.w, 4, 20, 0, 255, 0, 8));

    CK("frame out of range false", !zcsr_image_anim_frame(anim, info.frame_count, &frame));
    CK("delay out of range 0", zcsr_image_anim_delay_ms(anim, -1) == 0);

    zcsr_image_anim_close(anim);

    {
        static const unsigned char garbage[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
        zcsr_image_anim_info bad;
        CK("garbage info_of false", !zcsr_image_anim_info_of(garbage, sizeof garbage, &bad));
    }
    free(data);
    if (!fails) printf("PASS: image_webp_anim\n");
    return fails ? 1 : 0;
}
