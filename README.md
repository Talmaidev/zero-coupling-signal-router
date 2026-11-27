# ZCSR

ZCSR is a pure C11 **2.5D engine library**: static libraries plus contract headers. It is not a runnable game, and it does not own a window.

`zcsr` engine and `crowd` are two different layers, not two names for the same thing. The engine owns records-in / draw-out: it validates the pointer, uploads the record array, binds state and submits. It does not own game objects, crowd simulation, grouping policy, culling decisions or a window. `crowd` is the caller layer: it owns the entities, decides which records are visible, how many records go into one call and how the caller chunks a large array. The engine never reads a crowd structure and never guesses a batch size; it only receives the record array and the count the caller passes.

The runnable entry is `zcsr_runtime`: the runtime host owns the single OS window, surface, input pump and main loop. `zcsr_editor` is an editor workspace module hosted by that runtime host; it never opens a second window.

Words used here have exactly one meaning:



* **engine library**: the C11 static libraries and contract headers in this repo. Owns no window.

* **runtime host**: the one owner of window, surface and main loop.

* **editor workspace**: authoring UI hosted by the runtime host.

* **2.5D scene**: orthographic camera over 2D projected quads. Not a 3D model scene.

* **quad**: one four-corner screen polygon with UV and tint.

* **layer**: one explicit draw group. Not painter depth.

## 架构



```
runtime host                       one window, one main loop
  ├── editor workspace             authoring UI, hosted
  ├── game module                  2.5D scene runtime
  └── engine (include/zcsr + static libs)
        core / signal / hsm / platform
        glrender / sprite / culling / atlas / font / image / color_pipeline
        light2d / camera / camera3d / scene_camera / scene / scene_render
        audio / audiomix / music / input / inputext / assets / resource / file_service
        collision / save / ui / viewport
```

三条轴互相独立，混在一起讲就会互相打架：

**1. capability axis（entry 组合）** —— 引擎不认识 low /normal/high /max 这些名字；每一项 capability 是一段可独立编入的 implementation。某项 off 时整段 code 不编入、symbol 不存在：不是 "效果差一点"，是那项 capability 不存在。产品把自己的 entry/func 组合起名叫哪一档，是 game 自己的事；引擎只认自己编出的 entry 与编译宏。

**2. contract axis** —— 全平台同一套 `include`、同一组 func 名与 func id、同样的参数语义。它管 "怎么调用"，不管 "里面是哪份算法"。切编译轴不换 func 名，只换哪个 func 存在。

**3. color space axis** —— 统一 linear working space 是一整块积木：`RGBA16F` linear frame + 一次 encode。它要么在、要么不在；在的时候所有走它的绘制统一，不在的时候走 direct sRGB。它只决定 present 时是否多一次 encode，不决定 layout、sort 或任何 texture 算法，也不要求 PVZ 那种手绘素材去用它 —— 那种场景纯 `image` 上屏，任何 texture 优化都不加。

三条轴可以自由组合：PVZ 场景 = 只摆图的 entry 组合 + 同一套 contract + color space 不启用。

## 公开 entry list（call = switch）

func 本身就是 API，不包 wrapper、不改名、不传 bool。是否走某条算法路径，只取决于 caller 调用哪一个 func：**没有 bool 开关、没有阈值门、没有 profile 枚举**。单次调用能提交多少实例是 renderer 的 init 期容量参数（`ZCSR_GL_MAX_INSTANCES`），不是 func 上的开关，也不藏进某个算法；算法是否存在只取决于是否调用那个 func。



| entry                                                                                              | record | 顶点数 / 属性                        | 剔除                                                | 排序                                        | 编译平台（layout tier）          | 什么时候由 game 调用                                                                                               |
| -------------------------------------------------------------------------------------------------- | ------ | ------------------------------- | ------------------------------------------------- | ----------------------------------------- | -------------------------- | ----------------------------------------------------------------------------------------------------------- |
| `zcsr_gl_draw_instances(renderer, texture, const zcsr_gl_instance*, count)`                        | 168 B  | 6 顶点（`GL_TRIANGLES`）/ 13 属性     | 是：cull/select、逐实例 bitmap 位测试、gather 重排            | 按 `ZCSR_SORT_TIER`：key 档编入排序阶段，none 档按提交序 | FULL、POS\_UV               | 需要剔除 + gather 重排的完整实例路径                                                                                     |
| `zcsr_gl_draw_instances_visible(renderer, texture, const zcsr_gl_instance*, count)`                | 168 B  | 6 顶点 / 13 属性                    | 否：caller guarantees all visible，no cull、no gather | 否：按提交序 draw                               | FULL、POS\_UV               | 调用方已保证全部可见，跳过 select/bitmap/gather                                                                          |
| `zcsr_gl_draw_instances_plain(renderer, texture, const zcsr_gl_instance_plain*, count)`            | 40 B   | 4 顶点（`GL_TRIANGLE_STRIP`）/ 5 属性 | 否                                                 | 否：按提交序                                    | FULL、POS\_UV（COMPACT 档不存在） | 纯图路径：单贴图、无 tint・mode・amount，alpha 在记录里。一次调用传整段；不要拆成 65 次 1023 条 |
| `zcsr_gl_draw_instances_compact(renderer, texture, const zcsr_instance_compact*, count)`           | 36 B   | 6 顶点 / 打包整型属性                   | 是                                                 | 是                                         | COMPACT                    | 移动端精简 record 路径                                                                                             |

