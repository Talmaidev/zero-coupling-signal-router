/* ICO/CUR container decoder.
 *
 * Parses the directory, selects the largest entry, and decodes the inner
 * payload: PNG entries go through the stb 8-bit path; bare BMP DIB entries are
 * wrapped by zcsr_image_decode_dib. Output pixels live in the caller buffer;
 * backend scratch stays behind the opaque handle with no retained per-frame
 * heap growth. CUR hotspots are intentionally ignored — this module produces
 * pixel buffers only.
 */
#include "zcsr/image.h"
#include "image_internal.h"

#include <stdint.h>
#include <string.h>

static uint16_t zcsr_rd16le(const uint8_t* p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t zcsr_rd32le(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int zcsr_is_png(const uint8_t* p, size_t len) {
    static const uint8_t png_sig[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
    return len >= 8 && memcmp(p, png_sig, 8) == 0;
}

static int zcsr_is_dib(const uint8_t* p, size_t len) {
    uint32_t bi_size;
    if (len < 40) return 0;
    bi_size = zcsr_rd32le(p);
    return bi_size == 40 || bi_size == 56 || bi_size == 108 || bi_size == 124;
}

zcsr_image zcsr_image_decode_container(const void* encoded, size_t encoded_len,
                                       void* buffer, size_t bytes) {
    const uint8_t* b = (const uint8_t*)encoded;
    uint16_t type;
    uint16_t count;
    int best = -1;
    uint32_t best_area = 0;
    size_t i;

    if (!b || encoded_len < 6 || !buffer || bytes == 0) {
        return (zcsr_image){ 0, 0, 0 };
    }
    if (zcsr_rd16le(b + 0) != 0) return (zcsr_image){ 0, 0, 0 };
    type = zcsr_rd16le(b + 2);
    if (type != 1 && type != 2) return (zcsr_image){ 0, 0, 0 };
    count = zcsr_rd16le(b + 4);
    if (count == 0) return (zcsr_image){ 0, 0, 0 };
    if (encoded_len < (size_t)6 + (size_t)count * 16u) {
        return (zcsr_image){ 0, 0, 0 }; /* truncated directory */
    }

    for (i = 0; i < count; ++i) {
        size_t e = 6u + i * 16u;
        uint32_t w = b[e + 0] == 0 ? 256u : (uint32_t)b[e + 0];
        uint32_t h = b[e + 1] == 0 ? 256u : (uint32_t)b[e + 1];
        uint32_t size = zcsr_rd32le(b + e + 8);
        uint32_t off = zcsr_rd32le(b + e + 12);
        uint32_t area;

        if (size == 0 || off == 0 || off > encoded_len || size > encoded_len - off) {
            return (zcsr_image){ 0, 0, 0 }; /* malformed entry: fail closed */
        }
        area = w * h;
        if (area > best_area) {
            best_area = area;
            best = (int)i;
        }
    }
    if (best < 0) return (zcsr_image){ 0, 0, 0 };

    {
        size_t e = 6u + (size_t)best * 16u;
        const uint8_t* inner = b + zcsr_rd32le(b + e + 12);
        uint32_t inner_len = zcsr_rd32le(b + e + 8);

        if (zcsr_is_png(inner, inner_len)) {
            return zcsr_image_decode_stb8(inner, inner_len, buffer, bytes);
        }
        if (inner_len >= 2 && inner[0] == 'B' && inner[1] == 'M') {
            return zcsr_image_decode_stb8(inner, inner_len, buffer, bytes);
        }
        if (zcsr_is_dib(inner, inner_len)) {
            return zcsr_image_decode_dib(inner, inner_len, buffer, bytes);
        }
        return (zcsr_image){ 0, 0, 0 }; /* unsupported inner payload */
    }
}
