# ZCSR

ZCSR 是一个 C11 **2.5D engine library**：static libraries + contract headers。它不是可运行的游戏，也不持有 window。

## Layers

| layer | owns | does not own |
|---|---|---|
| `zcsr` engine | record validation、upload、GL state、draw、present | game object、crowd simulation、visibility policy、grouping、window |
| `crowd` | entity store、visibility、record count、call grouping | GL state、upload、draw |
| `runtime host` | 单一 OS window、surface、input pump、main loop | engine internals |
| `editor workspace` | authoring UI，由 runtime host 承载 | 第二个 window |

`zcsr` 与 `crowd` 属于不同 layer。engine 只接收 caller 传入的 record array 与 count，不读取 crowd 结构，不推断 batch size。

`zcsr_runtime` 是 runtime host；`zcsr_editor` 是 hosted editor workspace，不打开第二个 window。

## Terms

| term | meaning |
|---|---|
| `engine library` | 本仓库的 C11 static libraries 与 contract headers；不持有 window |
| `runtime host` | window、surface、main loop 的唯一 owner |
| `editor workspace` | 由 runtime host 承载的 authoring UI |
| `2.5D scene` | orthographic camera 下的 2D projected quad，不是 3D model scene |
| `quad` | 一个带 UV 与 tint 的四角 screen polygon |
| `layer` | 显式 draw group，不是 painter depth |

## Modules

```
runtime host
  ├── editor workspace
  ├── game module
  └── engine (include/zcsr + static libs)
        core / signal / hsm / platform
        glrender / sprite / culling / instance_sort / atlas / font / image / color_pipeline
        light2d / light2d_environment / camera / camera3d / scene_camera
        scene / scene_io / scene_render / scene_submit
        audio / audiomix / music / video / input / inputext / assets / resource / file_service
        collision / save / ui / viewport
```

`modules/` 下的 target 独立编译；module 集合以顶层 `CMakeLists.txt` 的 `zcsr_module()` 为准。module 之间不 include 对方 source。组合发生在 product 层。

| group | modules |
|---|---|
| base | `core` `signal` `hsm` `platform` |
| render | `glrender` `sprite` `culling` `instance_sort` `atlas` `font` `image` `color_pipeline` |
| light / camera | `light2d` `light2d_environment` `camera` `camera3d` `scene_camera` |
| scene | `scene` `scene_io` `scene_render` `scene_submit` |
| media | `audio` `audiomix` `music` `video` |
| input / resource | `input` `inputext` `assets` `resource` `file_service` |
| game / UI | `collision` `save` `ui` `viewport` |

## Build axes

三个 compile-time axis 独立取值，不组成 profile bundle，不在 draw path 上引入 runtime bool、threshold 或 enum dispatch。

| axis | CMake option | values | default | compiled effect |
|---|---|---|---|---|
| layout | `ZCSR_INSTANCE_LAYOUT` | `full` / `compact` | `full` | record type、stride、可用 draw entry |
| color | `ZCSR_COLOR_TIER` | `linear` / `direct` | `linear` | present 是否执行 LUT encode |
| sort | `ZCSR_SORT_TIER` | `key` / `none` | `key` | CPU index sort stage 是否编入 |

### Layout

| value | record | entries |
|---|---|---|
| `full` | `zcsr_gl_instance`，168 B | `zcsr_gl_draw_instances`、`zcsr_gl_draw_instances_visible`、`zcsr_gl_draw_instances_plain` |
| `compact` | `zcsr_instance_compact`，36 B | `zcsr_gl_draw_instances_compact` |

`POS_UV` 是 32 B assembly-time layout，不是 CMake cache value，也不产生 public draw entry。

Entry 是否存在由 layout 决定。调用 active layout 未编入的 symbol 产生 link error，不产生 runtime return-0 stub。

Contract：全平台共享同一组 `include`、func name 与 signature；layout/color/sort 只切换编译内容，不改变同名 func 的参数语义。

### Color

| value | frame | present |
|---|---|---|
| `linear` | `RGBA16F` linear frame | `zcsr_gl_present` 执行一次 LUT encode，写入 display-encoded RGBA8 |
| `direct` | default framebuffer | 无 encode；recolor / ramp / glow / chroma / emiss / rim fragment 不编入 |