两个 168 B entry 是并列关系：`draw_instances` 带剔除与重排；`draw_instances_visible` 假定调用方已经保证全部可见，跳过 select/bitmap/gather。调哪一个是 game 的选择，引擎不做运行时路由，也没有第三份开关在两者之间切换。

plain 只有一个入口。ZCSR 没有 `DrawMeshInstanced` 的 1,023 上限，65,535 条记录一次调用就是一次 draw；不要把它拆成 65 次 1023 条。实测拆成 65 draw 与 1 draw 都是 0.835 ms 左右，拆批没有收益，是负 ROI；这条路径已不作为性能项，也不保留单独的入口。

plain 记录 = 10 float（center /size/uv rect /rotation/alpha，40 B）；没有 tint、mode、amount、recolor、ramp、glow、chroma、emiss、rim 字段。compact 记录只表达位置、尺寸、UV、旋转（16 位）、alpha（8 位）；tint /mode/amount /recolor/ramp /glow/chroma /emiss/rim 在这份 struct 里同样**没有字段**，所以不存在 "检测到不支持再拒绝" 的路径 —— 表达不了的东西不会被传进来。

FULL / POS\_UV 档编出 `draw_instances`、`draw_instances_visible`、`draw_instances_plain`；COMPACT 档只编出 `draw_instances_compact`。prototype 都在同一份 `include/zcsr/gl_render.h`；某个 layout 档下只编出它自己的 entry。调错不是静默 fallback，是 link error —— capability off 的显式表达，编译期就知道。

这几份是引擎自带的 reference implementation，不是封闭枚举。产品可以自己加 algo：在自己的 module 里定义自己的 record 与 func（命名按 game 自己的 low /normal/high /max），引擎不认识也不需要认识它。引擎只决定自己哪份 entry 存在；外部 entry 不被任何引擎枚举，唯一约束是不去调用当前 layout 档下不存在的引擎 entry。contract 锁的是共用名字的签名不变，不是 "只许存在这几份算法"。自定义 entry 复用引擎内部 VBO/shader 不属于 contract —— 它要么走公开 entry，要么自己持有 GL 对象。

## 容量与分批

单次调用能提交多少实例是 renderer 的 init / 构建期容量参数，不是一项算法，也不藏进某个 func：



* 默认容量 `ZCSR_GL_MAX_INSTANCES = 65,535`；可用构建宏覆盖（`-DZCSR_GL_MAX_INSTANCES=...`），不提供 runtime 参数、bool 开关或环境变量。

* 容量是参数，不是算法选择：count ≤ 容量时一次 `glDrawArraysInstanced` 画完（`draw_calls=1`）；只有 count > 容量才进入内部循环分片，该循环仅是防溢出保护，不参与算法选择。分片次数由调用方循环决定，引擎不替 game 决定一批画多少。

* 内存占用（按容量 × stride 预留的 ring）与调用次数都由 game 按自己的场景选；引擎不把分批当成 cull、sort 或任何着色能力。

### 调用契约与已踩过的坑



* **不要拆 plain。** `draw_instances_plain` 的整段调用只上传一次、只 draw 一次；把 65,535 条拆成 65 次 1023 条是 Unity `DrawMeshInstanced` 上限的做法，不是 ZCSR 的性能路径。拆批实测与 1 draw 相等，没有收益。

* **不要在外层重复 make-current。** 平台层 `zcsr_gl_make_current` 已经先查 `wglGetCurrentContext` / `glXGetCurrentContext` / `eglGetCurrentContext` / `[NSOpenGLContext currentContext]`，同 context 直接返回。每 draw 调一次 `wglMakeCurrent` 是驱动 round-trip，曾把 65 批的 submit 从 0.33 ms 推到 2.38 ms。

* `instanced_base_instance` 只是能力报告，不是 “更快” 的证明。同一台 Windows / Arc B580 / GL 3.3 上，`glDrawArraysInstancedBaseInstance` 比 5 次 `glVertexAttribPointer` rebase 更慢（1 draw submit 0.345 vs 0.101 ms）。引擎的 draw 路径固定走 pointer rebase；能力位保留给调用方查询，不用来选择路径。

* **linear 是独立的 present 轴。** 纯像素 / 纯单图项目可以用 direct sRGB，不必为了跑起来开 `RGBA16F` + encode；linear 只影响 present 是否多一次 LUT encode，不影响 record、draw 入口或分批。

### 显存账（ring VBO：`ZCSR_GL_INSTANCE_SLOTS(8) × 容量 × stride`）

