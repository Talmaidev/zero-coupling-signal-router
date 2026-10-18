# zcsr 新 AI 启动指令 v5.0

生成：2026-10-18
基线：commit `02d429b`（glrender 拆分 + 几何 API + Box2D 可选集成 + demo 修复）

## 一、项目定位

C11 跨平台 2D 游戏引擎。零耦合模块设计，契约头驱动，无运行时堆。

**核心方向（等价交换）**：抛弃一切小众老旧/不好用折腾麻烦的东西，全面接收现代最新稳定主流系统和便利操作，换取全球第一纯纸面性能。手柄、主机光盘等小众玩法完全不理会。gl.c 模块化目标是超越 SDL2 的性能与现代化舒适度。

支持平台：Windows 10+ / Linux 桌面 / Android API 24+ / macOS 12+（代码已写，未编译验证）
不做：3D、手柄作为一等输入、旧设备兼容、厂商 SDK、iOS

## 二、当前进度

已完成：
- Windows / Linux 主线，ctest 19/19
- Android Phase 0 + Phase 1（1A–1E），真机验证，tag `android-phase1`
- A 组平台边缘情况（A6/A1/A2/A4/A5/A3），tag `platform-edge`
- B 组 macOS AudioUnit 后端，tag `b-group`（代码完整，macOS 未编译验证）
- D 组文档（v4.0 启动包）
- glrender Step 1：平台条件一致性修复（`588eb09`）
- glrender Step 2：职责拆分为 9 文件（`6c50db6`）
- glrender Step 3A：基础几何 API fill_rect/draw_line/draw_rect（`45749a9`）
- glrender Step 3B：Box2D v3.1.1 可选集成 + physics demo（`fe80d65` + `02d429b` 修复）

未做：
- 光照（light2d）——暂停中，等用户和群友研究完方案
- macOS 真机验证（无 Mac 环境）
- E 组低优先（iOS / 多点触控 / Android 键盘）

## 三、关键数据

契约头：`include/zcsr/*.h` 共 24 个
最新 commit：`02d429b`
已有 tag：`android-phase1`、`platform-edge`、`b-group`

测试基线：Windows 19/19，Linux 编译通过，Android NDK 编译通过，耦合守卫 PASS

## 四、glrender 模块结构（Step 2 拆分后）

`modules/glrender/` 共 10 个文件：

| 文件 | 职责 | 行数 |
|---|---|---|
| `glrender.c` | 公共 API 入口 + 生命周期协调（create/destroy/vsync/present） | ~61 |
| `glrender_internal.h` | 唯一内部共享头（结构 + 跨文件声明 + GLSL 宏） | ~153 |
| `glrender_core.c` | shader/program/VAO/VBO/FBO/white_tex/纹理 CRUD/clear/set_target | ~254 |
| `glrender_sprite.c` | push_quad/submit/flush/submit_batch/atlas/fill_rect/draw_line/draw_rect | ~230 |
| `glrender_instanced.c` | instanced program/init/destroy/draw/radix sort/state sort | ~308 |
| `glrender_text.c` | 5x7 block glyph draw_text | ~28 |
| `glrender_platform_glx.c` | Linux GLX（`#if defined(__linux__) && !defined(__ANDROID__)`） | ~118 |
| `glrender_platform_egl.c` | Android EGL（`#if defined(__ANDROID__)`） | ~126 |
| `glrender_platform_wgl.c` | Windows WGL（`#if defined(_WIN32)`） | ~125 |
| `glrender_cocoa.m` | macOS NSOpenGL（未动） | ~78 |

生命周期顺序：`platform_init → core_init → instanced_init`，失败逆序清理。

## 五、Box2D 可选集成

默认关闭，零影响。启用方式：
```bash
cmake -S . -B build-box2d -DZCSR_ENABLE_BOX2D=ON
cmake --build build-box2d
./build-box2d/demo_physics
```