`linear` 与 `direct` 只决定 present 轴，不改变 record layout、draw entry 或 sort stage。设备不支持 `RGBA16F` color attachment 时，`linear` renderer 创建失败，不降级到 `direct`。

`direct` tier 下，`zcsr_gl_instance` 中 active 的 recolor / ramp / glow / chroma / emiss / rim stage 由 cull/select 拒绝，不静默绘制未变换颜色。

### Sort

`ZCSR_SORT_TIER=key` 编入 4 B key stable radix/counting sort；`none` 编入 identity order，sort stage 整段消失。

Sort 是 caller 可选的 CPU index stage，对应 `zcsr_sort_instance_order`。所有 draw entry 按 submission order 提交，不在内部排序；多 texture 场景由 caller 先按 texture 分区，再对每个 run 调用一次 draw entry。

## Draw entries

每个 entry 一次只接收一个 `texture`。`records` 由 caller 持有，draw 返回后仍归 caller。

| function | record | layout | primitive / attributes | cull / select | sort | count limit |
|---|---|---|---|---|---|---|
| `zcsr_gl_draw_instances` | `zcsr_gl_instance` 168 B | `full` | 6-vertex `GL_TRIANGLES` / 13 attributes | yes：cull/select + gather | no | 65,535 |
| `zcsr_gl_draw_instances_visible` | `zcsr_gl_instance` 168 B | `full` | 6-vertex `GL_TRIANGLES` / 13 attributes | no | no | internal chunk |
| `zcsr_gl_draw_instances_plain` | `zcsr_gl_instance_plain` 40 B | `full` | 4-vertex `GL_TRIANGLE_STRIP` / 5 attributes | no | no | internal chunk |
| `zcsr_gl_draw_instances_compact` | `zcsr_instance_compact` 36 B | `compact` | 6 vertices / packed attributes | yes：cull/select | no | 65,535 |

### `zcsr_gl_draw_instances`

```c
size_t zcsr_gl_draw_instances(zcsr_gl_renderer* r, zcsr_gl_texture texture,
                              const zcsr_gl_instance* instances, size_t count);
```

FULL layout 的通用 entry。一次调用处理一个 texture。

Behavior：

- 对 `instances[0..count)` 执行 cull/select：offscreen、`alpha <= 0`、invalid record（`ZCSR_DEBUG_VALIDATE`）、`ramp_amount > 0` 但未绑定 ramp atlas、以及当前 layout/color tier 未编入的 stage，均被拒绝。
- retained record 按原 submission order gather 到 staging array，再 upload、draw。gather 不改变相对顺序。
- `count` 上限为 65,535（`ZCSR_CULL_BITMAP_CAPACITY`）。
- 返回实际 submitted 的 instance count；`r == NULL`、`texture == 0`、`instances == NULL` 或 `count == 0` 返回 0。Culling 可能使返回值小于 `count`。
- 不执行 sort。多 texture 或需要 painter order 时，先由 caller 分区/排序。

Availability：`full` layout。

### `zcsr_gl_draw_instances_visible`

```c
size_t zcsr_gl_draw_instances_visible(zcsr_gl_renderer* r, zcsr_gl_texture texture,
                                      const zcsr_gl_instance* instances, size_t count);
```

FULL layout 的 no-cull sibling entry。Caller 保证 `count` 个 record 全部可见且有效。

Behavior：

- 不执行 select、cull、per-instance bitmap test 或 gather/reorder。
- 168 B record 连续 upload，按 submission order draw。
- `count` 超过 `ZCSR_GL_MAX_INSTANCES` 时内部按 capacity 分 chunk，不丢 record。
- 返回 submitted count；invalid renderer/texture/array 返回 0。
- 不执行 sort。

Availability：`full` layout。

### `zcsr_gl_draw_instances_plain`

```c
size_t zcsr_gl_draw_instances_plain(zcsr_gl_renderer* r, zcsr_gl_texture texture,
                                    const zcsr_gl_instance_plain* records, size_t count);
```

