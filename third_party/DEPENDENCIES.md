# 媒体依赖台账（third_party/prebuilt）

本目录是可选媒体后端（FFmpeg / libwebp）的**本地预编译落位**。二进制不入库（`third_party/prebuilt/` 由 `.gitignore` 排除），所以新 clone 默认没有它们，对应后端 fail-closed。

**用户侧不要照这里的路径自建**：README 的「可选媒体依赖」段给的是 apt / dnf / pacman / vcpkg / MacPorts 的安装命令，本文件只记录维护者侧的许可与布局口径。

## 1. 许可要求（硬约束）

| 项 | 要求 |
|---|---|
| 项目许可 | AGPL-3.0（仓库根 `LICENSE`） |
| FFmpeg | **LGPL 变体 + 动态链接**：`--enable-version3 --enable-shared --disable-static`；不得 `--enable-gpl`、不得 `--enable-nonfree`；关掉 `libx264 libx265 libxvid libfdk-aac` |
| 不可用变体 | `Gyan.FFmpeg*`、`BtbN.FFmpeg.GPL.Shared`、Homebrew 的 `ffmpeg` 公式、MacPorts 默认 `+gpl2` —— 均为 GPL |
| libwebp | BSD-3-Clause，静态或动态链接均可 |

## 2. 落位与探测

| 来源 | 前缀（`ZCSR_FFMPEG_ROOT` / `ZCSR_LIBWEBP_ROOT`） | 链接 |
|---|---|---|
| 仓库 prebuilt（Windows） | `third_party/prebuilt/win64/{ffmpeg,libwebp}` | `lib/{avcodec,avformat,avutil,swscale,swresample}.lib`；`lib/{libwebp,libwebpdemux,libsharpyuv}.lib` |
| 仓库 prebuilt（Linux/macOS 默认位） | `third_party/prebuilt/linux64/{ffmpeg,libwebp}` | 裸名 `avcodec avformat avutil swscale swresample`、`webp webpdemux sharpyuv` |
| vcpkg（Windows） | `C:\vcpkg\installed\x64-windows` | 同上 `.lib` |
| Debian / Ubuntu / Kali | `/usr`（header 实际在 `include/<triplet>/`） | 裸名 |
| Fedora / RHEL（RPM Fusion） | `/usr`（header 实际在 `include/ffmpeg/`） | 裸名 |
| Arch | `/usr` | 裸名 |
| macOS（MacPorts） | `/opt/local` | 裸名 |

CMake 探测必须**头文件与库同时命中**才算启用，不能只用 `find_path` 判定：

- FFmpeg：头文件 `libavcodec/avcodec.h` 之外，还必须 `find_library` 到 `libavcodec`、`libavformat`、`libavutil`、`libswscale`、`libswresample` 五个库。
- libwebp：头文件 `webp/decode.h` 之外，还必须 `find_library` 到 `libwebp`、`libwebpdemux`、`libsharpyuv` 三个库。
- 头文件在、库不在 ⇒ `ZCSR_FFMPEG_FOUND=FALSE` / `ZCSR_LIBWEBP_FOUND=FALSE`，configure 输出逐项列出缺失的头/库，不会留到链接期才爆 undefined symbol。
- 许可证探测：若 `ffmpeg -buildconf`（回退 `-version`）出现 `--enable-gpl` 或 `--enable-nonfree`，FFmpeg 后端**禁用**并在 configure 阶段明确报这是 GPL/nonfree、非 LGPL 不合规；prebuilt 必须是 LGPL 构建。

只检测、不安装的脚本：`tools/check_media_deps.ps1`（Windows）、`tools/check_media_deps.sh`（Linux），输出缺头/缺库、版本、许可证与 root 变量，不执行 sudo、不改系统状态。

## 3. 版本与已验证矩阵

| 依赖 | 登记版本 | 许可证 | 链接方式 |
|---|---|---|---|
| FFmpeg | **BtbN `n8.1` LGPL shared**（本机 Windows prebuilt 实测 `n8.1.3-...`，libavcodec 62 / libavformat 62 / libavutil 60 / swscale 9 / swresample 6） | LGPL v3（`--enable-version3 --enable-shared --disable-static`；无 `--enable-gpl`、无 `--enable-nonfree`；未链 x264/x265/xvid/fdk-aac） | 动态链接 shared import lib + 运行时 DLL |
| libwebp | **1.4.0** | BSD-3-Clause（含 sharpyuv） | 静态或动态链接均可 |

已验证版本矩阵：

| 平台 | FFmpeg | libwebp | 状态 |
|---|---|---|---|
| Windows 10/11（VS 18 2026, MSVC 19.51, x64） | BtbN n8.1 LGPL shared（`third_party/prebuilt/win64/ffmpeg`，n8.1.3 实测） | 1.4.0（`third_party/prebuilt/win64/libwebp`） | 本机实测通过 |
| Kali 2026（GCC / Clang / ASan） | 系统 apt LGPL 共享库（`libavcodec-dev` 等），以 `/usr` 前缀探测 | 系统 `libwebp-dev libsharpyuv-dev` | 待 Kali 连通后按 `tools/check_media_deps.sh` 复核（本轮标 unverified） |

> 版本口径：维护者落库的 prebuilt 按上表 pin；用户侧用发行版包管理器时，版本随发行版走，但许可证基线（LGPL shared、禁 GPL/nonfree）不变。

## 4. 重新获取

| 平台 | 命令 |
|---|---|
| Windows | `vcpkg install ffmpeg libwebp --triplet x64-windows` |
| Debian / Ubuntu / Kali | `sudo apt-get install -y libavcodec-dev libavformat-dev libavutil-dev libswscale-dev libswresample-dev libwebp-dev libsharpyuv-dev` |
| Fedora / RHEL | `sudo dnf install -y ffmpeg-devel libwebp-devel`（需先启用 RPM Fusion free） |
| Arch | `sudo pacman -S --needed ffmpeg libwebp` |
| macOS | `sudo port install ffmpeg -gpl2 webp` |

完整块（含对应的 `-DZCSR_*_ROOT` 取值）见仓库根 `README.md` 的「可选媒体依赖」。

## 5. fixture 生成器

`prebuilt/win64/ffmpeg/bin/ffmpeg.exe`（Windows）、`prebuilt/linux64/ffmpeg/bin/ffmpeg`（Linux，需 `LD_LIBRARY_PATH=prebuilt/linux64/ffmpeg/lib`）。fixture 生成命令统一记录在 `tests/data/FIXTURES.md`；用系统包管理器装的话，直接用 PATH 上的 `ffmpeg`。