8 个 slot 是多槽环形缓冲：每上传 / 画完一轮推进一个 slot，下一个 slot 要等一整帧被 GPU 消费完才复用，避免覆盖仍在被读的缓冲。



| record 档       | stride | 65,535 容量（字节 / MiB / MB）            | 10,000 容量（字节 / MiB / MB）            |
| -------------- | ------ | ----------------------------------- | ----------------------------------- |
| general（168 B） | 168 B  | 88,079,040 B / 84.00 MiB / 88.08 MB | 13,440,000 B / 12.82 MiB / 13.44 MB |
| plain（40 B）    | 40 B   | 20,971,200 B / 20.00 MiB / 20.97 MB | 3,200,000 B / 3.05 MiB / 3.20 MB    |
| compact（36 B）  | 36 B   | 18,874,080 B / 18.00 MiB / 18.87 MB | 2,880,000 B / 2.75 MiB / 2.88 MB    |

（MiB = 1024² 字节；MB = 10⁶ 字节。）除 ring VBO 外，全档 renderer 结构体内嵌一份 CPU 暂存数组（不 ×8）；实测 `sizeof(zcsr_gl_renderer)`：full 档 13,930,824 B（13.29 MiB）、compact 档 2,920,944 B（2.79 MiB），内置测试 / 工具 / Android 宿主预留 16 MiB 静态 /arena 池。

## 三条编译轴独立传参

三条轴互不捆绑，各自落到编译期：



```
# 默认：full layout + linear HDR + key sort
cmake -S . -B ..\zcsr-build\win-msvc -G "Visual Studio 18 2026" -A x64

# 三轴分别选择（示例：compact layout + direct sRGB + 不排序）
cmake -S . -B ..\zcsr-build\compact-direct-none -G "Visual Studio 18 2026" -A x64 `
  -DZCSR_INSTANCE_LAYOUT=compact -DZCSR_COLOR_TIER=direct -DZCSR_SORT_TIER=none
```

每条轴只决定自己那一段是否编入：layout 决定哪个 record/entry 存在；color tier 只决定 present 时是否多一次 encode；sort tier 决定排序阶段是否编入。一条轴不顺带决定另外两条，也不改变 contract 里的名字。

三轴两两正交、可任意组合，没有 profile 捆绑：`ZCSR_INSTANCE_LAYOUT`（full | compact）、`ZCSR_COLOR_TIER`（linear | direct）、`ZCSR_SORT_TIER`（key | none）互不要求对方取特定值。layout 不要求某种 color 或 sort；sort 不要求特定 layout；color 不回过来决定 record 字段或排序。linear 只影响 present 是否多一次 LUT encode，不影响 layout、sort 或任何 texture 算法。

## 分批接入

按需要一步一步加，每一步都能独立验收；不加的那一步不该出现在 binary 里。



| 步骤 | 加什么                                                   | 什么会消失                                            | 什么时候不该加                     |
| -- | ----------------------------------------------------- | ------------------------------------------------ | --------------------------- |
| 1  | 只画图：`image` + 一次 quad draw                            | texture 优化、采样器状态、encode pass 都不存在                | 只是把美术画好的图摆上屏时，就停在这一步        |
| 2  | 批处理：instanced + culling（compact layout 用 36 B record） | 每批 culling 的 scan 与 upload 合并成一次                 | 同屏几十个 sprite 时，单次 draw 更快   |
| 3  | 统一颜色：linear space（`RGBA16F` + 一次 encode）              | 直接写默认 FB 的路径消失；设备必须支持 `RGBA16F` color attachment | 素材是已烘好的 sRGB 图、不需要线性混合时，不要开 |
| 4  | 光照：light2d + 阴影                                       | 单 pass 的简化路径消失                                   | 2D 手绘素材不需要光照时，不要开           |

## 三轴对照



| 编译轴    | 取值            | 编入什么                                                                              | 不编入什么                                                        |
| ------ | ------------- | --------------------------------------------------------------------------------- | ------------------------------------------------------------ |
| layout | full（168 B）   | `draw_instances` / `draw_instances_visible` / `draw_instances_plain`，完整 record 字段 | —                                                            |
| layout | compact（36 B） | `draw_instances_compact`                                                          | 168 B record 上的 tint/mode/recolor/ramp/glow/chroma/emiss/rim |
| color  | linear HDR    | linear `RGBA16F` frame + present 时一次 LUT encode                                   | —                                                            |
| color  | direct sRGB   | 直接写默认 FB，无 encode                                                                 | OKLab /recolor/ramp /glow/chroma /emiss/rim 片段               |
| sort   | key           | 4 B key 稳定 radix/counting sort                                                    | —                                                            |
| sort   | none          | 按提交序，排序阶段整段不编入                                                                    | key sort 阶段                                                  |

layout 不要求某种 color 或 sort 组合；color 只动 present 的 encode，不回过来决定 record 字段或排序。逐实例改色是否可用，只由 layout record 上有没有对应字段决定。

## 核心要求



* **C11**：只用 C11；不引入 C++、异常、RTTI。

* **运行期零堆分配**：引擎运行期不调用 `malloc/free`；状态放固定池或调用方缓冲区。系统库与驱动内部的内存不计。

* **一个进程一个窗口**：window、surface、input pump 只属于 runtime host。

* **模块只看得见 contract header**：模块只能 `#include <zcsr/...>`，不得 include 另一个模块的 source 或相对路径。`tools/check_no_cross_includes.sh` 扫这条。