FULL layout 的纯图 entry。独立 program、VAO、VBO。

Behavior：

- record 只包含 center、size、UV rect、rotation、alpha。
- fragment 输出 `texel * vec4(1,1,1,a)`，与 general entry 在 `tint=1`、`MULTIPLY`、无 optional stage 时的结果一致。
- 无 tint、mode、amount、recolor、ramp、glow、chroma、emiss、rim 字段；这些 stage 不参与该 entry。
- 不执行 cull、select 或 sort。
- `count` 超过 `ZCSR_GL_MAX_INSTANCES` 时内部按 capacity 分 chunk。
- 返回 submitted count；invalid renderer/texture/array 返回 0。

Availability：`full` layout。一次调用提交完整 range；不提供按 chunk 拆分的独立 entry。

### `zcsr_gl_draw_instances_compact`

```c
size_t zcsr_gl_draw_instances_compact(zcsr_gl_renderer* r, zcsr_gl_texture texture,
                                      const zcsr_instance_compact* records, size_t count);
```

COMPACT layout entry。直接处理 36 B record，不经过 168 B record staging 或转换。

Behavior：

- 对 36 B record 执行 cull/select，retained record gather 后 upload、draw。
- record 只表达 center、size、UV、rotation、alpha；tint / mode / amount / recolor / ramp / glow / chroma / emiss / rim 不编入该 layout。
- `count` 上限为 65,535（`ZCSR_CULL_BITMAP_CAPACITY`）。
- 返回实际 submitted 的 record count；invalid renderer/texture/array 返回 0。
- 不执行 sort。

Availability：`compact` layout。

### `zcsr_sort_instance_order`

```c
size_t zcsr_sort_instance_order(const uint32_t* keys, uint32_t* order,
                                uint32_t* scratch, size_t count);
```

CPU index sort stage，不接触 wide record array。

- `ZCSR_SORT_TIER=key`：对 `keys[0..count)` 执行 stable LSD radix sort，输出 `order[0..count)` permutation；tie 保持原 index 升序。
- `ZCSR_SORT_TIER=none`：`order[i] = i`，sort code 不编入。
- `order` 与 `scratch` 由 caller 提供，各 `count` 个 `uint32_t`。
- 返回 `count`；`keys == NULL`、`order == NULL`、`scratch == NULL` 或 `count == 0` 返回 0。

## Renderer lifecycle

| function | behavior |
|---|---|
| `zcsr_gl_renderer* zcsr_gl_create(zcsr_surface* surface, void* buffer, size_t bytes)` | 在 surface 上创建 GL 3.3 Core / GLES 3.x context 与 renderer state；state 位于 caller buffer；失败返回 `NULL` |
| `void zcsr_gl_destroy(zcsr_gl_renderer* r)` | 销毁 renderer 与 context |
| `void zcsr_gl_clear(zcsr_gl_renderer* r, float r, float g, float b, float a)` | 清空当前 render target |
| `void zcsr_gl_present(zcsr_gl_renderer* r)` | `linear` tier 执行一次 LUT encode 并 present；`direct` tier 直接 present |
| `bool zcsr_gl_read_rgba(zcsr_gl_renderer* r, void* rgba, size_t bytes, int* out_w, int* out_h)` | 读取 encoded RGBA8 frame；用于 present 前的 deterministic screenshot |

`zcsr_gl_create` 的 renderer 结构体较大。reference hosts 预留 16 MiB static/arena buffer；buffer 不足时返回 `NULL`。

## Records

### `zcsr_gl_instance` — 168 B