- FetchContent 拉取 Box2D v3.1.1（GIT_SHALLOW）
- `demo_physics` 仅桌面（Win/Linux），Android 排除
- zcsr 15 模块完全不知道物理存在，零耦合
- Box2D v3 API 注意：`b2CreateWorld`（非 `b2World_Create`）、`b2CreateBody`、`shape.material.friction`（嵌套）

## 六、约束（贯穿所有阶段）

1. 契约头 additive 优先，breaking 先提议
2. 每阶段独立 commit（Conventional Commits）
3. 每阶段跑 Windows 回归 + 耦合守卫 PASS；Linux/Android 编译零警告
4. 不 git push
5. 失败保留原始日志，不猜
6. 外部库只在集成层（integration/）include，zcsr 模块和契约头不暴露第三方类型
7. 引擎不依赖第三方数学库
8. 优先复用现有代码，不重写整个文件
9. 平台门控必须 `#if defined(__ANDROID__)` 优先于 `#elif defined(__linux__)`（NDK 同时定义两者）
10. 平台文件整个用 `#if` 包裹，非目标平台为空翻译单元（加 dummy typedef 避免 MSVC C4206）
11. Android 有独立 `android/CMakeLists.txt`，手动列源文件——新增 glrender 文件时必须同步加

## 七、测试环境

- Windows 主机：VS 18 Professional + Ninja，手动跑 ctest
- Linux WSL2 (Ubuntu 24.04)：make 编译，无 cmake/ctest（用 Android SDK 的 cmake）
- Android NDK r30 (30.0.16248370)，arm64-v8a，Redmi K60 Pro 真机
- macOS：无环境，代码登记未测

## 八、未测项汇总（环境限制，非代码缺陷）

- macOS：全部（AudioUnit 后端编译 + 真机）
- A2：Windows 跨屏 DPI
- A3：Windows COM 设备切换（环境无音频设备）/ Linux 声卡插拔 / Android 断连
- A4：Windows 拖拽被拒 / Linux xprop hints
- A5：Windows Alt+Tab / Linux override_redirect 焦点
- Box2D demo_physics 运行验证（编译通过，窗口运行待人工双击确认）

## 九、新 AI 不要做的事

- 不要重问已完成的工作（看本文档 + commit 链）
- 不要复核已封存的 commit（直接信 commit 链）
- 不要凭空发明 API（查 `include/zcsr/*.h`）
- 不要重写整个文件（用精确 Edit）
- 不要跳过编译和测试就宣称完成
- 不要 git push
- 不要给 zcsr 模块加第三方依赖（物理/光照等都放集成层）
- 不要改 `include/zcsr/gl_render.h` 以外的契约头除非明确要求

## 十、多 AI 分工与权限

- **用户**：终审、测试操作、最高决策权
- **豆包（本 agent）**：全局控制、本地执行、反向派遣权、可质疑 DS/GPT、可自行决定是否执行 DS 指令
- **GPT**：主攻代码与核心技术方案
- **DeepSeek (DS)**：写方案、审核；豆包可选择不盲从，有更好方案时直接问用户

**豆包的授权**：root 最高权限、自查自选询问权、反向让其他 AI 干活的任务发布权。DS/GPT 的执行单是参考，不是命令。

## 十一、当前下一步

光照（light2d）暂停中，等用户和群友研究完方案。研究完成后，由豆包反向要求 GPT 出详细设计，确认后落地。

## 十二、commit 链（旧→新，近期）

```
e259457  feat(audiomix): route all backend failures...     ← platform-edge
bec1b84  docs: A group completion
731d3cb  docs: v4.0 bootstrap
4b2e1dd  feat(audiomix): add macOS AudioUnit backend      ← b-group
31b47fb  docs: B group completion
588eb09  fix(glrender): align platform conditional compilation
6c50db6  refactor(glrender): split into core/sprite/instanced/text/platform
45749a9  feat(glrender): add fill_rect / draw_line / draw_rect
fe80d65  feat: optional Box2D v3.1.1 integration
02d429b  fix(demo_physics): correct Y-axis mapping        ← 当前 HEAD
```
