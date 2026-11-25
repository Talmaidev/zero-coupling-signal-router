# Media format support (T0)

This document is the T0 support contract. It has three layers only:

1. **T0（真支持）** — real encoded fixture, registered CTest, pixel / frame /
   PCM assertion, and `caps` / `supported` reporting true on every supported
   host.
2. **tested legacy** — a test exists and still passes, but the format is not
   part of the T0 promise and will not be extended.
3. **out of scope** — explicitly not implemented. These are deliberately not
   listed as a backlog; the public enums for them have been removed and the
   internal stb paths for them are closed.

Fixture hashes, generator commands, and per-format metadata live in
`tests/data/FIXTURES.md` and `tests/video_data/FIXTURES.md`.

## 1. T0（真支持）

### 1.1 Image

| Format | Container / codec | Backend | Fixture | Test target | Assertion type | License / risk |
|---|---|---|---|---|---|---|
| PNG | Portable Network Graphics | stb_image | `tests/data/t0.png` | `image_png` | Exact pixel compare (TL red / TR green / BL blue / BR white) | stb MIT; no known risk |
| JPEG | JFIF / Exif JPEG | stb_image | `tests/data/t0.jpg` | `image_jpeg` | Pixel compare, ±8 channel tolerance | stb MIT; lossy quantization |
| GIF (static) | GIF87a / GIF89a, first frame | stb_image | `tests/data/t0.gif` | `image_gif` | Pixel compare, ±8 tolerance after palette quantization | stb MIT; palette quantisation |
| GIF (animated) | GIF89a multi-frame | stb GIF frame loader | `tests/data/t0_anim.gif` | `image_gif_anim` | Two-frame sequence, per-frame pixels and frame delay | stb MIT; disposal compositing is loader-side |
| WebP (static) | lossless WebP | libwebp | `tests/data/t0.webp` | `image_webp` | Pixel compare, ±8 tolerance | libwebp/sharpyuv BSD; prebuilt required |
| WebP (animated) | animated WebP | libwebp demux | `tests/data/t0_anim.webp` | `image_webp_anim` | Two-frame sequence, per-frame pixels | libwebp BSD; disposal/blend by libwebp |
| BMP | Windows BMP (DIB) | stb_image | `tests/data/t0.bmp` | `image_bmp` | Exact pixel compare | stb MIT; no known risk |

### 1.2 Music

| Format | Container / codec | Backend | Fixture | Test target | Assertion type | License / risk |
|---|---|---|---|---|---|---|
| MP3 | MPEG-1/2 Layer III | minimp3 | `tests/data/t0.mp3` | `music_mp3` | Sample rate / channels; non-zero PCM; frame count is not reported by minimp3 | minimp3 permissive; frame count stays 0 by design |
| AAC | AAC ADTS | FFmpeg | `tests/data/t0.aac` | `music_aac` | 44100 Hz / 2 ch / 23552 frames; 7721 non-zero samples | FFmpeg LGPL; encoder priming and padding make duration slightly longer than 0.5 s |
| M4A | AAC-in-MP4 (ISO BMFF) | FFmpeg | `tests/data/t0.m4a` | `music_m4a` | 44100 Hz / 2 ch / 22050 frames; 8189 non-zero samples | FFmpeg LGPL; AudioSpecificConfig copied via `avcodec_parameters_to_context` |
| WAV | RIFF/WAVE PCM16 (IEEE float accepted) | Hand-written RIFF parser | `tests/data/t0.wav` | `music_wav` | 44100 Hz / 2 ch / 13230 frames; 52056 non-zero int16 samples | Own parser; compressed WAV (ADPCM / A-law / μ-law) rejected |
| FLAC | FLAC native | dr_flac | `tests/data/t0.flac` | `music_flac` | 44100 Hz / 2 ch / 22050 frames | dr_flac standalone; no known risk |
| OGG | Ogg Vorbis | stb_vorbis | `tests/data/t0.ogg` | `music_ogg` | 44100 Hz / 2 ch / 22050 frames | stb MIT; no known risk |
| Opus | Ogg Opus | FFmpeg | `tests/data/t0.opus` | `music_opus` | 48000 Hz / 2 ch / 24312 frames; 8970 non-zero samples | FFmpeg LGPL; decoder pre-skip leaves skipped samples at the start |

Lossy formats (MP3 / AAC / M4A / OGG / Opus) are asserted at the
non-zero + rate / channels / frames / duration level; they are not compared
sample-by-sample. WAV is the only music path with an exact PCM value check.

### 1.3 Video