| offset | field | type | upload | semantics |
|---|---|---|---|---|
| 0 | `cx`, `cy` | `float[2]` | GPU | center，pixel |
| 8 | `w`, `h` | `float[2]` | GPU | size，pixel |
| 16 | `u0`, `v0`, `u1`, `v1` | `float[4]` | GPU | atlas UV sub-rect；full texture 用 `0,0,1,1` |
| 32 | `r`, `g`, `b`, `a` | `float[4]` | GPU | modulation color |
| 48 | `rotation` | `float` | GPU | radians，绕 center |
| 52 | `amount` | `float` | GPU | MIX/ADD blend strength |
| 56 | `mode` | `float` | GPU | `zcsr_color_mode` |
| 60 | `recolor` | `zcsr_color_transform` | GPU | hue / ramp / glow / chroma / emiss / rim；zero = unchanged |
| 156 | `tex` | `uint32_t` | CPU only | texture handle，用于按 texture 分 run |
| 160 | `z` | `float` | CPU only | painter order；larger = farther，先 draw |
| 164 | `has_alpha` | `bool` | CPU only | texture 含非 opaque pixel；用于 translucent sort |

`z` 必须为 finite non-NaN。`tex`、`z`、`has_alpha` 不进入 GPU attribute。

### `zcsr_gl_instance_plain` — 40 B

```c
typedef struct zcsr_gl_instance_plain {
    float cx, cy;
    float w, h;
    float u0, v0, u1, v1;
    float rotation;
    float a;
} zcsr_gl_instance_plain;
```

10 floats，40 B。无 tint、mode、amount、recolor、ramp、glow、chroma、emiss、rim、layer 字段。

### `zcsr_instance_compact` — 36 B

```c
typedef struct {
    float cx, cy;
    float w, h;
    float u0, v0, u1, v1;
    uint32_t pack;
} zcsr_instance_compact;
```

`pack` 为 little-endian `uint32_t`：

| bits | field | encoding |
|---|---|---|
| 0..15 | rotation | normalized short；one full circle = 65,536 |
| 16..23 | alpha | 8-bit，0..255 |
| 24..31 | reserved | write 0，read ignore |

### `zcsr_instance_pos_uv` — 32 B

`POS_UV` assembly value 的 record：center、size、UV，无 rotation/alpha。该 layout 不通过 CMake 暴露，也不产生 public draw entry。

## Capacity and memory

`ZCSR_GL_MAX_INSTANCES` 默认 65,535。覆盖方式为 compile definition，例如：

```cmake
target_compile_definitions(zcsr_glrender PRIVATE ZCSR_GL_MAX_INSTANCES=<value>)
```

CMake cache 不提供该 option。

`count <= ZCSR_GL_MAX_INSTANCES` 时一次 `glDrawArraysInstanced` 完成，`draw_calls=1`。`visible` 与 `plain` 在 `count > capacity` 时由 engine 内部按 capacity 分 chunk；generic 与 compact 的 cull/select 路径上限为 65,535。

Ring VBO 为 `ZCSR_GL_INSTANCE_SLOTS(8) × capacity × stride`：

| record | stride | 65,535 capacity | 10,000 capacity |
|---|---|---|---|
| `zcsr_gl_instance` | 168 B | 88,079,040 B / 84.00 MiB / 88.08 MB | 13,440,000 B / 12.82 MiB / 13.44 MB |
| `zcsr_gl_instance_plain` | 40 B | 20,971,200 B / 20.00 MiB / 20.97 MB | 3,200,000 B / 3.05 MiB / 3.20 MB |
| `zcsr_instance_compact` | 36 B | 18,874,080 B / 18.00 MiB / 18.87 MB | 2,880,000 B / 2.75 MiB / 2.88 MB |

MiB = 1024² bytes；MB = 10⁶ bytes。除 ring VBO 外，renderer 结构体内嵌一份 CPU staging array，不 ×8。

`sizeof(zcsr_gl_renderer)`：`full` 13,930,824 B（13.29 MiB）；`compact` 2,920,944 B（2.79 MiB）。

## Usage

```c
#include <zcsr/gl_render.h>
```

Link `zcsr_glrender`。`zcsr_surface` 由 runtime host 提供；renderer state 位于 caller buffer。

```c
static unsigned char arena[16u << 20];

zcsr_gl_renderer* r = zcsr_gl_create(surface, arena, sizeof arena);
if (!r) return 1;

zcsr_gl_texture tex = zcsr_gl_texture_create(r, rgba, w, h);
zcsr_gl_texture_filter(r, tex, false); /* nearest */

zcsr_gl_clear(r, 0.02f, 0.025f, 0.035f, 1.0f);
zcsr_gl_draw_instances_plain(r, tex, records, count);
zcsr_gl_present(r);

zcsr_gl_texture_destroy(r, tex);
zcsr_gl_destroy(r);
```

