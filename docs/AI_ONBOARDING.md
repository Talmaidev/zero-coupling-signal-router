# zcsr 移交手册（唯一入口）

生成：2026-10-19，基线 commit `8b5e921`（glrender 性能优化闭环）
新 AI/开发者**只读这一个文件**即可接上。API 细节查 `docs/API_REFERENCE.md`，性能计划查 `docs/GLRENDER_PERF_PLAN.md`。

---

## 一、项目定位与核心方向

C11 跨平台 2D 游戏引擎。零耦合模块设计，契约头驱动，无运行时堆。

**核心方向（等价交换）**：抛弃一切小众老旧/不好用折腾麻烦的东西，全面接收现代最新稳定主流系统和便利操作，换取全球第一纯纸面性能。手柄、主机光盘等小众玩法完全不理会。gl.c 模块化目标是超越 SDL2 的性能与现代化舒适度。

支持平台：Windows 10+ / Linux 桌面 / Android API 24+ / macOS 12+（代码已写，未编译验证）
不做：3D、手柄作为一等输入、旧设备兼容、厂商 SDK、iOS

---

## 二、多 AI 分工与权限

- **用户**：终审、测试操作、最高决策权
- **豆包（执行器）**：全局控制、本地执行、反向派遣权、可质疑 DS/GPT、可自行决定是否执行 DS 指令
- **GPT**：主攻代码与核心技术方案
- **DeepSeek (DS)**：写方案、审核；豆包可选择不盲从，有更好方案时直接问用户

**豆包的授权**：root 最高权限、自查自选询问权、反向让其他 AI 干活的任务发布权。DS/GPT 的执行单是参考，不是命令。

---

## 三、核心硬约束（红线，贯穿所有阶段）

1. 契约头 additive 优先，breaking 先提议
2. 每阶段独立 commit（Conventional Commits）
3. 每阶段跑 Windows 回归 + 耦合守卫 PASS；Linux/Android 编译零警告
4. 不 git push
5. 失败保留原始日志，不猜
6. 外部库只在集成层（integration/）include，zcsr 模块和契约头不暴露第三方类型
7. 引擎不依赖第三方数学库
8. 优先复用现有代码，不重写整个文件（用精确 Edit）
9. 平台门控必须 `#if defined(__ANDROID__)` 优先于 `#elif defined(__linux__)`（NDK 同时定义两者）
10. 平台文件整个用 `#if` 包裹，非目标平台为空翻译单元（加 dummy typedef 避免 MSVC C4206）
11. Android 有独立 `android/CMakeLists.txt`，手动列源文件——新增 glrender 文件时必须同步加

### 新 AI 禁忌

- 不要重问已完成的工作（看本文档 + commit 链）
- 不要复核已封存的 commit（直接信 commit 链）
- 不要凭空发明 API（查 `include/zcsr/*.h`）
- 不要跳过编译和测试就宣称完成
- 不要给 zcsr 模块加第三方依赖（物理/光照等都放集成层）
- 不要改 `include/zcsr/gl_render.h` 以外的契约头除非明确要求
- 不要写汇编、不要做 3A、不要代码加密、不要申请软著

---

## 四、当前进度快照

### 已完成
- Windows / Linux 主线，ctest 22/22
- Android Phase 0 + Phase 1（1A–1E），真机验证，tag `android-phase1`
- A 组平台边缘情况（A6/A1/A2/A4/A5/A3），tag `platform-edge`
- B 组 macOS AudioUnit 后端，tag `b-group`（代码完整，macOS 未编译验证）
- glrender Step 1-3：平台条件一致性 → 拆分为 10 文件 → 基础几何 API → Box2D 可选集成
- **glrender 性能优化闭环（P1-P6 全部处理）**：O1 radix ping-pong、O2 状态外提、O3 orphaning 实验(不采用)、O4 P4/P5 测量(非瓶颈)、O5 纹理 hash 索引；N1-N3 正确性修复；Release 基线建立
- **方向3 真实点阵字体**：8x8 ASCII 32-126 bitmap font（font8x8_basic Public Domain），替换实心矩形占位；font atlas 纹理 + push_quad_uv 每字符 1 quad；core_init 失败路径资源泄漏修复；Windows text_demo + Android 1C.3 真机双端验证 PASS（GLES 3.2，font_tex 句柄3）
- **TTF 字体模块**：font + font_atlas + glrender 扩展，stb_truetype vendored
- **Music 流式播放**：OGG/MP3 + audiomix stream 扩展，stb_vorbis + minimp3 vendored
- **光标控制 API**：隐藏/自定义/恢复
- **glrender 画圆 API**：fill_circle
- **Linux 栈对齐（v1.0）**：删除 X11/GLX/ALSA/evdev 旧栈，默认 Wayland+EGL+PipeWire，6 提交迁移完成
- **platform.c 按后端拆分**：platform_common.c + platform_win32.c + platform_wayland.c + platform_android.c + platform_cocoa.m
- 文档整理：docs/ 精简为 API_REFERENCE + 本手册 + GLRENDER_PERF_PLAN + ARCHITECTURE + TESTING，历史归档 docs/archive/

