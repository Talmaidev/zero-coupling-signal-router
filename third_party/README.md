# third_party

Vendored single-header dependencies (no package manager; keeps the project dependency-light).

- **`stb_image.h`** — committed (public domain / MIT). Used by `modules/image`
  for multi-format decode to RGBA32. The module defines
  `#define STB_IMAGE_IMPLEMENTATION` in exactly one of its own TUs and configures
  `STBI_MALLOC`/`STBI_FREE` to bump-allocate from the caller's buffer (no global heap growth).

- **`glad/`** — minimal vendored OpenGL 3.3 Core loader surface, used only by `modules/glrender`.
  It exposes the subset of GL entrypoints the fixed renderer needs, keeps the
  single-header + single-source boundary, and is compiled into the glrender module only on backends
  that implement OpenGL loading. No other module may include it.