* **build tree 必须在 source tree 外**：根 `CMakeLists.txt` 的 guard 在 configure 期拒绝仓内目录。

* **只有一个 render backend**：GPU（OpenGL 3.3 Core / GLES 3.x）。`CPU` 位保留但 `zcsr_render_backend_available(CPU)` 恒 false；GPU 不可用直接失败，不退回 software。

* **linear working space 是独立可选积木**：开了 linear 时，`RGBA16F` color attachment 不支持则 renderer 创建失败，不静默降级。

* **只画图的组合必须能跑**：不需要 linear space、不需要改色的项目，直接选 compact layout + direct sRGB + sort none，不为了跑起来去满足 linear 档的附加条件。

## 模块与用法

29 个 module 各自独立编译，module 之间不互相 include；组合发生在产品层。



| 组      | module                                                                |
| ------ | --------------------------------------------------------------------- |
| 基础     | `core` `signal` `hsm` `platform`                                      |
| 渲染     | `glrender` `sprite` `culling` `atlas` `font` `image` `color_pipeline` |
| 光照与相机  | `light2d` `camera` `camera3d` `scene_camera`                          |
| 场景     | `scene` `scene_render`                                                |
| 媒体     | `audio` `audiomix` `music`                                            |
| 输入与资源  | `input` `inputext` `assets` `resource` `file_service`                 |
| 游戏与 UI | `collision` `save` `ui` `viewport`                                    |

把本仓库 `add_subdirectory` 进来（或 install 后 `find_package`），link 需要的 `zcsr_<module>` target，include 对应的 `<zcsr/xxx.h>`。每个 target 都 PUBLIC link `zcsr_contracts` 与 `zcsr_core`，不需要手工补依赖顺序。

最小绘制链（168 B record）：



```
size_t count = zcsr_scene_build_instances(&scene, instances, capacity);
zcsr_gl_draw_instances(renderer, texture, instances, count);
/* 调用方已保证全部可见时，并列入口是 zcsr_gl_draw_instances_visible：
   同样的 record，跳过 cull/select/gather，连续上传并按提交序 draw。 */
zcsr_gl_present(renderer);   /* linear 档在这里做唯一一次 encode；direct 档没有这一步 */
```

compact layout 直接用 36 B record，不经过 `zcsr_gl_instance`，也没有转换：



```
zcsr_instance_compact* recs = /* caller-owned, 36 B each */;
zcsr_gl_draw_instances_compact(renderer, texture, recs, n);
```

纯图、65,535 条：一次调用，一次 draw。

```
/* one upload of 65,535 plain records, one draw */
zcsr_gl_draw_instances_plain(renderer, texture, plain_records, 65535);
```

`zcsr_scene_camera` 与 `camera3d` 只做投影数学，不拥有 GL；`glrender` 不读 scene document，只接 record array。

## 脚本与系统差异

`tools/` 下脚本按系统分工，互不替代；所有脚本都拒绝把输出写进 source tree。



| 脚本                           | 运行系统                 | 前置                                                   | 作用                                                         | 不做什么               |
| ---------------------------- | -------------------- | ---------------------------------------------------- | ---------------------------------------------------------- | ------------------ |
| `check_no_cross_includes.sh` | 任意 POSIX shell       | 无                                                    | 扫 module 是否越界 include                                      | 不编译、不改文件           |
| `check_media_deps.sh`        | Linux / macOS        | bash                                                 | 只读探测 FFmpeg /libwebp 的头、库、版本、license 类别                    | 不安装、不 sudo         |
| `check_media_deps.ps1`       | Windows              | PowerShell 7                                         | 同上                                                         | 同上                 |
| `android_pack.sh`            | Linux / macOS        | SDK build-tools、JDK `keytool`、`zip`/`unzip`、keystore | aapt2 link、放 `.so`、zipalign、apksigner                      | 不编 native lib，不装设备 |
| `test_all_android.sh`        | Linux / macOS        | 已授权真机、上一步的 APK                                       | 安装、启动、touch/swipe、HOME 恢复、截图与 logcat，断言 13 项 native marker | 不做长时间 soak         |
| `verify_macos_linear.sh`     | 真实 Mac + GUI session | Xcode、可用 NSOpenGL                                    | 构建并跑 linear frame gate，读真实 backbuffer                      | 不能跑无头 CI           |
| `test_all_macos.sh`          | 真实 Mac + GUI session | 五个 repo 同目录、`ZCSR_REPO_ROOT`                         | 逐仓仓外构建 + ctest + editor smoke                              | 不写 source tree     |
| `collect_macos_logs.sh`      | 真实 Mac               | 上一步的 build 产物                                        | 收集日志与截图到仓外                                                 | 不重新构建              |
| `verify_light2d_shot.ps1`    | Windows + 真实窗口       | 已构建的 demo                                            | 状态触发截图，读 flame pixel 做亮度探针                                 | 不做无窗口断言            |
| `gpu_probe.c`                | 任意有 GL 的系统           | 手编一个 C 文件                                            | 打印 `GL_RENDERER`/`GL_VERSION`/extension                    | 不是 test，不进 CTest   |