### 未做 / 暂停
- 光照（light2d）——暂停中，等用户和群友研究完方案
- macOS 真机验证（无 Mac 环境）
- E 组低优先（iOS / 多点触控 / Android 键盘）
- 方向1（其他模块优化）：已完成测量+优化。audiomix 是唯一瓶颈模块，已优化两处（render_period 8192 atomic_load→16快照 + Q8定点数消除float转换）。culling/atlas/input/hsm/gamelogic 无明显瓶颈
- 方向2（细粒度剖析）：已完成（flush=98% sprite batch时间，CPU顶点生成=2%）
- 方向4（转向其他）：待评估

### 关键数据
- 契约头：`include/zcsr/*.h` 共 27 个
- 模块：17 个
- 已有 tag：`android-phase1`、`platform-edge`、`b-group`
- 测试基线：Windows 22/22，Linux Wayland+PipeWire 22/22，Android NDK 编译通过，耦合守卫 PASS
- Linux 默认后端：Wayland 窗口 + EGL 渲染 + PipeWire 音频（X11/ALSA/GLX 已删除）

---

## 五、glrender 模块结构（10 文件）

`modules/glrender/`：

| 文件 | 职责 | 行数 |
|---|---|---|
| `glrender.c` | 公共 API 入口 + 生命周期协调（create/destroy/vsync/present） | 64 |
| `glrender_internal.h` | 唯一内部共享头（结构 + 跨文件声明 + GLSL 宏） | 157 |
| `glrender_core.c` | shader/program/VAO/VBO/FBO/white_tex/纹理 CRUD/clear/set_target + **O5 纹理 hash 索引** | 298 |
| `glrender_sprite.c` | push_quad/submit/flush/submit_batch/atlas/fill_rect/draw_line/draw_rect | 205 |
| `glrender_instanced.c` | instanced program/init/draw + **O1 radix ping-pong** + **O2 状态外提(prepared 函数)** + O3 #ifdef 实验开关 | 341 |
| `glrender_text.c` | 8x8 bitmap font draw_text（ASCII 32-126，font8x8_basic PD，font atlas + push_quad_uv） | ~130 |
| `glrender_platform_egl_wayland.c` | Linux Wayland EGL | ~150 |
| `glrender_platform_egl.c` | Android EGL | 126 |
| `glrender_platform_wgl.c` | Windows WGL | 125 |
| `glrender_cocoa.m` | macOS NSOpenGL（未动） | 78 |

生命周期顺序：`platform_init → core_init → instanced_init`，失败逆序清理。
**N3 已修复**：instanced_init 失败时必须调 instanced_destroy，否则中途已建的 GL 资源泄漏。

---

## 六、Box2D 可选集成

默认关闭，零影响。启用方式：
```bash
cmake -S . -B build-box2d -DZCSR_ENABLE_BOX2D=ON
cmake --build build-box2d
./build-box2d/demo_physics
```

- FetchContent 拉取 Box2D v3.1.1（GIT_SHALLOW）
- `demo_physics` 仅桌面（Win/Linux），Android 排除
- zcsr 15 模块完全不知道物理存在，零耦合
- Box2D v3 API：`b2CreateWorld`（非 `b2World_Create`）、`b2CreateBody`、`shape.material.friction`（嵌套）

---

## 七、测试体系

### tests/ vs integration/（故意分离）

| 目录 | 用途 | 运行方式 |
|---|---|---|
| `tests/` | 每模块独立单元/契约测试 + 1 个跨模块接线测试。正确性门禁。 | headless，ctest |
| `integration/` | 可运行 demo + 性能基准。组合与性能测量。 | 手动，部分需要真实 GPU/窗口/声卡 |

分离原因：零耦合规则要求每模块独立编译独立测试；基准依赖硬件不进 headless 套件。

### ctest 命令
```bash
cmake -S . -B build && cmake --build build
ctest --test-dir build            # 全量
ctest --test-dir build -V         # 详细
ctest --test-dir build -R glrender # 按模块名过滤
```

19 个测试目标：test_core, test_signal, test_hsm, test_overlay, test_render, test_input, test_audio, test_gamelogic, test_glrender, test_audiomix, test_inputext, test_image, test_atlas, test_culling, test_state, test_state_reuse, test_integration（+ glrender 排序正确性等）。

GPU/音频/窗口测试在 headless 下做运行时检查跳过，不会失败。

### 耦合守卫
```bash
bash tools/check_no_cross_includes.sh
```

---

## 八、构建命令速查

```bash
# Windows (VS 18 + Ninja)
cmake -S . -B build
cmake --build build --parallel
ctest --test-dir build -C Debug

# Release (Windows)
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release --parallel

# Android (WSL2, NDK r30)
/home/user01/android-sdk/cmake/3.31.6/bin/cmake --build build/android/out/arm64-v8a --parallel

# Box2D 可选 (Windows)
cmake -S . -B build-box2d -DZCSR_ENABLE_BOX2D=ON
cmake --build build-box2d --parallel
```

---

