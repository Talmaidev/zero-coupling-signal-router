/* Contract tests for the ICO/CUR container decoder: directory parsing, largest
 * entry selection, inner PNG decode, and the malformed/failure paths. */
#include "zcsr/image.h"

#include <stdio.h>
#include <string.h>

static int fails = 0;
#define CK(label, cond) do { if (!(cond)) { printf("FAIL: %s\n", label); fails++; } } while (0)

/* Minimal valid 2x2 RGBA PNG (red, green / blue, white); same sample as test_image.c. */
static const unsigned char PNG_2x2[] = {
0x89,0x50,0x4e,0x47,0x0d,0x0a,0x1a,0x0a,0x00,0x00,0x00,0x0d,0x49,0x48,0x44,0x52,
0x00,0x00,0x00,0x02,0x00,0x00,0x00,0x02,0x08,0x06,0x00,0x00,0x00,0x72,0xb6,0x0d,
0x24,0x00,0x00,0x00,0x12,0x49,0x44,0x41,0x54,0x78,0xda,0x63,0xf8,0xcf,0xc0,0xf0,
0x1f,0x0c,0x81,0x34,0x18,0x00,0x00,0x49,0xc8,0x09,0xf7,0x03,0xd9,0x64,0xf1,0x00,
0x00,0x00,0x00,0x49,0x45,0x4e,0x44,0xae,0x42,0x60,0x82
};

/* Bare 32bpp uncompressed BMP DIB (no "BM" file header), as stored inside an
 * ICO entry. biHeight is doubled (2 image rows + 2 mask rows) per the icon
 * convention; pixels bottom-up: blue/white, red/green. */
static const unsigned char ICO_DIB_2x2[] = {
    0x28, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x01, 0x00, 0x20, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x12, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x00, 0x00, 0xff, 0xff, 0x00, 0xff, 0x00, 0xff, 0x00, 0x00,
};

static void w16(unsigned char* p, unsigned int v) {
    p[0] = (unsigned char)(v & 0xFFu);
    p[1] = (unsigned char)((v >> 8) & 0xFFu);
}

static void w32(unsigned char* p, unsigned int v) {
    p[0] = (unsigned char)(v & 0xFFu);
    p[1] = (unsigned char)((v >> 8) & 0xFFu);
    p[2] = (unsigned char)((v >> 16) & 0xFFu);
    p[3] = (unsigned char)((v >> 24) & 0xFFu);
}

/* Build a one-entry ICO/CUR around an arbitrary inner payload. */
static size_t build_one(unsigned char* out, unsigned int type,
                        const unsigned char* payload, size_t plen,
                        unsigned char w, unsigned char h) {
    size_t off = 6u + 16u;
    w16(out + 0, 0);
    w16(out + 2, type);
    w16(out + 4, 1);
    out[6] = w;
    out[7] = h;
    out[8] = 0;
    out[9] = 0;
    w16(out + 10, 1);
    w16(out + 12, 32);
    w32(out + 14, (unsigned int)plen);
    w32(out + 18, (unsigned int)off);
    memcpy(out + off, payload, plen);
    return off + plen;
}

static void test_ico_png(void) {
    static unsigned char encoded[256];
    static unsigned char buffer[128 * 1024];
    size_t len = build_one(encoded, 1, PNG_2x2, sizeof PNG_2x2, 2, 2);
    zcsr_image img = zcsr_image_decode(encoded, len, buffer, sizeof buffer);

    CK("ico detect", zcsr_image_detect_format(encoded, len) == ZCSR_IMAGE_FMT_ICO);
    CK("ico width", img.w == 2);
    CK("ico height", img.h == 2);
    CK("ico rgba", img.rgba != 0);
    if (img.rgba) {
        CK("ico inner red", img.rgba[0] == 255);
        CK("ico inner green", img.rgba[5] == 255);
        CK("ico inner blue", img.rgba[10] == 255);
    }
}

static void test_cur_png(void) {
    static unsigned char encoded[256];
    static unsigned char buffer[128 * 1024];
    size_t len = build_one(encoded, 2, PNG_2x2, sizeof PNG_2x2, 2, 2);
    zcsr_image img = zcsr_image_decode(encoded, len, buffer, sizeof buffer);

    CK("cur detect", zcsr_image_detect_format(encoded, len) == ZCSR_IMAGE_FMT_CUR);
    CK("cur decodes", img.w == 2 && img.h == 2 && img.rgba != 0);
}