`zcsr_gl_texture_create` 创建 DISPLAY texture；`zcsr_gl_texture_create_ex` 显式声明 encoding；texture `0` 为 invalid。

Frame sequence：

1. 构造 record array：`zcsr_gl_instance`、`zcsr_gl_instance_plain` 或 `zcsr_instance_compact`。
2. 多 texture 场景先按 texture 分区；需要 painter order 时调用 `zcsr_sort_instance_order`。
3. 对每个 texture run 调用一次对应 draw entry，检查返回值。
4. `linear` tier 调用 `zcsr_gl_present` 完成唯一一次 encode；`direct` tier 无 encode。

168 B general path：

```c
size_t count = zcsr_scene_build_instances(&scene, instances, capacity);
zcsr_gl_draw_instances(renderer, texture, instances, count);
zcsr_gl_present(renderer);
```

Caller 保证全部可见时：

```c
zcsr_gl_draw_instances_visible(renderer, texture, instances, count);
```

COMPACT path：

```c
zcsr_gl_draw_instances_compact(renderer, texture, records, count);
```

Plain path：

```c
zcsr_gl_draw_instances_plain(renderer, texture, records, 65535);
```

`zcsr_scene_camera` 与 `camera3d` 只做 projection math，不持有 GL。`glrender` 不读取 scene document，只接收 record array。

## Build options

| option | values | default | effect |
|---|---|---|---|
| `ZCSR_INSTANCE_LAYOUT` | `full` / `compact` | `full` | record type、stride、draw entry set |
| `ZCSR_COLOR_TIER` | `linear` / `direct` | `linear` | present encode path |
| `ZCSR_SORT_TIER` | `key` / `none` | `key` | CPU index sort stage |
| `ZCSR_BUILD_TESTS` | `ON` / `OFF` | `ON` | repository contract tests |
| `ZCSR_ENABLE_FORMAT_GATE` | `ON` / `OFF` | `ON` | printf format-gate CTest |

## Build

Windows：

```
cmake -S . -B ..\zcsr-build\win-msvc -G "Visual Studio 18 2026" -A x64
cmake --build ..\zcsr-build\win-msvc --config Release
ctest --test-dir ..\zcsr-build\win-msvc -C Release --output-on-failure
```

Linux：

```
cmake -S . -B ../zcsr-build/linux-gcc -DCMAKE_BUILD_TYPE=Release
cmake --build ../zcsr-build/linux-gcc -j
ctest --test-dir ../zcsr-build/linux-gcc --output-on-failure
```

Linux backend dependencies：X11/GLX 需要 `libx11-dev`、`libgl1-mesa-dev`，`libxrandr-dev` 可选；`inputext_x11` 需要 `libxtst-dev`；Wayland/EGL 需要 `wayland-client`、`wayland-scanner`、`wayland-protocols`、`libxkbcommon-dev`、`libegl1-mesa-dev`；audio 可选 `libpipewire-0.3-dev` + `libspa-0.2-dev` 或 `libasound2-dev`。

macOS 15+ / Xcode 16+：

```
cmake -S . -B ../zcsr-build/macos -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0
cmake --build ../zcsr-build/macos -j
./tools/verify_macos_linear.sh
```

macOS 构建与运行需要真实 GUI session，不能使用 headless CI。

## Android

NativeActivity + NDK，不使用 Gradle：

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

生命周期、input、AAudio 与 13 项 native marker 见 `android/README.md`。

## Tools

`tools/` 脚本按系统分工，输出写入 source tree 之外。

