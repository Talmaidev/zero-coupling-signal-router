# ZCSR

ZCSR is a pure C11 **2.5D engine library**. It provides static libraries and
contract headers. It is not a runnable game and it is not a window host.

The runnable entry is `zcsr_runtime`: the runtime host owns the one OS window,
surface, input pump and main loop. `zcsr_editor` is an editor workspace module
hosted by that runtime host; it does not create a second window.

This README uses these words with exactly one meaning:

- **engine library**: the C11 static libraries and contract headers in this
repository. It does not own a window.
- **runtime host**: the one owner of the OS window, surface and main loop.
- **editor workspace**: the authoring UI hosted by the runtime host.
- **2.5D scene**: a live runtime world drawn with an orthographic camera and
  two-dimensional projected quads. It is not a 3D model scene.
- **perspective camera projection**: the `camera3d` math utility. It projects
  points and billboards; it is not a 3D renderer.
- **quad**: one four-corner screen polygon with UV and tint.
- **layer**: one explicit draw group. It is not the same as painter depth.

## 仓库分工

ZCSR is split by product authority, not by source copy. The engine does not
know any upper product:

```
zcsr_runtime                       runtime host: one window, one main loop
  ├── zcsr_editor                  editor workspace module
  │     ├── UIM                    smallest UI component manager
  │     ├── setting page container  setting page identity and visibility
  │     └── scene authoring        2.5D scene document and commands
  ├── game module                  2.5D scene runtime
  └── zero-coupling-signal-router  engine libraries (29 static libraries)
       ├── core / signal / hsm / platform
       ├── glrender / light2d / sprite / camera / camera3d / scene_camera / scene / scene_render / culling / atlas / font / image / color_pipeline
       ├── audio / audiomix / music / input / inputext / assets / resource / file_service
       └── collision / save / ui / viewport
```

`zcsr_ui_page_manager` is a legacy target name for the independent setting page
container. It owns setting page identity and visibility only; it is not a route
stack. Menu is a separate `zcsr_ui_menu` component target. Neither target owns
UIM page documents. `zcsr_tools` is a developer tool layer; the editor does not
link the whole tool layer.

## 这个仓库包含什么

- `include/zcsr/`：模块唯一允许包含的契约头。
- `modules/`：29 个模块，每个模块独立编译，不 include 另一个模块的源码。
- `tests/`：无窗口、可重复的 CTest 用例。
- `integration/`：需要真实窗口、GPU 或音频设备的示例和手工验证，不作为自动测试。
- `third_party/`：单文件第三方依赖，只在对应模块内部使用。

引擎自己的运行期代码不使用 `malloc/free`。固定池和调用方缓冲区承担状态存储；系统库/驱动内部的分配不计入这个约束。

## 构建

需要 CMake 3.16+、C11 编译器；Windows 使用 MSVC。Linux 不是“只装 C11 编译器”就能完整构建：X11/GLX 后端需要 `libx11-dev` + `libgl1-mesa-dev`（发行版等价包名即可），`libxrandr-dev` 可选；全量测试的 `inputext_x11` 用例还需要 `libxtst-dev`，缺失时 CMake 自动跳过该用例，其余测试仍会构建和运行。Wayland/EGL 后端需要 `wayland-client`、`wayland-scanner`、`wayland-protocols`、`libxkbcommon-dev`、`libegl1-mesa-dev`；音频可选 `libpipewire-0.3-dev` + `libspa-0.2-dev` 或 `libasound2-dev`，完全缺失时走静默后端。

构建目录必须放在源码树**外**——根 `CMakeLists.txt` 的 guard 会在 configure 期拒绝仓内目录并打印可用路径；下面的 `..\zcsr-build` 只是示例，任意仓外路径都可以：

```powershell
cmake -S . -B ..\zcsr-build\win-msvc -G "Visual Studio 18 2026" -A x64
cmake --build ..\zcsr-build\win-msvc --config Release
ctest --test-dir ..\zcsr-build\win-msvc -C Release --output-on-failure
```