/* Inner bare BMP DIB: wrapped in a synthetic BM header by the DIB route. */
static void test_ico_dib(void) {
    static unsigned char encoded[256];
    static unsigned char buffer[128 * 1024];
    size_t len = build_one(encoded, 1, ICO_DIB_2x2, sizeof ICO_DIB_2x2, 2, 2);
    zcsr_image img = zcsr_image_decode(encoded, len, buffer, sizeof buffer);
    int saw_red = 0, saw_green = 0, saw_blue = 0, saw_white = 0;
    int i;

    CK("ico dib width", img.w == 2);
    CK("ico dib height", img.h == 2);
    CK("ico dib rgba", img.rgba != 0);
    if (!img.rgba) return;
    for (i = 0; i < 4; ++i) {
        int r = img.rgba[i * 4 + 0];
        int g = img.rgba[i * 4 + 1];
        int b = img.rgba[i * 4 + 2];
        if (r > 100 && g < 60 && b < 60) saw_red = 1;
        if (g > 100 && r < 60 && b < 60) saw_green = 1;
        if (b > 100 && r < 60 && g < 60) saw_blue = 1;
        if (r > 200 && g > 200 && b > 200) saw_white = 1;
    }
    CK("ico dib red", saw_red);
    CK("ico dib green", saw_green);
    CK("ico dib blue", saw_blue);
    CK("ico dib white", saw_white);
}

/* Two entries: a 1x1 entry whose inner payload is garbage, then the valid 2x2
 * PNG. The selector must pick the larger entry and decode it. */
static void test_largest_entry(void) {
    static unsigned char encoded[256];
    static unsigned char buffer[128 * 1024];
    static const unsigned char garbage[4] = { 0x01, 0x02, 0x03, 0x04 };
    size_t off0 = 6u + 16u + 16u;       /* header + two directory entries */
    size_t off1 = off0 + sizeof garbage;

    w16(encoded + 0, 0);
    w16(encoded + 2, 1);
    w16(encoded + 4, 2);
    /* entry 0: 1x1, garbage inner */
    encoded[6] = 1; encoded[7] = 1; encoded[8] = 0; encoded[9] = 0;
    w16(encoded + 10, 1); w16(encoded + 12, 32);
    w32(encoded + 14, (unsigned int)sizeof garbage);
    w32(encoded + 18, (unsigned int)off0);
    /* entry 1: 2x2, valid PNG inner */
    encoded[22] = 2; encoded[23] = 2; encoded[24] = 0; encoded[25] = 0;
    w16(encoded + 26, 1); w16(encoded + 28, 32);
    w32(encoded + 30, (unsigned int)sizeof PNG_2x2);
    w32(encoded + 34, (unsigned int)off1);
    memcpy(encoded + off0, garbage, sizeof garbage);
    memcpy(encoded + off1, PNG_2x2, sizeof PNG_2x2);

    zcsr_image img = zcsr_image_decode(encoded, off1 + sizeof PNG_2x2, buffer, sizeof buffer);
    CK("largest entry selected", img.w == 2 && img.h == 2 && img.rgba != 0);
}

static void test_malformed(void) {
    static unsigned char buffer[128 * 1024];
    static unsigned char encoded[256];
    zcsr_image img;

    /* Zero entries. */
    w16(encoded + 0, 0); w16(encoded + 2, 1); w16(encoded + 4, 0);
    CK("ico zero entries invalid",
       zcsr_image_decode(encoded, 6, buffer, sizeof buffer).w == 0);

    /* Truncated directory: claims 2 entries, only header present. */
    w16(encoded + 0, 0); w16(encoded + 2, 1); w16(encoded + 4, 2);
    CK("ico truncated dir invalid",
       zcsr_image_decode(encoded, 10, buffer, sizeof buffer).w == 0);

    /* Entry offset out of range. */
    build_one(encoded, 1, PNG_2x2, sizeof PNG_2x2, 2, 2);
    w32(encoded + 18, 99999u);
    CK("ico bad offset invalid",
       zcsr_image_decode(encoded, 22 + sizeof PNG_2x2, buffer, sizeof buffer).w == 0);

    /* Unsupported inner payload (WebP bytes inside the container). */
    {
        static const unsigned char webp[12] = { 'R','I','F','F',0,0,0,0,'W','E','B','P' };
        size_t len = build_one(encoded, 1, webp, sizeof webp, 2, 2);
        img = zcsr_image_decode(encoded, len, buffer, sizeof buffer);
        CK("ico unsupported inner invalid", img.w == 0 && img.rgba == 0);
    }

    /* Null / undersized guards. */
    CK("ico null invalid", zcsr_image_decode(0, 0, buffer, sizeof buffer).w == 0);
    CK("ico null buffer invalid",
       zcsr_image_decode(encoded, 22, 0, 0).rgba == 0);
}

int main(void) {
    test_ico_png();
    test_cur_png();
    test_ico_dib();
    test_largest_entry();
    test_malformed();
    if (!fails) printf("PASS: image ico/cur container\n");
    return fails ? 1 : 0;
}