Android 与 macOS 的脚本缺设备或缺 GUI 时直接失败，不假装跳过。

## 构建

需要 CMake 3.16+ 与 C11 编译器；Windows 用 MSVC。



```
cmake -S . -B ..\zcsr-build\win-msvc -G "Visual Studio 18 2026" -A x64
cmake --build ..\zcsr-build\win-msvc --config Release
ctest --test-dir ..\zcsr-build\win-msvc -C Release --output-on-failure
```

Linux 需要的不只是 C11 编译器：X11/GLX backend 要 `libx11-dev` + `libgl1-mesa-dev`（`libxrandr-dev` 可选）；`inputext_x11` case 还要 `libxtst-dev`，缺了 CMake 自动跳过该 case；Wayland/EGL 要 `wayland-client`、`wayland-scanner`、`wayland-protocols`、`libxkbcommon-dev`、`libegl1-mesa-dev`；audio 可选 `libpipewire-0.3-dev` + `libspa-0.2-dev` 或 `libasound2-dev`，全缺走 silent backend。



```
cmake -S . -B ../zcsr-build/linux-gcc -DCMAKE_BUILD_TYPE=Release
cmake --build ../zcsr-build/linux-gcc -j
ctest --test-dir ../zcsr-build/linux-gcc --output-on-failure
```

macOS 15+ / Xcode 16+ 用同一套 source，但必须在有 GUI session 的真实 Mac 上构建与运行：



```
cmake -S . -B ../zcsr-build/macos -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0
cmake --build ../zcsr-build/macos -j
./tools/verify_macos_linear.sh
```

## Android

NativeActivity + NDK，不用 Gradle：



```
cmake -S android -B ../zcsr-build/android-arm64 -G "Unix Makefiles" \
  -DCMAKE_TOOLCHAIN_FILE=$NDK/build/cmake/android.toolchain.cmake \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-24 \
  -DCMAKE_BUILD_TYPE=Release
cmake --build ../zcsr-build/android-arm64 --parallel

ANDROID_SDK_ROOT=$SDK ZCSR_ANDROID_SO=../zcsr-build/android-arm64/libzcsr_phase0.so \
ZCSR_ANDROID_APK=../zcsr-build/android-arm64/zcsr_phase0_1.0.0_arm64.apk \
ZCSR_DEBUG_KEYSTORE=$HOME/android/zcsr-debug.keystore ZCSR_DEBUG_STOREPASS=android \
bash tools/android_pack.sh

ANDROID_SDK_ROOT=$SDK ZCSR_ANDROID_APK=../zcsr-build/android-arm64/zcsr_phase0_1.0.0_arm64.apk \
ZCSR_ANDROID_LOG_DIR=../zcsr-build/android-arm64/device-logs \
bash tools/test_all_android.sh
```

生命周期、input、AAudio、13 项 marker 见 `android/README.md`。

### 设备门禁与 bench 复现命令（K60 Pro / Adreno 740 / GLES3）

> **Windows 本机 adb 为空，全部设备动作走 Kali**
>
> SSH：`ssh -i C:\Users\chen\.ssh\id_ed25519_kali chen@192.168.1.12`
>
> Kali 上唯一允许的 adb 是 `~/android/platform-tools/adb`，serial `3d16ff0e`（Redmi K60 Pro，22127RK46C / socrates，SDK 36，arm64-v8a，1440×3200，density 560）。

固定口径：65,535 实例、33×42、同 `zombie_die.png`（1650×85，25 帧）、nearest、alpha 0.75、GLES3、一次 draw、不剔除不排序、direct、present 一次。计时前先过像素门（`screencap` 证明 65,535 个 quad 画上屏；纯清屏帧作废，数据不进表）。零售机不可锁频：≥10 s warmup，≥5 轮取中位，记录机身温度与轮数，不取单轮。

ZCSR（plain 40 B 入口，debuggable，`run-as` 取 CSV）：



