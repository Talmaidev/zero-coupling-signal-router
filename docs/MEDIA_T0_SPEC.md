# ZCSR 媒体 T0 契约（执行冻结版）

日期：2026-11-25
状态：**冻结**。四条执行线（font / music / image / video）与收口方共同遵守；任何偏离先在此文档里改，再改代码。

## 1. 交付原则

- **T0 = 真支持**。判定三件套缺一不可：真实 fixture + 注册进 CTest 的正向测试 + 像素/帧/PCM 断言。
- **`caps=false` 即失败**。T0 格式在 Windows Release 与 Kali GCC/Clang/ASan 上都必须是 `caps=true` 且测试通过。
- 不接受 `caps-only`、`not tested`、`stub`、`扩展名驱动`。没有解码路径的格式不出现在 public enum 里。
- 不做自研 codec。codec 交给 FFmpeg / 平台 codec / 已有 vendored 解码器；只有简单容器、PCM、胶水层手写。
- 执行期约束：不 commit、不 push、不打 tag；收口提交由 release owner 统一执行。

## 2. T0 矩阵（唯一权威）

| 域 | T0 格式 | 解码后端 | fixture | 注册测试目标 | 断言要点 |
|---|---|---|---|---|---|
| image | PNG | stb_image | `tests/data/t0.png` | `image_png` | 尺寸 + RGBA 像素（已知色块） |
| image | JPEG | stb_image | `tests/data/t0.jpg` | `image_jpeg` | 尺寸 + 像素容差（有损，±8） |
| image | GIF static | stb_image | `tests/data/t0.gif` | `image_gif` | 首帧尺寸 + 像素 |
| image | GIF animated | stb_image GIF frame loader | `tests/data/t0_anim.gif` | `image_gif_anim` | 帧数 ≥2、每帧 delay、canvas 合成、loop |
| image | WebP static | libwebp `WebPDecodeRGBA` | `tests/data/t0.webp` | `image_webp` | 尺寸 + 像素（有损容差 ±8） |
| image | WebP animated | libwebp `WebPAnimDecoder` (webpdemux) | `tests/data/t0_anim.webp` | `image_webp_anim` | 帧数 ≥2、delay、合成 |
| image | BMP | stb_image | `tests/data/t0.bmp` | `image_bmp` | 尺寸 + 精确像素 |
| music | MP3 | minimp3（既有） | `tests/data/t0.mp3` | `music_mp3` | 帧数/采样数 > 0、PCM 非全零 |
| music | AAC (ADTS) | FFmpeg | `tests/data/t0.aac` | `music_aac` | 采样率/声道/时长、PCM 非全零 |
| music | M4A (AAC in MP4) | FFmpeg | `tests/data/t0.m4a` | `music_m4a` | 容器识别 + 解码 PCM 非全零 |
| music | WAV PCM | 手写 RIFF（既有） | `tests/data/t0.wav` | `music_wav` | 精确 PCM 值比对 |
| music | FLAC | dr_flac（既有） | `tests/data/t0.flac` | `music_flac` | 帧数/采样、非全零、metadata |
| music | OGG Vorbis | stb_vorbis（既有） | `tests/data/t0.ogg` | `music_ogg` | 帧数、非全零 |
| music | Opus (Ogg) | FFmpeg | `tests/data/t0.opus` | `music_opus` | 采样率 48k、非全零 |
| video | MP4 / H.264 | Win: MF；Linux: FFmpeg | `tests/video_data/t0_h264.mp4` | `video_mp4_h264` | 宽高、≥3 帧、像素色块、seek |
| video | WebM / VP9 | Win: FFmpeg；Linux: FFmpeg | `tests/video_data/t0_vp9.webm` | `video_webm_vp9` | 宽高、≥3 帧、像素色块、seek |
| font | TTF | stb_truetype | `tests/data/t0.ttf` | `font_ttf` | load/metrics/rasterize 非空、advance 合理 |
| font | OTF (CFF) | stb_truetype | `tests/data/t0.otf` | `font_otf` | load/metrics/rasterize 非空 |

`tests/data/test_ttf.h` 现有内嵌 TTF 可继续用于旧测试；T0 必须用**文件 fixture**，不用 header 内嵌字节。

## 3. public surface 冻结（enum 与 API）

所有 `include/zcsr/*.h` 的 enum 注释写着 ABI。本轮按 1.0 前最后一次清理：**直接删值并保持编号连续**。若发现已外发（tag/发布产物），停下来报告，不擅自 bump major。

### 3.1 `include/zcsr/image.h`

删除：`TGA`、`PSD`、`PIC`、`PNM`、`TIFF`、`HEIF`、`AVIF`、`JPEGXL`、`EXR`。
保留（legacy extension，非 T0，已有测试，不再扩展）：`HDR`、`ICO`、`CUR`。
新增 T0：`WEBP`。

