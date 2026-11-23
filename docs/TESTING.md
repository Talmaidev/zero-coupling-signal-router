# Testing

Two complementary layers, kept in **separate directories on purpose**:

| Directory | Purpose | Runs where |
|---|---|---|
| `tests/` | **Per-module unit / contract tests** — each module exercised *in isolation* against its contract header, plus one cross-module wiring test. Correctness gates. | Headless, via `ctest`. |
| `integration/` | **Runnable demo + performance benchmarks** — modules composed into a running app, and frame-time / throughput measured. | Manual; some need a real display / sound card / gamepad. |

## Why two directories

The split mirrors the project's zero-coupling rule:

- **`tests/` proves each module works alone.** A module compiles independently and is tested only through its public contract — never by reaching into another module. So each `tests/test_<module>.c` links exactly the one module (plus the contract layer it depends on) and asserts that module's behavior. These are fast, deterministic, **headless**, and are the suite a CI job runs. The single exception, `tests/test_integration.c`, checks that several modules wire together *correctly* (logic), not performance.
- **`integration/` proves the modules work together as a real app, and how fast.** It holds the `zcsr_demo` (wires the whole stack and prints status) and the benchmarks (`bench*.c` — sprite frame-time, worst-case adversary, raylib comparison, culling, atlas, etc.). These are about **composition and performance**, and several depend on a real GPU / window / device, so they are not part of the headless correctness suite. Benchmarks **skip** themselves when run headless rather than fail.

Keeping them apart means the correctness suite stays fast, deterministic and CI-friendly, while the display-/hardware-dependent demo and perf artifacts don't pollute it.

## tests/ — unit & contract suite (`ctest`)

The engine repository now has its own CTest entry points via the top-level
`enable_testing()` and `tests/CMakeLists.txt`. The current Windows Release suite
has 37 tests, covering core, signal, hsm, platform, glrender, light2d, audio,
music, input, collision, save, ui, viewport, color_pipeline, scene_camera,
scene, scene_render and scene_submit, plus the cross-module checks. The extra
`light2d_math` entry is the pure-CPU shadow/projection contract (29 checks).

`glrender` includes a real-backbuffer assertion for the unified linear frame:
source texture round-trip and 50% black-over-white linear blending. On Windows
(Intel Arc B580/WGL) and Linux (Mesa llvmpipe/GLX) it prints
`PASS: unified linear frame round-trip and linear blending`. A surface-less
environment still passes the CPU sort/contract checks but skips the GL probe.

`game_test` is retired. The consumer builds in `zcsr_runtime` and `zcsr_editor`
register their own UIM page/settings tests; they are not part of this engine
suite.

## Build & run

### Windows (MSVC + VS 2026 generator)

```powershell
cmake -S . -B build -G "Visual Studio 18 2026" -A x64
cmake --build build --config Release

# Targeted: only the module/contract touched by the change, 8 at a time.
ctest --test-dir build -C Release -R glrender -j 8
ctest --test-dir build -C Release -R state -j 8

# Deliberate full gate, not a default after every edit.
ctest --test-dir build -C Release -j 8 --output-on-failure
ctest --test-dir build -C Release -V       # verbose (see each assertion)
```

High-concurrency runs are not for the physical host. Move to the Doubao
virtual desktop first, then use the repo-level runner and name only the suites
the change actually touched:

```powershell
..\run_tests.ps1 -Suite PVZ -Jobs 8 -VirtualDesktop
..\run_tests.ps1 -Suite Engine,Editor -Jobs 8 -VirtualDesktop
..\run_tests.ps1 -Suite Engine -BuildOnly      # compile check only, no test run
```

The runner refuses to run tests without `-Suite` and `-VirtualDesktop`, so a
one-file edit does not silently launch all six suites (including audio). Use
`-BuildOnly` when the change is compile-only; reserve the test path for the
single end-of-work verification.


### Linux (X11/GLX default; Wayland/EGL optional)

