cmake_minimum_required(VERSION 3.16)

set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING
    "Minimum supported macOS version")

project(zcsr C)

set(CMAKE_C_STANDARD 11)          # C11/C17 — no C++
set(CMAKE_C_STANDARD_REQUIRED ON)
# Warning flags differ by toolchain: GCC/Clang use -Wall -Wextra -Wpedantic; MSVC uses /W4.
if(MSVC)
  # /utf-8: 仓库内 249 个源文件全部是严格 UTF-8 无 BOM（含 settings_panel.h 的中文界面
  # 文案"[应用]""[显示模式]"）。MSVC 默认按当前 ANSI 代码页解释源文件，非中文 Windows
  # 与英文 Windows 上这些字面量会变成乱码；/utf-8 让源码按 UTF-8 解释，源码与运行时
  # 字面量在三平台一致。
  add_compile_options(/W4 /utf-8 /experimental:c11atomics) # audiomix uses C11 <stdatomic.h>
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS) # fopen/strncpy/... are fine here, not C4996
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# macOS backends are Objective-C (.m). Enable only on Apple so Linux/Windows builds stay pure C.
if(APPLE)
  enable_language(OBJC)
endif()

# --- Optional Box2D v3 physics (fetched on demand; zero impact on default builds) ---
option(ZCSR_ENABLE_BOX2D "Fetch Box2D v3 for game projects" OFF)
if(ZCSR_ENABLE_BOX2D)
  include(FetchContent)
  FetchContent_Declare(box2d
    GIT_REPOSITORY https://github.com/erincatto/box2d.git
    GIT_TAG v3.1.1
    GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(box2d)
endif()

# --- Shared interface contracts: the ONLY include surface modules may use ---
add_library(zcsr_contracts INTERFACE)
target_include_directories(zcsr_contracts INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)

# --- Each module compiles independently, seeing ONLY the contracts (zero-coupling) ---
function(zcsr_module name)
  file(GLOB SRC CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/modules/${name}/*.c)
  if(APPLE)
    file(GLOB MSRC CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/modules/${name}/*.m)
    list(APPEND SRC ${MSRC})
  endif()
  add_library(zcsr_${name} STATIC ${SRC})
  target_link_libraries(zcsr_${name} PUBLIC zcsr_contracts)
  # 每个模块都可能通过 ZCSR_LOG_* / zcsr_log_write 打日志，而 log 实现在 core。
  # 之前没有声明这条依赖，Windows(link.exe) 靠重扫静态库掩盖了问题；GNU ld 单遍
  # 扫描时，libzcsr_ui.a / libzcsr_file_service.a / libzcsr_runtime_viewport.a 里的
  # zcsr_log_write 全都解析不到（undefined reference）。在宏里统一声明，链接顺序
  # 与传递依赖一次性闭合。
  if(NOT name STREQUAL "core")
    target_link_libraries(zcsr_${name} PUBLIC zcsr_core)
  endif()
  if(APPLE AND MSRC)
    set_source_files_properties(${MSRC} PROPERTIES COMPILE_OPTIONS "-fobjc-arc") # Cocoa backends use ARC
  endif()
endfunction()

zcsr_module(core)      # Agent 1
zcsr_module(signal)    # Agent 2
zcsr_module(hsm)       # Agent 3
zcsr_module(overlay)   # Agent 4
zcsr_module(platform)  # Agent 5

# --- Task #7: Classic 2D Game Core extension (zero-coupling, builds on the contracts above) ---
zcsr_module(input)     # Game Core Agent 1 — input / window / 60FPS timer
zcsr_module(audio)     # Game Core Agent 2 — WAV audio (signals only)
# zcsr_module(render)  # 已删除：PNG 解码归 modules/image（同样 vendor stb_image），
#                       批渲染归 modules/glrender + modules/sprite。
#                       原 modules/render 全仓零引用（仅 android_main.c 里一个同名局部变量
#                       zcsr_renderer_storage，不是该模块的 API），故整目录移除。
zcsr_module(gamelogic) # Game Core Agent 4 — AABB collision + binary save/load

# --- Engine extension (Game-Usable Core): additive, zero-coupling, optional next to the above ---
zcsr_module(glrender)  # Engine-ext Agent 1 — OpenGL 3.3 renderer (vendors glad; coexists w/ software render)
zcsr_module(audiomix)  # Engine-ext Agent 2 — multi-channel WAV mixer (coexists w/ single-stream audio)
zcsr_module(inputext)  # Engine-ext Agent 3 — mouse + gamepad (additive to keyboard input)
zcsr_module(light2d)   # Light2D additive lighting (no GL calls; drives glrender via gl_render_light2d_ext.h)
target_link_libraries(zcsr_light2d PUBLIC zcsr_glrender)
target_include_directories(zcsr_light2d PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/glad/include)
zcsr_module(image)     # Engine-ext Agent — decode→RGBA32 + offline CPU processing (separated from rendering; vendors stb_image)
zcsr_module(atlas)     # Texture atlas shelf-packing + region table (pure logic, no GL)
zcsr_module(culling)   # Viewport culling: linear + uniform grid (pure logic, no GL)
zcsr_module(font)      # TrueType font loader/rasterizer (stb_truetype, no GL dependency)
if(UNIX AND NOT APPLE)
  target_link_libraries(zcsr_font PUBLIC m)  # stb_truetype uses sqrt/floor/ceil/fmod/pow
endif()
zcsr_module(music)     # Streaming music player (OGG/MP3, depends on audiomix)
zcsr_module(ui)        # Generic UI runtime (node/input/focus/apply/preset/draw-list + GL backend)
zcsr_module(app_shell) # App shell: window skeleton + main menu + settings overlay
zcsr_module(camera)    # 2.5D orthographic camera (world units + shear)
zcsr_module(camera3d)  # software perspective camera (3D -> screen px)
zcsr_module(sprite)     # Lit sprite renderer (uses G-buffer from light2d)
zcsr_module(runtime_viewport) # Engine-Owned Runtime Viewport (Contain Fit + 坐标变换)
zcsr_module(file_service) # Unified File Service + VFS/Mount + Win32(file_win32.c) / POSIX(file_posix.c) backends
zcsr_module(resource)      # Resource Manager + Handle + Generation + Cache
target_link_libraries(zcsr_app_shell PUBLIC
    zcsr_glrender zcsr_platform zcsr_input zcsr_inputext zcsr_ui)
# Per-backend system libs are added by the implementer when each backend lands, e.g.:
#   glrender: opengl32+gdi32 (Win) / GL+X11 (Linux, find_package(OpenGL)) / -framework OpenGL (mac)
#   audiomix: (Win WASAPI: ole32) / ALSA (Linux: find_package(ALSA), asound) / -framework AudioToolbox,AudioUnit (mac)
#   inputext: xinput (Win) / X11 (have) + evdev(none) (Linux) / -framework IOKit,GameController (mac)
# Phase-0 stubs make no backend calls, so they link with no extra libs.
if(NOT WIN32)
  find_package(Threads REQUIRED)
  target_link_libraries(zcsr_core PUBLIC Threads::Threads)  # log.c uses C11 threads
  target_link_libraries(zcsr_audio PUBLIC Threads::Threads)
  target_link_libraries(zcsr_audiomix PUBLIC Threads::Threads ${CMAKE_DL_LIBS})
else()
  target_link_libraries(zcsr_audio PUBLIC winmm)

  target_link_libraries(zcsr_audiomix
    PUBLIC ole32 uuid avrt mmdevapi winmm
  )

  target_link_libraries(zcsr_inputext
    PUBLIC xinput comctl32
  )
endif()
# --- glrender: vendored minimal glad loader compiles on every platform ---------------------
target_sources(zcsr_glrender PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/glad/src/glad.c)
target_include_directories(zcsr_glrender PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/glad/include)

# music depends on audiomix (stream channels)
target_link_libraries(zcsr_music PUBLIC zcsr_audiomix)
if(UNIX AND NOT APPLE)
  target_link_libraries(zcsr_music PUBLIC m)  # stb_vorbis uses log/exp/pow/sin/cos
endif()

if(UNIX AND NOT APPLE)
  # Linux: Wayland window backend + EGL + PipeWire audio (post-migration default).
  # X11/GLX/ALSA/evdev legacy backends removed in commit 5.
  #
  # 依赖全部改成"探测到才启用"：这台机器没有 Wayland/PipeWire 开发包时（默认 Kali
  # X11 桌面、容器、CI），configure 仍然成功，所有库与纯 CPU 测试照常构建，只是
  # 窗口/音频后端不参与链接 —— 引擎本身零依赖，不该被宿主显示栈挡住编译。

  find_package(PkgConfig REQUIRED)

  # ---- 窗口后端探测：X11/GLX 与 Wayland/EGL 各一套，选中的那套才参与编译 ----
  pkg_check_modules(WAYLAND_CLIENT wayland-client)
  find_program(WAYLAND_SCANNER wayland-scanner)
  find_file(XDG_SHELL_XML NAMES xdg-shell.xml
      PATHS /usr/share/wayland-protocols/stable/xdg-shell)
  pkg_check_modules(X11 x11)
  find_package(OpenGL)          # X11/GLX 需要 libGL（GLVND 的 OpenGL::OpenGL）

  set(ZCSR_LINUX_HAS_WAYLAND FALSE)
  if(WAYLAND_CLIENT_FOUND AND WAYLAND_SCANNER AND XDG_SHELL_XML)
    set(ZCSR_LINUX_HAS_WAYLAND TRUE)
  endif()
  set(ZCSR_LINUX_HAS_X11 FALSE)
  if(X11_FOUND AND OpenGL_FOUND)
    set(ZCSR_LINUX_HAS_X11 TRUE)
  endif()

  # AUTO：两套都装时优先 X11 —— X11 客户端在纯 X11 会话和 Wayland 桌面的
  # XWayland 里都能跑，覆盖面比 Wayland-only 大。指定 WAYLAND/X11 可强制。
  set(ZCSR_LINUX_WINDOW_BACKEND "auto" CACHE STRING
      "Linux window backend: auto | x11 | wayland | none")
  set_property(CACHE ZCSR_LINUX_WINDOW_BACKEND PROPERTY STRINGS auto x11 wayland none)

  set(ZCSR_LINUX_BACKEND "none")
  if(ZCSR_LINUX_WINDOW_BACKEND STREQUAL "x11")
    if(NOT ZCSR_LINUX_HAS_X11)
      message(FATAL_ERROR "ZCSR_LINUX_WINDOW_BACKEND=x11 但缺 X11/GL 开发包（libx11-dev + libgl1-mesa-dev）")
    endif()
    set(ZCSR_LINUX_BACKEND "x11")
  elseif(ZCSR_LINUX_WINDOW_BACKEND STREQUAL "wayland")
    if(NOT ZCSR_LINUX_HAS_WAYLAND)
      message(FATAL_ERROR "ZCSR_LINUX_WINDOW_BACKEND=wayland 但缺 wayland-client / wayland-scanner / xdg-shell.xml")
    endif()
    set(ZCSR_LINUX_BACKEND "wayland")
  elseif(ZCSR_LINUX_WINDOW_BACKEND STREQUAL "none")
    set(ZCSR_LINUX_BACKEND "none")
  else() # auto
    if(ZCSR_LINUX_HAS_X11)
      set(ZCSR_LINUX_BACKEND "x11")
    elseif(ZCSR_LINUX_HAS_WAYLAND)
      set(ZCSR_LINUX_BACKEND "wayland")
    endif()
  endif()
  message(STATUS "Linux window backend: ${ZCSR_LINUX_BACKEND} (x11=${ZCSR_LINUX_HAS_X11} wayland=${ZCSR_LINUX_HAS_WAYLAND})")

  if(ZCSR_LINUX_BACKEND STREQUAL "x11")
    target_sources(zcsr_platform PRIVATE modules/platform/platform_x11.c)
    target_compile_definitions(zcsr_platform PRIVATE ZCSR_LINUX_BACKEND_X11)
    target_link_libraries(zcsr_platform PUBLIC ${X11_LIBRARIES} m)

    target_compile_definitions(zcsr_input PRIVATE ZCSR_LINUX_BACKEND_X11)
    target_compile_definitions(zcsr_inputext PRIVATE ZCSR_LINUX_BACKEND_X11)
    target_link_libraries(zcsr_input PUBLIC ${X11_LIBRARIES})
    target_link_libraries(zcsr_inputext PUBLIC ${X11_LIBRARIES})

    target_compile_definitions(zcsr_glrender PRIVATE ZCSR_LINUX_BACKEND_X11)
    target_include_directories(zcsr_glrender PRIVATE ${X11_INCLUDE_DIRS})
    # GLX entry points (glXChooseFBConfig / glXMakeCurrent ...) live in libGL, not libOpenGL
    target_link_libraries(zcsr_glrender PUBLIC ${X11_LIBRARIES} OpenGL::GL OpenGL::OpenGL m)
  elseif(ZCSR_LINUX_BACKEND STREQUAL "wayland")
    add_custom_command(
        OUTPUT ${CMAKE_BINARY_DIR}/gen/xdg-shell-client-protocol.h
               ${CMAKE_BINARY_DIR}/gen/xdg-shell-protocol.c
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/gen
        COMMAND ${WAYLAND_SCANNER} client-header
                ${XDG_SHELL_XML} ${CMAKE_BINARY_DIR}/gen/xdg-shell-client-protocol.h
        COMMAND ${WAYLAND_SCANNER} private-code
                ${XDG_SHELL_XML} ${CMAKE_BINARY_DIR}/gen/xdg-shell-protocol.c
        DEPENDS ${XDG_SHELL_XML}
        VERBATIM)
    target_sources(zcsr_platform PRIVATE
        modules/platform/platform_wayland.c
        ${CMAKE_BINARY_DIR}/gen/xdg-shell-protocol.c)
    target_include_directories(zcsr_platform PRIVATE
        ${CMAKE_BINARY_DIR}/gen
        ${WAYLAND_CLIENT_INCLUDE_DIRS})
    target_compile_definitions(zcsr_platform PRIVATE ZCSR_LINUX_BACKEND_WAYLAND)
    target_link_libraries(zcsr_platform PUBLIC ${WAYLAND_CLIENT_LIBRARIES} m)

    target_compile_definitions(zcsr_input PRIVATE ZCSR_LINUX_BACKEND_WAYLAND)
    target_compile_definitions(zcsr_inputext PRIVATE ZCSR_LINUX_BACKEND_WAYLAND)
    target_include_directories(zcsr_input PRIVATE ${CMAKE_BINARY_DIR}/gen ${WAYLAND_CLIENT_INCLUDE_DIRS})
    target_include_directories(zcsr_inputext PRIVATE ${CMAKE_BINARY_DIR}/gen ${WAYLAND_CLIENT_INCLUDE_DIRS})
    target_link_libraries(zcsr_input PUBLIC ${WAYLAND_CLIENT_LIBRARIES})
    target_link_libraries(zcsr_inputext PUBLIC ${WAYLAND_CLIENT_LIBRARIES})

    pkg_check_modules(WAYLAND_EGL wayland-egl)
    pkg_check_modules(EGL egl)
    target_compile_definitions(zcsr_glrender PRIVATE ZCSR_LINUX_BACKEND_WAYLAND)
    target_include_directories(zcsr_glrender PRIVATE
        ${CMAKE_BINARY_DIR}/gen
        ${WAYLAND_CLIENT_INCLUDE_DIRS}
        ${WAYLAND_EGL_INCLUDE_DIRS}
        ${EGL_INCLUDE_DIRS})
    target_link_libraries(zcsr_glrender PUBLIC
        ${WAYLAND_CLIENT_LIBRARIES}
        ${WAYLAND_EGL_LIBRARIES}
        ${EGL_LIBRARIES}
        OpenGL::OpenGL m)
  else()
    message(STATUS "Linux: 无窗口后端（headless）—— 库与纯 CPU 测试仍可构建")
  endif()

  # ---- PipeWire audio ----
  pkg_check_modules(PIPEWIRE libpipewire-0.3)
  pkg_check_modules(SPA libspa-0.2)
  if(PIPEWIRE_FOUND AND SPA_FOUND)
  target_compile_definitions(zcsr_audiomix PRIVATE ZCSR_AUDIO_BACKEND_PIPEWIRE)
  target_include_directories(zcsr_audiomix PRIVATE
      ${PIPEWIRE_INCLUDE_DIRS}
      ${SPA_INCLUDE_DIRS})
  target_link_libraries(zcsr_audiomix PUBLIC
      ${PIPEWIRE_LIBRARIES}
      ${SPA_LIBRARIES})
  else()
  message(STATUS "Linux: PipeWire 音频后端未启用（缺 libpipewire-0.3 / libspa-0.2），audiomix 保持静音实现")
  endif()

elseif(WIN32)
  # GL 1.1 entry points (textures, glDrawArrays, ...) live in opengl32; the WGL/DC calls
  # (ChoosePixelFormat / SetPixelFormat / SwapBuffers / GetDC / ReleaseDC) live in gdi32. The
  # GL 2.0+ entry points are resolved at run time by glad via wglGetProcAddress.
  target_link_libraries(zcsr_glrender PUBLIC opengl32 gdi32)
endif()

if(APPLE)
  # Cocoa backends (NSWindow / NSView draw / NSEvent / NSSound) for platform/overlay/render/input/audio.
  # (find_library REQUIRED needs CMake 3.18; we target 3.16, so check explicitly.)
  find_library(COCOA_FRAMEWORK Cocoa)
  if(NOT COCOA_FRAMEWORK)
    message(FATAL_ERROR "Cocoa framework not found — required for the macOS backends.")
  endif()
  foreach(m platform overlay render input audio)
    target_link_libraries(zcsr_${m} PUBLIC ${COCOA_FRAMEWORK})
  endforeach()
  # glrender's NSOpenGL backend (glrender_cocoa.m) needs Cocoa plus the OpenGL framework.
  find_library(OPENGL_FRAMEWORK OpenGL)
  if(NOT OPENGL_FRAMEWORK)
    message(FATAL_ERROR "OpenGL framework not found — required for the macOS glrender backend.")
  endif()
  target_link_libraries(zcsr_glrender PUBLIC ${COCOA_FRAMEWORK} ${OPENGL_FRAMEWORK})

  # B1: macOS AudioUnit backend for audiomix.
  find_library(ZCSR_COREAUDIO_FRAMEWORK CoreAudio REQUIRED)
  find_library(ZCSR_AUDIOTOOLBOX_FRAMEWORK AudioToolbox REQUIRED)
  find_library(ZCSR_AUDIOUNIT_FRAMEWORK AudioUnit REQUIRED)
  target_link_libraries(zcsr_audiomix PUBLIC
    "${ZCSR_COREAUDIO_FRAMEWORK}"
    "${ZCSR_AUDIOTOOLBOX_FRAMEWORK}"
    "${ZCSR_AUDIOUNIT_FRAMEWORK}"
  )
endif()

# --- Zero-coupling guard: fail if a module includes another module ---
add_custom_target(check_coupling
  COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_no_cross_includes.sh
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  COMMENT "Verifying no cross-module includes")