| script | system | function |
|---|---|---|
| `check_no_cross_includes.sh` | POSIX shell | 检查 module 是否越界 include |
| `check_media_deps.sh` | Linux / macOS | 探测 FFmpeg / libwebp headers、libs、version、license |
| `check_media_deps.ps1` | Windows | 同上 |
| `android_pack.sh` | Linux / macOS | aapt2 link、`.so` packaging、zipalign、apksigner |
| `test_all_android.sh` | Linux / macOS | install、launch、touch/swipe、HOME restore、screenshot、logcat、13 native markers |
| `verify_macos_linear.sh` | macOS GUI session | 构建并运行 linear frame gate，读取真实 backbuffer |
| `test_all_macos.sh` | macOS GUI session | 仓外构建 + ctest + editor smoke |
| `collect_macos_logs.sh` | macOS | 收集 logs 与 screenshots |
| `verify_light2d_shot.ps1` | Windows | 状态触发 screenshot，flame pixel 亮度探针 |
| `gpu_probe.c` | GL system | 打印 `GL_RENDERER`、`GL_VERSION`、extensions |

Android 与 macOS 脚本在缺少设备或 GUI session 时失败，不跳过。

## Optional media dependencies

FFmpeg 与 libwebp 不随仓库分发；`third_party/prebuilt/` 被 `.gitignore` 排除。缺失时 build 仍成功，对应 backend fail-closed：WebP 与 FFmpeg 格式报告 `caps=false`，decode 返回 `UNSUPPORTED`。

License 基线：本项目 AGPL-3.0；FFmpeg 必须为 LGPL variant 且 dynamic link。GPL variant 不可用。

Probe 要求 header 与 lib 同时存在：FFmpeg 五个 lib（avcodec / avformat / avutil / swscale / swresample）；libwebp 三个 lib（webp / webpdemux / sharpyuv）。Root override：`ZCSR_DEPS_ROOT`、`ZCSR_FFMPEG_ROOT`、`ZCSR_LIBWEBP_ROOT`。

| system | install |
|---|---|
| Windows | `C:\vcpkg\vcpkg.exe install ffmpeg libwebp --triplet x64-windows`；root 指向 `C:/vcpkg/installed/x64-windows` |
| Debian / Kali | `apt-get install -y libavcodec-dev libavformat-dev libavutil-dev libswscale-dev libswresample-dev libwebp-dev libsharpyuv-dev`；root `/usr` |
| Fedora / RHEL | 启用 RPM Fusion；`dnf install -y ffmpeg-devel libwebp-devel`；root `/usr` |
| Arch | `pacman -S --needed ffmpeg libwebp`；root `/usr` |
| macOS | 使用 LGPL dynamic build（自建或 LGPL package）；MacPorts/Homebrew 默认 GPL variant 不可用 |
| Android | 无系统包；cross-compile 或 per-ABI prebuilt；缺失时 fail-closed |

版本矩阵见 `third_party/DEPENDENCIES.md`；格式能力见 `docs/MEDIA_SUPPORT.md`。

## Constraints

- C11 only；不使用 C++、exception、RTTI。
- Runtime 零 heap allocation；engine 不调用 `malloc/free`。state 位于 fixed pool 或 caller buffer。
- 一个 process 一个 window；window、surface、input pump 属于 runtime host。
- Module 只 include `<zcsr/...>` contract header；不 include 其他 module source 或 relative path。
- Build tree 必须在 source tree 外；根 `CMakeLists.txt` 在 configure 期拒绝 in-tree build。
- 单一 render backend：OpenGL 3.3 Core / GLES 3.x。`ZCSR_RENDER_BACKEND_CPU` 为 reserved，`zcsr_render_backend_available(ZCSR_RENDER_BACKEND_CPU)` 返回 false；GPU 不可用时 create 失败，不退回 software。
- `linear` 为独立 present 轴；`RGBA16F` color attachment 不可用时 renderer 创建失败，不静默降级。
- 纯图配置可直接使用 `compact` layout + `direct` color + `sort none`，不依赖 linear 的附加条件。

## Performance reference

固定负载：65,535 instances、1920×1080、nearest、alpha 0.75、`direct`、单 texture、无 cull/sort。ZCSR 与 Unity 各自按可用 API 形状提交，不是同形状 A/B。GPU 为 finish-equivalent frame time，present 不计入。

