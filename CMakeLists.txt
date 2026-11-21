cmake_minimum_required(VERSION 3.16)

set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING
    "Minimum supported macOS version")

project(zcsr VERSION 1.0.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)          # C11/C17 — no C++
set(CMAKE_C_STANDARD_REQUIRED ON)
# Warning flags differ by toolchain: GCC/Clang use -Wall -Wextra -Wpedantic; MSVC uses /W4.
if(MSVC)
  # /utf-8: 仓库内 249 个源文件全部是严格 UTF-8 无 BOM（含 settings_panel.h 的中文界面
  # 文案"[应用]""[显示模式]"）。MSVC 默认按当前 ANSI 代码页解释源文件，非中文 Windows
  # 与英文 Windows 上这些字面量会变成乱码；/utf-8 让源码按 UTF-8 解释，源码与运行时
  # 字面量在三平台一致。
  add_compile_options(/W4 /WX /utf-8 /experimental:c11atomics) # audiomix uses C11 <stdatomic.h>
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
option(ZCSR_BUILD_TESTS "Build repository-native contract tests" ON)
option(ZCSR_ENABLE_FORMAT_GATE "Build the printf format-gate CTest" ON)
option(ZCSR_BUILD_FUZZERS "Build libFuzzer targets (requires Clang)" OFF)
option(ZCSR_ENABLE_SANITIZERS "Build with ASan+UBSan (GCC/Clang)" OFF)
option(ZCSR_ENABLE_TSAN "Build with ThreadSanitizer (GCC/Clang)" OFF)
if(ZCSR_ENABLE_SANITIZERS AND ZCSR_ENABLE_TSAN)
  message(FATAL_ERROR "ZCSR_ENABLE_SANITIZERS and ZCSR_ENABLE_TSAN are mutually exclusive")
endif()
if(ZCSR_ENABLE_SANITIZERS)
  if(MSVC)
    message(FATAL_ERROR "ZCSR_ENABLE_SANITIZERS is supported with GCC/Clang (use a Linux/GCC build)")
  endif()
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()
if(ZCSR_ENABLE_TSAN)
  if(MSVC)
    message(FATAL_ERROR "ZCSR_ENABLE_TSAN is supported with GCC/Clang (use a Linux/GCC build)")
  endif()
  add_compile_options(-fsanitize=thread -fno-omit-frame-pointer)
  add_link_options(-fsanitize=thread)
endif()
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
  # 扫描时，libzcsr_ui.a / libzcsr_file_service.a / libzcsr_viewport.a 里的
  # zcsr_log_write 全都解析不到（undefined reference）。在宏里统一声明，链接顺序
  # 与传递依赖一次性闭合。
  if(NOT name STREQUAL "core")
    target_link_libraries(zcsr_${name} PUBLIC zcsr_core)
  endif()
  if(APPLE AND MSRC)
    set_source_files_properties(${MSRC} PROPERTIES COMPILE_OPTIONS "-fobjc-arc") # Cocoa backends use ARC
  endif()
endfunction()

zcsr_module(core)      # Core
zcsr_module(signal)    # Signal
zcsr_module(hsm)       # HSM
zcsr_module(platform)  # Platform
if(NOT WIN32)
  set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/modules/platform/platform_win32.c
      PROPERTIES HEADER_FILE_ONLY TRUE)
endif()

# --- Classic 2D Game Core extension (zero-coupling, builds on the contracts above) ---
zcsr_module(input)     # input / window / 60FPS timer
zcsr_module(audio)     # WAV audio (signals only)
# zcsr_module(render)  # 已删除：PNG 解码归 modules/image（同样 vendor stb_image），
#                       批渲染归 modules/glrender + modules/sprite。
#                       原 modules/render 全仓零引用（仅 android_main.c 里一个同名局部变量
#                       zcsr_renderer_storage，不是该模块的 API），故整目录移除。
zcsr_module(collision) # AABB collision only
zcsr_module(save)      # binary save/load only

# --- Engine extension (Game-Usable Core): additive, zero-coupling, optional next to the above ---
zcsr_module(color_pipeline) # Replaceable color transfer / LUT algorithm (no GL, no UI)
if(UNIX AND NOT APPLE)
  # color_pipeline.c uses powf(); libm must be a transitive dependency so
  # standalone consumers (e.g. color_pipeline_test) link on Linux too.
  target_link_libraries(zcsr_color_pipeline PUBLIC m)
