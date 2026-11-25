# T0 fixtures

本文件记录 `tests/data/` 下 T0 契约测试所用 fixture 的来源、许可证、体积与复现方式。
各执行线只维护自己域的小节；新 fixture 按域追加。

## font（字体）

字体 fixture 允许超出 spec §6 的 64 KB 上限：真实可分发字体通常 100 KB 以上，
下表已选候选中体积最小、许可证最宽松的可用文件；未引入子集化工具（不增加新依赖）。

### `t0.ttf` — TrueType（glyf/loca）

| 项 | 值 |
|---|---|
| 用途 | `font_ttf` 测试目标；zcsr_font TTF 正向 + 负向契约 |
| 本地路径 | `tests/data/t0.ttf` |
| 来源主机 | Kali `192.168.1.12`，`/usr/share/fonts/truetype/noto/NotoMono-Regular.ttf` |
| 上游 | Noto Mono, Google, https://github.com/googlefonts/noto-fonts |
|  Debian 包 | `fonts-noto-mono` |
| 许可证 | SIL Open Font License 1.1（OFL-1.1）；Copyright 2010, 2012–2020 Google Inc.; 2015–2020 Google LLC |
| 大小 | 107,848 字节（≈ 105 KB） |
| sha256 | `41fd7ccc82375e2a1e47f0cc2c941c14d7c99ba2f57cf69c9f738d07fb257686` |
| 结构 | sfnt 版本 `0x00010000`；表：`glyf`/`loca`/`cmap`/`hmtx` 等（无 `CFF `） |
| 码点覆盖 | `A` U+0041、`H` U+0048、`É` U+00C9 有字形；`中` U+4E2D 无字形（测试断言返回 false） |

获取命令（只读 Kali，不改动其上任何仓库文件）：

```sh
scp -i ~/.ssh/id_ed25519_kali \
  chen@192.168.1.12:/usr/share/fonts/truetype/noto/NotoMono-Regular.ttf \
  tests/data/t0.ttf
```

### `t0.otf` — OpenType/CFF（Type2 charstring）

| 项 | 值 |
|---|---|
| 用途 | `font_otf` 测试目标；验证 stb_truetype 的 **CFF 分支**（非改扩展名的 TTF） |
| 本地路径 | `tests/data/t0.otf` |
| 来源主机 | Kali `192.168.1.12`，`/usr/share/fonts/opentype/roboto/slab/RobotoSlab-Regular.otf` |
| 上游 | Roboto Slab, Google Inc. / Christian Robertson, https://github.com/googlefonts/robotoslab |
| Debian 包 | `fonts-roboto-slab` |
| 许可证 | Apache License 2.0；Copyright 2013–2017 Google Inc., 2013–2015 Christian Robertson |
| 大小 | 108,908 字节（≈ 106 KB） |
| sha256 | `c916797fcdea3a647fff5d211c564ca72a5f66c60093d49400804b3cbc03a50a` |
| 结构 | sfnt 版本 `OTTO`；表：`CFF `/`cmap`/`hmtx`/`GPOS` 等；**无** `glyf`/`loca` |
| 码点覆盖 | `A` U+0041、`H` U+0048、`É` U+00C9 有字形；`中` U+4E2D 无字形（测试断言返回 false） |

获取命令：

```sh
scp -i ~/.ssh/id_ed25519_kali \
  chen@192.168.1.12:/usr/share/fonts/opentype/roboto/slab/RobotoSlab-Regular.otf \
  tests/data/t0.otf
```

选型说明：

- 排除 `Cantarell-VF.otf`：它是 **CFF2** 可变字体（表目录含 `CFF2`/`fvar`/`avar`），
  vendored stb_truetype 只支持 CFF（Type2），不支持 CFF2，故不作为 fixture。
- 排除 `urw-base35/*`：AGPL + font exception，按任务要求尽量不用。
- 排除 Kali 上 Noto CJK `.ttc`：20 MB+，且为集合字体，非最小可用文件。
- 未选 MathJax 系列 OTF：字形面向数学排版，拉丁字面量覆盖不如正文字体稳妥；
  Roboto Slab 为常规正文 CFF OTF，cmap 与轮廓标准。

### 既有内嵌 fixture（旧测试保留）

