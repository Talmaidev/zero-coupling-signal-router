# Android Phase 1 完成交接记录

## 1. 阶段结论

**Phase 1（1A–1E）已封存。**

目标是在保留 zcsr 既有 C11 与模块边界的基础上，完成 Android NativeActivity 生命周期、EGL/GLES 渲染、纹理与精灵绘制、AAudio 输出，以及触摸与手柄输入后端。

当前封存版本：

- HEAD：`b541845`
- 封存提交：`feat(android): add touch and gamepad input backend`
- 工作树：封存回执报告为干净
- Android APK：最后一次验收使用 `versionCode=6`
- Windows Debug / Release：最后阶段的回执均报告 `ctest 17/17`
- 耦合守卫：通过

本文件根据各阶段执行与验收回执整理，不替代独立环境复测。

## 2. 阶段与提交记录

| 阶段 | Commit | 主要交付 | 验收状态 |
|---|---|---|---|
| 1A：NativeWindow 生命周期 | `dca490c` | Android 原生窗口绑定与解绑、surface 生命周期管理 | 已封存 |
| 1B：EGL/GLES 渲染底座 | `1367960` | EGL/GLES 3.x 后端、清屏接口与首帧验证 | 已封存 |
| 1C：纹理与精灵绘制 | `4f24be5`、`8bc93d5`、`c035267` | RGBA 纹理上传与精灵批绘、Atlas UV、实例化绘制验证 | 已封存 |
| 1C：打包与接口文档 | `2bc73b1`、`44cd59d` | 可复现 APK 打包脚本、Phase 1B/1C 接口与打包记录 | 已封存 |
| 1D：AAudio 音频后端 | `66a72c2` | AAudio 运行时加载、PCM 环形缓冲区、播放与停止生命周期 | 已封存 |
| 1E：触摸与手柄输入 | `b541845` | 单主触点触摸状态、Android 手柄映射、输入生命周期重置 | 已封存 |

**相关构建辅助提交：**

- `8dbb5e6`：增加 Android `versionCode` / `versionName` 环境变量覆盖，方便升级安装时保持版本号递增。该提交属于打包流程支持，不替代上表中的阶段提交。

## 3. 各阶段接口边界

### 1A — 原生窗口生命周期

公开接口新增：

```c
bool zcsr_surface_attach_native(
    zcsr_surface* surface,
    void* native_window);

void zcsr_surface_detach_native(
    zcsr_surface* surface);
```

边界约定：

- Android 原生窗口绑定由 platform 层处理。
- surface 使用既有不透明句柄，不向上层暴露 Android 内部实现。
- Android 侧获取并持有 `ANativeWindow` 引用，解绑时释放。
- 必须先销毁依赖当前窗口的 EGL/渲染资源，再解绑原生窗口。
- `APP_CMD_INIT_WINDOW` 与 `APP_CMD_TERM_WINDOW` 驱动窗口生命周期。

### 1B — EGL/GLES 渲染底座

公开接口新增：

```c
void zcsr_gl_clear(/* 以头文件中的实际声明为准 */);
```

以上仅用于标记本阶段的新增接口，不作为函数签名定义；准确原型以 `include/zcsr/gl_render.h` 为准。

边界约定：

- Android 使用 EGL 与 OpenGL ES 3.x。
- 不使用桌面平台的 GLAD、GLX 或 X11 后端。
- Android shader 使用 `#version 300 es` 及相应精度声明；桌面 shader 保留原有桌面 GLSL 路径。
- Renderer 使用调用方提供的固定存储；Android 示例运行时为 renderer 提供静态存储。
- `TERM_WINDOW` 路径必须先销毁 renderer，再解绑原生窗口。

### 1C — 纹理、Atlas 与精灵绘制

本阶段完成或验证：

- RGBA 纹理上传。
- 精灵批绘。
- Atlas UV 区域绘制。
- 实例化精灵绘制。
- 可重复执行的 APK 打包流程。

设备演示使用非对称的 16×16 RGBA 测试纹理，并验证纹理颜色、Atlas 分区以及实例化绘制结果。演示在 Batch、Atlas UV、Instanced 场景之间切换；渲染提交接口已经负责对应的 present/swap，调用方不应无依据地额外执行一次 present。

接口边界：

- Android GLES 后端与桌面渲染后端分离。
- 本阶段以增加 Android 能力及验证现有渲染路径为主，不以替换桌面接口为目标。
- 未记录到需要破坏既有公开 API 的变更；具体函数原型以当前提交中的公共头文件为准。

