# Classic 2D Game Core — Historical Architecture Notes

> Historical architecture note (2026-05). Retained for design evolution. The module
> names below are the snapshot names from that date; later work split `render` into
> `image` + `glrender` + `sprite`, and `gamelogic` into `collision` + `save`.
> This file is not the current implementation contract.

Extends the zero-coupling `zcsr` foundation into a minimal 2D game core. Same rules:
modules include **only** `include/zcsr/*` contracts (never each other); no heap
(fixed/caller buffers); `tools/check_no_cross_includes.sh` enforces it. C11.

## Module / contract map (2026-05 snapshot)

| Module (then) | Contracts | Responsibility |
|---|---|---|
| `modules/input` | `clock.h`, `input.h`, `window_event.h` | keyboard down/up, window close + cleanup, frame timer |
| `modules/audio` | `audio.h` | WAV player in one dedicated thread; **signals only** (`PLAY_SOUND`/`STOP_SOUND`) |
| `modules/render` | `texture.h`, `sprite.h` | PNG via stb_image + resource cache; batch sprite renderer |
| `modules/gamelogic` | `collision.h`, `save.h` | AABB collision; binary save/load (header + checksum) |
| `integration/tests` | — | CTest coverage, sprite benchmark, zero-coupling guard |

Contract work, validation and review were separate from backend implementation; this
division is historical process information, not a current ownership map.

## Key invariants

- Audio talks to the rest of the game ONLY through the **signal router**
  (`signal.h`): wire `PLAY_SOUND` → `zcsr_audio_slot_play`, `STOP_SOUND` →
  `zcsr_audio_slot_stop`. No direct calls from game logic into audio.
- Rendering reuses the platform **surface** (`platform.h`) — no new windowing.
- `stb_image.h` is vendored under `third_party/` and included **only** by the
  render/image path (configured with `STBI_MALLOC` to allocate from the cache
  buffer, no global heap growth).
- All cross-module communication is via contracts + signals; no module includes
  another module.
- Later extension work may make small behavior-preserving fixes to this core when
  required for coexistence with a new module. Such fixes must preserve the public
  contract, remain no-heap and zero-coupled, and be reviewed explicitly.

## Historical acceptance notes

- 200 sprites render under 1 ms/frame was the early target; this is a historical
  gate, not a current performance claim. The current performance baseline lives in
  the repository README.
- All CTests pass; coupling guard green; existing earlier modules unaffected.

## Evolution after this snapshot

- `modules/render` was retired and split: PNG/JPEG decode moved to `modules/image`,
  batching to `modules/glrender` + `modules/sprite`.
- `modules/gamelogic` was split into `modules/collision` and `modules/save`.
- The current module set, public headers and test counts are indexed in
  `docs/ARCHITECTURE.md`, `docs/API_REFERENCE.md` and `docs/TESTING.md`.
