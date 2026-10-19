cmake_minimum_required(VERSION 3.16)

set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING
    "Minimum supported macOS version")

project(zcsr C)

set(CMAKE_C_STANDARD 11)          # C11/C17 — no C++
set(CMAKE_C_STANDARD_REQUIRED ON)
# Warning flags differ by toolchain: GCC/Clang use -Wall -Wextra -Wpedantic; MSVC uses /W4.
if(MSVC)
  add_compile_options(/W4 /experimental:c11atomics) # audiomix uses C11 <stdatomic.h>
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
target_include_directories(zcsr_contracts INTERFACE ${CMAKE_SOURCE_DIR}/include)

# --- Each module compiles independently, seeing ONLY the contracts (zero-coupling) ---
function(zcsr_module name)
  file(GLOB SRC CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/modules/${name}/*.c)
  if(APPLE)
    file(GLOB MSRC CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/modules/${name}/*.m)
    list(APPEND SRC ${MSRC})
  endif()
  add_library(zcsr_${name} STATIC ${SRC})
  target_link_libraries(zcsr_${name} PUBLIC zcsr_contracts)
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
zcsr_module(render)    # Game Core Agent 3 — PNG (stb_image) + batch sprite renderer
zcsr_module(gamelogic) # Game Core Agent 4 — AABB collision + binary save/load

# --- Engine extension (Game-Usable Core): additive, zero-coupling, optional next to the above ---
zcsr_module(glrender)  # Engine-ext Agent 1 — OpenGL 3.3 renderer (vendors glad; coexists w/ software render)
zcsr_module(audiomix)  # Engine-ext Agent 2 — multi-channel WAV mixer (coexists w/ single-stream audio)
zcsr_module(inputext)  # Engine-ext Agent 3 — mouse + gamepad (additive to keyboard input)
zcsr_module(image)     # Engine-ext Agent — decode→RGBA32 + offline CPU processing (separated from rendering; vendors stb_image)
zcsr_module(atlas)     # Texture atlas shelf-packing + region table (pure logic, no GL)
zcsr_module(culling)   # Viewport culling: linear + uniform grid (pure logic, no GL)
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
    PUBLIC ole32 uuid avrt mmdevapi
  )

  target_link_libraries(zcsr_inputext
    PUBLIC xinput comctl32
  )
endif()
# --- glrender: vendored minimal glad loader compiles on every platform ---------------------
target_sources(zcsr_glrender PRIVATE ${CMAKE_SOURCE_DIR}/third_party/glad/src/glad.c)
target_include_directories(zcsr_glrender PRIVATE ${CMAKE_SOURCE_DIR}/third_party/glad/include)

if(UNIX AND NOT APPLE)
  find_package(X11 REQUIRED)
  target_link_libraries(zcsr_platform PUBLIC ${X11_LIBRARIES})
  target_include_directories(zcsr_platform PRIVATE ${X11_INCLUDE_DIR})
  # Optional multi-monitor backends for A1. Both are optional; if absent the
  # platform module compiles a single-screen fallback without breaking the build.
  if(X11_Xrandr_FOUND)
    target_compile_definitions(zcsr_platform PRIVATE ZCSR_HAVE_XRANDR)
    target_link_libraries(zcsr_platform PUBLIC ${X11_Xrandr_LIB})
  endif()
  if(X11_Xinerama_FOUND)
    target_compile_definitions(zcsr_platform PRIVATE ZCSR_HAVE_XINERAMA)
    target_link_libraries(zcsr_platform PUBLIC ${X11_Xinerama_LIB})
  endif()
  target_link_libraries(zcsr_platform PUBLIC m)  # isfinite/fabsf/strtod for A2 DPI parsing
  target_link_libraries(zcsr_overlay PUBLIC ${X11_LIBRARIES})
  target_include_directories(zcsr_overlay PRIVATE ${X11_INCLUDE_DIR})
  target_link_libraries(zcsr_input PUBLIC ${X11_LIBRARIES})  # input pumps X11 key/close events
  target_include_directories(zcsr_input PRIVATE ${X11_INCLUDE_DIR})
  target_link_libraries(zcsr_render PUBLIC ${X11_LIBRARIES})
  target_include_directories(zcsr_render PRIVATE ${X11_INCLUDE_DIR})
  target_link_libraries(zcsr_inputext PUBLIC ${X11_LIBRARIES})
  target_include_directories(zcsr_inputext PRIVATE ${X11_INCLUDE_DIR})
  find_package(OpenGL REQUIRED)
  target_link_libraries(zcsr_glrender PUBLIC ${X11_LIBRARIES} OpenGL::GL m)
  target_include_directories(zcsr_glrender PRIVATE ${X11_INCLUDE_DIR})
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

# --- Agent 6: Integration & Validation (architecture/review owned) ---
add_executable(zcsr_demo  integration/demo.c)
foreach(exe zcsr_demo)
  target_link_libraries(${exe} PRIVATE
    zcsr_core zcsr_signal zcsr_hsm zcsr_overlay zcsr_platform)
endforeach()

if(APPLE AND TARGET zcsr_demo)
  set_target_properties(zcsr_demo PROPERTIES
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_INFO_PLIST
      "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Info.plist.in"
    MACOSX_BUNDLE_BUNDLE_NAME "zcsr_demo"
    MACOSX_BUNDLE_GUI_IDENTIFIER "org.zcsr.demo"
    MACOSX_BUNDLE_BUNDLE_VERSION "1"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "1.0"
    MACOSX_BUNDLE_COPYRIGHT "Copyright ZCSR contributors"
  )
endif()

if(APPLE)
  add_executable(verify_macos
    "${CMAKE_CURRENT_SOURCE_DIR}/integration/verify_macos.c"
  )
  target_link_libraries(verify_macos PRIVATE
    zcsr_audiomix
  )
endif()

# --- Tests ---
enable_testing()
add_executable(zcsr_test_signal tests/test_signal.c)
target_link_libraries(zcsr_test_signal PRIVATE zcsr_signal zcsr_contracts)
add_test(NAME signal_router COMMAND zcsr_test_signal)

add_executable(test_core tests/test_core.c)
target_link_libraries(test_core PRIVATE zcsr_core)
add_test(NAME core COMMAND test_core)

add_executable(test_log tests/test_log.c)
target_link_libraries(test_log PRIVATE zcsr_core)
add_test(NAME log COMMAND test_log)

add_executable(test_platform tests/test_platform.c)
target_link_libraries(test_platform PRIVATE zcsr_platform)
add_test(NAME platform_api COMMAND test_platform)
add_executable(test_overlay tests/test_overlay.c)
target_link_libraries(test_overlay PRIVATE zcsr_overlay zcsr_platform)
add_test(NAME overlay COMMAND test_overlay)

add_executable(test_hsm tests/test_hsm.c)
target_link_libraries(test_hsm PRIVATE zcsr_hsm zcsr_core)  # hsm persists into the core state buffer
add_test(NAME hsm COMMAND test_hsm)

add_executable(test_state_reuse tests/test_state_reuse.c)
target_link_libraries(test_state_reuse PRIVATE zcsr_core)
add_test(NAME state_reuse COMMAND test_state_reuse)

add_executable(test_integration tests/test_integration.c)
target_link_libraries(test_integration PRIVATE zcsr_overlay zcsr_hsm zcsr_core zcsr_signal zcsr_platform)
add_test(NAME integration COMMAND test_integration)

add_executable(test_gamelogic tests/test_gamelogic.c)
target_link_libraries(test_gamelogic PRIVATE zcsr_gamelogic)
add_test(NAME gamelogic COMMAND test_gamelogic)

add_executable(test_input tests/test_input.c)
target_link_libraries(test_input PRIVATE zcsr_input zcsr_platform)  # input uses platform accessors
add_test(NAME input COMMAND test_input)

add_executable(test_render tests/test_render.c)
target_link_libraries(test_render PRIVATE zcsr_render zcsr_platform)  # render uses platform accessors
add_test(NAME render COMMAND test_render)

add_executable(test_audio tests/test_audio.c)
target_link_libraries(test_audio PRIVATE zcsr_audio zcsr_signal)
add_test(NAME audio COMMAND test_audio)

add_executable(test_glrender tests/test_glrender.c)
target_link_libraries(test_glrender PRIVATE zcsr_glrender zcsr_platform)
add_test(NAME glrender COMMAND test_glrender)

add_executable(test_audiomix tests/test_audiomix.c)
target_link_libraries(test_audiomix PRIVATE zcsr_audiomix zcsr_signal)
add_test(NAME audiomix COMMAND test_audiomix)

add_executable(test_inputext tests/test_inputext.c)
target_link_libraries(test_inputext PRIVATE zcsr_inputext zcsr_platform)
add_test(NAME inputext COMMAND test_inputext)

add_executable(test_image tests/test_image.c)
target_link_libraries(test_image PRIVATE zcsr_image)
add_test(NAME image COMMAND test_image)

add_executable(test_atlas tests/test_atlas.c)
target_link_libraries(test_atlas PRIVATE zcsr_atlas)
add_test(NAME atlas COMMAND test_atlas)

add_executable(test_culling tests/test_culling.c)
target_link_libraries(test_culling PRIVATE zcsr_culling)
add_test(NAME culling COMMAND test_culling)

add_executable(test_state tests/test_state.c)
target_link_libraries(test_state PRIVATE zcsr_glrender zcsr_platform)
add_test(NAME state_sort COMMAND test_state)

# Worst-case GPU throughput bench (adversarial textures / blend modes). Not a ctest.
add_executable(zcsr_glrender_worst_bench integration/bench_worst.c)
target_link_libraries(zcsr_glrender_worst_bench PRIVATE zcsr_glrender zcsr_platform)

# Text rendering demo (real 8x8 bitmap font verification). Not a ctest.
add_executable(zcsr_text_demo integration/text_demo.c)
target_link_libraries(zcsr_text_demo PRIVATE zcsr_glrender zcsr_platform)

# Atlas end-to-end bench (unpacked vs packed). Not a ctest.
add_executable(zcsr_atlas_bench integration/bench_atlas.c)
target_link_libraries(zcsr_atlas_bench PRIVATE zcsr_glrender zcsr_atlas zcsr_platform)

# Instanced rendering bench (legacy vs instanced). Not a ctest.
add_executable(zcsr_inst_bench integration/bench_inst.c)
target_link_libraries(zcsr_inst_bench PRIVATE zcsr_glrender zcsr_platform)

# Culling bench (linear vs grid across view sizes). Pure CPU, headless. Not a ctest.
add_executable(zcsr_culling_bench integration/bench_culling.c)
target_link_libraries(zcsr_culling_bench PRIVATE zcsr_culling)

# Windows backend verification (WASAPI + XInput + mouse). Interactive, not a ctest.
add_executable(zcsr_verify_windows integration/verify_windows.c)
target_link_libraries(zcsr_verify_windows PRIVATE zcsr_audiomix zcsr_inputext zcsr_platform)

# Offline image-bake throughput (chroma-key / variant / palette). Informational, not a ctest gate
# (load-time bakes, not a per-frame budget); pure CPU, runs headless.
add_executable(zcsr_image_bench integration/bench_image.c)
target_link_libraries(zcsr_image_bench PRIVATE zcsr_image)

# Optional Box2D physics demo (only built when ZCSR_ENABLE_BOX2D=ON; desktop only)
if(ZCSR_ENABLE_BOX2D)
  if(WIN32 OR CMAKE_SYSTEM_NAME STREQUAL "Linux")
    if(NOT ANDROID)
      add_executable(demo_physics integration/demo_physics.c)
      target_link_libraries(demo_physics PRIVATE
        zcsr_platform zcsr_glrender zcsr_input box2d::box2d)
      target_compile_features(demo_physics PRIVATE c_std_11)
    endif()
  endif()
endif()

# --- Zero-coupling guard: fail if a module includes another module ---
add_custom_target(check_coupling
  COMMAND ${CMAKE_SOURCE_DIR}/tools/check_no_cross_includes.sh
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  COMMENT "Verifying no cross-module includes")

# --- A2: embed PerMonitorV2 DPI manifest into every Windows executable ---
if(WIN32)
  enable_language(RC)
  get_property(ZCSR_ALL_TARGETS DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
  foreach(target_name IN LISTS ZCSR_ALL_TARGETS)
    get_target_property(target_type "${target_name}" TYPE)
    if(target_type STREQUAL "EXECUTABLE")
      target_sources(
        "${target_name}"
        PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/windows/zcsr.rc"
      )
      # Disable CMake's default auto-generated manifest so the RC-embedded
      # PerMonitorV2 manifest is the only one (avoids LNK1123 duplicate resource).
      set_target_properties("${target_name}" PROPERTIES LINK_FLAGS "/MANIFEST:NO")
    endif()
  endforeach()
endif()
