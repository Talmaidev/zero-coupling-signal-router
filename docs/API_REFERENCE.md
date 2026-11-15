# ZCSR API Reference

The public API is defined by the headers in `include/zcsr/`. There are 62 headers; each module implements the functions declared in its own contract header.

The headers are authoritative. This page is an index, not a second copy of the prototypes.

## Contract groups

| Area | Headers |
|---|---|
| Core | `arena.h`, `state_buffer.h`, `value.h`, `log.h`, `diagnostics.h` |
| Signal / state machine | `signal.h`, `hsm.h` |
| Platform / input | `platform.h`, `window_event.h`, `input.h`, `input_ext.h`, `input_ext_android.h`, `clock.h` |
| Rendering | `gl_render.h`, `gl_render_l2d_geo.h`, `gl_render_light2d_ext.h`, `texture.h`, `texture_atlas.h`, `sprite.h`, `color.h`, `transform.h`, `camera.h`, `camera3d.h`, `viewport.h` |
| Light2D | `light2d.h`, `light2d_floor.h`, `light2d_wall.h`, `light2d_box.h`, `light2d_light.h` |
| Audio | `audio.h`, `audio_mix.h`, `music.h` |
| Assets / imaging | `assets.h`, `resource.h`, `file_service.h`, `image.h`, `font.h`, `font_atlas.h` |
| Game / runtime data | `collision.h`, `save.h`, `culling.h` |
| UI | `ui.h`, `ui_event.h`, `ui_types.h`, `ui_node.h`, `ui_panel.h`, `ui_button.h`, `ui_toggle.h`, `ui_slider.h`, `ui_dropdown.h`, `ui_label.h`, `ui_separator.h`, `ui_image.h`, `ui_text_input.h`, `ui_scroll_view.h`, `ui_input.h`, `ui_draw.h`, `ui_draw_v2.h`, `ui_gl_backend.h`, `ui_gl_render_v2.h`, `ui_i18n.h`, `ui_preset.h` |

## Rules

- A module may include only `include/zcsr/*.h`; it must not include another module's source or a relative header.
- Public handles are opaque. Callers use create/destroy and the functions declared in the matching header.
- Memory is caller-provided or fixed-pool storage; the engine's own code does not allocate with `malloc/free`.
- CMake target names match the module directories under `modules/`.

The previous generated reference is in `docs/archive/API_REFERENCE_2026-10-19.md`. It is historical and does not describe the current target set.