```
ADB=~/android/platform-tools/adb
# 安装
$ADB -s 3d16ff0e install -r -t <zcsr plain-v2 apk>
# 每轮先清旧进程与旧 CSV
$ADB -s 3d16ff0e shell am force-stop com.zcsr.phase0
$ADB -s 3d16ff0e shell run-as com.zcsr.phase0 rm -f files/zcsr_bench.csv
# nofinish 轮（缺省）
$ADB -s 3d16ff0e shell am start -W -n com.zcsr.phase0/android.app.NativeActivity \
  --es zcsr_bench 1 --es zcsr_rounds 100 --es zcsr_warmup 30
# finish 轮（追加 --es zcsr_finish 1；GPU 结论列只认 finish 等价口径）
$ADB -s 3d16ff0e shell am start -W -n com.zcsr.phase0/android.app.NativeActivity \
  --es zcsr_bench 1 --es zcsr_rounds 100 --es zcsr_warmup 30 --es zcsr_finish 1
# 取回 CSV
$ADB -s 3d16ff0e exec-out run-as com.zcsr.phase0 cat files/zcsr_bench.csv > zcsr_bench.csv
# 像素门
$ADB -s 3d16ff0e exec-out screencap -p > shot.png
```

Unity（instanced；CSV 写 `Application.persistentDataPath`，`adb pull` 取回，无 draw-only 分项）：



```
# 安装
$ADB -s 3d16ff0e install -r -t <unity v3 apk>
# 每轮先 force-stop 清旧产物
$ADB -s 3d16ff0e shell am force-stop com.zcsrbench.zcsr_unity_bench
# 启动；命令行经 intent extra `unity` 传入
$ADB -s 3d16ff0e shell am start -W \
  -n com.zcsrbench.zcsr_unity_bench/com.unity3d.player.UnityPlayerActivity \
  -e unity "-path=instanced -run=r<N> -count=65535 -frames=120 -warmup=60 -alpha=0.75"
# 取回 CSV / 日志 / 截图
$ADB -s 3d16ff0e pull /sdcard/Android/data/com.zcsrbench.zcsr_unity_bench/files/<csv> .
$ADB -s 3d16ff0e exec-out screencap -p > shot.png
```

收尾：两端 `am force-stop` 并确认 `pidof` 无输出，Kali 上 `$ADB kill-server`；不在设备上用 root、不绕过调试标识读私有目录。

## 可选媒体依赖（FFmpeg /libwebp）

两者体量大、license 独立，不随仓库分发（`third_party/prebuilt/` 被 `.gitignore` 排除）。缺失时构建照常成功，对应 backend fail-closed：WebP 与 FFmpeg 支撑的格式报 `caps=false`、解码返回 `UNSUPPORTED`。

License 基线：本项目 AGPL-3.0，FFmpeg 必须是 **LGPL 变体且 dynamic link**。GPL 变体（`Gyan.FFmpeg`、`BtbN.FFmpeg.GPL.Shared`、Homebrew 的 `ffmpeg`、MacPorts 默认 `+gpl2`）一律不可用。

探测要求 header 与 lib 同时存在：FFmpeg 五个 lib（avcodec/avformat/avutil/swscale/swresample）、libwebp 三个 lib（webp/webpdemux/sharpyuv）。三个变量可覆盖探测根：`ZCSR_DEPS_ROOT`、`ZCSR_FFMPEG_ROOT`、`ZCSR_LIBWEBP_ROOT`。configure 打印 `FFmpeg backend enabled: <root> (license=LGPL)` 或 `DISABLED … missing: …`。



| 系统            | 安装                                                                                                                                           |
| ------------- | -------------------------------------------------------------------------------------------------------------------------------------------- |
| Windows       | `C:\vcpkg\vcpkg.exe install ffmpeg libwebp --triplet x64-windows`，两个 root 都指 `C:/vcpkg/installed/x64-windows`                                |
| Debian / Kali | `apt-get install -y libavcodec-dev libavformat-dev libavutil-dev libswscale-dev libswresample-dev libwebp-dev libsharpyuv-dev`，root 用 `/usr` |
| Fedora / RHEL | 先装 RPM Fusion，再 `dnf install -y ffmpeg-devel libwebp-devel`，root 用 `/usr`                                                                    |
| Arch          | `pacman -S --needed ffmpeg libwebp`，root 用 `/usr`                                                                                            |
| macOS         | `port install ffmpeg -gpl2 webp`，root 用 `/opt/local`                                                                                         |
| Android       | 无系统包，自行 cross-compile 或放 per-ABI prebuilt；不提供则 fail-closed                                                                                   |

版本矩阵与链接方式见 `third_party/DEPENDENCIES.md`；格式能力与降级见 `docs/MEDIA_SUPPORT.md`。

## 测量口径

性能只认同机、同条件、同 build 的 A/B；跨机、跨引擎、跨分辨率、跨批次不可比。end-to-end frame time 与 submit-only、`glFinish`、GPU timer 是不同测量面，不混表。`zcsr_bench` 是测量工具，不进 CTest。跨引擎横比还必须先对齐 draw 形状（1 draw vs 65 draw）与上传口径（见下「ZCSR 1 draw vs Unity 65 draw」）；形状、API、上传口径三件事不能混在一个并排里。

## 最终速度表（Win / Linux / Android）