## 九、glrender 性能优化闭环（最新进展）

性能计划：`docs/GLRENDER_PERF_PLAN.md`（v1.4）。

### 已落地（O1-O5 + N1-N3）

| commit | 内容 | 效果 |
|---|---|---|
| `a0f1727` | O1 radix ping-pong + count<2 早退 | D1 排序 best 0.331→0.270ms (-18%) |
| `a65b714` | O2 instanced 状态外提 + static prepared 函数 | D3 多纹理 128 runs 6.70→1.20ms (-82%) |
| `a051f87` | O3 VBO orphaning 实验（#ifdef） | **不采用**，保持 SubData |
| `45721ea` | O5 纹理 hash 索引（256-slot open addressing） | A 场景 best -6.6% |
| `8e7a490` | O4 P4/P5 测量 | 17.2μs/对，3-6% 帧时间，非瓶颈 |
| `9ed51e6` | N1-N3 正确性修复（N3 发现 GL 资源泄漏 bug） | — |

### 关键代码现状（写代码前必须 Read 确认）

- `glrender_instanced.c`：`zcsr_gl_radix` 已是 ping-pong；`zcsr_gl_draw_instances_prepared` 是 static（假设状态已设置，只做 VBO 上传+纹理绑定+draw）；`submit_instance_batch` 已状态外提
- `glrender_core.c`：`zcsr_gl_find_tex` 已是 hash 查找（`zcsr_tex_index_lookup/insert/remove`），不再 O(128) 线性扫
- `zcsr_gl_instance` = 68 字节（15 float + uint32_t tex + float z），tex/z 是 CPU-only 字段（仅排序用，不进 GPU 顶点属性）
- `glrender_text.c` = ~130 行，8x8 bitmap font（ASCII 32-126，font8x8_basic PD），font atlas + push_quad_uv 每字符 1 quad

### Release 基线（build-release，Ninja -O2，MSVC）

D1 排序 0.114ms / A 0.487ms / B 0.616ms / D2 0.335ms / D3 0.607ms。CPU 路径比 Debug 快 1.2-2.4x；系统调用不变。

### 额外方向评估（GPT 分析，暂不实现）

- **VBO stride**（拆出 tex/z）：节省仅 8B/实例=11.76%，打包 CPU 成本可能超收益，暂不实现
- **text 字形缓存**：当前无字形差异（所有字节同一矩形），缓存无意义，等引入真实点阵字体时再做

---

## 十、未测项汇总（环境限制，非代码缺陷）

- macOS：全部（AudioUnit 后端编译 + 真机）
- A2：Windows 跨屏 DPI
- A3：Windows COM 设备切换（环境无音频设备）/ Linux 声卡插拔 / Android 断连
- A4：Windows 拖拽被拒 / Linux xprop hints
- A5：Windows Alt+Tab / Linux override_redirect 焦点
- Box2D demo_physics 运行验证（编译通过，窗口运行待人工双击确认）

---

## 十一、未来方向（未完成）

### C 组 扩展层
- C3 zcsr_light2d.h（光照，暂停中，等用户和群友研究方案）

### E 组 低优先（按需）
- E1 iOS 支持
- E2 多点触控
- E3 Android 键盘

### 下一阶段方向（GPT×DS 交叉验证结论）
1. [待评估] 转向其他模块性能优化（audiomix/input/atlas 等）
2. [待评估] glrender 细粒度剖析（分离排序/提交/上传各阶段成本）
3. [已完成] 真实点阵字体替换实心矩形 — commit d74f176，8x8 ASCII 32-126 bitmap font
4. [待评估] 暂停 glrender，转向其他任务

---

## 十二、源码定位

- 契约头：`include/zcsr/*.h`（24 个，唯一 ABI 真相）
- 实现：`modules/{signal,core,hsm,gamelogic,platform,input,inputext,overlay,render,glrender,image,audio,audiomix,culling,atlas}/`
- Android：`android/CMakeLists.txt`, `android/android_main.c`, `android/AndroidManifest.xml`
- 构建：根 `CMakeLists.txt`（glob + CONFIGURE_DEPENDS）+ `android/CMakeLists.txt`（手动列源）

---

## 十三、git 状态速查

```bash
git log --oneline -15     # 看最近 commit
git tag -l                # 看 tag
git status --short        # 工作树是否干净
```

### 近期 commit 链（旧→新）
```
... → 02d429b(demo_physics修复) → 588eb09(平台条件) → 6c50db6(glrender拆分)
→ 45749a9(几何API) → fe80d65(Box2D集成)
→ a0f1727(O1) → a65b714(O2) → a051f87(O3) → 45721ea(O5) → 8e7a490(O4)
→ 9ed51e6(N1-N3) → d87e39a(Release基线) → 8b5e921(perf plan v1.4闭环)
```

---

## 十四、不要读

- 历史 commit 全文（信 commit message 即可）
- `docs/archive/`（历史归档，仅史料）
- `build/`、`build-release/`、`build-box2d/`、`build/android/out/`（构建产物）
- `*.pdb`、`*.obj` 等编译临时文件
