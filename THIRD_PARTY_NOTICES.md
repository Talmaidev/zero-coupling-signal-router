# Third-Party Notices

The repository's own source code is licensed under AGPL-3.0 (see `LICENSE`).
The following vendored or prebuilt components keep their original licenses.
AGPL-3.0 does not replace, relicense, or override those terms.

| Component | License | Location / notice |
| --- | --- | --- |
| stb_image.h, stb_vorbis.c, stb_truetype.h | MIT OR Public Domain (Unlicense) | `third_party/`; full text at the end of each header |
| minimp3.h | CC0-1.0 / public domain dedication | `third_party/minimp3.h`; dedication at the top of the header |
| glad (OpenGL loader) | MIT | `third_party/glad/`; generated from the glad project, upstream MIT |
| libwebp 1.4.0 | BSD-3-Clause | `third_party/prebuilt/win64/libwebp/` and `third_party/prebuilt/linux64/libwebp/`; copyright and license notice in the WebP headers |
| FFmpeg (prebuilt, dynamically linked) | LGPL-3.0 | `third_party/prebuilt/win64/ffmpeg/LICENSE.txt`; see `third_party/DEPENDENCIES.md` |
| Box2D / tinyspline (optional FetchContent dependencies) | MIT | fetched at configure time, not vendored in this repository |

`third_party/README.md` and `third_party/DEPENDENCIES.md` record the build-time
role of each component. When redistributing binaries, keep the applicable
license texts and notices next to the binary package.
