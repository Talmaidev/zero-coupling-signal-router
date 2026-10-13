# API Reference — Zero-Coupling UI Signal Router

Auto-generated from the contract headers in `include/zcsr/` (the single source of truth).
Every module includes only these contract headers, never another module's source. Each
`zcsr_foo` is an **opaque handle** + C prototypes = the stable ABI. Regenerate after contract changes.

Contract header count: 22. Module count: 15. Test target count: 17.


## Foundation (signal router)

### `zcsr/value.h`

Zero-Coupling Signal Router — shared value type (Agent 1 data primitive). ONLY string / int / bool, per spec. No heap. C11/C17.

*(types/enums only — see header)*

### `zcsr/arena.h`

Agent 1 — stack-only memory manager (no runtime/heap allocation). Implementer: modules/core. CONTRACT ONLY (opaque handle). Modules include this header, never another module's source.

| Function | Notes |
|---|---|
| `zcsr_arena* zcsr_arena_init(void* buffer, size_t bytes)` |  |
| `void* zcsr_arena_alloc(zcsr_arena*, size_t bytes, size_t align)` | NULL if exhausted |
| `void zcsr_arena_reset(zcsr_arena*)` | free all (LIFO) |
| `size_t zcsr_arena_used(const zcsr_arena*)` |  |
| `size_t zcsr_arena_capacity(const zcsr_arena*)` |  |

### `zcsr/state_buffer.h`

Agent 1 — read-only game state buffer (string/int/bool). Implementer: modules/core. Reads take `const zcsr_state*`; only the privileged writer mutates. No heap growth.

| Function | Notes |
|---|---|
| `zcsr_state* zcsr_state_init(void* buffer, size_t bytes)` |  |
| `zcsr_value zcsr_state_get(const zcsr_state*, const char* key)` |  |
| `bool zcsr_state_has(const zcsr_state*, const char* key)` |  |
| `bool zcsr_state_set(zcsr_state*, const char* key, zcsr_value v)` |  |

### `zcsr/signal.h`

Agent 2 — compile-time signal/slot. No heap, no dynamic registry. Implementer: modules/signal. Spec signatures: `void signal(string)` / `bool slot(string)`. In C, "compile-time / zero runtime overhead" means: connections live in a `static const` table built at compile time (ideally via an X-macro), and dispatch is a fixed walk over that table — no malloc, no runtime registration.

| Function | Notes |
|---|---|
| `void zcsr_emit(const zcsr_conn* table, size_t count, const char* signal, const char* payload)` |  |

### `zcsr/hsm.h`

Agent 3 — Hierarchical State Machine. No global state, no heap. Transitions via signals only. Persists current state to the Core state buffer (key "hsm.state"). Implementer: modules/hsm. Config is caller-owned const tables (compile-time, zero-coupling): states form a tree via `parent` names; a transition fires when the current state — or any ANCESTOR of it — has a matching (from, signal) entry.

| Function | Notes |
|---|---|
| `zcsr_hsm* zcsr_hsm_create(zcsr_state* persist_to, const zcsr_hsm_state* states, size_t state_count, const zcsr_hsm_transition* transitions, size_t transition_count, const char* initial)` |  |
| `void zcsr_hsm_destroy(zcsr_hsm*)` |  |
| `bool zcsr_hsm_dispatch(zcsr_hsm*, const char* signal, const char* payload)` |  |
| `const char* zcsr_hsm_current(const zcsr_hsm*)` |  |

### `zcsr/platform.h`

Agent 5 — Platform abstraction. Win32 / X11 / Cocoa, auto-selected at compile time. NO OpenGL/DirectX/Vulkan. Exactly one backend is selected by the compile-time gates. Implementer: modules/platform.

