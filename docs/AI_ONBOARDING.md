# zcsr 新对话必读文件包

## 一、必读（按优先级）

1. `docs/BOOTSTRAP_v4.0.md` — 启动指令，项目定位、进度、约束、边界
2. `docs/PHASE_A_COMPLETE.md` — A 组封存报告，commit 链、未测项、已知限制
3. `docs/PHASE1.md`（若存在）— Android Phase 1（1A–1E）交接记录
4. `API_REFERENCE.md` — 契约摘要（注意：可能是 Phase 0 时生成的，以 `include/zcsr/*.h` 原文为准）

## 二、按需读

- `include/zcsr/*.h` — 24 个契约头，唯一 ABI 真相。需要 API 签名时查。
- `modules/*/` — 15 个实现模块。需要看实现时查。
- `android/` — Android CMake 工程 + NativeActivity 入口（Phase 1 独立工程）
- `tests/*.c` — 现有 19 个 ctest
- `integration/*.c` — 基准测试和真机验证程序
- `tools/check_no_cross_includes.sh` — 耦合守卫
- `tools/android_pack.sh` — APK 打包

## 三、不要读

- 历史 commit 全文（信 commit message 即可）
- 旧快照 / 旧交接文档（v3.1 不是文件，是历史上的对话上下文，不存在于仓库）
- `build/`、`build-release/`、`build/android/out/`（构建产物）
- `vc140.pdb` 等编译临时文件

## 四、git 状态速查

```
git log --oneline -12     # 看最近 12 commit
git tag -l                # 看 tag（android-phase1, platform-edge）
git status --short        # 工作树是否干净
```

## 五、源码定位

契约头：`include/zcsr/*.h`
实现：`modules/{signal,core,hsm,gamelogic,platform,input,inputext,overlay,render,glrender,image,audio,audiomix,culling,atlas}/`
Android：`android/CMakeLists.txt`, `android/android_main.c`, `android/AndroidManifest.xml`
测试：`tests/*.c`（19 个）
构建：根 `CMakeLists.txt` + `android/CMakeLists.txt`（独立）
