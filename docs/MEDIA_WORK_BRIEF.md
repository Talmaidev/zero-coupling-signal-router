# ZCSR 媒体能力四条并行工作线

日期：2026-11-25
仓库根：`C:\Users\chen\Documents\GitHub\zero-coupling-signal-router`

## 共同约束

- 引擎核心必须是 C11；C++ 只能作为消费端/薄壳，不得成为第二套核心。
- 编译门禁：MSVC `/W4 /WX /utf-8`；新增第三方单头文件必须把其警告抑制限定在 include 范围。
- 所有构建走 CMake；测试走 CTest，新增测试必须在仓库外构建根下 `ctest -C Release` 跑通。
- 执行期约束：不 commit、不 push、不打 tag；收口提交由 release owner 统一执行。
- 不修改不属于本线的文件。共享文件（根 `CMakeLists.txt`、`docs/TESTING.md`、`CHANGES.txt`）由第 4 线统一收口；前三条线如需要，只提交“需要的 CMake 片段/测试目标名”给第 4 线，不直接改共享文件。
- 不伪造支持：格式没有真实解码路径时，在矩阵里标为“未支持”，不得只在 App 扫描层加扩展名。
- 现有事实：`zcsr_music` 已支持 OGG Vorbis / MP3 / WAV（WAV 本轮刚修通，engine CTest 37/37）。图像仅 `zcsr_image` 的 stb_image RGBA8 路径；视频模块完全不存在。

## 第 1 线：图像格式

目标：盘点并实际补齐主流静态/动态图像格式。

- 现有：PNG/JPEG/BMP/TGA/GIF（首帧）/PSD/PIC/PNM，RGBA8。
- 缺口：WebP、TIFF、HEIF/HEIC、AVIF、JPEG XL、OpenEXR、Radiance HDR、ICO/CUR、动图序列（GIF/APNG/WebP）。
- 优先落地：不引入新全局堆、仍走 caller buffer 的格式；需要平台 codec 的格式单独做可选后端，并在能力查询中如实暴露。
- 文件边界：`include/zcsr/image.h`、`modules/image/**`、`tests/test_image*.c`、图像专用 `third_party/*`。
- 交付：至少一个真实新增格式的解码 + 契约测试；能力矩阵素材；不得只改注释。

## 第 2 线：音频格式

目标：把 `zcsr_music` 从 OGG/MP3/WAV 扩到主流音频容器/编码。

- 现有：OGG Vorbis（stb_vorbis）、MP3（minimp3）、WAV PCM/float（本轮新增）。
- 缺口：FLAC、AAC（ADTS）、M4A/MP4 audio、ALAC、Ogg Opus、AIFF/AIFC、AU/SND、WMA、AC-3/E-AC-3、DTS。
- 优先落地：FLAC（可用 `dr_flac`，注意 caller-buffer/allocator）、AIFF/AIFC PCM（纯 C）；AAC/M4A/Opus 需要 decoder/平台后端，先做能力探测和接口，不伪造。
- 文件边界：`include/zcsr/music.h`、`modules/music/**`、`tests/test_music*.c`、`tests/data/*` 音频样本。
- 交付：WAV 回归保持 37/37；至少一个新增格式真实解码 + 契约测试；FLAC 样本必须真实文件，不能用空壳。

## 第 3 线：视频格式

目标：从零建立引擎视频能力，不是给 App 加扩展名。

- 现状：无 `include/zcsr/video.h`、无 video 模块、无 demux/decode。
- 缺口：MP4/MOV（H.264/H.265/AV1）、WebM（VP8/VP9/AV1）、MKV、AVI、MPEG-TS、MPEG-1/2、ProRes。
- 优先落地：先定义最小 `zcsr_video` 契约（open/info/read_frame/seek/close、RGBA8 输出、时间戳），再做一条真实后端。平台后端（Windows Media Foundation）或纯 C 的 MPEG-1（pl_mpeg 风格）二选一，必须能跑测试。
- 文件边界：新建 `include/zcsr/video.h`、`modules/video/**`、`tests/test_video*.c`。
- 交付：至少一个真实视频文件能解出首帧/多帧并校验尺寸与像素；不能只写 stub。

## 第 4 线：矩阵、集成与验收

目标：把前三条线的能力写成可核验矩阵，并完成共享工程收口。

- 文件边界：`docs/MEDIA_SUPPORT.md`、`docs/TESTING.md`、`CHANGES.txt`、根 `CMakeLists.txt`、`tests/CMakeLists.txt`。
- 职责：先写矩阵（现有/缺口/后端/测试/风险），再接收前三条线的 CMake 片段与测试目标名，统一接入；最后跑完整 engine CTest 并记录真实结果。
- 不得替前三条线宣布“已支持”；矩阵状态必须与代码/测试一致。
- 交付：`MEDIA_SUPPORT.md` 可直接给用户看；完整 CTest 输出；未完成项明确列出。

## 验收口径

- 每个“已支持”格式都必须有：真实解码路径、明确的格式枚举/API、至少一个正向契约测试。
- 每个“平台相关”格式都必须有：能力查询、失败返回、缺失 codec 时不崩溃。
- 每个“未支持”格式都必须出现在矩阵缺口表，不能静默失败。