endif()
zcsr_module(glrender)  # OpenGL 3.3 renderer (vendors glad; coexists w/ software render)
zcsr_module(audiomix)  # multi-channel WAV mixer (coexists w/ single-stream audio)
zcsr_module(inputext)  # mouse + gamepad (additive to keyboard input)
if(WIN32 OR APPLE OR ANDROID)
  # Linux evdev gamepad TU is empty on these targets; MSVC /WX rejects C4206.
  set_source_files_properties(
      ${CMAKE_CURRENT_SOURCE_DIR}/modules/inputext/inputext_linux_gamepad.c
      PROPERTIES HEADER_FILE_ONLY TRUE)
endif()
zcsr_module(light2d)   # Light2D additive lighting (no GL calls; drives glrender via gl_render_light2d_ext.h)
target_link_libraries(zcsr_light2d PUBLIC zcsr_glrender)
target_include_directories(zcsr_light2d PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/glad/include)
zcsr_module(image)
zcsr_module(assets)   # 开发者自带资源导入（相对路径 -> 纹理/字体/WAV）
target_link_libraries(zcsr_assets PUBLIC zcsr_image zcsr_glrender zcsr_audiomix)     # decode→RGBA32 + offline CPU processing (separated from rendering; vendors stb_image)
zcsr_module(atlas)     # Texture atlas shelf-packing + region table (pure logic, no GL)
zcsr_module(culling)   # Viewport culling: linear + uniform grid (pure logic, no GL)
zcsr_module(font)      # TrueType font loader/rasterizer (stb_truetype, no GL dependency)
# glrender calls surface/font/atlas entry points; declare the dependency so
# GNU ld orders them after libzcsr_glrender.a instead of reporting undefined
# references when a consumer links only zcsr_glrender.
target_link_libraries(zcsr_glrender PUBLIC zcsr_platform zcsr_font zcsr_atlas zcsr_color_pipeline)
if(UNIX AND NOT APPLE)
  target_link_libraries(zcsr_font PUBLIC m)  # stb_truetype uses sqrt/floor/ceil/fmod/pow
endif()
zcsr_module(music)     # Streaming music player (OGG/MP3, depends on audiomix)
zcsr_module(ui)        # Generic UI runtime (node/input/focus/apply/preset/draw-list + GL backend)
zcsr_module(camera)    # 2.5D orthographic camera (world units + shear)
if(UNIX AND NOT APPLE)
  target_link_libraries(zcsr_camera PUBLIC m) # camera pitch uses cosf/sinf/atan2f
endif()
zcsr_module(camera3d)  # software perspective camera (3D -> screen px)
zcsr_module(sprite)     # Lit sprite renderer (uses G-buffer from light2d)
zcsr_module(viewport)  # Engine-owned viewport fit + coordinate transform
zcsr_module(file_service) # Unified File Service + VFS/Mount + Win32(file_win32.c) / POSIX(file_posix.c) backends
if(NOT WIN32)
  set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/modules/file_service/file_win32.c
      PROPERTIES HEADER_FILE_ONLY TRUE)