```sh
cmake -S . -B build -G Ninja -DZCSR_ENABLE_BOX2D=OFF
cmake --build build

# Headless-safe tests run without a compositor:
ctest --test-dir build -j 8

# platform/inputext window tests need a display:
xvfb-run -a -s '-screen 0 1280x720x24 +extension RANDR' ctest --test-dir build -j 8
# or, for the native Wayland backend:
WAYLAND_DISPLAY=wayland-0 ctest --test-dir build -j 8
```

Linux window backends are still built and selectable. X11/GLX is the default when both X11 and Wayland development packages are present; Wayland/EGL is used only when selected (or when X11 is unavailable). Audio auto-selects PipeWire, then ALSA, then the silent backend. The CMake switches are `ZCSR_LINUX_WINDOW_BACKEND=auto|x11|wayland|none` and `ZCSR_AUDIO_BACKEND=auto|pipewire|alsa|none`.

Linux 1.0 验证口径（可回链细节见 `docs/LINUX_VERIFICATION.md`）：

- Ubuntu 22.04 + GCC 11 + Mesa llvmpipe，X11/GLX + Xvfb：**35/35**，含 `inputext_x11`。
- Ubuntu 22.04 + Weston 9 headless，原生 Wayland/EGL：**34/34**，`glrender_test` 输出 `backend=egl-wayland` 并真实回读三条 PASS。
- Ubuntu 22.04 + PipeWire 0.3.48 + null sink：**35/35**，流真实连到 null sink；物理扬声器未验证。
- Ubuntu 20.04 chroot + GCC 10 + glibc 2.31，X11/GLX：**35/35**；产物最高 `GLIBC_2.29` 符号。Xvfb 没有真实刷新率，XRandR 正路径另在 Xorg dummy/VNC 的 60Hz 模式上验证全屏返回 true、窗口模式返回 false。
- Kali 物理机（真 X11 + 真 WM）此前 **35/35**；本次没有严重改动，按用户口径沿用该结果，不重跑全量。
- 云主机只作补充：无 `/dev/dri`、无物理声卡，缺 GPU/声卡/显示模式不作为发布阻塞；Linux AMD/NVIDIA 驱动、Steam Deck/Gamescope 仍未单独验证。
- 按用户口径，Linux 环境不再作为 1.0 门；物理机结果沿用，不为云副本差异重跑。若只改可编译代码，只做编译检查；需要验证时按改动套件选择，不默认全套。

### macOS

macOS uses Cocoa/NSOpenGL and requires Xcode Command Line Tools. The dedicated
gate is:

```sh
./tools/verify_macos_linear.sh
```

It must print both `PASS: unified linear frame round-trip and linear blending`
and `PASS: GL renderer smoke` on a real GUI session. See
`docs/MACOS_VERIFICATION.md`; a headless Mac runner may skip the GL surface and
is not sufficient for a real Mac check. **按用户 2026-11-23 决定，macOS 真机验收
已 pass、不作为 1.0 发布门；没有真实 Mac 时不重跑此项。**

### Android

Android has a separate `android/CMakeLists.txt`. Build with the NDK toolchain. Tests run on-device via `adb shell`.

## Coupling guard

```sh
bash tools/check_no_cross_includes.sh
```

Verifies that no module includes another module's source or a relative path — only `include/zcsr/*.h` contract headers are allowed. Must PASS on every commit.

## Test data

- `tests/data/sample.mp3` — 44100Hz stereo MP3 (generated by ffmpeg, vendored)
- `tests/data/sample.ogg` — 44100Hz stereo OGG (generated by ffmpeg, vendored)

These are consumed by the standalone `tests/test_music_codec.c` harness. That
harness is **not registered in CTest** (it is a manual direct-decoder check, and
it SKIPs when the samples are absent), so the audio gate is `audio` / `audiomix`.
Keep the samples for that manual harness (zero-network test policy).
