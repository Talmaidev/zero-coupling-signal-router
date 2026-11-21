#ifndef ZCSR_ASSETS_H
#define ZCSR_ASSETS_H

/*
 * 资源导入入口（给 game developer 用，而不是只给内置预设）：
 *   - 一律接受"相对路径"，基准目录由开发者设定（默认 ZCSR_ASSET_DIR 环境变量，
 *     否则进程当前目录）。开发者可以完全不使用引擎默认纹理/字体/音效。
 *   - 纹理走 modules/image 解码 + modules/glrender 上传，采样方式跟随当前
 *     纹理质量预设（0 最近邻 / 1 双线性 / 2 双线性+mipmap）。
 *   - 不持有任何全局默认资产；失败返回 false，调用方保留自己的回退。
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "zcsr/gl_render.h"
#include "zcsr/audio_mix.h"

#define ZCSR_ASSET_PATH_CAP 512

/* 设定相对路径的基准目录（NULL/"" 恢复默认：ZCSR_ASSET_DIR 或 cwd）。 */
bool zcsr_assets_set_base_dir(const char* dir);

/* Android only: bind the APK's AAssetManager so relative reads come from
 * assets/ inside the APK instead of fopen(). Pass activity->assetManager once
 * at startup; on other platforms this is a harmless false. This is a platform
 * binding, not a per-asset call: every zcsr_assets_* read then works unchanged. */
bool zcsr_assets_set_android_asset_manager(void* asset_manager);

/* Read a relative asset into caller storage. On Android, an APK asset is
 * preferred; filesystem path is the fallback. No heap allocation. */
bool zcsr_assets_read(const char* relative, void* out, size_t capacity, size_t* out_len);

/* 把相对路径解析成实际路径；已是绝对路径时原样返回。 */
bool zcsr_assets_resolve(const char* relative, char* out, int cap);

/* 加载任意 PNG/JPEG/BMP/TGA/GIF/PSD 为 GPU 纹理。成功后 w/h 可空。
 * encoded/arena 均为调用方自有缓冲；本函数不再分配堆内存。
 * encoded_bytes 不足文件大小时返回 false；arena_bytes 同时容纳解码 scratch
 * 与 w*h*4 输出，调用方按图片尺寸自行估算。 */
bool zcsr_assets_texture_with_buffers(zcsr_gl_renderer* r, const char* relative,
                                      unsigned char* encoded, size_t encoded_bytes,
                                      void* arena, size_t arena_bytes,
                                      zcsr_gl_texture* out_tex, int* out_w, int* out_h);

/* 用开发者自己的 TTF 覆盖 UI 字体（相对路径，pixel_height 为字号）。
 * buffer/bytes 是引擎的字体堆缓冲；可用 zcsr_gl_font_buffer_bytes_required(文件字节) 估算。 */
bool zcsr_assets_font(zcsr_gl_renderer* r, const char* relative, int pixel_height,
                      void* buffer, size_t bytes);

/* 注册开发者自己的 WAV（PCM16 / 44.1kHz），id 是播放时使用的名字。 */
bool zcsr_assets_sound(zcsr_mixer* m, const char* id, const char* relative);

#endif /* ZCSR_ASSETS_H */