| Function | Notes |
|---|---|
| `zcsr_surface* zcsr_surface_create(const char* title, zcsr_rect bounds)` |  |
| `void zcsr_surface_destroy(zcsr_surface*)` |  |
| `void* zcsr_surface_native_handle(const zcsr_surface*)` | HWND / Window / NSWindow* |
| `void* zcsr_surface_native_display(const zcsr_surface*)` |  |
| `void zcsr_surface_pump_events(zcsr_surface*)` |  |

### `zcsr/overlay.h`

Agent 4 — Native overlay window. Borderless, transparent, always-on-top. Renders ONLY: text, one bitmap image, up to 3 buttons. GDI/X11/Cocoa only. Target <1ms/frame. Implementer: modules/overlay.

| Function | Notes |
|---|---|
| `zcsr_overlay* zcsr_overlay_create(zcsr_surface*)` |  |
| `void zcsr_overlay_destroy(zcsr_overlay*)` |  |
| `void zcsr_overlay_set_text(zcsr_overlay*, const char* utf8)` |  |
| `void zcsr_overlay_set_bitmap(zcsr_overlay*, const unsigned char* rgba, int w, int h)` |  |
| `void zcsr_overlay_set_buttons(zcsr_overlay*, const zcsr_button* buttons, int count)` | <=3 |
| `void zcsr_overlay_render(zcsr_overlay*)` | single frame, target <1ms |
| `bool zcsr_overlay_on_pointer(zcsr_overlay*, int x, int y, bool clicked)` |  |


## Classic 2D game core

### `zcsr/clock.h`

Agent 1 (Game Core) — high-precision clock + fixed-step frame pacing (~60 FPS). Implementer: modules/input. No heap (timer placed in caller buffer).

| Function | Notes |
|---|---|
| `uint64_t zcsr_clock_now_ns(void)` |  |
| `zcsr_frame_timer* zcsr_frame_timer_init(void* buffer, size_t bytes, uint32_t target_fps)` |  |
| `bool zcsr_frame_timer_tick(zcsr_frame_timer*)` |  |
| `uint64_t zcsr_frame_timer_delta_ns(const zcsr_frame_timer*)` |  |

### `zcsr/input.h`

Agent 1 (Game Core) — keyboard input (key down / key up). Implementer: modules/input. Events are buffered in a fixed ring (no heap). Pumped from the platform window.

| Function | Notes |
|---|---|
| `zcsr_input* zcsr_input_init(void* buffer, size_t bytes)` |  |
| `void zcsr_input_pump(zcsr_input*, zcsr_surface* window)` |  |
| `bool zcsr_input_poll(zcsr_input*, zcsr_key_event* out)` |  |
| `bool zcsr_input_is_down(const zcsr_input*, zcsr_keycode)` |  |

### `zcsr/audio.h`

Agent 2 (Game Core) — WAV audio player. Runs in ONE dedicated background thread. Communicates ONLY via signals: "PLAY_SOUND" / "STOP_SOUND" (payload = registered sound id). Implementer: modules/audio. Wires into the zcsr/signal.h router.

| Function | Notes |
|---|---|
| `zcsr_audio* zcsr_audio_start(void)` | spawns the dedicated audio thread |
| `void zcsr_audio_stop(zcsr_audio*)` | stops playback + joins the thread |
| `bool zcsr_audio_register_wav(zcsr_audio*, const char* id, const char* wav_path)` |  |
| `bool zcsr_audio_slot_play(const char* payload)` | connect to "PLAY_SOUND" |
| `bool zcsr_audio_slot_stop(const char* payload)` | connect to "STOP_SOUND" |

### `zcsr/sprite.h`

Agent 3 (Game Core) — batch sprite renderer. Submit many sprites, draw them in one flush. Target: render n*10000 (n>0) sprites within the frame budget; bench gate = 200 sprites <1ms. Implementer: modules/render. Draws onto the platform surface.

| Function | Notes |
|---|---|
| `zcsr_sprite_batch* zcsr_sprite_batch_init(void* buffer, size_t bytes, zcsr_surface* target)` |  |
| `void zcsr_sprite_batch_begin(zcsr_sprite_batch*)` |  |
| `void zcsr_sprite_batch_submit(zcsr_sprite_batch*, const zcsr_sprite*)` |  |
| `void zcsr_sprite_batch_flush(zcsr_sprite_batch*)` | draw all submitted in one batch |