macOS 15+ / Xcode 16+ 使用同一套 CMake 源，但必须在有 GUI 会话的真实 Mac 上构建和运行：

```bash
cmake -S . -B ../zcsr-build/macos -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0
cmake --build ../zcsr-build/macos -j
./tools/verify_macos_linear.sh
```

脚本保留供真实 Mac 复验。当前没有真实 Mac 构建/运行证据，macOS 标记为**未验证**，不声称 pass；它不阻塞 Windows/Linux 工作，但阻塞“四平台完全支持”的结论。

当前 Windows 基线：router **68 个 CTest 注册项**（67 通过 + 1 个硬件跳过），五仓合计 **154 个注册项**（153 通过 + 1 跳过）。同一源码在无 prebuilt 的干净快照下为 **59 通过 + 9 显式 skip / 0 失败**，skip 原因写在 CTest 输出里。Linux 当前 HEAD（Kali x86_64，GCC 16.2）为 **154 个注册项**（148 通过 + 6 显式 skip / 0 失败）；router 单独为 68 项（62 通过 + 6 skip），Clang 21 + ASan 同为 0 失败。旧 Linux 35/35、34/34 等数字是历史副本，不再作为当前 HEAD 证据。Linux 的 5 个媒体 skip 是因为干净测试环境未装 LGPL FFmpeg 开发包，不是假 pass；Windows 媒体测试在本机 LGPL prebuilt 下通过。macOS 为未验证。测试覆盖 core、signal、hsm、platform、glrender、light2d、audio、music、input、collision、save、ui、viewport、color_pipeline、camera3d、scene_camera、scene、scene_render 等模块。

## 可选媒体依赖（FFmpeg / libwebp）

这两个库体量大、许可证独立，**不随仓库分发**（`third_party/prebuilt/` 由 `.gitignore` 排除，新 clone 里没有它们）。缺失时构建照常成功，只是对应后端 fail-closed：WebP 与 FFmpeg 支撑的格式（AAC/M4A/Opus、WebM/video、WebP 静图与动图）报 `caps=false`、解码返回 `UNSUPPORTED`。

**版本口径**：维护者落库的 prebuilt 为 FFmpeg **BtbN `n8.1` LGPL shared**、libwebp **1.4.0**（详见 `third_party/DEPENDENCIES.md` 的版本矩阵）。下面每段都是可直接复制的安装命令；装完把 root 指向系统前缀（Linux/macOS 是 `/usr` / `/opt/local`，Windows 是 vcpkg 的 `installed/x64-windows`），configure 会打印 `FFmpeg backend enabled: <root> (license=LGPL)` 或 `DISABLED … missing: …`。头文件落在 Debian 的 multiarch 目录或 Fedora 的 `include/ffmpeg/` 子目录也能命中。探测要求头文件与库同时存在：FFmpeg 五个库（avcodec/avformat/avutil/swscale/swresample）、libwebp 三个库（webp/webpdemux/sharpyuv）缺一即 fail-closed。三个 cache 变量可覆盖：`ZCSR_DEPS_ROOT`、`ZCSR_FFMPEG_ROOT`、`ZCSR_LIBWEBP_ROOT`。

**许可前提**：本项目 AGPL-3.0，FFmpeg 必须是 LGPL 变体且**动态链接**。包管理器里能装到的 GPL 变体一律不可用：`Gyan.FFmpeg`、`BtbN.FFmpeg.GPL.Shared`、Homebrew 的 `ffmpeg`、MacPorts 默认的 `+gpl2`。

**Windows（x64 / MSVC）** — vcpkg 一条命令装齐（默认 features 不含 `gpl`/`nonfree`，`x64-windows` triplet 即动态链接）：