| Format | Container / codec | Backend | Fixture | Test target | Assertion type | License / risk |
|---|---|---|---|---|---|---|
| MP4 | ISO BMFF, H.264 (AVC, yuv420p) | Media Foundation primary on Windows; FFmpeg fallback on Linux | `tests/video_data/t0_h264.mp4` | `video_mp4_h264` | 4 decoded frames; dominant-channel RGB at red / green / blue sample points; seek to frame 2; missing-file / fake-container / truncated failure paths | MF system component; FFmpeg LGPL fallback; YUV→RGB leaves ±10 channel deviation, asserted with a 30-channel margin |
| WebM | EBML WebM, VP9 (yuv420p) | FFmpeg | `tests/video_data/t0_vp9.webm` | `video_webm_vp9` | 4 drained frames; dominant-channel RGB; seek to frame 2; failure paths | FFmpeg LGPL; WebM container does not state `nb_frames` |

### 1.4 Font

| Format | Outline | Backend | Fixture | Test target | Assertion type | License / risk |
|---|---|---|---|---|---|---|
| TTF | TrueType (glyf/loca) | stb_truetype | `tests/data/t0.ttf` | `font_ttf` | Known codepoints have glyphs; missing codepoint returns false | stb MIT; fixture Noto Mono, SIL OFL-1.1 |
| OTF | OpenType / CFF (Type2 charstring) | stb_truetype CFF branch | `tests/data/t0.otf` | `font_otf` | Known codepoints have glyphs; missing codepoint returns false; real CFF, not a renamed TTF | stb MIT; fixture Roboto Slab, Apache-2.0; CFF2 variable fonts are not supported |

### 1.5 Dependency probing

FFmpeg and libwebp are probed at configure time
(`ZCSR_DEPS_ROOT` / `ZCSR_FFMPEG_ROOT` / `ZCSR_LIBWEBP_ROOT` cache overrides
allowed). A backend is enabled only when BOTH its headers AND every required
link library are present: FFmpeg needs `libavcodec libavformat libavutil
libswscale libswresample`, libwebp needs `libwebp libwebpdemux libsharpyuv`.
A header-only match does not claim support; the missing headers/libraries are
listed item by item in the configure output, so the failure never surfaces as
a link-time undefined symbol. A missing prebuilt does **not** fail the
configure: the dependent backend compiles as an honest fail-closed path
(`supported` / `caps` report false, decode returns `UNSUPPORTED`).

The dependent T0 tests are conditionally registered: when the backend is
absent they appear in CTest as explicit **Skipped** entries (label `skip`) with
the missing dependency in the output line, rather than being built against a
missing backend or run to failure. The macros that gate the real backends are
`ZCSR_HAVE_FFMPEG` (music; video FFmpeg fallback additionally gates on
`ZCSR_VIDEO_HAVE_FFMPEG`) and `ZCSR_HAVE_LIBWEBP`.

License gate: if the detected `ffmpeg -buildconf` / `-version` reports
`--enable-gpl` or `--enable-nonfree`, the FFmpeg backend is disabled at
configure time with a non-compliance warning (this project requires an LGPL
shared build). Read-only detectors: `tools/check_media_deps.ps1` /
`tools/check_media_deps.sh` (no install, no state change).

## 2. tested legacy

These formats keep an existing regression test and still pass, but they are
outside the T0 promise: no new fixtures, no new features, no expansion of
their surface.

| Format | Backend | Test target | Note |
|---|---|---|---|
| HDR (Radiance RGBE `.hdr`) | stb_image, tonemapped | `image_hdr` | Legacy tonemap path; retained for existing consumers only |
| ICO | Windows icon container | `image_ico` | Legacy container; single-image decode path |
| CUR | Windows cursor container | retained in `zcsr_image_format` enum | No dedicated new test; not extended |

## 3. out of scope

The following are explicitly not implemented. Their public enum values have
been removed and the internal decoder paths for them are closed; they are not
maintained as a backlog.

- **Image:** TIFF, HEIF / HEIC, AVIF, JPEG XL, OpenEXR, TGA, PSD, PIC, PNM
  (PPM/PGM/PBM).
- **Music:** ALAC, WMA, AC-3, DTS, AU/SND; compressed WAV (ADPCM, A-law,
  μ-law). Only PCM and IEEE-float WAV are accepted.
- **Video:** MKV (generic Matroska), MPEG-TS, MPEG-PS, H.265/HEVC, AV1,
  ProRes, MPEG-1/2 video, MJPEG, AVI.
- **Font:** WOFF, WOFF2 (web font containers); CFF2 variable fonts.
