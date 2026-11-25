# zcsr Android arm64 port — 1.0 verification notes

## Status (2026-11-25)

- **Builds**: arm64-v8a native library `libzcsr_phase0.so` builds with NDK r30
  (`30.0.16248370`), Android 24 minimum, target SDK 36.
- **APK**: `tools/android_pack.sh` produces a signed, zipaligned
  `zcsr_phase0_1.0.0_arm64.apk` (versionCode 1 / versionName 1.0.0).
- **Device**: PASS on a real Xiaomi 22127RK46C (product `socrates`,
  Redmi K60 Pro), Android 16 / SDK 36, arm64-v8a, 1440x3200, density 560,
  adb serial `3d16ff0e`. Functional run: 13/13 PASS. Concurrent one-shot
  stress (4 in-device input producers, 133 touch events): PASS. No Java
  FATAL, GL error, or AAudio failure; process stayed alive. See the
  `ZCSR_Android_real_device_20261125.log` evidence bundle outside the repo.
  This PASS is for the demo scope below, not for full Runtime Player/UIM.
- **Scope**: this is the engine + native demo on Android. It is not the desktop
  Editor, and it does not claim full Runtime Player/UIM integration. The 1.0
  Android scope is engine rendering/input/audio/assets plus the native demo.

## What the demo covers

- EGL + GLES 3.x context, 120 Hz refresh request where supported.
- Generated RGBA texture upload, sprite batch, atlas UV regions, instanced batch,
  and bitmap text; scenes rotate every 2 s.
- Touch and keyboard through `zcsr_input_ext` / `zcsr_input`; AAudio mixer via
  `zcsr_mix` with a generated WAV.
- `APP_CMD_INIT_WINDOW` / `TERM_WINDOW` / `GAINED_FOCUS` / `LOST_FOCUS` /
  `CONFIG_CHANGED` lifecycle handling, plus HOME/resume reinitialization.
- APK assets through `zcsr_assets_set_android_asset_manager`.

## Build and package

```bash
SDK=/path/to/android-sdk
NDK="$SDK/ndk/30.0.16248370"

cmake -S android -B build/android-arm64 -G "Unix Makefiles" \
  -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-24 \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build/android-arm64 --parallel

ANDROID_SDK_ROOT="$SDK" ZCSR_ANDROID_SO="$PWD/build/android-arm64/libzcsr_phase0.so" \
ZCSR_ANDROID_APK="$PWD/build/zcsr_phase0_1.0.0_arm64.apk" \
ZCSR_DEBUG_KEYSTORE=/path/to/debug.keystore ZCSR_DEBUG_STOREPASS=android \
bash tools/android_pack.sh
```

Use a debug keystore outside the repository; never commit a `.keystore` or
`build/` output.

## Device test

```bash
ANDROID_SDK_ROOT="$SDK" ZCSR_ANDROID_APK="$PWD/build/zcsr_phase0_1.0.0_arm64.apk" \
ZCSR_ANDROID_LOG_DIR="$PWD/build/android-device-logs" \
bash tools/test_all_android.sh
```

The script installs the APK, captures four scene screenshots, injects touch and
a swipe, sends HOME then relaunches, captures filtered logcat, and requires these
native markers:

```
android boot ok
1C.0 texture=
1C.0 sprite batch PASS
1C.1 atlas PASS
1C.2 instanced PASS
1C.3 text PASS
1E input extension initialized
1E keyboard state initialized
1E touch:
1D audio: WAV registered
GLES version:
```

It also fails on a Java `FATAL EXCEPTION` or a dead process after resume.

The checked-in script is sequential. The 2026-11-25 real-device run is a
sequential functional pass plus a separate one-shot concurrent stress run:
4 input producers inside the device shell, 6 tap + 6 swipe each, all while
the render and AAudio threads were active. Android InputManager serializes
injected touch, so this is a concurrent input/render/audio stress, not a
multi-process rendering test. Do not describe it as a long soak.

## Current result (2026-11-25)

Xiaomi 22127RK46C (`socrates`, Redmi K60 Pro), Android 16 / SDK 36,
arm64-v8a, 1440x3200, density 560. GLES 3.2, EGL 1.5.

- Functional: 13/13 PASS; cold launch 249 ms, HOME then HOT resume 51 ms;
  AAudio WAV registered and finished channel 0; resume recreated surface and
  reran EGL/GLES init.
- Concurrent one-shot stress: 133 touch events, 38 process threads,
  native render thread and `AudioTrack` present; all 1C scene checks pass;
  no Java FATAL, GL error, or AAudio failure; process alive.
- Raw evidence lives outside the repo in
  `ZCSR_verify_20261125/android/real_device_20261125/`.

## Historical result

An earlier build was observed on a Redmi K60 Pro (`socrates`) with GLES 3.2 and
a 1440x3200 window; that result predates the 1.0 input/audio/assets changes.
The current result above supersedes it for the demo scope.

## Constraints

- Public `zcsr/gl_render.h` API unchanged.
- No PNG/image/stb_image dependency in the Android demo.
- Renderer state lives in caller buffers.
- No Gradle wrapper / AAB pipeline yet; NativeActivity + `android_pack.sh` is
  the current packaging path.
