# third_party

Vendored single-header dependencies (no package manager; keeps the project dependency-light).

- **`stb_image.h`** — used ONLY by `modules/render` for PNG decoding (Task #7, Game Core ).
  Vendor the official single header here (public domain / MIT). In exactly one TU define
  `#define STB_IMAGE_IMPLEMENTATION` before including it, and configure `STBI_MALLOC`/`STBI_FREE`
  to draw from the render module's cache buffer so there's no global heap growth.
  (Not yet committed —  adds it when implementing the renderer.)

- **`glad/`** — OpenGL 3.3 Core loader, used ONLY by `modules/glrender` ().
  Generate from https://glad.dav1d.de (gl=3.3, profile=core, no extensions); vendor the produced
  `glad.h` + `khrplatform.h` + `glad.c` here. It is the project's only new third-party dependency.
  `glad.c` is compiled into the glrender module only; `glad.h` is included only by glrender sources.
  (Not yet committed —  adds it when implementing the GL renderer.)