endif()
zcsr_module(resource)      # Resource Manager + Handle + Generation + Cache
# zcsr_module(app_shell) removed: settings is zcsr_ui_page_manager/pages/settings.
# zcsr_module(overlay) removed: debug overlay is zcsr_tools/zcsr_debug_overlay.
# zcsr_module(gamelogic) split: collision + save, each one responsibility.
# Per-backend system libs are added by the implementer when each backend lands, e.g.:
#   glrender: opengl32+gdi32 (Win) / GLX+X11 or EGL+Wayland (Linux) / -framework OpenGL (mac)
#   audiomix: WASAPI (Win) / PipeWire or ALSA (Linux) / AudioToolbox,AudioUnit (mac) / AAudio (Android)
#   inputext: xinput (Win) / X11+XIM or Wayland+xkbcommon + evdev gamepads (Linux)
#             / GameController (mac) / AInputEvent+JNI KeyEvent (Android)
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
  # Linux: X11/GLX and Wayland/EGL window backends, PipeWire/ALSA audio,
  # evdev gamepads. X11 is the default when both window stacks are present.
  #
  # 依赖全部改成"探测到才启用"：这台机器没有 Wayland/PipeWire 开发包时（默认 Kali
  # X11 桌面、容器、CI），configure 仍然成功，所有库与纯 CPU 测试照常构建，只是
  # 窗口/音频后端不参与链接 —— 引擎本身零依赖，不该被宿主显示栈挡住编译。

  find_package(PkgConfig REQUIRED)

  # ---- 窗口后端探测：X11/GLX 与 Wayland/EGL 各一套，选中的那套才参与编译 ----
  pkg_check_modules(WAYLAND_CLIENT wayland-client)
  pkg_check_modules(XKBCOMMON xkbcommon)
  find_program(WAYLAND_SCANNER wayland-scanner)
  find_file(XDG_SHELL_XML NAMES xdg-shell.xml
      PATHS /usr/share/wayland-protocols/stable/xdg-shell)
  pkg_check_modules(X11 x11)
  pkg_check_modules(XRANDR xrandr)
  find_package(OpenGL)          # X11/GLX 需要 libGL（GLVND 的 OpenGL::OpenGL）

  set(ZCSR_LINUX_HAS_WAYLAND FALSE)
  if(WAYLAND_CLIENT_FOUND AND WAYLAND_SCANNER AND XDG_SHELL_XML AND XKBCOMMON_FOUND)
    set(ZCSR_LINUX_HAS_WAYLAND TRUE)
  endif()
  set(ZCSR_LINUX_HAS_X11 FALSE)
  if(X11_FOUND AND OpenGL_FOUND AND XRANDR_FOUND)
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

  # Do not compile backends that cannot be selected on this host.  An empty TU
  # is not valid ISO C and -Wpedantic flags it even though the glob includes it.
  if(NOT ZCSR_LINUX_BACKEND STREQUAL "x11")
    set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/modules/platform/platform_x11.c
        PROPERTIES HEADER_FILE_ONLY TRUE)
  endif()
  if(NOT ZCSR_LINUX_BACKEND STREQUAL "wayland")
    set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/modules/platform/platform_wayland.c
        PROPERTIES HEADER_FILE_ONLY TRUE)
  endif()

  if(ZCSR_LINUX_BACKEND STREQUAL "x11")
    target_sources(zcsr_platform PRIVATE modules/platform/platform_x11.c)
    target_compile_definitions(zcsr_platform PRIVATE ZCSR_LINUX_BACKEND_X11)
    target_include_directories(zcsr_platform PRIVATE ${XRANDR_INCLUDE_DIRS})
    target_link_libraries(zcsr_platform PUBLIC ${X11_LIBRARIES} ${XRANDR_LIBRARIES} m)

    target_compile_definitions(zcsr_input PRIVATE ZCSR_LINUX_BACKEND_X11)
    target_compile_definitions(zcsr_inputext PRIVATE ZCSR_LINUX_BACKEND_X11)
    target_link_libraries(zcsr_input PUBLIC ${X11_LIBRARIES})
    target_link_libraries(zcsr_inputext PUBLIC ${X11_LIBRARIES})

    target_compile_definitions(zcsr_glrender PRIVATE ZCSR_LINUX_BACKEND_X11)
    target_include_directories(zcsr_glrender PRIVATE ${X11_INCLUDE_DIRS})
    # GLX entry points (glXChooseFBConfig / glXMakeCurrent ...) live in libGL, not libOpenGL
    target_link_libraries(zcsr_glrender PUBLIC ${X11_LIBRARIES} OpenGL::GL OpenGL::OpenGL m)
  elseif(ZCSR_LINUX_BACKEND STREQUAL "wayland")
    if(NOT XKBCOMMON_FOUND)
      message(FATAL_ERROR "Wayland text input requires libxkbcommon-dev (xkbcommon pkg-config not found)")
    endif()
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
    target_include_directories(zcsr_inputext PRIVATE
        ${CMAKE_BINARY_DIR}/gen
        ${WAYLAND_CLIENT_INCLUDE_DIRS}
        ${XKBCOMMON_INCLUDE_DIRS})
    target_link_libraries(zcsr_input PUBLIC ${WAYLAND_CLIENT_LIBRARIES})
    target_link_libraries(zcsr_inputext PUBLIC
        ${WAYLAND_CLIENT_LIBRARIES}
        ${XKBCOMMON_LIBRARIES})

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
    # Headless: no window backend. glrender is globbed into the build anyway
    # (CPU tests and UI runtime tests link it); its platform entry points come
    # from glrender_platform_headless.c, and libGL supplies the GL 1.1 symbols.
    if(OpenGL_FOUND)
      target_link_libraries(zcsr_glrender PUBLIC OpenGL::GL OpenGL::OpenGL m)
    endif()
    message(STATUS "Linux: 无窗口后端（headless）—— 库与纯 CPU 测试仍可构建")
  endif()

  # ---- Linux audio backend selection ----
  # Availability only. This never selects Intel/AMD GPU behavior and never
  # requires the application to call anything: auto uses PipeWire when present,
  # otherwise ALSA when present, otherwise the explicit silent contract.
  set(ZCSR_AUDIO_BACKEND "auto" CACHE STRING "Linux audiomix backend: auto|pipewire|alsa|none")
  set_property(CACHE ZCSR_AUDIO_BACKEND PROPERTY STRINGS auto pipewire alsa none)
  pkg_check_modules(PIPEWIRE IMPORTED_TARGET libpipewire-0.3)
  pkg_check_modules(SPA IMPORTED_TARGET libspa-0.2)
  find_package(ALSA QUIET)
  set(ZCSR_CAN_PIPEWIRE FALSE)
  if(PIPEWIRE_FOUND AND SPA_FOUND)
    set(ZCSR_CAN_PIPEWIRE TRUE)
  endif()
  if(ZCSR_AUDIO_BACKEND STREQUAL "pipewire" AND NOT ZCSR_CAN_PIPEWIRE)
    message(FATAL_ERROR "ZCSR_AUDIO_BACKEND=pipewire 但缺 libpipewire-0.3 / libspa-0.2")
  endif()
  if(ZCSR_AUDIO_BACKEND STREQUAL "alsa" AND NOT ALSA_FOUND)
    message(FATAL_ERROR "ZCSR_AUDIO_BACKEND=alsa 但找不到 ALSA 开发包")
  endif()

  if(ZCSR_CAN_PIPEWIRE AND (ZCSR_AUDIO_BACKEND STREQUAL "auto" OR
                            ZCSR_AUDIO_BACKEND STREQUAL "pipewire"))
    target_compile_definitions(zcsr_audiomix PRIVATE ZCSR_AUDIO_BACKEND_PIPEWIRE)
    target_include_directories(zcsr_audiomix PRIVATE
        ${PIPEWIRE_INCLUDE_DIRS}
        ${SPA_INCLUDE_DIRS})
    # pkg-config's -L is not emitted by the imported target on all CMake
    # generators. Resolve the actual soname link path from the pkg-config
    # library directory first; fall back to the imported target for a system
    # install where the linker search path is already complete.
    if(PIPEWIRE_LIBRARY_DIRS)
      list(GET PIPEWIRE_LIBRARY_DIRS 0 ZCSR_PIPEWIRE_LIBDIR)
    else()
      set(ZCSR_PIPEWIRE_LIBDIR "")
    endif()
    set(ZCSR_PIPEWIRE_LIBRARY "${ZCSR_PIPEWIRE_LIBDIR}/libpipewire-0.3.so")
    if(NOT EXISTS "${ZCSR_PIPEWIRE_LIBRARY}")
      find_library(ZCSR_PIPEWIRE_LIBRARY
          NAMES pipewire-0.3
          PATHS ${PIPEWIRE_LIBRARY_DIRS}
          NO_DEFAULT_PATH)
    endif()
    message(STATUS "PipeWire link path: ${ZCSR_PIPEWIRE_LIBRARY}")
    if(ZCSR_PIPEWIRE_LIBRARY AND EXISTS "${ZCSR_PIPEWIRE_LIBRARY}")
      target_link_libraries(zcsr_audiomix PUBLIC "${ZCSR_PIPEWIRE_LIBRARY}")
    else()
      target_link_libraries(zcsr_audiomix PUBLIC PkgConfig::PIPEWIRE)
    endif()
    target_link_libraries(zcsr_audiomix PUBLIC PkgConfig::SPA)
    message(STATUS "Linux: audio backend = PipeWire")
  elseif(ALSA_FOUND AND (ZCSR_AUDIO_BACKEND STREQUAL "auto" OR
                         ZCSR_AUDIO_BACKEND STREQUAL "alsa"))
    # Old laptops often have no PipeWire session. ALSA is the dependency-free
    # output path that always exists on a Debian/Kali install.
    target_compile_definitions(zcsr_audiomix PRIVATE ZCSR_AUDIO_BACKEND_ALSA)
    target_include_directories(zcsr_audiomix PRIVATE ${ALSA_INCLUDE_DIRS})
    if(TARGET ALSA::ALSA)
      target_link_libraries(zcsr_audiomix PUBLIC ALSA::ALSA)
    else()
      target_link_libraries(zcsr_audiomix PUBLIC ${ALSA_LIBRARIES})
    endif()
    message(STATUS "Linux: audio backend = ALSA (${ALSA_LIBRARIES})")
  else()
    message(STATUS "Linux: audio backend = silent (PipeWire/ALSA unavailable or disabled)")
  endif()