```powershell
git clone https://github.com/microsoft/vcpkg C:\vcpkg
C:\vcpkg\bootstrap-vcpkg.bat
C:\vcpkg\vcpkg.exe install ffmpeg libwebp --triplet x64-windows

# 两个 root 指向同一个 vcpkg 前缀即可（探测分别在其 include/ 下找到 libavcodec 与 webp）
cmake -S . -B ..\zcsr-build\win-msvc -G "Visual Studio 18 2026" -A x64 `
      -DZCSR_FFMPEG_ROOT=C:/vcpkg/installed/x64-windows `
      -DZCSR_LIBWEBP_ROOT=C:/vcpkg/installed/x64-windows
```

首次编译 FFmpeg 是数十分钟量级；NASM 由 vcpkg 自己拉。DLL 不用手动拷，`zcsr_copy_ffmpeg_runtime()` 会把 `bin/*.dll` 放到测试可执行文件旁边。备选 `winget install --id BtbN.FFmpeg.LGPL.Shared`（LGPL，归档内含 `include/lib`），但 winget 没有 libwebp 的开发包，WebP 仍要 vcpkg 或自建。

**Debian / Kali（apt）**：

```bash
sudo apt-get update
sudo apt-get install -y libavcodec-dev libavformat-dev libavutil-dev \
     libswscale-dev libswresample-dev libwebp-dev libsharpyuv-dev
cmake -S . -B ../zcsr-build/linux-gcc -DCMAKE_BUILD_TYPE=Release \
      -DZCSR_FFMPEG_ROOT=/usr -DZCSR_LIBWEBP_ROOT=/usr
```

`libsharpyuv-dev` 从 Debian 13 / Ubuntu 24.04 起才有，Ubuntu 22.04 没有（缺它会链接失败）。Ubuntu 22.04 的 libavcodec 仍是 58，本项目是否硬依赖 ≥ 60 未逐个确认——22.04 编译失败就换发行版。

**Fedora / RHEL（dnf）** — `ffmpeg-devel` 在 RPM Fusion，主仓库只有 `ffmpeg-free`：

```bash
sudo dnf install -y https://download1.rpmfusion.org/free/fedora/rpmfusion-free-release-$(rpm -E %fedora).noarch.rpm
sudo dnf install -y ffmpeg-devel libwebp-devel
cmake -S . -B ../zcsr-build/linux-gcc -DCMAKE_BUILD_TYPE=Release \
      -DZCSR_FFMPEG_ROOT=/usr -DZCSR_LIBWEBP_ROOT=/usr
```

**Arch（pacman）** — `ffmpeg` 主包自带 headers：

```bash
sudo pacman -S --needed ffmpeg libwebp
cmake -S . -B ../zcsr-build/linux-gcc -DCMAKE_BUILD_TYPE=Release \
      -DZCSR_FFMPEG_ROOT=/usr -DZCSR_LIBWEBP_ROOT=/usr
```

Kali 上多数媒体库已预装，`apt-get install` 只补缺的 dev 包。

**macOS 15+ / Xcode 16+** — 同样用 sudo 命令行，走 MacPorts（前置 `xcode-select --install`）；默认的 `+gpl2` 变体是 GPL，必须显式关掉：

```bash
sudo port install ffmpeg -gpl2 webp
cmake -S . -B ../zcsr-build/macos -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0 \
      -DZCSR_FFMPEG_ROOT=/opt/local -DZCSR_LIBWEBP_ROOT=/opt/local
```

Homebrew 的 `ffmpeg` 公式是 `--enable-gpl`（依赖 x264/x265/xvid），不符合许可基线；brew 也明确拒绝 sudo 运行，所以 macOS 上不用它。

**Android** — 无系统包管理器，也没有官方预编译：

```text
用 NDK 交叉编译（--disable-everything 只保留所需解码器），
或放入 per-ABI 产物 third_party/prebuilt/android-<abi>/{ffmpeg,libwebp} 后传 ZCSR_*_ROOT。
不提供时不编译这两个后端，相关格式一律 fail-closed。
```