### `zcsr/texture.h`

Agent 3 (Game Core) — PNG loading (stb_image, single-header) + resource cache (dedupe). Implementer: modules/render. Cache lives in a caller buffer; stb_image is configured to allocate from that buffer (STBI_MALLOC), so no global heap growth.

| Function | Notes |
|---|---|
| `zcsr_texture_cache* zcsr_texture_cache_init(void* buffer, size_t bytes)` |  |
| `const zcsr_texture* zcsr_texture_load(zcsr_texture_cache*, const char* png_path)` |  |

### `zcsr/texture_atlas.h`

Engine-ext — texture atlas shelf-packing + UV region table. Pure logic, no GL. Implementer: modules/atlas. The caller owns all memory: the region table and any packing scratch.

| Function | Notes |
|---|---|
| `zcsr_atlas_region_t zcsr_atlas_lookup(const zcsr_atlas_table_t*, uint32_t texture_id)` | pure query, no GL |

*(additional packing helpers may be present — see header)*

### `zcsr/collision.h`

Agent 4 (Game Core) — AABB collision. Pure functions, no state, no heap. Implementer: modules/gamelogic.

| Function | Notes |
|---|---|
| `bool zcsr_aabb_intersect(zcsr_aabb a, zcsr_aabb b)` |  |
| `bool zcsr_aabb_contains_point(zcsr_aabb a, float px, float py)` |  |

### `zcsr/save.h`

Agent 4 (Game Core) — binary save/load of game state. No heap, no file I/O in the contract (caller persists the bytes). Writes a small header (magic + version + length + checksum) so loads can be validated. Implementer: modules/gamelogic.

*(types/enums only — see header)*

### `zcsr/window_event.h`

Agent 1 (Game Core) — window-level events (close request) + clean shutdown. Ties to the platform surface. Implementer: modules/input (or platform).

| Function | Notes |
|---|---|
| `bool zcsr_window_close_requested(zcsr_surface*)` |  |


## Game-usable engine extension

### `zcsr/gl_render.h`

Engine-ext Agent 1 — GPU rendering (OpenGL 3.3 Core). OPTIONAL renderer, beside the existing software/overlay path (which stays unchanged). Implementer: modules/glrender (vendors glad; glad used ONLY inside that module). Includes only shared contracts. No runtime heap: renderer state lives in a caller buffer; GPU memory is the driver's.

**Legacy sprite path (per-sprite vertex batch):**

| Function | Notes |
|---|---|
| `zcsr_gl_renderer* zcsr_gl_create(zcsr_surface* surface, void* buffer, size_t bytes)` |  |
| `void zcsr_gl_destroy(zcsr_gl_renderer*)` |  |
| `void zcsr_gl_set_vsync(zcsr_gl_renderer*, bool on)` |  |
| `void zcsr_gl_present(zcsr_gl_renderer*)` | swap front/back buffers |
| `zcsr_gl_texture zcsr_gl_texture_create(zcsr_gl_renderer*, const uint8_t* rgba, int w, int h)` |  |
| `bool zcsr_gl_texture_upload(zcsr_gl_renderer*, zcsr_gl_texture, const uint8_t* rgba, int w, int h)` |  |
| `void zcsr_gl_texture_filter(zcsr_gl_renderer*, zcsr_gl_texture, bool linear)` |  |
| `void zcsr_gl_texture_destroy(zcsr_gl_renderer*, zcsr_gl_texture)` |  |
| `void zcsr_gl_begin(zcsr_gl_renderer*)` |  |
| `void zcsr_gl_submit(zcsr_gl_renderer*, const zcsr_gl_sprite*)` |  |
| `void zcsr_gl_flush(zcsr_gl_renderer*)` |  |
| `void zcsr_gl_submit_batch(zcsr_gl_renderer*, const zcsr_gl_sprite* sprites, size_t count)` |  |
| `void zcsr_gl_set_target(zcsr_gl_renderer*, zcsr_gl_texture target_or_zero)` |  |
| `void zcsr_gl_draw_text(zcsr_gl_renderer*, const char* utf8, float x, float y, float scale, float r, float g, float b, float a)` | built-in bitmap font |

