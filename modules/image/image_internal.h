#ifndef ZCSR_IMAGE_INTERNAL_H
#define ZCSR_IMAGE_INTERNAL_H
/* Internal shared contract for the modules/image TUs. NOT part of the installed
 * public API: only files under modules/image/ may include this. */
#include "zcsr/image.h"

/* 8-bit stb_image path: decodes PNG/JPEG/BMP/GIF straight to RGBA8 from the
 * caller buffer. Shared by the public dispatcher and by the ICO/CUR container
 * parser (inner PNG/BMP entries). Returns {0,0,NULL} on failure / undersized
 * buffer. */
zcsr_image zcsr_image_decode_stb8(const void* encoded, size_t encoded_len,
                                  void* buffer, size_t bytes);

/* Header-only stb info probe: fills w/h for PNG/JPEG/BMP/GIF. Returns false
 * when stb cannot identify the bytes. Pure decode probe, no output pixels. */
int zcsr_image_stb8_info(const void* encoded, size_t encoded_len, int* w, int* h);

/* GIF multi-frame loader (stb frame loader, canvas-composited by stb's own
 * disposal handling). On success returns the interleaved RGBA frame buffer
 * (frame_count * w * h * 4 bytes, inside the caller arena) and fills
 * *delays with per-frame millisecond delays (also inside the arena). Returns
 * NULL on failure. */
const uint8_t* zcsr_image_load_gif_frames(const void* encoded, size_t encoded_len,
                                          void* buffer, size_t bytes,
                                          int* w, int* h, int* frame_count,
                                          int** delays);

/* WebP static decode route (libwebp WebPDecodeRGBAInto into the caller buffer).
 * Returns {0,0,NULL} when libwebp is not compiled in or the decode fails. */
zcsr_image zcsr_image_decode_webp(const void* encoded, size_t encoded_len,
                                  void* buffer, size_t bytes);

/* Bare Windows DIB (BITMAPINFOHEADER + pixels, no "BM" file header) as stored
 * inside ICO/CUR entries. Wraps the DIB in a synthetic BMP file header (the
 * ICO height field is halved per the icon/DIB convention) and decodes through
 * the 8-bit stb path. Only uncompressed 16/24/32 bpp DIBs are accepted;
 * palette/RLE/compressed DIBs fail closed. */
zcsr_image zcsr_image_decode_dib(const void* dib, size_t dib_len,
                                 void* buffer, size_t bytes);

/* ICO/CUR container: parses the directory, picks the largest entry, decodes
 * the inner PNG/BMP payload through the routes above. */
zcsr_image zcsr_image_decode_container(const void* encoded, size_t encoded_len,
                                       void* buffer, size_t bytes);

#endif /* ZCSR_IMAGE_INTERNAL_H */
