/* Format signature detection and static capability table for the image module.
 * Pure memory sniffing: no decoding, no allocations. */
#include "zcsr/image.h"

#include <string.h>

static int zcsr_has(const uint8_t* b, size_t len, size_t need) {
    return b != 0 && len >= need;
}

static int zcsr_memeq(const uint8_t* b, const char* s, size_t n) {
    return memcmp(b, s, n) == 0;
}

zcsr_image_format zcsr_image_detect_format(const void* encoded, size_t encoded_len) {
    const uint8_t* b = (const uint8_t*)encoded;

    if (!zcsr_has(b, encoded_len, 1)) return ZCSR_IMAGE_FMT_UNKNOWN;

    /* PNG: 89 50 4E 47 0D 0A 1A 0A */
    if (zcsr_has(b, encoded_len, 8) &&
        b[0] == 0x89 && zcsr_memeq(b + 1, "PNG\r\n\x1a\n", 7)) {
        return ZCSR_IMAGE_FMT_PNG;
    }
    /* JPEG: FF D8 FF */
    if (zcsr_has(b, encoded_len, 3) && b[0] == 0xFF && b[1] == 0xD8 && b[2] == 0xFF) {
        return ZCSR_IMAGE_FMT_JPEG;
    }
    /* BMP: "BM" */
    if (zcsr_has(b, encoded_len, 2) && b[0] == 'B' && b[1] == 'M') {
        return ZCSR_IMAGE_FMT_BMP;
    }
    /* GIF: GIF87a / GIF89a */
    if (zcsr_has(b, encoded_len, 6) && zcsr_memeq(b, "GIF8", 4)) {
        return ZCSR_IMAGE_FMT_GIF;
    }
    /* WebP: RIFF....WEBP */
    if (zcsr_has(b, encoded_len, 12) && zcsr_memeq(b, "RIFF", 4) && zcsr_memeq(b + 8, "WEBP", 4)) {
        return ZCSR_IMAGE_FMT_WEBP;
    }
    /* Radiance HDR: "#?RADIANCE\n" or "#?RGBE\n" */
    if (zcsr_has(b, encoded_len, 11) && zcsr_memeq(b, "#?RADIANCE\n", 11)) {
        return ZCSR_IMAGE_FMT_HDR;
    }
    if (zcsr_has(b, encoded_len, 7) && zcsr_memeq(b, "#?RGBE\n", 7)) {
        return ZCSR_IMAGE_FMT_HDR;
    }
    /* ICO / CUR: reserved=0, type 1=icon 2=cursor */
    if (zcsr_has(b, encoded_len, 4) && b[0] == 0 && b[1] == 0) {
        uint16_t type = (uint16_t)(b[2] | ((uint16_t)b[3] << 8));
        if (type == 1) return ZCSR_IMAGE_FMT_ICO;
        if (type == 2) return ZCSR_IMAGE_FMT_CUR;
    }
    return ZCSR_IMAGE_FMT_UNKNOWN;
}

const zcsr_image_caps_info* zcsr_image_caps(void) {
    static const zcsr_image_caps_info caps = {
        .decode = {
            [ZCSR_IMAGE_FMT_PNG]      = true,
            [ZCSR_IMAGE_FMT_JPEG]     = true,
            [ZCSR_IMAGE_FMT_BMP]      = true,
            [ZCSR_IMAGE_FMT_GIF]      = true,
#if defined(ZCSR_HAVE_LIBWEBP)
            [ZCSR_IMAGE_FMT_WEBP]     = true,
#else
            [ZCSR_IMAGE_FMT_WEBP]      = false,
#endif
            [ZCSR_IMAGE_FMT_HDR]      = true,
            [ZCSR_IMAGE_FMT_ICO]      = true,
            [ZCSR_IMAGE_FMT_CUR]      = true,
        },
        .animated = true, /* GIF / animated WebP sequences decode multi-frame */
    };
    return &caps;
}

const char* zcsr_image_format_name(zcsr_image_format format) {
    static const char* const names[ZCSR_IMAGE_FMT_COUNT] = {
        [ZCSR_IMAGE_FMT_UNKNOWN]   = "unknown",
        [ZCSR_IMAGE_FMT_PNG]       = "png",
        [ZCSR_IMAGE_FMT_JPEG]      = "jpeg",
        [ZCSR_IMAGE_FMT_BMP]        = "bmp",
        [ZCSR_IMAGE_FMT_GIF]       = "gif",
        [ZCSR_IMAGE_FMT_WEBP]      = "webp",
        [ZCSR_IMAGE_FMT_HDR]        = "hdr",
        [ZCSR_IMAGE_FMT_ICO]        = "ico",
        [ZCSR_IMAGE_FMT_CUR]        = "cur",
    };
    if (format < 0 || format >= ZCSR_IMAGE_FMT_COUNT) return "unknown";
    return names[format];
}
