# zcsr Android port

## Implemented renderer paths

- EGL + GLES 3.x clear-frame.
- Generated RGBA texture upload and regular sprite batch.
- Atlas UV region sampling.
- Instanced sprite batch.
- 8x8 bitmap font text path.

## Renderer lifecycle

`APP_CMD_INIT_WINDOW`:
1. attach ANativeWindow via platform API
2. create renderer + EGL
3. create/upload texture, render current scene

`APP_CMD_TERM_WINDOW`:
1. destroy texture + renderer while EGL valid
2. detach ANativeWindow

## Constraints

- Public `zcsr/gl_render.h` API unchanged for this port.
- No PNG/image/stb_image.
- No audio/input in this port.
- Renderer state in caller buffer.
- Test textures generated programmatically.
- APK packaging via `tools/android_pack.sh`.

## Verified on device (Redmi K60 Pro, socrates)

- APK built under `build/android/` (versionCode from AndroidManifest.xml).
- Device GLES: `OpenGL ES 3.2` (EGL 1.5), window size `1440x3200`.
- Scenes rotate every ~2 s: batch -> atlas -> instanced -> batch.
- All draw paths PASS in logcat, no GL errors, no zcsr crash.
- Window teardown/recreation (HOME -> resume) re-initializes EGL and repaints.

## Test log excerpts

```
zcsr-gl : EGL 1.5; GLES=OpenGL ES 3.2 V@...; size=1440x3200
zcsr    : texture=2 upload=PASS filter=NEAREST
zcsr    : sprite batch PASS: 200 grid + 2 center sprites
zcsr    : atlas PASS: four UV regions sampled
zcsr    : instanced PASS: 202 instances submitted
zcsr    : text PASS: 8x8 bitmap font rendered (6 lines)
```
