cmake_minimum_required(VERSION 3.16)

# --- Hard guard: the build tree must live OUTSIDE the source tree ------------
# ZCSR repositories (and their Linux checkouts)
# must stay free of build output: no in-source builds, no in-repo build/ or
# build-*/ directories, no scratch CMakeCache.txt. This is enforced here at
# configure time so a stale command line cannot silently re-pollute the tree.
#
# Guard interval: CMAKE_BINARY_DIR == CMAKE_SOURCE_DIR, or CMAKE_BINARY_DIR is
# located underneath CMAKE_SOURCE_DIR. Comparison is case-insensitive on
# Windows (paths there are case-insensitive) and separator-aware, so a sibling
# directory whose name merely starts with the source path is still allowed.
if(CMAKE_BINARY_DIR)
  # Normalise to forward slashes first (file(TO_CMAKE_PATH) exists since 3.16,
  # unlike cmake_path, which would raise the minimum CMake version).
  file(TO_CMAKE_PATH "${CMAKE_SOURCE_DIR}" _zcsr_guard_src)
  file(TO_CMAKE_PATH "${CMAKE_BINARY_DIR}" _zcsr_guard_bin)
  if(WIN32)
    string(TOLOWER "${_zcsr_guard_src}" _zcsr_guard_src_cmp)
    string(TOLOWER "${_zcsr_guard_bin}" _zcsr_guard_bin_cmp)
  else()
    set(_zcsr_guard_src_cmp "${_zcsr_guard_src}")
    set(_zcsr_guard_bin_cmp "${_zcsr_guard_bin}")
  endif()
  string(LENGTH "${_zcsr_guard_src_cmp}" _zcsr_guard_src_len)
  string(SUBSTRING "${_zcsr_guard_bin_cmp}" 0 ${_zcsr_guard_src_len}
         _zcsr_guard_bin_head)
  set(_zcsr_guard_in_repo FALSE)
  if(_zcsr_guard_bin_cmp STREQUAL _zcsr_guard_src_cmp)
    set(_zcsr_guard_in_repo TRUE)
  elseif(_zcsr_guard_bin_head STREQUAL _zcsr_guard_src_cmp)
    string(SUBSTRING "${_zcsr_guard_bin_cmp}" ${_zcsr_guard_src_len} 1
           _zcsr_guard_sep)
    if(_zcsr_guard_sep STREQUAL "/" OR _zcsr_guard_sep STREQUAL "\\")
      set(_zcsr_guard_in_repo TRUE)
    endif()
  endif()
  if(_zcsr_guard_in_repo)
    get_filename_component(_zcsr_repo_name "${CMAKE_CURRENT_SOURCE_DIR}" NAME)
    get_filename_component(_zcsr_default_build_root
      "${CMAKE_CURRENT_SOURCE_DIR}/../../build" ABSOLUTE)
    message(FATAL_ERROR
      "In-repo build directory refused by ZCSR build-layout guard.\n"
      "  CMAKE_SOURCE_DIR = ${CMAKE_SOURCE_DIR}\n"
      "  CMAKE_BINARY_DIR = ${CMAKE_BINARY_DIR}\n"
      "\n"
      "Build trees must live outside the source tree; the checkout must stay clean.\n"
      "Use any absolute out-of-tree build directory. The portable default is:\n"
      "  ${_zcsr_default_build_root}/zcsr/${_zcsr_repo_name}/<preset>\n"
      "\n"
      "Example:\n"
      "  cmake -S . -B \"${_zcsr_default_build_root}/zcsr/${_zcsr_repo_name}/win-msvc\"\n"
      "  cmake -S . -B \"${_zcsr_default_build_root}/zcsr/${_zcsr_repo_name}/linux-gcc\"\n"
      "\n"
      "Pass -B explicitly (or set ZCSR_BUILD_ROOT in your own wrapper) to use\n"
      "another out-of-tree root.\n")
  endif()
  unset(_zcsr_repo_name)
  unset(_zcsr_default_build_root)
  unset(_zcsr_guard_src)
  unset(_zcsr_guard_bin)
  unset(_zcsr_guard_src_cmp)
  unset(_zcsr_guard_bin_cmp)
  unset(_zcsr_guard_src_len)
  unset(_zcsr_guard_bin_head)
  unset(_zcsr_guard_sep)
  unset(_zcsr_guard_in_repo)