固定负载：65,535 实例、33×42、同贴图、nearest、alpha 0.75、direct、一次 draw、不剔除不排序。CPU 列分 data\_gen /submit/ 整帧（frame）；GPU 列只认 finish 等价口径（GL timer query 在 swap 前结束，Unity `gpuFrameTime` 同口径，present 不计入）。每格为多轮中位，轮数见列；只放成立的数字，取不到的记 `n/a`，不估算。



| 平台 / GPU             | engine | API         | path                                 | CPU data\_gen (ms) | CPU submit (ms) | CPU 整帧 (ms) | GPU 整帧 (ms) | GPU draw-only (ms) | 轮数  | 注脚                                                                                        |
| -------------------- | ------ | ----------- | ------------------------------------ | ------------------ | --------------- | ----------- | ----------- | ------------------ | --- | ----------------------------------------------------------------------------------------- |
| Windows / Arc B580   | ZCSR   | GL 3.3      | visible(168B)                        | 0.8833             | 0.5235          | 1.9373      | 2.5147      | n/a                | 5+5 | GPU 2.5147 为旧 general program 耦合路径对照值（6 顶点 / 13 属性 / 168B，finish 150 samples），保留不删        |
| Windows / Arc B580   | Unity  | D3D11       | instanced（DrawMeshInstanced，65 draw） | 6.2928             | 1.0829          | 7.4393      | 1.0219      | n/a                | 5   | FrameTimingManager gpuFrameTime（不含 present）；gpu\_valid 595/600                            |
| Linux / GTX 1650     | ZCSR   | GL 3.3      | visible(168B)                        | 2.3572             | 2.2153          | 7.2819      | 5.938       | n/a                | 5+5 | PRIME offload；present via child-window GLX；finish 130 samples                             |
| Linux / GTX 1650     | Unity  | GL Core 4.5 | instanced（65 draw）                   | 9.1364             | 2.2845          | 11.4239     | n/a         | n/a                | 3   | FrameTimingManager gpu 0/300 有效；-force-glcore                                             |
| Android / Adreno 740 | ZCSR   | GLES3       | plain(40B)                           | 1.039              | 13.209          | 16.023      | 15.960      | 15.962             | 5   | 5 轮 ×100 样本；`EXT_disjoint_timer_query` 可用；信箱区渲染（覆盖 0.3435），与 Unity 全屏非同画布；温度 32.9–33.3 °C |
| Android / Adreno 740 | Unity  | GLES3       | instanced（65 draw）                   | 7.1203             | 3.6875          | 33.1338     | 28.1046     | n/a                | 5   | 120 帧 / 轮，600/600 有效；全原生 1440×3200；无 draw-only 分项；finish/nofinish 列 n/a；温度 32.9–33.3 °C   |

表下口径与偏差：



* **GPU 只认 finish 等价列**：nofinish 下 query 滞后于提交、采样不足，只作参考不进表。整帧 = clear + draw；present 不计入（GL query 须在 swap 前结束，Unity gpuFrameTime 同口径）。

* **Android 两行非同画布**：ZCSR 在 1920×1080 信箱区内渲染（非清屏覆盖 0.343507；`SetResolution(1920,1080)` 在 Android 全屏未生效），Unity 全原生 1440×3200（覆盖 0.999921）。Android 的 GPU 与整帧不能与对方直接横比，只作平台内对照参考。

* Android 零售机不可锁频：≥10 s warmup，多轮取中位，记录机身温度与轮数。

* 仍记 n/a：Linux Unity GPU（0/300 有效）；Unity finish/nofinish 两组（台架无 glFinish 开关）；Android Unity draw-only；所有平台 GPU raster/fragment/present 分段（引擎只有一个总 elapsed query，不拆造；Unity FrameTimingManager 同样无此分段）。禁止把 n/a 写成 0 或估算值。

## GPU 公平性：plain 40 B vs general 168 B（Windows / B580）

同口径：65,535 实例、1920×1080、nearest、alpha 0.75、direct、单贴图、一次 draw、不剔除不排序；5 轮 ×150 个 coherent query 样本，finish 口径。



| path                                  | 顶点 / 属性 /record      | 整帧 clear+draw (ms) | draw-only (ms) |
| ------------------------------------- | -------------------- | ------------------ | -------------- |
| plain（`zcsr_gl_draw_instances_plain`） | 4 顶点 / 5 属性 / 40 B   | 1.468              | 1.416          |
| general（168B 耦合路径）                    | 6 顶点 / 13 属性 / 168 B | 3.107              | 2.642          |

像素门：general（tint=1，alpha 0.75，rot=0）与 plain 同帧 65,535 实例离屏 dump SHA-256 完全相同 `C12A1564F7742DBFB5A6E276793EEB7732BB4C69D74E70CF349A7821963FF17F`，0 字节差异，两路 `draw_calls=1`。同像素、同 overdraw 前提下，general 比 plain 多出的 GPU 时间（整帧 +1.639 ms、draw-only +1.226 ms）只来自顶点装配（6→4）与实例记录 / 属性带宽（13→5 属性、168 B→40 B，含每帧入口内 VBO 上传 11.01 MB→2.62 MB），不是填充率差异。

