# Testing

## Building

### Build directory layout (enforced, not advisory)

Build and test artifacts **never** live inside the repository tree. All
build outputs go to a sibling build root; the repository checkout contains
only source, fixtures, and documentation.

- Windows: `C:\Users\chen\Documents\build\zcsr\<repo>\<preset>`
- Linux: `/home/chen/zcsr-builds/<repo>/<preset>`

> 旧根 `zcsr-builds` 已整体迁入 `build\zcsr`，旧路径不再使用。

Do not create `build`, `build-*`, `out`, `out-*`, or any scratch directory
inside the repository root. This layout is enforced by three layers:

1. **Configure-time guard** — the root `CMakeLists.txt` refuses any build
   tree that equals or sits under the source tree (`CMAKE_BINARY_DIR` under
   `CMAKE_SOURCE_DIR`) with a fatal configure error. In-repo configure
   fails; out-of-tree configure succeeds.
2. **`repo_hygiene` CTest gate** — registered under `LABELS
   "gate;hygiene"` (only when `ZCSR_BUILD_TESTS=ON`). It is a pure CMake
   `cmake -P` script (`cmake/repo_hygiene_check.cmake`), cross-platform,
   with no PowerShell/shell dependency, and it writes nothing inside the
   source tree. It scans this repository only: any `build*/`, `out*/`,
   `CMakeCache.txt`, `CMakeFiles/`, `Testing/`, `_dl/`, `_src/`, or stray
   `*.obj/*.pdb/*.ilk/*.exe` outside the allowlist fails the gate.
   `tests/data/**` (including the required fixture `tests/data/t0.wav`),
   `tests/video_data/**`, `third_party/prebuilt/**`, `third_party/**`, and
   `.git/**` are allowed.
3. **Six-repo read-only sweep (Windows workstation only)** —
   `C:\Users\chen\Documents\build\_reports\check_repo_hygiene.ps1` scans the
   router and its five sibling repositories. Exit codes: `0` clean,
   `1` violations found, `2` required repo missing / scan incomplete.
   Linux checkouts hold only this repository, so the gate never fails there
   for missing siblings; the full six-repo sweep does not run off Windows.

Run the gate before every push and in CI:

```sh
ctest -L gate --output-on-failure
```

Historical stray products moved out of the router root are quarantined (not
deleted) at
`C:\Users\chen\Documents\build\zcsr\_quarantine\router-root-strays-20261125\`,
with a `manifest.json` recording original paths, byte sizes, sha256,
timestamps, and post-move verification. Full rules and exit-code semantics
are in `C:\Users\chen\Documents\build\_reports\BUILD_LAYOUT.md`.

### Windows (MSVC, Visual Studio 18 2026, x64, Release)

```sh
cmake -S C:\Users\chen\Documents\GitHub\zero-coupling-signal-router ^
      -B C:\Users\chen\Documents\build\zcsr\zero-coupling-signal-router\win-msvc ^
      -G "Visual Studio 18 2026" -A x64
cmake --build C:\Users\chen\Documents\build\zcsr\zero-coupling-signal-router\win-msvc --config Release
ctest --test-dir C:\Users\chen\Documents\build\zcsr\zero-coupling-signal-router\win-msvc -C Release --output-on-failure
```

### Linux (GCC / Clang, Release; ASan+UBSan preset)

```sh
# GCC
cmake -S /home/chen/zcsr_media_test \
      -B /home/chen/zcsr-builds/zero-coupling-signal-router/linux-gcc \
      -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=gcc

# Clang
cmake -S /home/chen/zcsr_media_test \
      -B /home/chen/zcsr-builds/zero-coupling-signal-router/linux-clang \
      -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang

# ASan + UBSan (GCC)
cmake -S /home/chen/zcsr_media_test \
      -B /home/chen/zcsr-builds/zero-coupling-signal-router/linux-asan \
      -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=gcc -DZCSR_ENABLE_SANITIZERS=ON

