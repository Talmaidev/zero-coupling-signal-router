# ZCSR Architecture

## Zero-Heap Policy

**Definition:** zcsr's own code does not call `malloc`, `free`, `calloc`, or `realloc`.

**Scope:** This constraint applies to all code under `modules/` and `include/` that is part of the zcsr project.

**Exclusions:** Allocations made internally by system libraries are NOT counted against the zero-heap constraint:
- Wayland client library (`libwayland-client`)
- PipeWire (`libpipewire-0.3`, `libspa-0.2`)
- EGL / OpenGL (libEGL, libOpenGL, GLAD loader)
- Operating system APIs (Win32, Android NDK, Cocoa)

**Implementation:** Fixed-size pools are used for all zcsr-managed objects:
- `zcsr_surface`: pool of 4 (`ZCSR_SURFACE_POOL`)
- `zcsr_overlay`: pool of 4 (`ZCSR_OVERLAY_POOL`)
- Other modules use static buffers or caller-provided memory

## Zero-Coupling Policy

**Definition:** Module source files may only include shared contract headers from `include/zcsr/`. They must not include headers from other modules (no `../other_module/` or `modules/other/` includes).

**Enforcement:** `tools/check_no_cross_includes.sh` verifies this at build time.

**Exception:** Same-directory internal headers (e.g., `platform_internal.h`, `audiomix_internal.h`) are allowed and never leave their module.

## Platform Backends

| Platform | Window | Rendering | Audio | Input |
|----------|--------|-----------|-------|-------|
| Windows | Win32 | WGL + OpenGL | WASAPI | Win32 message pump |
| Linux | Wayland + xdg-shell | EGL + OpenGL | PipeWire | Wayland (wl_keyboard, wl_pointer) |
| Android | ANativeWindow | EGL + OpenGL | AAudio | AInputEvent |
| macOS | Cocoa | NSOpenGL | AudioUnit | Cocoa events |

### Linux Backend Split (post-v1)

`modules/platform/` is split per-backend:
- `platform_common.c` — shared pool, DPI, focus callbacks, display helpers
- `platform_win32.c` — Windows backend
- `platform_wayland.c` — Wayland backend
- `platform_android.c` — Android backend
- `platform_cocoa.m` — macOS backend
- `platform_internal.h` — shared struct definitions and internal interfaces

Each backend file contains only its platform's code; no cross-platform `#if` blocks.

## Audio Backend

`modules/audiomix/`:
- `audiomix.c` — core mixer (channel mixing, format conversion)
- `audiomix_pipewire.c` — PipeWire output backend (Linux)
- `audiomix_internal.h` — shared backend struct and interface

The PipeWire backend uses a SPSC ring buffer between the mixer thread and the realtime process callback, with int32 accumulation and clamping to avoid overflow.

## Input Backend

`modules/input/` — keyboard input (zcsr_input)
`modules/inputext/` — mouse + gamepad input (zcsr_input_ext)

Linux uses Wayland `wl_keyboard` and `wl_pointer` with evdev keycode mapping (zcsr_keycode = evdev + 8, matching X11 convention).