**Atlas sprite path:**

| Function | Notes |
|---|---|
| `void zcsr_gl_submit_atlas_batch(zcsr_gl_renderer*, const zcsr_gl_atlas_sprite* sprites, size_t count)` | all sprites sharing one atlas coalesce into one glDrawArrays |

**Instanced sprite path (Phase 3):**

`zcsr_gl_instance` struct = 68 bytes; the last 8 bytes (tex, z) are CPU-only metadata for sort, not read by the shader. GPU attrs occupy the first 60 bytes.

| Function | Notes |
|---|---|
| `void zcsr_gl_draw_instances(zcsr_gl_renderer*, zcsr_gl_texture texture, const zcsr_gl_instance* instances, size_t count)` | single glDrawElementsInstanced call; count clamped to ZCSR_GL_MAX_INSTANCES |
| `size_t zcsr_sort_instances(zcsr_gl_instance* instances, size_t count, void* temp, size_t temp_bytes)` | LSD radix by tex; temp must be >= count * sizeof(zcsr_gl_instance) |
| `void zcsr_instance_run(const zcsr_gl_instance* sorted, size_t count, size_t run_index, zcsr_gl_texture* out_texture, size_t* out_start, size_t* out_len)` |  |
| `void zcsr_gl_submit_instance_batch(zcsr_gl_renderer*, zcsr_gl_instance* instances, size_t count, void* temp, size_t temp_bytes)` | sort + draw in texture runs |

**Full state sort (opaque batching + translucent painter order):**

Opaque instances (a >= 1 and mode not MIX/ADD) are sorted by tex for batching. Translucent instances (a < 1 or mode MIX/ADD) are sorted back-to-front by z (larger z = farther). The radix key transform on `float z` is monotonic for IEEE-754; NaN is undefined by contract.

| Function | Notes |
|---|---|
| `size_t zcsr_sort_state(zcsr_gl_instance* instances, size_t count, void* temp, size_t temp_bytes, size_t* out_opaque)` | returns opaque count; temp >= count * sizeof(instance) |
| `void zcsr_gl_submit_state(zcsr_gl_renderer*, zcsr_gl_instance* instances, size_t count, void* temp, size_t temp_bytes)` | sort + draw + present when targeting the window |

### `zcsr/culling.h`

Engine-ext — viewport culling for 2D AABBs. Pure logic, no GL, no heap. Implementer: modules/culling. The caller owns all memory: the box array, the grid scratch, and the output index list.

Two strategies:
- **linear**: test every box against the view (O(n), cache-friendly).
- **uniform grid**: hash boxes into fixed-size cells, then walk only the cells the view overlaps (fast when the view covers a small fraction of the world).

| Function | Notes |
|---|---|
| `int zcsr_aabb_in_view(const zcsr_aabb_t* box, const zcsr_view_t* view)` | 1 if intersecting (inclusive edges) |
| `size_t zcsr_cull_linear(const zcsr_aabb_t* boxes, size_t count, const zcsr_view_t* view, uint32_t* out_indices, size_t out_capacity)` | stops early at out_capacity |
| `size_t zcsr_grid_build(zcsr_grid_t* grid, const zcsr_aabb_t* boxes, size_t count)` | returns number of (box, cell) insertions |
| `size_t zcsr_grid_query(const zcsr_grid_t* grid, const zcsr_aabb_t* boxes, size_t box_count, const zcsr_view_t* view, uint32_t* out_indices, size_t out_capacity, uint8_t* seen)` | de-duplicated via seen[] (length box_count) |
| `float zcsr_view_coverage(const zcsr_grid_t* grid, const zcsr_view_t* view)` | view area / world area, 0..1 |