cmake --build /home/chen/zcsr-builds/zero-coupling-signal-router/<preset> -j8
ctest --test-dir /home/chen/zcsr-builds/zero-coupling-signal-router/<preset> --output-on-failure
```

## Test inventory

The full gate is **61 tests on Windows** and **60 tests on Linux**. Each gate
includes one `repo_hygiene` CTest gate (the `gate;hygiene` scripted check
described above); the media and engine cases themselves are unchanged from the
prior 60 / 59 count. The one remaining test-count difference is
platform-conditional:

- `log_no_malloc` — MSVC only (dumpbin-based).
- `input_win32_pump` — WIN32 only (real Win32 message path).
- `inputext_x11` — Linux X11 only (registered when `XTest.h` and `libXtst`
  are found).

The T0 media gate registers 18 format targets, plus one music negative
contract:

| Domain | Targets |
|---|---|
| Image (7) | `image_png`, `image_jpeg`, `image_gif`, `image_gif_anim`, `image_webp`, `image_webp_anim`, `image_bmp` |
| Music (7) | `music_mp3`, `music_aac`, `music_m4a`, `music_wav`, `music_flac`, `music_ogg`, `music_opus` |
| Music negative | `music_negative` |
| Video (2) | `video_mp4_h264`, `video_webm_vp9` |
| Font (2) | `font_ttf`, `font_otf` |

Legacy and regression targets (`image_hdr`, `image_ico`, `music_api`,
`music_flac_legacy`, `music_flac_codec`, `font`, and the engine/core/ui
suites) keep their existing registrations.

## Fixture passing conventions

The four domains pass fixture paths differently; the CTest registration
matches each convention explicitly.

- **Image tests** read the directory from the environment variable
  `ZCSR_TEST_DATA_DIR` (they do not inspect `argv`; fallback `"data"`). The
  registration sets
  `ZCSR_TEST_DATA_DIR=<repo>/tests/data` for all seven image targets.
- **Music and font tests** read the directory from `argv[1]` (fallback
  `"tests/data"`). They are registered as explicit targets with
  `COMMAND <target>_test <repo>/tests/data`.
- **Video tests** check `argv[1]` first, then fall back to
  `ZCSR_VIDEO_DATA_DIR`. Both are set: the command line passes
  `<repo>/tests/video_data` and the test property exports
  `ZCSR_VIDEO_DATA_DIR=<repo>/tests/video_data`.

Because every test runs from a per-test `run_<name>` build folder, no
relative `"."` or `"data"` fallback can resolve the fixtures; absolute
paths are always supplied by the registration.

## Device locks and timeouts

- Music targets hold the `zcsr_audio_device` resource lock so audio tests do
  not contend on the mixer/device. Timeout: **8 s**.
- Video targets: timeout **60 s** (decode + seek + failure paths).
- Font targets: timeout **10 s**.
- GUI/display targets hold the `zcsr_gui_display` lock.
- The format-gate and log suites keep their existing timeouts.

## Runtime dependencies

### Windows DLL placement

FFmpeg ships as shared DLLs. A POST_BUILD step (`zcsr_copy_ffmpeg_runtime`)
copies `${ZCSR_FFMPEG_ROOT}/bin/*.dll` next to every test executable that
links the FFmpeg backend (all music and video targets, plus `music_api`).
Image targets do not need the FFmpeg DLLs.

### Linux shared-library resolution

On Linux the FFmpeg shared libraries (`libav*.so`) resolve through the build
rpath: `${ZCSR_FFMPEG_ROOT}/lib` is added to `CMAKE_BUILD_RPATH` for FFmpeg
linked targets. No DLL copy step exists off Windows. If a local build drops
the rpath (e.g. a packaged binary), export
`LD_LIBRARY_PATH=${ZCSR_FFMPEG_ROOT}/lib` before running the tests.

### Sanitizer noise allowance

FFmpeg's own global allocations can outlive `avformat`/`avio` teardown and
are reported by ASan as `<unknown module>` leaks. FFmpeg-linked music and
video targets run with `ASAN_OPTIONS=detect_leaks=0`; ASan/UBSan detection
otherwise stays enabled. Leaks, heap-buffer-overflow, or use-after-free
reports attributed to zcsr code are treated as defects, not noise.

## Warning policy

- MSVC: `/W4 /WX` — warnings are errors; a clean build means zero `warning C`
  diagnostics.
- GCC / Clang: `-Wall -Wextra -Wpedantic`.
- The only accepted warnings are pre-existing baseline noise in frozen
  headers/modules (for example the variadic-macro extension in
  `diagnostics.h`, and the >4095-char shader literal in
  `glrender_instanced.c`); new T0 code must compile warning-free on both
  toolchains.