上表两组数字是每批 make-current 修正前的同一 build 对照；修正后 plain 1 draw 整帧中位 0.833 ms，general 未重测，不能把 1.468 当现值。

## ZCSR 1 draw vs Unity 65 draw（Windows / B580，65,535 实例，1920×1080，同贴图 nearest）

ZCSR 用 `zcsr_gl_draw_instances_plain`：65,535 条记录一次上传、一次 draw。B580 不能锁频，第一轮冷时钟（1.20 ms）不计入；暖机后 5 轮 ×330 样本。

| 路径 | draw 形状 | GPU 整帧中位 (ms) | 暖机后范围 (ms) | 样本 / draw_calls |
|---|---|---|---|---|
| ZCSR plain，capacity 65,535 | 1 draw | 0.833 | 0.821–0.842 | 5 轮×330；draw_calls=1；upload once |
| Unity instanced，D3D11 | 65 draw | 1.0219 | n/a | gpu_valid 595/600；DrawMeshInstanced |

- ZCSR 1 draw 对 Unity 被迫的 65 draw：GPU 快 **18%**（0.833 vs 1.0219），CPU submit 快 **10 倍**（0.104 vs 1.0829 ms）。
- **ZCSR 不保留 65 draw 性能项。** 65 次与 1 次实测都是 0.835 ms 左右，相等；相等没有收益，拆批是负 ROI。`plain_run` 和 65 次 test row 已删除，只保留 ZCSR 1 draw 对 Unity 65 draw。
- Unity 的 65 draw 是 `DrawMeshInstanced` 单次 1,023 实例上限逼出来的（65,535 / 1,023 向上取整 = 65）；它每轮还要 `Array.Copy` 1,023 个 64 B 矩阵、填 1,023 个 16 B UV 到 MPB。Unity 的 65 是 API 限制的 workaround，不是性能算法。
- **上传口径不对称**：ZCSR 的 GPU 整帧包住入口内 `glBufferSubData` 2.62 MB（40 B × 65,535）；Unity `gpuFrameTime` 不含其每帧 4.19 MB（64 B × 65,535）矩阵上传。ZCSR 在更宽的计时面里仍领先 18%。

## 36 B 隔离组结论（PC）

隔离组口径：linear HDR + sort none + 不剔除，同机同 build A/B，168 B general 与 36 B compact 对照。



| 平台               | 168 B submit / frame (ms) | 36 B submit / frame (ms) | Δ submit / Δ frame (ms) |
| ---------------- | ------------------------- | ------------------------ | ----------------------- |
| Windows / B580   | 1.0418 / 2.1023           | 0.5072 / 1.5286          | −0.5346 / −0.5737       |
| Linux / GTX 1650 | 3.1922 / 7.3014           | 0.9434 / 6.2405          | −2.2488 / −1.0609       |

36 B 在 PC 隔离组净赚（submit 与整帧都下降），因此**不把 36 B 路径锁在移动端**；compact 档作为可选 layout tier 编入，是否使用由 game 按自身场景选择。

## 文档

README 负责总览、架构、tier 用法与脚本差异；`docs/` 放 architecture、contract、API 与平台细节。



| 文件                                    | 内容                                     |
| ------------------------------------- | -------------------------------------- |
| `docs/ARCHITECTURE.md`                | 零堆、零耦合、平台 /audio/input backend 与线程模型   |
| `docs/API_REFERENCE.md`               | contract header 索引与 API 规则             |
| `docs/RENDER2D_CONTRACT.md`           | 2D 坐标、color space 与 G-buffer contract  |
| `docs/COLOR_LIGHT_SHADOW_CONTRACT.md` | color /light/shadow 的跨 module contract |
| `docs/LIGHT2D_MATH_MODEL.md`          | Light2D 数学约定                           |
| `docs/LIGHT2D_INTEGRATION.md`         | Light2D 接入                             |
| `docs/MEDIA_SUPPORT.md`               | 媒体格式能力与降级矩阵                            |
| `docs/TESTING.md`                     | 自动测试与手工验证的分工                           |
| `docs/LINUX_VERIFICATION.md`          | Linux 各 backend 验证证据与限制                |
| `docs/MACOS_VERIFICATION.md`          | macOS 验收门与未验证项                         |
| `docs/DEVIATIONS.md`                  | 已知偏差与明确不采纳的条目                          |

`docs/archive/` 只保留历史 architecture 与 API 说明，不代表当前实现。

## License

GNU Affero General Public License v3.0（AGPL-3.0），完整文本见 `LICENSE`。版权行：Copyright (C) 2026 Talmaidev。

`third_party/` 及 `modules/*/third_party/` 下的第三方组件保留各自 license，见各自目录与 `THIRD_PARTY_NOTICES.md`。