### 1D — AAudio 音频输出

公开音频契约沿用：

`include/zcsr/audio_mix.h`

封存回执记录为：该头文件只有注释变更，没有 API 函数签名变更。

实现边界：

- Android 音频输出位于 `modules/audiomix/audiomix.c`。
- 通过 `dlopen("libaaudio.so")` 与 `dlsym` 运行时加载 AAudio，共解析 13 个符号。
- Android 链接新增 `dl`、`m`，不直接链接 `aaudio`。
- AAudio 设备流由后端管理；PCM 通过固定容量环形缓冲区交给音频回调。
- 通道注册、播放、完成检测、停止与关闭仍通过既有音频混合模块契约使用。

验收回执记录：设备可以实际听到测试提示音，播放完成状态能被检测；HOME / 恢复及退出路径没有报告崩溃。

### 1E — 触摸与手柄输入

跨平台公共契约新增：

- `zcsr_touch_state`
- `zcsr_input_ext_touch()`

Android 专属契约新增：

- `include/zcsr/input_ext_android.h`
- `zcsr_input_ext_android_event(...)`
- `zcsr_input_ext_android_reset(...)`

接口边界：

- Android 原生输入事件类型留在 Android-only 头文件与平台实现边界。
- 不要求平台无关的游戏逻辑直接处理 `AInputEvent`。
- `zcsr_input_ext_touch()` 在非 Android 平台返回零状态，维持跨平台构建。
- 输入扩展使用调用方传入的固定存储，不新增 zcsr 运行时堆分配。
- 本阶段未修改 `input.c`、`glrender` 或 `audiomix` 的功能实现。

触摸采用单主触点，以 pointer ID 跟踪。`pressed` 与 `released` 表示边沿，`dx` 与 `dy` 按 pump 周期累积提交；`active` 与坐标保留最新状态。多点触控和 Android 键盘输入不在本阶段范围内。

手柄使用现有 14 个按钮映射，摇杆死区为 `0.15`；扳机优先读取 `LTRIGGER` / `RTRIGGER` 轴，必要时回退到 `Z` / `RZ`。

## 4. Android 构建集变化

阶段间的 Android 构建源集合变化如下：

| 进度 | Android 构建源集合变化 |
|---|---|
| 从 1A 起 | `platform` + `input` + `inputext` |
| 1B | 增加 `glrender` |
| 1C | 打包、纹理与精灵渲染验证；沿用 1B 的渲染模块集合 |
| 1D | 增加 `audiomix` |
| 1E | 扩展既有 `inputext` 的 Android 后端；没有新增 CMake 源文件要求 |

1E 阶段的回执确认 `modules/inputext/inputext.c` 在 Android CMake 构建中已存在，因此该阶段无须再次添加同一个源文件。

Android 构建配置使用 Android 原生平台库与 GLES/EGL；音频后端额外使用 `dl`、`m`。打包脚本位于 `tools/android_pack.sh`。

## 5. 关键常量与当前约定

### 平台与 APK

| 项目 | 当前值 / 约定 |
|---|---|
| Android `minSdk` | `24`，保留不变 |
| AAudio 可用 API 级别 | API 26+；运行时加载 AAudio |
| AAudio ELF 直接依赖 | 不应出现 `libaaudio.so` 的 `NEEDED` 项 |
| 版本号覆盖 | `ZCSR_ANDROID_VERSION_CODE`、`ZCSR_ANDROID_VERSION_NAME` |
| 最后验收 APK `versionCode` | `6` |
| 安装方式 | 通过递增 versionCode 使用 `adb install -r`；验收不需要 `-d` |
| Debug 签名密码 | 通过 `ZCSR_DEBUG_STOREPASS` 环境变量提供，不硬编码进脚本 |

打包脚本使用 `aapt2 link`、`zipalign -P 16 -f -v 4` 与 `apksigner`。当前流程关闭 v4 签名：`--v4-signing-enabled false`。签名使用 Android debug keystore。

### 存储与输入

| 项目 | 当前值 / 约定 |
|---|---|
| `sizeof(zcsr_input_ext)` | Android arm64 实测为 `144` 字节 |
| Android 输入扩展静态存储 | `256` 字节 |
| Renderer 静态存储 | `8 MiB` |
| 手柄槽位上限 `ZCSR_PAD_MAX` | `2` |
| 手柄摇杆死区 | `0.15` |
| 触摸跟踪 | 单主触点，按 pointer ID 跟踪 |
| 触摸源过滤 | 只处理 `SOURCE_TOUCHSCREEN` |