endif()

set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING
    "Minimum supported macOS version")

project(zcsr VERSION 1.0.0 LANGUAGES C)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

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

# --- Three independent render axes (compile-time) ---
# No profile bundle: each axis is its own cache STRING and maps to the number
# the tier headers expect. No axis implies another; everything resolves through
# compile-time `#if` — there is no runtime tier table.
#
#   ZCSR_INSTANCE_LAYOUT : full | compact  -> FULL(0) / COMPACT(2)
#   ZCSR_COLOR_TIER      : linear | direct -> LINEAR_HDR(0) / DIRECT_SRGB(1)
#   ZCSR_SORT_TIER       : key | none      -> KEY(1) / NONE(0)
set(ZCSR_INSTANCE_LAYOUT "full" CACHE STRING "Instance record layout: full | compact")
set_property(CACHE ZCSR_INSTANCE_LAYOUT PROPERTY STRINGS full compact)
set(ZCSR_COLOR_TIER "linear" CACHE STRING "Color pipeline tier: linear | direct")
set_property(CACHE ZCSR_COLOR_TIER PROPERTY STRINGS linear direct)
set(ZCSR_SORT_TIER "key" CACHE STRING "Sort stage tier: key | none")
set_property(CACHE ZCSR_SORT_TIER PROPERTY STRINGS key none)

if(ZCSR_INSTANCE_LAYOUT STREQUAL "compact")
  set(_zcsr_layout_num 2)   # ZCSR_LAYOUT_COMPACT
else()
  set(_zcsr_layout_num 0)   # ZCSR_LAYOUT_FULL
endif()
if(ZCSR_COLOR_TIER STREQUAL "direct")
  set(_zcsr_color_num 1)    # ZCSR_COLOR_OPT_DIRECT_SRGB
else()
  set(_zcsr_color_num 0)   # ZCSR_COLOR_OPT_LINEAR_HDR
endif()
if(ZCSR_SORT_TIER STREQUAL "none")
  set(_zcsr_sort_num 0)     # ZCSR_SORT_TIER_NONE
else()
  set(_zcsr_sort_num 1)     # ZCSR_SORT_TIER_KEY
endif()