- `test_ttf.h`：历史内嵌 TTF 字节，仅供现有 `test_font.c`（`font` CTest 目标）使用；
  T0 的 `font_ttf` / `font_otf` 一律走上面的文件 fixture，不使用内嵌字节。

## music（音频）

音频 fixture 全部由随仓库落位的 FFmpeg 可执行从 440 Hz 正弦合成生成，
单文件 < 64 KB；断言只做"非全零 + 采样率/声道/帧数/时长"级别，有损格式
（MP3/AAC/M4A/OGG/Opus）不做逐样本精确比对，仅 WAV 走精确 PCM 值比对。

| fixture | 格式 / 后端 | 采样率 / 声道 | 大小 | sha256 |
|---|---|---|---|---|
| `t0.mp3` | MP3 / minimp3 | 44100 / 2 | 3,037 B | `7903e8eaee9f1f33340bcae6d47d34c558144801a00004edd7c356c7122608b7` |
| `t0.aac` | AAC ADTS / FFmpeg | 44100 / 2 | 4,288 B | `913e7b913d05c2403e1734c2403762be2cfc4eacf57bdef3e2979eaef923ecc9` |
| `t0.m4a` | AAC-in-MP4 / FFmpeg | 44100 / 2 | 5,046 B | `6201270ee7433e748ac99e785c9c48d6c60cbd926908c0d243db18b92b0bfcd6` |
| `t0.wav` | WAV PCM16 / 手写 RIFF | 44100 / 2 | 52,998 B | `dfe5f16851683de79a7afdf56200fd32e97df40cc4a34721fbe7829ca4b20401` |
| `t0.flac` | FLAC / dr_flac | 44100 / 2 | 14,458 B | `12d4cde84bc822063520805ede14caef86ebf845ba798495b3818a164eb63234` |
| `t0.ogg` | Ogg Vorbis / stb_vorbis | 44100 / 2 | 5,768 B | `e8b2d3dadaa3f2aca6d18c3636f8ccd692ea666a2f4a8b172223fe97460a1724` |
| `t0.opus` | Ogg Opus / FFmpeg | 48000 / 2 | 2,274 B | `0245dc3066b4883a651fe89d7bd1f2eb36f12695a8d33dee7a66f76dbcbf03ae` |

复现命令（仓库根目录为 cwd；`FF` 指向随仓库落位的 LGPL shared 构建）：

```sh
FF=third_party/prebuilt/win64/ffmpeg/bin/ffmpeg.exe

# MP3（minimp3 解码路径）
$FF -f lavfi -i "sine=frequency=440:duration=0.5:sample_rate=44100" -ac 2 \
    -c:a libmp3lame -q:a 5 -y tests/data/t0.mp3

# AAC ADTS（FFmpeg 后端，原生 aac encoder）
$FF -f lavfi -i "sine=frequency=440:duration=0.5:sample_rate=44100" -ac 2 \
    -c:a aac -b:a 64k -y tests/data/t0.aac

# M4A：AAC 封装进 MP4（FFmpeg 后端）
$FF -f lavfi -i "sine=frequency=440:duration=0.5:sample_rate=44100" -ac 2 \
    -c:a aac -b:a 64k -f mp4 -movflags +faststart -y tests/data/t0.m4a

# WAV PCM16（手写 RIFF 解析器；0.3 s 是为压进 64 KB 上限）
$FF -f lavfi -i "sine=frequency=440:duration=0.3:sample_rate=44100" -ac 2 \
    -c:a pcm_s16le -y tests/data/t0.wav

# FLAC（dr_flac 解码路径）
$FF -f lavfi -i "sine=frequency=440:duration=0.5:sample_rate=44100" -ac 2 \
    -c:a flac -y tests/data/t0.flac

# Ogg Vorbis（stb_vorbis 解码路径）
$FF -f lavfi -i "sine=frequency=440:duration=0.5:sample_rate=44100" -ac 2 \
    -c:a libvorbis -q:a 4 -y tests/data/t0.ogg

# Ogg Opus（FFmpeg 后端；libopus encoder，48 kHz）
$FF -f lavfi -i "sine=frequency=440:duration=0.5:sample_rate=48000" -ac 2 \
    -c:a libopus -b:a 32k -y tests/data/t0.opus
```

说明：

