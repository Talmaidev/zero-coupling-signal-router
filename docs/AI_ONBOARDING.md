# zcsr 新对话必读文件包

## 一、必读（按优先级）

1. `docs/BOOTSTRAP_v5.0.md` — 启动指令 v5.0，项目定位、进度、约束、glrender 拆分结构、Box2D 集成、多 AI 权限
2. `docs/PHASE_A_COMPLETE.md` — A 组封存报告
3. `docs/PHASE_B_COMPLETE.md` — B 组 macOS 封存报告
4. `docs/BOX2D_INTEGRATION.md` — Box2D v3.1.1 可选集成说明

## 二、按需读

- `include/zcsr/*.h` — 24 个契约头，唯一 ABI 真相
- `modules/*/` — 15 个实现模块。glrender 已拆为 9 文件（见 BOOTSTRAP_v5.0 第四节）
- `android/` — Android CMake 工程（独立 CMakeLists，手动列源文件）
- `tests/*.c` — 19 个 ctest
- `integration/*.c` — 基准测试、验证程序、demo_physics
- `tools/check_no_cross_includes.sh` — 耦合守卫
- `tools/android_pack.sh` — APK 打包

## 三、不要读

- 历史 commit 全文（信 commit message 即可）
- `docs/BOOTSTRAP_v4.0.md`（已过时，被 v5.0 取代）
- `build/`、`build-release/`、`build-box2d/`、`build/android/out/`（构建产物）
- `vc140.pdb` 等编译临时文件

## 四、git 状态速查

```
git log --oneline -15     # 看最近 commit
git tag -l                # 看 tag（android-phase1, platform-edge, b-group）
git status --short        # 工作树是否干净
```

## 五、源码定位

- 契约头：`include/zcsr/*.h`
- 实现：`modules/{signal,core,hsm,gamelogic,platform,input,inputext,overlay,render,glrender,image,audio,audiomix,culling,atlas}/`
- glrender 拆分：`modules/glrender/glrender.c`（入口）+ `glrender_core.c` + `glrender_sprite.c` + `glrender_instanced.c` + `glrender_text.c` + `glrender_platform_{glx,egl,wgl}.c` + `glrender_cocoa.m`
- Android：`android/CMakeLists.txt`, `android/android_main.c`, `android/AndroidManifest.xml`
- 测试：`tests/*.c`（19 个）
- 构建：根 `CMakeLists.txt`（glob + CONFIGURE_DEPENDS）+ `android/CMakeLists.txt`（手动列源）

## 六、构建命令速查

```bash
# Windows (VS 18 + Ninja)
cmake -S . -B build
cmake --build build --parallel
ctest --test-dir build -C Debug

# Linux (WSL2, make)
make -C build/linux-a2 -j4

# Android (WSL2, NDK r30)
/home/user01/android-sdk/cmake/3.31.6/bin/cmake --build build/android/out/arm64-v8a --parallel

# Box2D 可选 (Windows)
cmake -S . -B build-box2d -DZCSR_ENABLE_BOX2D=ON
cmake --build build-box2d --parallel

# 耦合守卫
bash tools/check_no_cross_includes.sh
```

## 七、glrender 性能优化阶段（最新进展，必读）

性能计划：`docs/GLRENDER_PERF_PLAN.md`（v1.3，含落地结果 + Release 基线）。

### 已落地（O1-O5 + N1-N3）

| commit | 内容 | 效果 |
|---|---|---|
| `a0f1727` | O1 radix ping-pong + count<2 早退（glrender_instanced.c） | D1 排序 best 0.331→0.270ms (-18%) |
| `a65b714` | O2 instanced 状态外提：framebuffer/viewport/program/VAO 提到 run 循环外；新增 static `zcsr_gl_draw_instances_prepared` | D3 多纹理 128 runs 6.70→1.20ms (-82%) |
| `a051f87` | O3 VBO orphaning 实验（`#ifdef ZCSR_GL_VBO_ORPHAN`） | **不采用**，保持 SubData |
| `45721ea` | O5 纹理 hash 索引（256-slot open addressing，glrender_core.c + glrender_internal.h） | A 场景 best -6.6% |
| `8e7a490` | O4 P4/P5 测量（bench_worst.c 微基准） | 17.2μs/对，3-6% 帧时间，非瓶颈 |
| `9ed51e6` | N1-N3 正确性修复：N3 发现 instanced_init 失败时未调 instanced_destroy（GL 资源泄漏），已修复；N1/N2 补契约注释 | — |
| `7c03bf0` / `d87e39a` | perf plan v1.2/v1.3：落地结果 + Release 基线 | — |

### 关键代码现状（写代码前必须 Read 确认）

- `glrender_instanced.c`：`zcsr_gl_radix` 已是 ping-pong（src/dst 交替，4 pass 偶数结束 src==items）；`zcsr_gl_draw_instances_prepared` 是 static 内部函数（假设状态已设置，只做 VBO 上传+纹理绑定+draw）；`zcsr_gl_submit_instance_batch` 已状态外提（只调一次 make_current/framebuffer/viewport/program/VAO）
- `glrender_core.c`：`zcsr_gl_find_tex` 已改为 hash 查找（`zcsr_tex_index_lookup/insert/remove`），不再 O(128) 线性扫；`zcsr_gl_texture_create` 调 `zcsr_tex_index_insert`，`zcsr_gl_texture_destroy` 调 `zcsr_tex_index_remove`
- `glrender.c`：`zcsr_gl_create` 失败路径已修复（instanced_init 失败时调 instanced_destroy）
- `zcsr_gl_instance` = 68 字节（15 float + uint32_t tex + float z），tex/z 是 CPU-only 字段（仅排序用，不进 GPU 顶点属性）
- `glrender_text.c` = 28 行，5x7 块字形，draw_text 逐 glyph 生成顶点走 sprite 队列

### Release 基线（build-release，Ninja -O2，MSVC）

D1 排序 0.114ms / A 0.487ms / B 0.616ms / D2 0.335ms / D3 0.607ms。CPU 路径比 Debug 快 1.2-2.4x；系统调用不变。所有优化承诺以 Release 为准。

### 当前剩余项

- **P2**：instanced VBO stride（tex/z 12 字节 CPU-only 字段占 18%，是否拆出）
- **P6**：text 字形缓存（draw_text 无缓存，逐 glyph 计算）
- P2/P6 任务单由豆包发布给 GPT，GPT 出方案，豆包审核落地验证