冻结顺序（`ZCSR_IMAGE_FMT_UNKNOWN = 0`）：

```
UNKNOWN=0, PNG=1, JPEG=2, BMP=3, GIF=4, WEBP=5, HDR=6, ICO=7, CUR=8, COUNT=9
```

`zcsr_image_caps_info` 保持 `bool decode[ZCSR_IMAGE_FMT_COUNT]`，T0 六项必须 true，`animated` 必须 true（GIF/WebP 动画）。

### 3.2 `include/zcsr/image_animation.h`（新建，独立契约）

不改 `zcsr_image_decode` 的返回类型；单帧契约原样保留。新增 opaque handle + frame sequence：

```c
typedef struct zcsr_image_anim zcsr_image_anim;

typedef struct {
    int w, h;            /* canvas size (logical screen) */
    int frame_count;     /* >= 1 */
    int loop_count;      /* 0 = infinite, 1 = play once, n = repeat n times */
} zcsr_image_anim_info;

bool   zcsr_image_anim_info_of(const void* encoded, size_t len, zcsr_image_anim_info* out);
size_t zcsr_image_anim_buffer_bytes(const void* encoded, size_t len);
zcsr_image_anim* zcsr_image_anim_open(const void* encoded, size_t len, void* buffer, size_t bytes);
bool   zcsr_image_anim_frame(zcsr_image_anim* anim, int index, zcsr_image* out);
int    zcsr_image_anim_delay_ms(const zcsr_image_anim* anim, int index);
void   zcsr_image_anim_close(zcsr_image_anim* anim);
```

- 支持 GIF89a 动画与 animated WebP（T0）；static PNG/JPEG/BMP/WebP/GIF 也能 open（frame_count=1），便于调用方统一走一条路径。
- `frame(index)`：从第 0 帧重放到 index 再输出，**必须做 canvas 合成**（GIF disposal method 0/1/2、透明索引；WebP 由 libwebp 负责）。输出写入 caller 的 `zcsr_image.rgba`，不返回内部分配的像素。
- 帧索引越界 → false；未支持容器 → false（`zcsr_image_anim_info_of` 返回 false）。

### 3.3 `include/zcsr/music.h`

删除：`AIFF`、`ALAC`、`WMA`、`AC3`、`DTS`、`AU`。保留并实现：`AAC`、`M4A`、`OPUS`。
压缩 WAV（ADPCM/A-law/μ-law）：删掉"识别但不支持"的代码路径，改为在解析器里直接返回 UNSUPPORTED（不计入 enum）。

冻结顺序（旧值 0/1/2/3 不动）：

```
OGG=0, MP3=1, WAV=2, FLAC=3, AAC=4, M4A=5, OPUS=6
```

`zcsr_music_format_supported()` 对以上 7 项必须 true。`detect_format()` 不得再返回"识别成功但 load=NULL"的值——枚举里已不存在这类值。

### 3.4 `include/zcsr/video.h`

容器删除：`MKV`、`AVI`、`MPEGTS`、`MPEG`。编码删除：`H265`、`AV1`、`VP8`、`PRORES`、`MPEG1`、`MPEG2`、`MJPEG`。

```
container: UNKNOWN=0, MP4=1, WEBM=2
codec:     UNKNOWN=0, H264=1, VP9=2
```

- AVI/MJPEG 仅作 test fixture 用途**不再保留**：最终 fixture 换成 ffmpeg 生成的 MP4/H.264 与 WebM/VP9，public surface 中不存在 AVI/MJPEG。
- `zcsr_video_platform_caps()` 在 Windows 与 Kali 上，MP4/WebM 与 H264/VP9 必须全部 true；任一 false 视为失败。
- 后端：Windows 优先 Media Foundation；FFmpeg 作为 fallback。Linux 只有 FFmpeg。`#if` 只选 backend，不用来替代解码实现。

### 3.5 `include/zcsr/font.h`

只留 TTF/OTF。WOFF/WOFF2 不进 native T0：不新增 enum、不加 decoder。若现有实现有 WOFF 分支，删除。

## 4. memory contract（统一口径，文档与注释同步改）

替换旧的"绝对无堆"表述，统一为：

> Output pixels/PCM are caller-owned. Backends may hold scratch memory behind the opaque handle; no retained per-frame heap grows with frame count, and no allocation happens per frame on the output path.

- `zcsr_image_decode` / `zcsr_image_anim_frame`：像素写入 caller buffer。
- `zcsr_music` 的 AAC/M4A/Opus FFmpeg 后端：解码器上下文（AVCodecContext/AVFrame）在 opaque handle 后持有；每帧不再 malloc。
- `zcsr_video` 同理。

## 5. 依赖落位（收口方提供，执行线只消费）