`sizeof(zcsr_input_ext) = 144` 是该封存版本在 Android arm64 构建中的实测结果，不应视为跨平台 ABI 保证，也不应在添加状态字段后未经重新测量便沿用。

### 音频

| 项目 | 当前值 / 约定 |
|---|---|
| PCM 环形缓冲区 | `4096` frames |
| 初始预填充 | `2048` frames |
| AAudio 动态符号 | `13` 个 |
| 链接方式 | 使用 `dl` 运行时加载，不直接依赖 `aaudio` |

## 6. Phase 1 验收汇总

以下数据来自各阶段执行回执。

### Android

- NativeWindow attach / detach / 恢复路径通过设备验证。
- EGL/GLES 清屏帧通过设备验证；回执报告 EGL 1.5 与 OpenGL ES 3.2。
- RGBA 纹理上传、精灵批绘、Atlas UV、实例化绘制均通过设备验证。
- AAudio 提示音可听见，通道播放完成状态可检测。
- 触摸 tap 的按下/释放边沿与 swipe 位移累积通过设备验证。
- HOME / 恢复路径无已报告的遗留触摸活动状态。
- Android NDK 构建回执报告零警告。
- 最后 APK `versionCode=6`，安装成功，未使用 `-d`。

### Windows 与模块边界

- Windows Debug：`ctest 17/17`。
- Windows Release：`ctest 17/17`。
- 耦合守卫通过。
- 最后阶段回执报告没有新增编译警告。

### 尚未验证的硬件项目

当前测试设备没有实体游戏手柄，因此 Android 手柄映射未经过真机验收。此项不应描述为"手柄已验证通过"。

## 7. 已知技术债与 Phase 2 待决

下列事项是后续候选工作，不代表 Phase 1 已确认存在故障，也不意味着 Phase 2 范围已经定案。

### 输入

1. **实体手柄验证：** 覆盖 14 个按钮、摇杆轴、`0.15` 死区、扳机优先轴与 `Z` / `RZ` 回退，以及断连、重连。
2. **多点触控：** 目前只有单主触点。是否需要多指交互，以及公共 API 如何表达多触点，留待决定。
3. **Android 键盘：** 当前未实现。需要先决定键盘事件是否由 `input` 还是 `inputext` 承担，以及跨平台公共契约的边界。

### 音频

1. **持续播放压力测试：** 验证长时间播放、环形缓冲区欠载/溢出行为与应用反复切后台。
2. **设备与路由变化：** 验证音频设备切换、系统音频中断和设备生命周期变化。
3. **错误可观测性：** 决定是否增加可被上层观察的音频启动失败、流关闭及运行时加载失败诊断。

### 图形与应用生命周期

1. **显示配置变化：** 对旋转、尺寸变化、窗口重建及反复 HOME / 恢复进行扩展回归。
2. **真实场景接入：** 当前验证覆盖的是清屏、纹理和精灵演示路径；后续仍需根据实际游戏场景验证资源生命周期和帧循环。
3. **APK 发布配置：** 当前封存路径使用 debug 签名。正式发布签名、发布版本与交付策略应单独制定。

### 架构与质量门

1. 持续执行 Windows Debug / Release 回归与耦合守卫。
2. 保持 Android 平台实现位于对应模块内，不允许模块之间直接调用或引用彼此私有实现。
3. 保持 zcsr 模块状态使用调用方提供的固定存储；OS、EGL/GLES 与音频驱动资源属于平台资源，不等同于允许模块自行引入运行时堆分配。
4. 如果增加公共接口，优先使用增量扩展；涉及布局、生命周期或语义变化时，必须同步评估 Windows/Linux 回归和既有调用方兼容性。

## 8. Phase 1 封存基线

当前交接基线为 `b541845`。

Phase 1 的工作重点已经从"打通 Android 平台能力"转为下一阶段的范围决策与真实应用集成。Phase 2 开始前，应优先确认输入设备支持范围、音频长时稳定性、正式资源/场景接入与发布构建需求，再按明确验收标准拆分任务。

在新的阶段决策通过之前，不应将本记录中的候选事项自动解释为已批准的实现任务。