### `zcsr/audio_mix.h`

Engine-ext Agent 2 — multi-channel WAV mixer. NEW module; COEXISTS with the single-stream zcsr/audio.h (does not change/break it). 16 software channels, one dedicated audio thread, lock-free ring buffer. Backend: Win WASAPI / Linux ALSA / macOS AudioUnit. Implementer: modules/audiomix. Signals-friendly: audio_play / audio_finished. No runtime heap (fixed pools).

| Function | Notes |
|---|---|
| `zcsr_mixer* zcsr_mix_start(void)` | spawn audio thread + open output device. NULL on fail. |
| `void zcsr_mix_stop(zcsr_mixer*)` | stop playback + join thread + close device. |
| `bool zcsr_mix_register_wav(zcsr_mixer*, const char* id, const char* wav_path)` |  |
| `int zcsr_mix_play(zcsr_mixer*, const char* id, float volume, bool loop)` | returns channel 0..15, or -1 |
| `void zcsr_mix_stop_channel(zcsr_mixer*, int channel)` |  |
| `bool zcsr_mix_slot_play(const char* payload)` | connect to "PLAY_SOUND" |
| `bool zcsr_mix_slot_stop(const char* payload)` | connect to "STOP_SOUND" (by id) |
| `size_t zcsr_mix_poll_finished(zcsr_mixer*, int* out, size_t cap)` | drain finished channels |

### `zcsr/input_ext.h`

Engine-ext Agent 3 — mouse + gamepad input. ADDITIVE to zcsr/input.h (keyboard there is unchanged). Implementer: modules/inputext. Pumped from the platform surface once per frame. Backend: Win XInput+GetAsyncKeyState+window subclass (WM_MOUSEWHEEL) / Linux X11+evdev / macOS IOKit HID + GameController. No runtime heap: state lives in a caller buffer.

| Function | Notes |
|---|---|
| `zcsr_input_ext* zcsr_input_ext_init(void* buffer, size_t bytes)` | NULL if buffer too small |
| `void zcsr_input_ext_shutdown(zcsr_input_ext*)` |  |
| `void zcsr_input_ext_pump(zcsr_input_ext*, zcsr_surface*)` | call once per frame |
| `zcsr_mouse_state zcsr_input_ext_mouse(const zcsr_input_ext*)` |  |
| `zcsr_pad_state zcsr_input_ext_pad(const zcsr_input_ext*, int index)` | index 0..ZCSR_PAD_MAX-1 |

### `zcsr/image.h`

Engine-ext Agent — image loading, decoding, and offline CPU processing to 8-bit RGBA (RGBA32). SEPARATED from rendering (per project decision): this module ONLY produces pixel buffers. The GPU path (glrender) and the software path (render) CONSUME the RGBA32 it produces through their existing texture-create entry points. `image` never includes a renderer; renderers never include `image`. Implementer: modules/image (vendors stb_image, used ONLY here for multi-format decode). No runtime heap: decode and all processing write into a CALLER-PROVIDED buffer; stb_image is configured (STBI_MALLOC) to bump-allocate from that buffer.

| Function | Notes |
|---|---|
| `zcsr_image zcsr_image_decode(const void* encoded, size_t encoded_len, void* buffer, size_t bytes)` |  |

### `zcsr/color.h`

Engine-ext — shared color-modulation type for the GPU runtime path (glrender) and the offline image-processing path (image). Full-RGBA modulation only: a single RGBA color is applied per `mode`; there is NO per-channel control surface (by design). MULTIPLY is value 0 so a zero-initialized modulation reproduces the legacy `texel * color` behavior. MIX/ADD are needed because a pure multiply can only darken; "flash red" / "freeze blue" must lighten/tint toward a color.

*(types/enums only — see header)*