**维护者自己的 prebuilt**：仓库的默认探测位置仍是 `third_party/prebuilt/{win64,linux64}/{ffmpeg,libwebp}`（不入库，新 clone 里没有）。这些二进制的来源、许可与链接方式记在 `third_party/DEPENDENCIES.md`；落库版本为 FFmpeg **BtbN `n8.1` LGPL shared**、libwebp **1.4.0**。用户侧用发行版包管理器时版本随发行版走，但许可证基线（LGPL shared、禁 GPL/nonfree）不变。

## Scene Core

`scene_camera` 是透视投影工具，`scene` 保存实体和任意四边形覆盖，`scene_render` 把场景转换成投影后的四边形实例。三者都不拥有 window、GL context 或 3D 渲染器。

- `zcsr_scene_camera_init` 建立 yaw/pitch/roll、distance、正交视口；`zcsr_scene_camera_project` 返回屏幕点和深度；`zcsr_scene_camera_screen_to_plane` 把屏幕点投到任意水平面，`screen_to_ground` 是 `plane_y = 0` 的快捷入口。
- `zcsr_scene_add` 返回稳定 entity id。默认几何是 AABB 矩形；`zcsr_scene_set_quad` 用四个世界坐标点覆盖，点可以共面也可以各自有高度。`zcsr_scene_entity_quad` 读取显式四边形或默认矩形。
- `zcsr_scene_pick` 先反向投影，再用 point-in-quad 判断，支持凹四边形；不再把 pick 限制成 AABB。
- `zcsr_scene_build_order` 按 layer → order → 四角平均相机眼距降序（远到近）→ insertion 产出稳定顺序。`zcsr_scene_build_instances` 输出矩形实例，`zcsr_scene_build_quad_instances` 输出四角屏幕坐标、UV、tint、texture 和平均深度。
- `zcsr_scene_draw_quads` 用调用方 scratch 缓冲区生成四角实例，再经 `zcsr_gl_submit_quad` 提交。GL 路径对凹四边形选择反射顶点相邻的合法对角线，不假设凸多边形；texture 为 0 时使用内部白色纹理。
- 容量是显式契约：`ZCSR_SCENE_MAX_ENTITIES` 是 4096，显式四边形覆盖数组复用同一容量。超限返回 0 或 false，不隐式扩容。
- 编辑器使用的 `.uiscene` 是 editor authoring 文件，不是 engine 的正式场景格式；engine 只接收内存中的 `zcsr_scene`。

## Release 1.0.0 口径

当前版本：`1.0.0`（`include/zcsr/version.h`）。

1.0 只收可构建、可回归、产品实际使用的契约：

- 输入链路包含 `zcsr/input_keymap.h` 的原生码到 `ZCSR_KEY_*` 映射，以及 Win32 真实消息泵测试；Linux/QWERTY 字母位置由 `test_input_keymap.c` 全覆盖。
- 不随发布提交计划、审计、一次性报告、实验性 format/fuzz/logging 门禁。
- 测试只保留最终稳定项；`zcsr_bench` 是测量工具，不是发布测试，也不能把不同条件的采样混成一张性能表。
- editor 目标平台 Windows/Linux/macOS 三平台；平台窗口/输入/GL 仍由 engine 后端提供，editor 只通过公开 `zcsr_*` 接口消费。当前验收状态见下条。
- 运行时只有一个可用渲染后端：GPU（OpenGL 3.3 Core / GLES 3.x）。`zcsr_render_backend` 保留 `CPU` 接口位，但 `zcsr_render_backend_available(CPU)` 返回 false，`zcsr_gl_create_with_backend(..., CPU)` 返回 NULL；不维护第二套软件光栅器。GPU 不可用时直接失败，不静默退回显示域。
- 统一线性工作空间是 1.0 硬契约：Path A 与 Path B 都写 `RGBA16F` 线性帧，`present/read_rgba` 只做一次 LUT encode。设备必须支持 `RGBA16F` 颜色附件，否则 renderer 创建失败。
- 平台证据：Windows 10 + Intel Arc B580 已通过真实 GPU 回读；当前 Windows 154 项注册（153 通过 + 1 硬件跳过）。Linux 当前 HEAD 已在 Kali x86_64 / GCC 16.2 上重跑：154 项注册（148 通过 + 6 显式媒体/硬件 skip / 0 失败），Clang 21 + ASan 0 失败；Editor smoke 为 llvmpipe 软件 GLX，物理 GPU 仍需单独验证。macOS Cocoa 代码路径已完成静态审读和 stub 语法检查，但**没有真实 Mac 构建/运行证据，状态为未验证，不声称 pass**。

