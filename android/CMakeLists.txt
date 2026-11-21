cmake_minimum_required(VERSION 3.22)
project(zcsr_phase0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(NOT ANDROID_ABI STREQUAL "arm64-v8a")
    message(FATAL_ERROR "T2 only supports ABI arm64-v8a; got: ${ANDROID_ABI}")
endif()

# zcsr repo root: android/ -> ..
set(ZCSR_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")

if(DEFINED CMAKE_ANDROID_NDK AND NOT CMAKE_ANDROID_NDK STREQUAL "")
    set(ZCSR_NDK_ROOT "${CMAKE_ANDROID_NDK}")
elseif(DEFINED ENV{ANDROID_NDK_HOME} AND NOT "$ENV{ANDROID_NDK_HOME}" STREQUAL "")
    set(ZCSR_NDK_ROOT "$ENV{ANDROID_NDK_HOME}")
else()
    message(FATAL_ERROR "Cannot determine NDK root. Set ANDROID_NDK_HOME.")
endif()

set(NATIVE_APP_GLUE_DIR "${ZCSR_NDK_ROOT}/sources/android/native_app_glue")

if(NOT EXISTS "${NATIVE_APP_GLUE_DIR}/android_native_app_glue.c")
    message(FATAL_ERROR "native_app_glue source not found: ${NATIVE_APP_GLUE_DIR}/android_native_app_glue.c")
endif()
if(NOT EXISTS "${NATIVE_APP_GLUE_DIR}/android_native_app_glue.h")
    message(FATAL_ERROR "native_app_glue header not found: ${NATIVE_APP_GLUE_DIR}/android_native_app_glue.h")
endif()

file(GLOB_RECURSE ZCSR_ENGINE_SOURCES CONFIGURE_DEPENDS
    "${ZCSR_ROOT}/modules/*.c"
    "${ZCSR_ROOT}/modules/*/*.c"
    "${ZCSR_ROOT}/third_party/glad/src/glad.c"
)
list(FILTER ZCSR_ENGINE_SOURCES EXCLUDE REGEX
    "/(platform_win32|platform_x11|platform_wayland|glrender_platform_glx|glrender_platform_wgl|glrender_platform_egl_wayland|audiomix_pipewire|inputext_wayland)\\.c$")

add_library(zcsr_phase0 SHARED
    "${CMAKE_CURRENT_SOURCE_DIR}/android_main.c"
    ${ZCSR_ENGINE_SOURCES}
    "${NATIVE_APP_GLUE_DIR}/android_native_app_glue.c"
)

target_include_directories(zcsr_phase0 PRIVATE
    "${ZCSR_ROOT}/include"
    "${ZCSR_ROOT}/third_party/glad/include"
    "${NATIVE_APP_GLUE_DIR}"
)

target_link_libraries(zcsr_phase0 PRIVATE
    android
    log
    EGL
    GLESv3
    dl
    m
)

set_target_properties(zcsr_phase0 PROPERTIES
    OUTPUT_NAME "zcsr_phase0"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)