```
third_party/prebuilt/win64/ffmpeg/{bin,lib,include}   # BtbN n8.1 win64 LGPL shared（DLL + .lib + 头）
third_party/prebuilt/win64/libwebp/{include,lib}      # libwebp 1.4.0，CMake 构建，静态
third_party/prebuilt/linux64/ffmpeg/{lib,include}     # BtbN n8.1 linux64 LGPL shared
third_party/prebuilt/linux64/libwebp/                 # 同上，静态
```

- 强制 `--disable-gpl --disable-nonfree`：BtbN 的 `*-lgpl-shared` 产物即 LGPL 变体；`third_party/DEPENDENCIES.md` 记录版本、来源 URL、许可证、校验和。
- CMake 变量：`ZCSR_FFMPEG_ROOT`、`ZCSR_LIBWEBP_ROOT`，默认指向上面路径；缺失时构建**不失败**，对应后端的 `caps` 置 false——但本任务验收要求两平台都在。
- 第三方头用 `#pragma warning(push/disable:...)` + `#include` + `pop` 局部抑制；不得全局 `-wd`。FFmpeg 头要求 `#define __STDC_CONSTANT_MACROS` 之外的 C 兼容处理按实际编译需要来。

## 6. fixture 规范

- 位置：音频/图像/字体 `tests/data/`，视频 `tests/video_data/`。
- 生成：一律用随依赖落位的 `ffmpeg` 可执行（`-c:v libx264 -pix_fmt yuv420p`、`-c:v libvpx-vp9`、`-c:a aac`、`-c:a libopus`）或 libwebp 的 `cwebp/img2webp`；**生成命令写进 `tests/data/FIXTURES.md`**，可复现。
- 体积：单文件 <64 KB；画面用纯色块/棋盘，便于像素断言（例如左上 16×16 红、右上绿、下方蓝）。
- 有损格式（JPEG/WebP/VP9/H.264/AAC/Opus）断言容差：像素 ±8；音频只用"非全零 + 帧数/采样率/声道"级别断言，不做逐样本精确比对。

## 7. 测试与目标命名

- 图像/音频/字体用 `zcsr_add_engine_test(<name> <source.c>)`；视频同样走该宏（它已预链接 MF 库）。
- 需要 argv 的（fixture 目录）显式 `add_executable` + `add_test(... "<data_dir>")`，或统一约定设备用环境变量 `ZCSR_TEST_DATA_DIR`。
- 名称严格按第 2 节表格：`image_png`、`image_jpeg`、`image_gif`、`image_gif_anim`、`image_webp`、`image_webp_anim`、`image_bmp`、`music_mp3`、`music_aac`、`music_m4a`、`music_wav`、`music_flac`、`music_ogg`、`music_opus`、`video_mp4_h264`、`video_webm_vp9`、`font_ttf`、`font_otf`。
- 旧测试可保留但不得与新目标重名；旧测试若引用已删 enum/格式，必须同步修或删。
- 每个域至少一个负向用例：损坏文件、截断、伪装扩展名 → 返回失败码，不崩溃。

## 8. 文件边界

| 线 | 可改 |
|---|---|
| font | `include/zcsr/font.h`、`modules/font/**`、`tests/test_font*.c`、`tests/data/t0.ttf`、`tests/data/t0.otf` |
| music | `include/zcsr/music.h`、`modules/music/**`、`tests/test_music*.c`、`tests/data/t0.*` 音频 |
| image | `include/zcsr/image.h`、`include/zcsr/image_animation.h`（新建）、`modules/image/**`、`tests/test_image*.c`、`tests/data/t0.*` 图像、图像专用 `third_party/*`（`third_party/stb_image.h` 的 `STBI_NO_*` 开关） |
| video | `include/zcsr/video.h`、`modules/video/**`、`tests/test_video*.c`、`tests/video_data/**` |
| 收口（我） | 根 `CMakeLists.txt`、`tests/CMakeLists.txt`、`tests/engine/CMakeLists.txt`、`docs/**`、`CHANGES.txt`、`third_party/prebuilt/**`、`third_party/DEPENDENCIES.md`、`.gitignore` |

执行线**不**改共享 CMake/docs；需要新目标时把准确片段与目标名报给收口方。

## 9. 验收清单（每条线自查后报数）

1. T0 各格式 `caps`/`supported` 为 true，且测试真跑通过（贴真实输出）。
2. 删除项在 public enum 里不存在，模块内部路径也真关掉（image 用 `STBI_NO_TGA/PSD/PIC/PNM`；music/video 无残留分支可被调用）。
3. 无 `caps-only`、无"识别但不支持"、无 stub 返回成功。
4. `/W4 /WX /utf-8`（Windows）与 `-Wall -Wextra -Wpedantic`（Linux）零警告；ASan 无泄漏/越界。
5. 注释与文档里的 memory 口径已改成第 4 节表述，不留"绝对无堆"旧话。
6. fixture 与生成命令可复现，`FIXTURES.md` 已写。