| draw shape | ZCSR GL 3.3 | Unity D3D11 |
|---|---|---|
| 1 draw | 1.468 ms（plain，upload included） | 2.282 ms（`DrawMeshInstancedIndirect`） |
| 65 draw | 2.390 ms（plain，capacity 1,023） | 1.0219 ms（`DrawMeshInstanced`） |

ZCSR 1→65 draw 为 1.468→2.390；Unity 65→1 draw 为 1.0219→2.282。四个 reference cell 上形状方向相反；`1.468` 是 locked pre-fix 参考，不能当作与 2.390 同 build 的差值。`0.833` 是 ZCSR `--noupload` 诊断，不是 cross-engine 单元；`1.468` / `2.390` 的 GPU 窗口含 2.62 MB entry upload，Unity `gpuFrameTime` 不含 matrix / indirect record upload。CPU submit `0.104`（ZCSR `plainrun` 65 draw，capacity 1,023）对 `1.0829`（Unity 65 draw）是同形状提交段差，约 10.4×；ZCSR 1 draw 的提交为 0.101–0.118。

Unity `DrawMeshInstanced` 的 1,023-instance 上限要求 65 draw；`DrawMeshInstancedIndirect` 可 1 draw 提交 65,535，实测 2.282 ms，比 65 draw instanced 的 1.0219 ms 慢。该限制是 API 提交形状，不是 Unity 不能单次 draw。

Complex linear path（ZCSR only，linear HDR，65,535 instances / 1920×1080 / nearest / alpha 0.75 / 1 draw）：168 B general linear 为 1.263 ms；激活 hue / ramp / glow / chroma / emiss / rim 后为 6.266 ms（GPU frame median，5 × 330 samples，B580）。

Benchmark coverage：

| case | ZCSR | Unity bench | comparison |
|---|---|---|---|
| simple instanced sprite，plain 40 B | measured | `DrawMeshInstanced` / indirect shader | cross-engine |
| linear HDR，168 B general | measured | no equivalent path | ZCSR only |
| complex color stages：hue / ramp / glow / chroma / emiss / rim | measured | no equivalent path | ZCSR only |
| Light2D / shadow | engine module + tests | no equivalent path | ZCSR only |

跨引擎结论仅适用于 simple instanced path。Unity 侧 benchmark 不含 linear HDR、complex color stage、Light2D 或 shadow 的对应实现，ZCSR-only 数据不能写成 Unity 输。Unity 的 editor、asset pipeline、plugin/社区生态不在 draw benchmark 口径内；本表不构成全平台/全生态排名。ZCSR 验证平台为 Windows GL 3.3、Linux GLX、macOS Cocoa、Android GLES3。

完整测量条件、linear complex 数据、record stride 对照与 pixel gate 见 `docs/PERFORMANCE.md`。

## Documentation

| file | content |
|---|---|
| `docs/ARCHITECTURE.md` | zero-heap、zero-coupling、platform / audio / input backend 与 thread model |
| `docs/API_REFERENCE.md` | contract header index 与 API rules |
| `docs/RENDER2D_CONTRACT.md` | 2D coordinate、color space、G-buffer contract |
| `docs/COLOR_LIGHT_SHADOW_CONTRACT.md` | color / light / shadow cross-module contract |
| `docs/LIGHT2D_MATH_MODEL.md` | Light2D math |
| `docs/LIGHT2D_INTEGRATION.md` | Light2D integration |
| `docs/MEDIA_SUPPORT.md` | media format capability matrix |
| `docs/PERFORMANCE.md` | measurement protocol 与完整数据 |
| `docs/TESTING.md` | automated / manual test split |
| `docs/LINUX_VERIFICATION.md` | Linux backend verification |
| `docs/MACOS_VERIFICATION.md` | macOS Cocoa backend contract 与 acceptance gates |
| `docs/DEVIATIONS.md` | known deviations |

`docs/archive/` 只保留历史 architecture 与 API 说明，不代表当前实现。

## License

GNU Affero General Public License v3.0（AGPL-3.0）。完整文本见 `LICENSE`。Copyright (C) 2026 Talmaidev。

`third_party/` 与 `modules/*/third_party/` 下的第三方组件保留各自 license；汇总见 `THIRD_PARTY_NOTICES.md`。
