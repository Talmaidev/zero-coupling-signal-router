# Engine Extension — Game-Usable Core

> Historical architecture note (2026-05). Retained for design evolution. It records
> the additive engine-extension rationale and capability boundaries; module names and
> implementation status are from that snapshot, not the current 1.0 tree.

Additive extension of the Zero-Coupling Signal Router + Game Core toward a lean,
GPU-capable SDL2-replacement core. New modules talk ONLY through their
`include/zcsr/*` contracts — no cross-module calls. Each platform calls native APIs
directly. Only new third-party dependency: **glad** (OpenGL 3.3 loader, vendored
under `third_party/glad`, used only by `modules/glrender`). **No runtime heap.**
No code-line limit; correctness + zero-coupling first.

## Implementation boundary

The extension is additive at the product/API level, but later engine work may uncover
that an earlier module must be adjusted to coexist with the new module. In that case,
the relevant contract header and implementation source are treated as one functional
unit. A small behavior-preserving fix to an earlier module is allowed when it is
required to complete the same feature and it:

- preserves the public contract and accepted behavior;
- remains zero-coupled and no-heap;
- is documented as a compatibility/coexistence fix;
- passes the full CTest suite and coupling guard.

Example: Linux keyboard input must not consume X11 pointer events needed by
`inputext`; the fix is to make `input` select events additively and drain only
key/close events.

## Module / contract map (2026-05 snapshot)

| Module (then) | Contract | Scope |
|---|---|---|
| `modules/glrender` | `zcsr/gl_render.h` | GL 3.3 context on the surface; batch sprites (200–10000) with pos/rot/scale/tint/alpha; RGBA32 textures; nearest/linear filter; vsync; single FBO render-to-texture; built-in texture + solid shaders; minimal bitmap text. Vendors glad. |
| `modules/audiomix` | `zcsr/audio_mix.h` | 16-channel mixer; dedicated audio thread + lock-free ring buffer; Win WASAPI / Linux ALSA / macOS AudioUnit; WAV 16-bit 44100 mono/stereo; linear resample only. Coexists with single-stream `audio.h`. |
| `modules/inputext` | `zcsr/input_ext.h` | Mouse (move/3-button/wheel) + gamepad (≤2, Xbox layout); Win RawInput+XInput / Linux X11+evdev / macOS IOKit HID + GameController. Additive to keyboard `input.h`. |
| `modules/image` | `zcsr/image.h` (+ `zcsr/color.h`) | Decode PNG/JPEG/BMP/TGA/GIF/PSD/PIC → RGBA32 + offline CPU processing (chroma-key transparency, RGBA variant recolor, 256-entry palette remap). Separated from rendering: produces pixel buffers only; glrender/render consume the RGBA32. Vendors stb_image (used only here). No-heap: decodes/processes into a caller buffer. |
| Discipline | — | No-heap: each module sizes fixed pools to the 200–10000 ceiling (GL CPU staging, audio buffers, input queues); GPU memory left to driver. Threading per-subsystem (NO general pool): audio = 1 dedicated thread; GL = render thread (context is thread-affine); input = main-loop poll. Optional small worker pool only if async asset loading is later needed. |
| `tests/` + `integration/` | — | CTest per module (logic) + benchmarks (200–10000 sprite frame time; audio latency ≤25 ms; input ≤1 frame). |

## Rendering/resource capability requirements

The engine extension grows toward SDL2-level practical rendering/resource basics while
keeping the zero-coupling/no-heap model:

- **Multiple file formats**: expose a resource-loading entry point that can route PNG
  now and add BMP/TGA/JPEG or other formats later behind the renderer/resource
  contracts. Format decoders must be vendored or implemented in fixed buffers; no
  runtime heap.
- **Offline image processing**: support build/load-time CPU processing for chroma-key
  removal, alpha generation, and variant texture generation. The result is cached as
  ordinary static textures before gameplay.
- **Hardware texture modulation**: support runtime color modulation in the GPU path
  for common effects such as combat flash red, frozen blue tint, damage fade, and
  alpha pulse. This should stay shader/uniform/vertex-color driven where possible.
- **Optional palette mapping**: for more complex dynamic recolors (for example hair
  color swaps), CPU palette remap may run once into a fixed staging/cache texture,
  then upload/cache that result as a normal static texture.
- **Software path coexistence**: the existing software/overlay path remains available
  for UI and fallback; the GL renderer is optional and must not break it.

These requirements are contracted by `image.h` / `color.h` plus the
`zcsr_gl_sprite` modulation fields:

- Multi-format decode + offline processing → `modules/image` / `zcsr/image.h`
  (`zcsr_image_decode`, `zcsr_image_chroma_key`, `zcsr_image_make_variant`,
  `zcsr_image_palette_remap`). Separated from rendering; emits RGBA32 that either
  render path uploads.
- Runtime hardware modulation → additive fields on `zcsr_gl_sprite`
  (`zcsr/gl_render.h`): a single RGBA color (no per-channel control) applied per
  `zcsr_color_mode` (`zcsr/color.h`) — `MULTIPLY` (legacy behavior, default), `MIX`
  (tint toward color, e.g. freeze-blue), `ADD` (additive, e.g. flash-red), with an
  `amount` blend strength. A zero-initialized sprite is byte-identical to today.

## Zero-coupling & validation

- Each module includes ONLY `zcsr/*` contracts (+ its vendored single-header dep).
  Enforced by `tools/check_no_cross_includes.sh` (scans `.c`/`.m`, third_party exempt).
- CMake needs no extra `find_package` beyond system GL/audio/input libs per backend.
- **Real-hardware acceptance** (headless verifies logic only): GPU needs a display,
  audio needs a sound device, gamepad needs a controller.

## Evolution note

This snapshot records the additive design and capability contracts. The current 1.0
tree keeps the same zero-coupling/no-heap boundary; specific module names, test
counts and implementation status are indexed in `docs/ARCHITECTURE.md`,
`docs/API_REFERENCE.md` and `docs/TESTING.md`.
