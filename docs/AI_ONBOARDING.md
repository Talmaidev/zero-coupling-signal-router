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