elseif(WIN32)
  # GL 1.1 entry points (textures, glDrawArrays, ...) live in opengl32; the WGL/DC calls
  # (ChoosePixelFormat / SetPixelFormat / SwapBuffers / GetDC / ReleaseDC) live in gdi32. The
  # GL 2.0+ entry points are resolved at run time by glad via wglGetProcAddress.
  target_link_libraries(zcsr_glrender PUBLIC opengl32 gdi32)
endif()

if(APPLE)
  # Cocoa backends (NSWindow / NSOpenGL / NSEvent / NSSound). overlay and
  # render are no longer module targets, so linking them here would make
  # macOS configure fail before compilation.
  find_library(COCOA_FRAMEWORK Cocoa)
  if(NOT COCOA_FRAMEWORK)
    message(FATAL_ERROR "Cocoa framework not found — required for the macOS backends.")
  endif()
  foreach(m platform input audio inputext)
    target_link_libraries(zcsr_${m} PUBLIC ${COCOA_FRAMEWORK})
  endforeach()
  find_library(ZCSR_GAMECONTROLLER_FRAMEWORK GameController)
  if(NOT ZCSR_GAMECONTROLLER_FRAMEWORK)
    message(FATAL_ERROR "GameController framework not found — required for macOS inputext.")
  endif()
  target_link_libraries(zcsr_inputext PUBLIC ${ZCSR_GAMECONTROLLER_FRAMEWORK})
  # glrender's NSOpenGL backend (glrender_cocoa.m) needs Cocoa plus the OpenGL framework.
  find_library(OPENGL_FRAMEWORK OpenGL)
  if(NOT OPENGL_FRAMEWORK)
    message(FATAL_ERROR "OpenGL framework not found — required for the macOS glrender backend.")
  endif()
  target_link_libraries(zcsr_glrender PUBLIC ${COCOA_FRAMEWORK} ${OPENGL_FRAMEWORK})

  # B1: macOS AudioUnit backend for audiomix.
  find_library(ZCSR_COREAUDIO_FRAMEWORK CoreAudio)
  find_library(ZCSR_AUDIOTOOLBOX_FRAMEWORK AudioToolbox)
  find_library(ZCSR_AUDIOUNIT_FRAMEWORK AudioUnit)
  if(NOT ZCSR_COREAUDIO_FRAMEWORK OR NOT ZCSR_AUDIOTOOLBOX_FRAMEWORK OR NOT ZCSR_AUDIOUNIT_FRAMEWORK)
    message(FATAL_ERROR "CoreAudio/AudioToolbox/AudioUnit frameworks not found — required for the macOS audiomix backend.")
  endif()
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

# --- Repository-native contract tests ---
# Only when this repository is the top-level project. When it is consumed via
# add_subdirectory (for example by game_test), the consumer owns its test target
# names and must not collide with the engine's own CTest entries.
if(ZCSR_BUILD_TESTS AND CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  enable_testing()
  add_subdirectory(tests)
endif()