- 正弦为 440 Hz、双声道；除 WAV（0.3 s）与 Opus（48 kHz）外均为 44100 Hz / 0.5 s。
- AAC/Opus 容器自带编码 priming 与收尾 padding，探测时长略长于 0.5 s，
  属正常；测试只断言时长落在合理区间，不逐样本对齐。

## image（图像）

图像 fixture 的源帧由仓库内工具 `modules/image/tools/gen_t0_ppm.py` 写成
字节精确的 32×32 PPM（ffmpeg `drawbox`/`color` 会把纯色电平压到 253/254，
不能直接做逐像素基准），再由随仓库落位的 FFmpeg 转成各目标格式。画布布局：
TL 红、TR 绿、BL 蓝、BR 白；单文件 < 64 KB。断言容差：JPEG/WebP ±8；GIF
经调色板量化（纯色可偏 2~3）按 ±8；PNG/BMP 精确比对。

| fixture | 格式 / 后端 | 大小 | sha256 |
|---|---|---|---|
| `t0.png` | PNG / stb_image | 136 B | `517c7f28395adb48c9aa983eefc9b04b34953f6586c550aebefd47893ba0a936` |
| `t0.jpg` | JPEG / stb_image | 233 B | `2cc0f7564d4491344d2ff64aeb6dc226477e4e4d697eafad7d7879070ede5413` |
| `t0.bmp` | BMP / stb_image | 3,126 B | `cd6957329c29c2c2b5414f9919ce4ec537caf5cf43ce52a5980e2ba96f799598` |
| `t0.gif` | GIF static / stb_image | 976 B | `f50fe3f22db69b1e5e983a41d9519ff1d2ba9213700f13ea063ee4ed06b52e11` |
| `t0.webp` | WebP static / libwebp | 64 B | `9959277a997a8f7728e6bff816621b0a082b0a61f4a1fdf6b503731f6e03e30a` |
| `t0_anim.gif` | GIF animated / stb GIF frame loader | 1,127 B | `5ebcc958f5d030a7ec9fbc2869b731449a896c7999947b795cfeb1219b0d5098` |
| `t0_anim.webp` | WebP animated / libwebp demux | 196 B | `8c601da5307bab4e9d84d79dcd708af5f9085a3b597a7d3dc4dd6568e0167457` |

复现命令（仓库根目录为 cwd；`FF` 指向随仓库落位的 LGPL shared 构建）：

```sh
FF=third_party/prebuilt/win64/ffmpeg/bin/ffmpeg.exe
PY=python3

# 1. 生成源帧 PPM（frame_a.ppm / frame_b.ppm 写到临时目录 tmp/）
$PY modules/image/tools/gen_t0_ppm.py tmp

# 2. 静态 fixture（帧 A）
$FF -y -i tmp/frame_a.ppm tests/data/t0.png
$FF -y -i tmp/frame_a.ppm -q:v 2 tests/data/t0.jpg
$FF -y -i tmp/frame_a.ppm tests/data/t0.bmp
$FF -y -i tmp/frame_a.ppm tests/data/t0.gif
$FF -y -i tmp/frame_a.ppm -c:v libwebp -lossless 1 -q:v 100 tests/data/t0.webp

# 3. 动画 fixture：序号序列 img000=A, img001=B，帧率 2 → 每帧 500 ms
cp tmp/frame_a.ppm tmp/img000.ppm && cp tmp/frame_b.ppm tmp/img001.ppm
$FF -y -framerate 2 -i tmp/img%03d.ppm -filter_complex \
  "[0:v]palettegen=reserve_transparent=0[pal];[0:v][pal]paletteuse" \
  tests/data/t0_anim.gif
$FF -y -framerate 2 -i tmp/img%03d.ppm -c:v libwebp_anim -lossless 1 -loop 0 \
  -q:v 100 tests/data/t0_anim.webp
```

说明：

- prebuilt 树不带 `cwebp`/`img2webp` 可执行文件；WebP 编码走 ffmpeg 内置
  `libwebp` / `libwebp_anim` 编码器。
- `t0_anim.gif` / `t0_anim.webp` 各 2 帧、500 ms/帧；GIF disposal 由 stb
  GIF frame loader 合成，WebP disposal/blend 由 libwebp 负责。