# Global definitions (not scoped to the glrender target) so module TUs and
# every contract test inherit the same tier assembly.
add_compile_definitions(ZCSR_INSTANCE_LAYOUT=${_zcsr_layout_num})
add_compile_definitions(ZCSR_COLOR_TIER=${_zcsr_color_num})
add_compile_definitions(ZCSR_SORT_TIER=${_zcsr_sort_num})

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
target_include_directories(zcsr_contracts INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

# --- Optional media prebuilt dependencies (FFmpeg, libwebp) -------------------
# Probed, never fatal: a missing dependency leaves the matching backend compiled
# as an honest fail-closed fallback (caps/supported = false) and the build still
# succeeds. Cache overrides: ZCSR_DEPS_ROOT, ZCSR_FFMPEG_ROOT, ZCSR_LIBWEBP_ROOT.
if(WIN32)
  set(ZCSR_MEDIA_DEPS_PLATFORM "win64")
else()
  set(ZCSR_MEDIA_DEPS_PLATFORM "linux64")
endif()
if(NOT DEFINED ZCSR_DEPS_ROOT OR ZCSR_DEPS_ROOT STREQUAL "")
  set(ZCSR_DEPS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/prebuilt/${ZCSR_MEDIA_DEPS_PLATFORM}")
endif()
if(NOT DEFINED ZCSR_FFMPEG_ROOT OR ZCSR_FFMPEG_ROOT STREQUAL "")
  set(ZCSR_FFMPEG_ROOT "${ZCSR_DEPS_ROOT}/ffmpeg")
endif()
if(NOT DEFINED ZCSR_LIBWEBP_ROOT OR ZCSR_LIBWEBP_ROOT STREQUAL "")
  set(ZCSR_LIBWEBP_ROOT "${ZCSR_DEPS_ROOT}/libwebp")
endif()

# Header + library lookup: a prebuilt root keeps headers directly under
# <root>/include, but a distro package lands them in the multiarch dir
# (Debian/Ubuntu: /usr/include/x86_64-linux-gnu) or a subdir (Fedora:
# /usr/include/ffmpeg). Both shapes are probed, so pointing the roots at /usr is
# enough after a plain `apt install libavcodec-dev` / `dnf install ffmpeg-devel`.
#
# A backend is enabled ONLY when BOTH its headers AND every required link
# library are present. A header-only match (dev headers without the import
# archive / shared object) must NOT claim support: that would surface as an
# undefined-symbol failure at link time instead of an honest configure-time
# fail-closed. Missing headers and missing libraries are listed item by item
# in the configure output.
set(ZCSR_MEDIA_ARCH_SUFFIX "")
if(CMAKE_LIBRARY_ARCHITECTURE)
  set(ZCSR_MEDIA_ARCH_SUFFIX "${CMAKE_LIBRARY_ARCHITECTURE}")
endif()

# ---- FFmpeg: header + 5 libraries (avcodec/avformat/avutil/swscale/swresample) ----
unset(ZCSR_AVCODEC_INCLUDE_DIR CACHE)
find_path(ZCSR_AVCODEC_INCLUDE_DIR
  NAMES libavcodec/avcodec.h
  PATHS "${ZCSR_FFMPEG_ROOT}/include"
        "${ZCSR_FFMPEG_ROOT}/include/${ZCSR_MEDIA_ARCH_SUFFIX}"
        "${ZCSR_FFMPEG_ROOT}/include/ffmpeg"
  NO_DEFAULT_PATH)
if(NOT ZCSR_AVCODEC_INCLUDE_DIR)
  # Distro package under /usr: fall back to the default system include search.
  find_path(ZCSR_AVCODEC_INCLUDE_DIR NAMES libavcodec/avcodec.h)
endif()

set(ZCSR_FFMPEG_LIB_VARS "")
foreach(_ff_lib IN ITEMS avcodec avformat avutil swscale swresample)
  string(TOUPPER "${_ff_lib}" _ff_up)
  unset(ZCSR_${_ff_up}_LIBRARY CACHE)
  find_library(ZCSR_${_ff_up}_LIBRARY
    NAMES ${_ff_lib} ${_ff_lib}-62 ${_ff_lib}-60 ${_ff_lib}-59 ${_ff_lib}-58
    PATHS "${ZCSR_FFMPEG_ROOT}/lib"
    NO_DEFAULT_PATH)
  if(NOT ZCSR_${_ff_up}_LIBRARY)
    find_library(ZCSR_${_ff_up}_LIBRARY NAMES ${_ff_lib})
  endif()
  list(APPEND ZCSR_FFMPEG_LIB_VARS ZCSR_${_ff_up}_LIBRARY)
endforeach()

# License compliance probe: this project links FFmpeg under the LGPL baseline.
# A GPL or nonfree build must NOT be enabled. Inspect ffmpeg -buildconf (falls
# back to -version) for --enable-gpl / --enable-nonfree. Detection only: this
# never installs or changes the system.
set(ZCSR_FFMPEG_LICENSE "unknown")
set(ZCSR_FFMPEG_GPL FALSE)
set(ZCSR_FFMPEG_NONFREE FALSE)
unset(ZCSR_FFMPEG_TOOL CACHE)
find_program(ZCSR_FFMPEG_TOOL ffmpeg
  PATHS "${ZCSR_FFMPEG_ROOT}/bin" NO_DEFAULT_PATH)
if(NOT ZCSR_FFMPEG_TOOL)
  find_program(ZCSR_FFMPEG_TOOL ffmpeg)
endif()
if(ZCSR_FFMPEG_TOOL)
  execute_process(
    COMMAND ${ZCSR_FFMPEG_TOOL} -buildconf
    OUTPUT_VARIABLE _ff_buildconf
    ERROR_VARIABLE _ff_buildconf_err
    RESULT_VARIABLE _ff_bc_rc)
  if(NOT _ff_bc_rc EQUAL 0 OR NOT _ff_buildconf)
    execute_process(
      COMMAND ${ZCSR_FFMPEG_TOOL} -version
      OUTPUT_VARIABLE _ff_buildconf
      ERROR_VARIABLE _ff_version_err
      RESULT_VARIABLE _ff_ver_rc)
  endif()
  if(_ff_buildconf MATCHES "--enable-gpl")
    set(ZCSR_FFMPEG_GPL TRUE)
  endif()
  if(_ff_buildconf MATCHES "--enable-nonfree")
    set(ZCSR_FFMPEG_NONFREE TRUE)
  endif()
  if(ZCSR_FFMPEG_GPL)
    set(ZCSR_FFMPEG_LICENSE "GPL")
  elseif(ZCSR_FFMPEG_NONFREE)
    set(ZCSR_FFMPEG_LICENSE "nonfree")
  else()
    set(ZCSR_FFMPEG_LICENSE "LGPL")
  endif()
endif()

set(ZCSR_FFMPEG_MISSING "")
if(NOT ZCSR_AVCODEC_INCLUDE_DIR)
  list(APPEND ZCSR_FFMPEG_MISSING "header libavcodec/avcodec.h")
endif()
foreach(_ff_lib IN ITEMS avcodec avformat avutil swscale swresample)
  string(TOUPPER "${_ff_lib}" _ff_up)
  if(NOT ZCSR_${_ff_up}_LIBRARY)
    list(APPEND ZCSR_FFMPEG_MISSING "library lib${_ff_lib}")
  endif()
endforeach()
set(ZCSR_FFMPEG_FOUND TRUE)
if(ZCSR_FFMPEG_MISSING)
  set(ZCSR_FFMPEG_FOUND FALSE)
endif()
if(ZCSR_FFMPEG_GPL OR ZCSR_FFMPEG_NONFREE)
  set(ZCSR_FFMPEG_FOUND FALSE)
endif()

# ---- libwebp: header + 3 libraries (webp / webpdemux / sharpyuv) ----
unset(ZCSR_WEBP_INCLUDE_DIR CACHE)
find_path(ZCSR_WEBP_INCLUDE_DIR
  NAMES webp/decode.h
  PATHS "${ZCSR_LIBWEBP_ROOT}/include"
        "${ZCSR_LIBWEBP_ROOT}/include/${ZCSR_MEDIA_ARCH_SUFFIX}"
  NO_DEFAULT_PATH)
if(NOT ZCSR_WEBP_INCLUDE_DIR)
  find_path(ZCSR_WEBP_INCLUDE_DIR NAMES webp/decode.h)
endif()

set(ZCSR_WEBP_LIB_VARS "")
foreach(_wp_lib IN ITEMS webp webpdemux sharpyuv)
  string(TOUPPER "${_wp_lib}" _wp_up)
  unset(ZCSR_${_wp_up}_LIBRARY CACHE)
  find_library(ZCSR_${_wp_up}_LIBRARY
    NAMES ${_wp_lib} lib${_wp_lib}
    PATHS "${ZCSR_LIBWEBP_ROOT}/lib"
    NO_DEFAULT_PATH)
  if(NOT ZCSR_${_wp_up}_LIBRARY)
    find_library(ZCSR_${_wp_up}_LIBRARY NAMES ${_wp_lib} lib${_wp_lib})
  endif()
  list(APPEND ZCSR_WEBP_LIB_VARS ZCSR_${_wp_up}_LIBRARY)
endforeach()

set(ZCSR_WEBP_MISSING "")
if(NOT ZCSR_WEBP_INCLUDE_DIR)
  list(APPEND ZCSR_WEBP_MISSING "header webp/decode.h")
endif()
foreach(_wp_lib IN ITEMS webp webpdemux sharpyuv)
  string(TOUPPER "${_wp_lib}" _wp_up)
  if(NOT ZCSR_${_wp_up}_LIBRARY)
    list(APPEND ZCSR_WEBP_MISSING "library lib${_wp_lib}")
  endif()
endforeach()
set(ZCSR_LIBWEBP_FOUND TRUE)
if(ZCSR_WEBP_MISSING)
  set(ZCSR_LIBWEBP_FOUND FALSE)
endif()

# FFmpeg: AAC/M4A/Opus music backend + video fallback/WebM-WebP backend.
add_library(zcsr_ffmpeg INTERFACE)
if(ZCSR_FFMPEG_FOUND)
  target_include_directories(zcsr_ffmpeg INTERFACE
    $<BUILD_INTERFACE:${ZCSR_AVCODEC_INCLUDE_DIR}>)
  target_compile_definitions(zcsr_ffmpeg INTERFACE ZCSR_HAVE_FFMPEG)
  if(NOT WIN32)
    target_link_directories(zcsr_ffmpeg INTERFACE
      $<BUILD_INTERFACE:${ZCSR_FFMPEG_ROOT}/lib>)
  endif()
  target_link_libraries(zcsr_ffmpeg INTERFACE
    ${ZCSR_AVCODEC_LIBRARY} ${ZCSR_AVFORMAT_LIBRARY} ${ZCSR_AVUTIL_LIBRARY}
    ${ZCSR_SWSCALE_LIBRARY} ${ZCSR_SWRESAMPLE_LIBRARY})
  message(STATUS "FFmpeg backend enabled: ${ZCSR_FFMPEG_ROOT} (license=${ZCSR_FFMPEG_LICENSE})")
else()
  if(ZCSR_FFMPEG_GPL OR ZCSR_FFMPEG_NONFREE)
    message(WARNING
      "FFmpeg backend DISABLED: detected a ${ZCSR_FFMPEG_LICENSE} build "
      "(${ZCSR_FFMPEG_TOOL}). This is GPL/nonfree, NOT the required LGPL "
      "variant, so it is non-compliant and must not be linked. Use an LGPL "
      "shared build (BtbN FFmpeg.LGPL.Shared / vcpkg x64-windows / distro LGPL).")
  else()
    message(STATUS "FFmpeg backend DISABLED (fail-closed) under ${ZCSR_FFMPEG_ROOT}.")
    foreach(_ff_missing IN LISTS ZCSR_FFMPEG_MISSING)
      message(STATUS "  missing: ${_ff_missing}")
    endforeach()
    message(STATUS "  music AAC/M4A/Opus and the video FFmpeg fallback are disabled.")
  endif()
endif()

# libwebp: static WebP static/animated image backend.
add_library(zcsr_libwebp INTERFACE)
if(ZCSR_LIBWEBP_FOUND)
  target_include_directories(zcsr_libwebp INTERFACE
    $<BUILD_INTERFACE:${ZCSR_WEBP_INCLUDE_DIR}>)
  target_compile_definitions(zcsr_libwebp INTERFACE ZCSR_HAVE_LIBWEBP)
  if(NOT WIN32)
    target_link_directories(zcsr_libwebp INTERFACE
      $<BUILD_INTERFACE:${ZCSR_LIBWEBP_ROOT}/lib>)
  endif()
  target_link_libraries(zcsr_libwebp INTERFACE
    ${ZCSR_WEBP_LIBRARY} ${ZCSR_WEBPDEMUX_LIBRARY} ${ZCSR_SHARPYUV_LIBRARY})
  message(STATUS "libwebp backend enabled: ${ZCSR_LIBWEBP_ROOT}")
else()
  message(STATUS "libwebp backend DISABLED (fail-closed) under ${ZCSR_LIBWEBP_ROOT}.")
  foreach(_wp_missing IN LISTS ZCSR_WEBP_MISSING)
    message(STATUS "  missing: ${_wp_missing}")
  endforeach()
  message(STATUS "  WebP decode and animated WebP are disabled.")
endif()

# Keep the wrapper interfaces in the install/export set so install(EXPORT) does
# not complain that an installed module requires a non-exported target.
set_property(GLOBAL APPEND PROPERTY ZCSR_INSTALL_TARGETS zcsr_ffmpeg zcsr_libwebp)

# Linux test executables must resolve the shared libav*.so at run time; add the
# prebuilt lib dir to the build rpath. Windows resolves DLLs via the POST_BUILD
# copy below (zcsr_copy_ffmpeg_runtime).
if(NOT WIN32 AND ZCSR_FFMPEG_FOUND)
  list(APPEND CMAKE_BUILD_RPATH "${ZCSR_FFMPEG_ROOT}/lib")
endif()

# Copies the FFmpeg shared DLLs next to a target after build. No-op off Windows.
function(zcsr_copy_ffmpeg_runtime target)
  if(WIN32 AND ZCSR_FFMPEG_FOUND)
    add_custom_command(TARGET ${target} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${ZCSR_FFMPEG_ROOT}/bin" "$<TARGET_FILE_DIR:${target}>"
      COMMENT "Copying FFmpeg runtime DLLs next to ${target}")
  endif()
endfunction()

# --- Each module compiles independently, seeing ONLY the contracts (zero-coupling) ---
function(zcsr_module name)
  file(GLOB SRC CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/modules/${name}/*.c)
  if(APPLE)
    file(GLOB MSRC CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/modules/${name}/*.m)
    list(APPEND SRC ${MSRC})
  endif()
  add_library(zcsr_${name} STATIC ${SRC})
  target_link_libraries(zcsr_${name} PUBLIC zcsr_contracts)
  set_property(GLOBAL APPEND PROPERTY ZCSR_INSTALL_TARGETS zcsr_${name})
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
zcsr_module(light2d_environment) # pure environment/sun/ambient contract (no GL calls)
if(UNIX AND NOT APPLE)
  # sqrtf in sun normalization/shadow length; MSVC links it via the CRT.
  target_link_libraries(zcsr_light2d_environment PUBLIC m)
endif()
zcsr_module(light2d)   # Light2D additive lighting (no GL calls; drives glrender via gl_render_light2d_ext.h)
target_link_libraries(zcsr_light2d PUBLIC zcsr_glrender zcsr_light2d_environment)
target_include_directories(zcsr_light2d PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/glad/include)
zcsr_module(image)
if(ZCSR_LIBWEBP_FOUND)
  target_link_libraries(zcsr_image PUBLIC zcsr_libwebp)
endif()
zcsr_module(assets)   # 开发者自带资源导入（相对路径 -> 纹理/字体/WAV）
target_link_libraries(zcsr_assets PUBLIC zcsr_image zcsr_glrender zcsr_audiomix)     # decode→RGBA32 + offline CPU processing (separated from rendering; vendors stb_image)
zcsr_module(atlas)     # Texture atlas shelf-packing + region table (pure logic, no GL)
zcsr_module(culling)   # Viewport culling: linear + uniform grid (pure logic, no GL)
zcsr_module(instance_sort) # Pipeline-stage instance order sort (4 B key / index, pure CPU)
zcsr_module(font)      # TrueType font loader/rasterizer (stb_truetype, no GL dependency)
# glrender calls surface/font/atlas entry points; declare the dependency so
# GNU ld orders them after libzcsr_glrender.a instead of reporting undefined
# references when a consumer links only zcsr_glrender.
target_link_libraries(zcsr_glrender PUBLIC zcsr_platform zcsr_font zcsr_atlas zcsr_color_pipeline)
if(UNIX AND NOT APPLE)
  target_link_libraries(zcsr_font PUBLIC m)  # stb_truetype uses sqrt/floor/ceil/fmod/pow
endif()
zcsr_module(music)     # Streaming music player (OGG/MP3, depends on audiomix)
# --- Video module: MF primary (Windows MP4/H.264), FFmpeg fallback + WebM/VP9 ---
# zcsr_module(video) globs non-recursive modules/video/*.c. video_ff.c compiles
# on every host; video_mf.c is Windows-only and is hidden as a header-only TU
# off Windows so the glob does not compile an empty translation unit under /WX.
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/modules/video/video.c)
  zcsr_module(video)
  if(ZCSR_FFMPEG_FOUND)
    target_link_libraries(zcsr_video PUBLIC zcsr_ffmpeg)
    # video_ff.c gates its real backend on this video-specific macro.
    target_compile_definitions(zcsr_video PRIVATE ZCSR_VIDEO_HAVE_FFMPEG)
  endif()
  if(WIN32)
    # MF source reader + decoder MFTs (MP4/H.264 primary backend).
    target_link_libraries(zcsr_video PUBLIC
      mfplat mfreadwrite mfuuid propsys ole32 uuid)
  else()
    # MF backend TU is Windows-only; hide it on Linux/macOS/Android.
    set_source_files_properties(
      ${CMAKE_CURRENT_SOURCE_DIR}/modules/video/video_mf.c
      PROPERTIES HEADER_FILE_ONLY TRUE)
  endif()
endif()
zcsr_module(ui)        # Generic UI runtime (node/input/focus/apply/preset/draw-list + GL backend)
zcsr_module(camera)    # 2.5D orthographic camera (world units + shear)
if(UNIX AND NOT APPLE)
  target_link_libraries(zcsr_camera PUBLIC m) # camera pitch uses cosf/sinf/atan2f
endif()
zcsr_module(camera3d)  # software perspective camera (3D -> screen px)
if(UNIX AND NOT APPLE)
  target_link_libraries(zcsr_camera3d PUBLIC m) # camera3d uses cosf/sinf/tanf/atan2f/sqrtf
endif()
zcsr_module(scene_camera) # 2.5D scene camera: yaw/pitch/roll/ortho + screen->ground
if(UNIX AND NOT APPLE)
  target_link_libraries(zcsr_scene_camera PUBLIC m)
endif()
zcsr_module(scene)        # fixed-capacity 2.5D scene entity list + stable order
target_link_libraries(zcsr_scene PUBLIC zcsr_scene_camera)
zcsr_module(scene_io)     # versioned .uiscene text IO shared by editor/player
target_link_libraries(zcsr_scene_io PUBLIC zcsr_scene)
zcsr_module(scene_render) # scene entities -> ordered rectangle instances
target_link_libraries(zcsr_scene_render PUBLIC zcsr_scene zcsr_glrender)
zcsr_module(scene_submit) # unified scene -> path A display / path B Light2D bridge
target_link_libraries(zcsr_scene_submit PUBLIC zcsr_scene zcsr_scene_render zcsr_glrender)
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
if(ZCSR_FFMPEG_FOUND)
  # AAC (ADTS), AAC-in-MP4 (M4A), Ogg Opus decoders live behind the FFmpeg backend.
  target_link_libraries(zcsr_music PUBLIC zcsr_ffmpeg)
endif()
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
  # XRandR is optional: it is only used for display refresh reporting and
  # exclusive-fullscreen mode switching.  Window creation, input and GLX do
  # not require its headers, so a missing libxrandr-dev must not disable X11.
  pkg_check_modules(XRANDR xrandr)
  find_package(OpenGL)          # X11/GLX 需要 libGL（GLVND 的 OpenGL::OpenGL）

  set(ZCSR_LINUX_HAS_WAYLAND FALSE)
  if(WAYLAND_CLIENT_FOUND AND WAYLAND_SCANNER AND XDG_SHELL_XML AND XKBCOMMON_FOUND)
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
      message(FATAL_ERROR "ZCSR_LINUX_WINDOW_BACKEND=x11 但缺 X11/GL 开发包（libx11-dev + libgl1-mesa-dev；XRandR 可选，装 libxrandr-dev 才启用刷新率/独占全屏模式切换）")
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
    target_link_libraries(zcsr_platform PUBLIC ${X11_LIBRARIES} m)
    if(XRANDR_FOUND)
      target_compile_definitions(zcsr_platform PRIVATE ZCSR_HAVE_XRANDR)
      target_include_directories(zcsr_platform PRIVATE ${XRANDR_INCLUDE_DIRS})
      target_link_libraries(zcsr_platform PUBLIC ${XRANDR_LIBRARIES})
    endif()

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

# --- Install / export package -------------------------------------------------
# A consumer must be able to `find_package(zcsr CONFIG REQUIRED)` and link
# `zcsr::zcsr_scene`, `zcsr::zcsr_platform`, ... without sibling source paths.
get_property(ZCSR_ALL_MODULES GLOBAL PROPERTY ZCSR_INSTALL_TARGETS)
install(TARGETS zcsr_contracts ${ZCSR_ALL_MODULES}
  EXPORT zcsrTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT zcsrTargets
  FILE zcsrTargets.cmake
  NAMESPACE zcsr::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/zcsr)
configure_package_config_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/cmake/zcsrConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/zcsrConfig.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/zcsr)
write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/zcsrConfigVersion.cmake
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion)
install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/zcsrConfig.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/zcsrConfigVersion.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/zcsr)

# --- Repository-native contract tests ---
# Only when this repository is the top-level project. When it is consumed via
# add_subdirectory (for example by game_test), the consumer owns its test target
# names and must not collide with the engine's own CTest entries.
if(ZCSR_BUILD_TESTS AND CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  enable_testing()
  add_subdirectory(tests)
endif()
