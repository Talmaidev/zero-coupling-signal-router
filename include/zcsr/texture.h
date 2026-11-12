#ifndef ZCSR_TEXTURE_H
#define ZCSR_TEXTURE_H
/* PNG 解码后的 CPU 像素描述。
 *
 * zcsr_texture_cache_init / zcsr_texture_load 已移除：它们由 modules/render 实现，
 * 该模块已整体删除；这两个 API 没有跨平台实现，调用方只有那份孤儿测试。
 * 需要真纹理请走 glrender（zcsr_gl_texture）。 */
#include <stddef.h>
#include <stdint.h>

typedef struct {
    int            width;
    int            height;
    const uint8_t* rgba; /* 8-bit RGBA, cache-owned (do not free) */
} zcsr_texture;

#endif /* ZCSR_TEXTURE_H */
