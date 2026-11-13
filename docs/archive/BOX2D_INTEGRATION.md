# Box2D v3.1.1 Optional Integration

Zero-coupling by design: **no zcsr module knows physics exists**. Box2D is an
external dependency fetched only when explicitly enabled, and only the
integration-level `demo_physics` links against it.

## Build

```bash
# Default build — Box2D is NOT fetched, NOT linked, NOT built.
cmake -S . -B build
cmake --build build

# Physics-enabled build — fetches Box2D v3.1.1 and builds demo_physics.
cmake -S . -B build-box2d -DZCSR_ENABLE_BOX2D=ON
cmake --build build-box2d
./build-box2d/demo_physics
```

## What gets added

| File | Purpose |
|---|---|
| `CMakeLists.txt` | `ZCSR_ENABLE_BOX2D` option (default OFF) + `FetchContent` for Box2D v3.1.1 + `demo_physics` target (desktop only) |
| `integration/demo_physics.c` | Interactive demo: falling boxes, static ground, contact events |
| `docs/BOX2D_INTEGRATION.md` | This document |

## What does NOT change

- All 15 zcsr modules are untouched.
- No public contract header gains a physics API.
- Default build output is identical (no Box2D symbols, no fetch).
- Android build is unaffected (`demo_physics` is excluded on Android).

## Coordinate mapping

Box2D uses meters with y-up. The demo maps `1 m = 50 px` and flips y so that
Box2D y=0 corresponds to the top of the window.

| Concept | Box2D | Screen |
|---|---|---|
| Ground | y=11.5 m, half-height 0.5 m | bottom 25 px strip |
| Box | 0.8 m × 0.8 m | 40 × 40 px |
| Gravity | (0, 9.8) m/s² y-down | — |
| Spawn | (8, 2) m with horizontal jitter | center-top area |

## Box2D v3 API notes (verified against v3.1.1)

- `b2ShapeDef` nests material in `b2SurfaceMaterial material` — use
  `shape.material.friction`, not `shape.friction`.
- `b2BodyDef` has `name` (const char*, debug only) and `fixedRotation` (bool).
- Validity checks: `b2World_IsValid`, `b2Body_IsValid`, `b2Shape_IsValid`.
- Position: `b2Body_GetPosition(b2BodyId)` returns `b2Vec2` by value.
- Contact events: `b2World_GetContactEvents(b2WorldId)` returns
  `b2ContactEvents` with `beginCount` / `beginEvents[]`.

## Demo controls

- Runs ~15 seconds or until any key is pressed.
- A new box spawns every 0.5 seconds (max 64).
- Contact begin events print `[Box2D] N contact(s) -> PHYSICS_CONTACT emitted`.
- Each contact emits a `PHYSICS_CONTACT` signal routed via `ZCSR_DEFINE_ROUTER`
  to `zcsr_mix_slot_play`, playing a 300ms 440Hz beep (`integration/click.wav`).

## Integration verification (2026-10-19)

| Item | Status |
|---|---|
| Windows default build (Box2D OFF) | ctest 18/18 pass, coupling guard PASS |
| Windows Box2D ON build | `demo_physics` compiles and runs |
| Window + rendering | 800x600 window, ground + falling boxes render correctly |
| Contact events | `enableContactEvents=true`, begin events detected and printed |
| Signal -> audio | `PHYSICS_CONTACT` -> `zcsr_mix_slot_play("click")`, beep audible on collision |
| Android NDK | `libzcsr_phase0.so` compiles; `demo_physics` excluded from Android |
| waveOut fallback | WASAPI `Activate(IID_IAudioClient)` fails on FxSound systems; waveOut backend auto-selected, skips FxSound virtual device |
