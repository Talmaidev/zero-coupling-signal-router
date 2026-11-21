# ZCSR

ZCSR 是一个纯 C11 的 2.5D 游戏/渲染内核。它提供静态库和契约头，不是可直接运行的游戏。

开发者的入口是 `zcsr_editor`。引擎、UIM、runtime、tools 都由 editor 或宿主工程按需链接。

## 仓库分工

ZCSR 按三层拆分，engine 不知道上层产品存在：

```
zcsr_editor                        开发者入口，编辑页面/布局/资产
  ├── UIM                          UI Instance Model + page manager
  ├── zcsr_runtime_shell           窗口、主循环、宿主生命周期
  ├── zcsr_tools                   debug overlay + 2.5D workspace
  └── zcsr engine                  25 个静态库
       ├── core / signal / hsm / platform
       ├── glrender / light2d / sprite / camera / camera3d / culling / atlas / font / image
       ├── audio / audiomix / music / input / inputext / assets / resource / file_service
       └── collision / save / ui / viewport
```

`zcsr_ui_page_manager` 是 UIM 内的 target；`settings`、`menu` 是 page manager 下的页面源码目录，不是独立仓库。

## 这个仓库包含什么

- `include/zcsr/`：模块唯一允许包含的契约头。
- `modules/`：25 个模块，每个模块独立编译，不 include 另一个模块的源码。
- `tests/`：无窗口、可重复的 CTest 用例。
- `integration/`：需要真实窗口、GPU 或音频设备的示例和手工验证，不作为自动测试。
- `third_party/`：单文件第三方依赖，只在对应模块内部使用。

引擎自己的运行期代码不使用 `malloc/free`。固定池和调用方缓冲区承担状态存储；系统库/驱动内部的分配不计入这个约束。

## 构建

需要 CMake 3.16+、C11 编译器；Windows 使用 MSVC。

```powershell
cmake -S . -B build -G "Visual Studio 18 2026" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

当前基线是 25 个模块库、23 个 CTest。测试覆盖 core、signal、hsm、platform、glrender、light2d、audio、music、input、collision、save、ui、viewport 等模块。

## Release 1.0 口径

1.0 只收可构建、可回归、产品实际使用的契约：

- 输入链路包含 `zcsr/input_keymap.h` 的原生码到 `ZCSR_KEY_*` 映射，以及 Win32 真实消息泵测试。
- 不随发布提交计划、审计、一次性报告、实验性 format/fuzz/logging 门禁。
- 测试只保留最终稳定项；`zcsr_bench` 是测量工具，不是发布测试，也不能把不同条件的采样混成一张性能表。
- editor 仍为 Windows-only，非 Windows 编译在 `editor_app.cpp` 显式 `#error`；不声明 Linux/macOS 编辑器支持。

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

不要从引擎 demo 开始做产品。编辑器仓库 `zcsr_editor` 负责窗口、页面编辑、资产和工程文件；它通过公开 target 链接 engine/runtime/UIM/tools，不复制这些仓库的源码。

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
| `docs/DEVIATIONS.md` | 已知偏差和明确不采纳的条目 |

`docs/archive/` 保留历史架构、API、数学分析和集成说明，用于追溯设计演进，不代表当前实现；临时任务分工、阶段计划和一次性审计报告不进入发布树。历史文档中的旧模块名和旧口径以归档快照为准。

## 许可

AGPLv3。衍生作品（包括网络服务）必须按同一许可开源；不能并入闭源商业产品。
