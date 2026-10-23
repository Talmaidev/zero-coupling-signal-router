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

## Thread Model

```
zcsr thread model:
  Owned threads (created by zcsr):
    - Main thread (game loop)           x1
    - Dedicated audio worker thread      x1 per mixer
    - Dedicated decode worker thread     x1 per zcsr_music
    - PipeWire control thread             x1 per mixer (pipewire backend only)
  External threads (created by system libs, NOT managed by zcsr):
    - PipeWire data-loop RT thread(s)    (libpipewire internal)
    - WASAPI audio thread                 (OS internal)
    - AAudio callback thread              (OS internal)
  Thread pool: none.
```

**Communication between threads:**
- SPSC lock-free ring (audio decode → audio worker / RT thread)
- Atomic flags (state, pause, volume)
- No cross-thread signal dispatch (signal router is single-threaded)

**Thread-safety contracts:**
- `zcsr_surface_acquire/release`: main thread only, not thread-safe
- `zcsr_mixer`: audio worker thread owned by audiomix
- `zcsr_music`: decode thread owned by zcsr_music
- `on_process` callback (PipeWire): RT-safe only (no malloc, no blocking lock, no syscall)

### Surface Pool Thread Contract

`zcsr_surfaces[4]` in `platform_common.c` is a static fixed pool owned exclusively by the engine main thread. `zcsr_surface_acquire()` and `zcsr_surface_release()` must only be called from the main thread. The current implementation provides no internal lock and does not claim thread safety.

Surface creation, destruction, and lifecycle-related operations must follow the platform API's calling-thread contract. Callers must not invoke acquire/release from worker threads, audio threads, system callback threads, or any other thread, unless a future version explicitly extends and verifies the relevant threading contract.

If multi-threaded surface creation or destruction is needed in the future, the fixed pool's concurrency ownership and lifecycle management must first be redesigned, then mutex synchronization added for slot acquire/release and related state transitions. Pool capacity, cross-thread release, init-failure cleanup, and concurrent access during destruction must also be re-evaluated. Adding atomic operations to the `in_use` flag alone does not constitute a multi-thread-safe refactor.

The fixed pool capacity is currently 4. On exhaustion, acquire must fail explicitly; it must not grow via heap allocation. Any change to the threading contract or pool capacity must update the API documentation and add corresponding concurrency and capacity tests.

### Zero-Heap and Threading Are Orthogonal

- **Zero-heap**: memory allocation strategy. zcsr source code never calls `malloc`/`calloc`/`realloc`/`free`. All state lives in caller-provided buffers or fixed module-level pools.
- **Threading**: execution model. zcsr uses dedicated worker threads for async I/O (audio, decode) plus a single-threaded game loop. No thread pool.

Fixed memory buffers are used by both main thread and worker threads. Being "fixed memory" does **not** mean "single-threaded", and being "multi-threaded" does **not** require heap allocation.

### Zero-Heap Exception Boundary

zcsr's own source code must not directly call `malloc`/`calloc`/`realloc`/`free`, nor indirectly execute such allocation through custom wrapper functions. Exceptions are limited to:
- Allocations performed internally by system or third-party libraries in their API implementations
- Native resources created and managed by OS APIs

Callers should use the creation, destruction, and lifecycle management interfaces provided by the respective API. This exception is not expanded by code location (platform, backend, integration, or system-library adaptation layer). Private state structs, ring buffers, scratch areas, and other zcsr-owned data stores created to adapt system libraries must still use caller-provided buffers, module-level fixed storage, or pre-declared-capacity static pools — not heap allocation. Passing a zcsr-allocated pointer to a system library does not change the allocation's ownership. Using objects created by system-library APIs or buffers managed by system libraries is not considered direct zcsr heap allocation.

Code review and CI must check for direct allocation calls and indirect allocation wrappers in zcsr's own source code; they must not exempt based on file path, module name, or "system adaptation layer" labels.
