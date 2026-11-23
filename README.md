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

```powershell
cmake -S . -B build -G "Visual Studio 18 2026" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

macOS 15+ / Xcode 16+ 使用同一套 CMake 源，但必须在有 GUI 会话的真实 Mac 上构建和运行：

```bash
cmake -S . -B build_macos -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0
cmake --build build_macos -j
./tools/verify_macos_linear.sh
```

脚本保留给将来有真实 Mac 时复验。按用户 2026-11-23 决定，macOS 真机验收已 pass、不再作为 1.0 发布门；没有真实 Mac 时不声称已编译或运行。

当前基线是 29 个模块库、37 个 CTest（36 个旧基线 + 新增纯 CPU `light2d_math`）。Windows 旧基线 36/36，新增 `light2d_math` 29/29；Linux 补充验证分四条路径：Ubuntu 22.04 + GCC 11 + X11/GLX（Xvfb/RANDR/XTEST）35/35、原生 Wayland/EGL（Weston headless）34/34、PipeWire 0.3.48 + null sink 35/35、Ubuntu 20.04 chroot + glibc 2.31 重新构建 35/35（这些日志是新增测试前的历史结果，未重跑）。Linux 日志来自两个互不一致的云副本：`/home/user/zcsr_linux` 有最新 PipeWire/XRandR，但仍含已清理的 composite 死代码和旧 XTest 门控；`/home/user/zcsr_linux_1_0_verify` 已清理 shader/加门控，但 audio/platform 仍是旧版。按用户口径 Linux 环境不再作为门，物理机结果已过，不为副本差异重跑；macOS 与 Git tag/发布打包不属于当前工作范围。Linux 产物在 glibc 2.31 工具链上的最高符号为 `GLIBC_2.29`。Kali 物理机此前通过 35/35，按用户口径沿用、不重跑全量；云主机只有 Mesa llvmpipe，缺 GPU/声卡不作为发布阻塞。Linux AMD/NVIDIA 真实 GPU 驱动仍未单独验证；细节和限制见 `docs/LINUX_VERIFICATION.md`。测试覆盖 core、signal、hsm、platform、glrender、light2d、audio、music、input、collision、save、ui、viewport、color_pipeline、scene_camera、scene、scene_render 等模块。

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
- 平台证据：Windows 10 + Intel Arc B580 已通过真实 GPU 回读；Kali 物理机此前 X11/GLX 35/35，本次无严重改动、按用户口径沿用；Linux X11/GLX 与原生 Wayland/EGL 也在 Mesa llvmpipe 上真实回读；Linux 产物在 Ubuntu 20.04/glibc 2.31 工具链上重编并 35/35 通过，最高 `GLIBC_2.29` 符号。Linux 真实 AMD/NVIDIA 驱动仍未单独验证，云主机缺 GPU 不作为发布阻塞。macOS Cocoa 代码路径已完成静态审读；按用户 2026-11-23 决定，macOS 真机验收 pass、不作为 1.0 门，且不声称已在 Mac 上运行。

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

## 许可

AGPLv3。衍生作品（包括网络服务）必须按同一许可开源；不能并入闭源商业产品。
