# third_party

Vendored single-header dependencies (no package manager; keeps the project dependency-light).

- **`stb_image.h`** — stb_image v2.30, source http://nothings.org/stb (repo:
  https://github.com/nothings/stb), dual-licensed MIT OR Public Domain
  (Unlicense, see end of the header). Used by `modules/image` for PNG / JPEG /
  BMP / GIF / HDR decode to RGBA32. The module defines
  `#define STB_IMAGE_IMPLEMENTATION` in exactly one of its own TUs and
  configures `STBI_MALLOC`/`STBI_FREE` to bump-allocate from the caller's
  buffer. The T0 image line switches off the removed spectator containers at
  the stb compile boundary: `STBI_NO_TGA` / `STBI_NO_PSD` / `STBI_NO_PIC` /
  `STBI_NO_PNM` are defined at the include site in `modules/image/image.c`.
  The HDR Radiance RGBE reader and the float `stbi_loadf_*` interface stay
  enabled (`STBI_NO_HDR` / `STBI_NO_LINEAR` are not defined); the HDR route
  tonemaps float pixels to RGBA8 in the module.
- **FFmpeg BtbN `n8.1` LGPL shared** (prebuilt, not vendored sources) — AAC/M4A/Opus music
  backend, the Linux video fallback and the WebM/VP9 backend. Prebuilt layout:
  `third_party/prebuilt/win64/ffmpeg/{include,lib,bin}` on Windows and
  `third_party/prebuilt/linux64/ffmpeg/` on Linux; the CMake root exposes it via
  `ZCSR_FFMPEG_ROOT`. Must be an **LGPL shared** build: no `--enable-gpl`, no
  `--enable-nonfree`, no x264/x265/xvid/fdk-aac. The probe requires both the
  headers and the five libraries (avcodec/avformat/avutil/swscale/swresample);
  a GPL/nonfree build is detected at configure time and the backend is disabled.
  Missing prebuilt compiles these routes fail-closed and the build does not fail.

- **libwebp 1.4.0** (prebuilt, not vendored sources) — WebP static and animated
  decode. Static images use `WebPDecodeRGBAInto`; animation uses the
  `WebPAnimDecoder` demux API. Prebuilt layout:
  `third_party/prebuilt/win64/libwebp/{include,lib}` on Windows and
  `third_party/prebuilt/linux64/libwebp/` on Linux; the CMake root exposes it
  via `ZCSR_LIBWEBP_ROOT`. Headers are included under a local
  `#pragma warning(push/disable ...)` + pop. When the prebuilt is missing the
  image module compiles without the WebP route and reports WebP caps false;
  the build does not fail.

- **`glad/`** — minimal vendored OpenGL 3.3 Core loader surface, used only by `modules/glrender`.
  It exposes the subset of GL entrypoints the fixed renderer needs, keeps the
  single-header + single-source boundary, and is compiled into the glrender module only on backends
  that implement OpenGL loading. No other module may include it.

- **`stb_vorbis.c`** — stb_vorbis v1.22, Ogg Vorbis decoder, dual-licensed MIT OR
  Public Domain (full text at the end of the file). Used by `modules/music` for
  OGG playback; the implementation lives in `modules/music/vorbis_impl.c`, with
  a header-only view for the module's declarations.

- **`minimp3.h`** — minimp3, MP3 decoder, dedicated to the public domain (CC0-style
  dedication at the top of the header). Used by `modules/music` for MP3 playback;
  the implementation lives in `modules/music/minimp3_impl.c`.

- **`stb_truetype.h`** — stb_truetype, TrueType/OpenType rasterizer, dual-licensed
  MIT OR Public Domain (full text at the end of the header). Used by
  `modules/font`; the implementation lives in `modules/font/stb_truetype_impl.c`.
