# ZCSR · 零耦合 2.5D 引擎

纯 C11 的 2.5D 游戏内核。26 个模块只通过 `include/zcsr/` 的契约头通信，模块之间没有 include；
主渲染路径零堆分配；平台后端在编译期选择。

本仓库只产出静态库与测试。可运行 demo 在宿主工程 `game_test/` 中。

## 事实

| 项 | 值 | 口径 |
|---|---|---|
| 模块 | 26 | `modules/` 下的目录数 |
| 公共契约头 | 67 | `include/zcsr/*.h` |
| 模块代码 | 约 22,300 行 | `modules/` 下 82 个 `.c/.h/.m` |
| 测试源 | 25 个 `test_*.c` | `tests/` |
| ctest 注册 | 14 项 | `game_test` 的 CMakeLists |
| 提交 | 164 | `main` 分支 |
| 语言 / 标准 | C11–C17 | 无 C++、无 STL |
| 引擎自身运行期堆分配 | 0 | 例外边界见 `docs/ARCHITECTURE.md` |
| 许可 | AGPLv3 | — |

## 架构

箭头只表示 include 关系，不表示调用顺序。

```
include/zcsr/*.h        ← 唯一的包含面
  ↑
  ├── core              arena + 只读状态缓冲 + 日志实现
  ├── signal            编译期连接表（X-macro），_Generic 校验槽签名
  ├── hsm               状态树 + 祖先链匹配，持久化到状态缓冲
  ├── platform          原生 surface，按平台编译期选后端
  ├── glrender          GL 3.3 Core / ES3；精灵批处理、纹理、字体、FBO
  ├── light2d           延迟光照：4 附件 G-buffer、逐光源累积、slab 阴影
  ├── camera            height-shear 正交投影，唯一的 k
  ├── input / inputext  固定环形队列 / 鼠标与手柄
  ├── ui                组件树、命中测试、绘制
  ├── app_shell         设置页数据模型
  └── runtime_viewport  contain fit + letterbox
```

唯一的依赖例外：所有模块都会链接 `core` 的 `zcsr_log_write`。
准确说法是"契约隔离 + 一个基础设施依赖"，不是绝对的零依赖。

`tools/check_no_cross_includes.sh` 在构建期扫描 `modules/`；任何模块 include 另一个模块、
或使用相对路径，构建失败。

## 构建

需要 CMake 3.16+ 与 C11 编译器（MSVC / GCC / Clang）。

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

MSVC 需要 `/experimental:c11atomics`（`audiomix` 使用 `<stdatomic.h>`，已在 `CMakeLists.txt` 中开启）。

可运行 demo 不在本仓库：宿主在 `game_test/`（`main.c` → `uim_component_manager_init()` →
`zcsr_window_main()`）。UI 外壳与组件管理器在 UIM 仓库，编辑器宿主在 UIR_Editor 仓库。

## 平台

只列有证据的状态。"构建"指本机能编过；"运行"指有实机或虚拟显示证据。

| 平台 | 构建 | 运行 | 已知限制 |
|---|---|---|---|
| Windows 10+ / Win32 | ✅ | ✅ 实机 | 主验证平台；overlay 有真实实现 |
| Linux / X11 | ✅ | ⚠️ Xvfb 出帧 | 无实机长跑记录 |
| Linux / Wayland | ✅ | ⚠️ 受限 | 显示器枚举固定返回 1920×1080；窗口模式恒为 WINDOWED；光标显示/位图/重置未实现；size limits 为空实现；overlay 不绘制；evdev 手柄代码不编译 |
| macOS 12+ / Cocoa | ⚠️ 有代码 | ❌ 未验证 | NSOpenGL + AudioUnit 路径未实机跑过 |
| Android API 24+ | ✅ 构建 + 签名 | ❌ 无设备 | `minSdkVersion=24`、`targetSdkVersion=36`；EGL + AAudio 路径未运行验证 |

这张表就是全部证据。没有"全平台支持"这句话。

## 设计取舍

- **延迟渲染**：光照成本与物体数解耦，只与像素数和光源数有关。
  代价：不支持硬件 MSAA（G-buffer 是屏幕空间纹理）；不处理半透明（镂空走 alpha-cutout，
  纯透明走 UI 路径）；四个附件每帧读写一遍，带宽开销固定。
- **解析式阴影**：遮挡物是轴对齐棱柱，用 segment-vs-prism slab 求交替代 shadow map，
  零 bias、不额外增加渲染 pass。8 个遮挡盒上限是性能预算（GLSL 循环会被完全展开），
  超出部分由 CPU 侧按距离裁剪。出现任意三角网格遮挡时需要改回 shadow map。
- **零堆**：渲染器状态建在调用方给的静态缓冲里，容量编译期定死。
  代价：不能动态扩容，宿主要更大的场景就得重启并给更大缓冲。
- **无深度测试**：2.5D 场景用 painter's order，排序键 `q = k*(y-cy) + z`，
  代价是宿主必须正确排序，两个斜插物体交叉时失效。

## 文档

| 文件 | 内容 |
|---|---|
| `docs/ARCHITECTURE.md` | 零堆 / 零耦合策略、线程模型、例外边界 |
| `docs/API_REFERENCE.md` | 契约头 API |
| `docs/RENDER2D_CONTRACT.md` | 2D 渲染契约：坐标、颜色空间、G-buffer 布局 |
| `docs/LIGHT2D_MATH_MODEL.md` | 光照数学模型 |
| `docs/LIGHT2D_INTEGRATION.md` | 接入指南与版本冻结记录 |
| `docs/TESTING.md` | 测试约定 |
| `docs/DEVIATIONS.md` | 已知偏离与未实现项 |

`docs/archive/` 是历史过程文档，不代表当前状态。

## 许可

AGPLv3。衍生作品（含网络服务）须以同一许可开源；不可并入闭源商业产品。