## 性能基线（唯一可引用口径）

条件：1920×1080、VSync off、65,535 个相同 sprite、Windows 10、Intel Arc B580；热机 60 帧预热后采样 300 帧，记录端到端帧时间（CPU submit + GPU 完成）。

| 实现 | ms/frame | FPS | update ms | render ms |
|---|---:|---:|---:|---:|
| Unity GL Burst | 0.948 | 1056 | 0.655 | 0.293 |
| Unity D3D11 Burst | 1.049 | 954 | 0.455 | 0.594 |
| ZCSR | 1.210 | 826 | 0.510 | 0.700 |
| Unity D3D11 Job（无 Burst） | 1.821 | 550 | 1.239 | 0.571 |

边界：ZCSR submit-only 0.873/0.943 ms、`glFinish` 4.86–5.62 ms 是不同测量面，不能与上表端到端数字混用；冷机、不同分辨率、不同批次的 CSV 属于探索数据，不作为 1.0 发布结论。

## 开发者入口

不要从引擎 demo 开始做产品。产品由 runtime host 启动；`zcsr_editor` 是它加载的
workspace module，负责 authoring UI、document editing、asset browsing 和 editor
commands。Editor 通过公开 target 链接 engine/runtime/UIM 和明确的 scene
authoring target；它不链接整个 `zcsr_tools`，也不复制这些仓库的源码。

editor 的构建和运行说明在 `zcsr_editor` 仓库的 README。

## 文档

README 负责所有模块的用法总览和发布口径；`docs/` 只放底层架构、函数参数、API 调用和契约详解，不放计划、审计或一次性报告。

| 文件 | 内容 |
|---|---|
| `docs/ARCHITECTURE.md` | 零堆、零耦合、平台/音频/输入后端和线程模型 |
| `docs/API_REFERENCE.md` | 契约头索引和 API 使用规则 |
| `docs/RENDER2D_CONTRACT.md` | 2D 渲染坐标、颜色空间和 G-buffer 契约 |
| `docs/LIGHT2D_MATH_MODEL.md` | Light2D 数学约定 |
| `docs/LIGHT2D_INTEGRATION.md` | Light2D 接入说明 |
| `docs/TESTING.md` | 自动测试与手工验证的分工 |
| `docs/LINUX_VERIFICATION.md` | Linux X11/Wayland/XTEST/XRandR/PipeWire/glibc 验证证据与限制 |
| `docs/MACOS_VERIFICATION.md` | macOS 1.0 验收门与未验证项 |
| `docs/DEVIATIONS.md` | 已知偏差和明确不采纳的条目 |

`docs/archive/` 只保留历史架构、API 和集成说明，用于追溯，不代表当前实现；计划、审计、一次性报告已从发布树移除，Git 历史仍可追溯。

## License

本项目按 GNU Affero General Public License v3.0（AGPL-3.0）授权，完整许可证文本见仓库根目录 `LICENSE`。版权行：Copyright (C) 2026 Talmaidev。

`third_party/` 及 `modules/*/third_party/` 下第三方组件保留其原有许可（见各自目录与 `THIRD_PARTY_NOTICES.md`）。
