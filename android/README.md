# zcsr Android port — Phase 1B / 1C

## Phase 1B / 1C

- **Phase 1B**: EGL + GLES 3.x, pure-color clear-frame.
- **Phase 1C.0**: generated RGBA texture, upload, regular sprite batch.
- **Phase 1C.1**: atlas UV region sampling.
- **Phase 1C.2**: instanced sprite batch.

## Renderer lifecycle

`APP_CMD_INIT_WINDOW`:
1. attach ANativeWindow via platform API
2. create renderer + EGL
3. create/upload texture, render current scene

`APP_CMD_TERM_WINDOW`:
1. destroy texture + renderer while EGL valid
2. detach ANativeWindow

## Constraints

- Public `zcsr/gl_render.h` API unchanged in 1C.
- No PNG/image/stb_image.
- No audio/input in 1C.
- Renderer state in caller buffer.
- Test textures generated programmatically.
- APK packaging via `tools/android_pack.sh`.

## Verified on device (Redmi K60 Pro, socrates)

- APK: `build/android/zcsr_phase1c.apk` (versionCode from AndroidManifest.xml).
- Device GLES: `OpenGL ES 3.2` (EGL 1.5), window size `1440x3200`.
- Scenes rotate every ~2 s: batch -> atlas -> instanced -> batch.
- All three draw paths PASS in logcat, no GL errors, no zcsr crash.
- Window teardown/recreation (HOME -> resume) re-initializes EGL and repaints.

## Test log excerpts

```
zcsr-gl : EGL 1.5; GLES=OpenGL ES 3.2 V@...; size=1440x3200
zcsr    : 1C.0 texture=2 upload=PASS filter=NEAREST
zcsr    : 1C.0 sprite batch PASS: 200 grid + 2 center sprites
zcsr    : 1C.1 atlas PASS: four UV regions sampled
zcsr    : 1C.2 instanced PASS: 202 instances submitted
```

## Phase 1 (complete)

See PHASE1.md for the full handover record.
Baseline commit: b541845.
