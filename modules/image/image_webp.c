/* WebP static decode route.
 *
 * libwebp writes the packed RGBA canvas straight into the caller-provided
 * buffer via WebPDecodeRGBAInto; decoder scratch is transient and released by
 * libwebp before the call returns. Output pixels are caller-owned.
 *
 * Build contract: the module compiles with ZCSR_HAVE_LIBWEBP when the
 * prebuilt libwebp (ZCSR_LIBWEBP_ROOT; third_party/prebuilt/<os>/libwebp) is
 * available. Without it this route fails closed and zcsr_image_caps() reports
 * WEBP as unsupported; the build does not fail.
 *
 * This TU keeps to portable C: no platform/Windows types are referenced here.
 */
#include "zcsr/image.h"
#include "image_internal.h"

#include <stdint.h>
#include <stddef.h>

#if defined(ZCSR_HAVE_LIBWEBP)
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4244 4267 4189 4389)
#endif
#include <webp/decode.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
#endif

static zcsr_image zcsr_image_invalid(void) {
    zcsr_image img = { 0, 0, 0 };
    return img;
}

zcsr_image zcsr_image_decode_webp(const void* encoded, size_t encoded_len,
                                  void* buffer, size_t bytes) {
#if defined(ZCSR_HAVE_LIBWEBP)
    int w = 0;
    int h = 0;
    size_t pixel_bytes = 0;
    uint8_t* out;

    if (!encoded || encoded_len == 0 || !buffer || bytes == 0) return zcsr_image_invalid();
    if (!WebPGetInfo((const uint8_t*)encoded, encoded_len, &w, &h)) return zcsr_image_invalid();
    if (w <= 0 || h <= 0) return zcsr_image_invalid();
    if ((size_t)w > SIZE_MAX / (size_t)h || (size_t)w * (size_t)h > SIZE_MAX / 4u) {
        return zcsr_image_invalid();
    }
    pixel_bytes = (size_t)w * (size_t)h * 4u;
    if (bytes < pixel_bytes) return zcsr_image_invalid();

    out = (uint8_t*)buffer;
    if (!WebPDecodeRGBAInto((const uint8_t*)encoded, encoded_len,
                             out, pixel_bytes, w * 4)) {
        return zcsr_image_invalid();
    }
    return (zcsr_image){ w, h, out };
#else
    (void)encoded;
    (void)encoded_len;
    (void)buffer;
    (void)bytes;
    return zcsr_image_invalid();
#endif
}
