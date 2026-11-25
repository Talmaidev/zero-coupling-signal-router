# Engine module tests that previously had no repository-native CTest entry.
# Each target links the real engine modules and their platform dependencies;
# these are not reduced to fake pure-CPU tests.
#
# Assertions stay active in Release builds: tests use assert() as their oracle,
# so an NDEBUG build would otherwise pass without checking anything.

set(ZCSR_ENGINE_TEST_LIBS
    zcsr_core
    zcsr_signal
    zcsr_hsm
    zcsr_platform
    zcsr_input
    zcsr_audio
    zcsr_collision
    zcsr_save
    zcsr_glrender
    zcsr_audiomix
    zcsr_inputext
    zcsr_light2d
    zcsr_image
    zcsr_assets
    zcsr_atlas
    zcsr_culling
    zcsr_font
    zcsr_music
    zcsr_ui
    zcsr_camera
    zcsr_camera3d
    zcsr_scene_camera
    zcsr_scene
    zcsr_scene_render
    zcsr_scene_submit
    zcsr_color_pipeline
    zcsr_sprite
    zcsr_viewport
    zcsr_file_service
    zcsr_resource
)

# Video module is registered conditionally at the root. Append it to the engine
# test link list only when the target exists; otherwise the image/music-only
# intermediate state would link a missing library. Guard lives after the list
# is set and before zcsr_add_engine_test() consumes it.
if(TARGET zcsr_video)
    list(APPEND ZCSR_ENGINE_TEST_LIBS zcsr_video)
endif()

set(ZCSR_TESTS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/..")

# Register an explicit CTest SKIP entry for a media/backend test whose dependency
# is absent. The entry still shows up in `ctest -N`; at run time it executes a
# no-op echo that exits the SKIP_RETURN_CODE, so CTest reports it as Skipped
# instead of letting an unlinked / runtime-failing binary count as a Failure.
# This is the "conditional registration + explicit skip evidence" contract: the
# test is never compiled against a missing backend, and the skip reason is
# printed both at configure time and in the CTest output line.
function(zcsr_add_skip_test name reason label)
    add_test(NAME ${name}
        COMMAND ${CMAKE_COMMAND} -E echo "SKIPPED [${label}]: ${reason}")
    set_tests_properties(${name} PROPERTIES
        SKIP_RETURN_CODE 0
        TIMEOUT 3
        LABELS "skip;${label}")
    message(STATUS "CTest ${name} registered as SKIPPED: ${reason}")
endfunction()

function(zcsr_add_engine_test name source)
    add_executable(${name}_test "${ZCSR_TESTS_DIR}/${source}")
    target_include_directories(${name}_test PRIVATE
        "${ZCSR_TESTS_DIR}"
        "${PROJECT_SOURCE_DIR}/include")
    target_link_libraries(${name}_test PRIVATE ${ZCSR_ENGINE_TEST_LIBS})
    if(MSVC)
        target_compile_options(${name}_test PRIVATE /UNDEBUG)
        target_link_libraries(${name}_test PRIVATE
            user32 gdi32 opengl32 winmm ole32 uuid avrt mmdevapi
            mfplat mfreadwrite mfuuid propsys xinput comctl32)
    else()
        target_compile_options(${name}_test PRIVATE -UNDEBUG)
    endif()
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/run_${name}")
    add_test(NAME ${name} COMMAND ${name}_test
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/run_${name}")
    set_tests_properties(${name} PROPERTIES
        TIMEOUT 120
        LABELS "module;${name}")
endfunction()

zcsr_add_engine_test(atlas test_atlas.c)
zcsr_add_engine_test(audio test_audio.c)
zcsr_add_engine_test(audiomix test_audiomix.c)
# Audio module tests are fast contracts, not hardware soak tests: hard 3s cap.
set_tests_properties(audio audiomix PROPERTIES
    TIMEOUT 3
    LABELS "module;audio;fast"
    RESOURCE_LOCK zcsr_audio_device)
# libpipewire keeps process-global allocations alive past pw_deinit(); LSan
# attributes them to <unknown module>. Keep ASan/UBSan on, opt out of LSan so
# the audio backend test does not report third-party state as a zcsr leak.
set_tests_properties(audiomix PROPERTIES ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0")
zcsr_add_engine_test(culling test_culling.c)
zcsr_add_engine_test(font test_font.c)

# font_ttf / font_otf: real file fixtures (tests/data/t0.ttf, t0.otf). The data
# dir is argv[1], so these are explicit targets rather than the no-argv macro.
foreach(fnt_t0 font_ttf font_otf)
    if(EXISTS "${ZCSR_TESTS_DIR}/test_${fnt_t0}.c")
        add_executable(${fnt_t0}_test "${ZCSR_TESTS_DIR}/test_${fnt_t0}.c")
        target_include_directories(${fnt_t0}_test PRIVATE
            "${ZCSR_TESTS_DIR}" "${PROJECT_SOURCE_DIR}/include")
        target_link_libraries(${fnt_t0}_test PRIVATE ${ZCSR_ENGINE_TEST_LIBS})
        if(MSVC)
            target_compile_options(${fnt_t0}_test PRIVATE /UNDEBUG)
            target_link_libraries(${fnt_t0}_test PRIVATE
                user32 gdi32 opengl32 winmm ole32 uuid avrt mmdevapi
                mfplat mfreadwrite mfuuid propsys xinput comctl32)
        else()
            target_compile_options(${fnt_t0}_test PRIVATE -UNDEBUG)
        endif()
        add_test(NAME ${fnt_t0} COMMAND ${fnt_t0}_test "${ZCSR_TESTS_DIR}/data")
        set_tests_properties(${fnt_t0} PROPERTIES
            TIMEOUT 10
            LABELS "module;font;fast")
    endif()
endforeach()

zcsr_add_engine_test(glrender test_glrender.c)
set_tests_properties(glrender PROPERTIES RESOURCE_LOCK zcsr_gui_display)
zcsr_add_engine_test(hsm test_hsm.c)
zcsr_add_engine_test(image test_image.c)
# image_hdr's capability-table contract asserts the WebP decode/animated slots
# are populated; without libwebp that table entry is false, so the test is
# registered as an explicit skip rather than run to failure.
if(EXISTS "${ZCSR_TESTS_DIR}/test_image_hdr.c")
    if(ZCSR_LIBWEBP_FOUND)
        zcsr_add_engine_test(image_hdr test_image_hdr.c)
    else()
        zcsr_add_skip_test(image_hdr
            "libwebp backend unavailable (ZCSR_LIBWEBP_FOUND=FALSE): image caps table reports WebP unsupported"
            "image")
    endif()
endif()
if(EXISTS "${ZCSR_TESTS_DIR}/test_image_ico.c")
    zcsr_add_engine_test(image_ico test_image_ico.c)
endif()

# Image T0 contracts: PNG / JPEG / GIF (static + animated) / WebP (static +
# animated) / BMP. These read the fixture dir from ZCSR_TEST_DATA_DIR (no argv);
# the macro runs from a per-test run_<name> build folder, so the env must point
# at the source-tree data dir explicitly.
foreach(img_t0 image_png image_jpeg image_gif image_gif_anim
              image_webp image_webp_anim image_bmp)
    if(NOT EXISTS "${ZCSR_TESTS_DIR}/test_${img_t0}.c")
        continue()
    endif()
    if(img_t0 MATCHES "webp")
        if(ZCSR_LIBWEBP_FOUND)
            zcsr_add_engine_test(${img_t0} test_${img_t0}.c)
        else()
            zcsr_add_skip_test(${img_t0}
                "libwebp backend unavailable (ZCSR_LIBWEBP_FOUND=FALSE): WebP static/animated decode disabled at configure time"
                "image")
        endif()
    else()
        zcsr_add_engine_test(${img_t0} test_${img_t0}.c)
    endif()
endforeach()
set(_IMG_ENV_TESTS image_png image_jpeg image_gif image_gif_anim image_bmp)
if(TEST image_webp)
    list(APPEND _IMG_ENV_TESTS image_webp)
endif()
if(TEST image_webp_anim)
    list(APPEND _IMG_ENV_TESTS image_webp_anim)
endif()
set_tests_properties(${_IMG_ENV_TESTS}
    PROPERTIES ENVIRONMENT "ZCSR_TEST_DATA_DIR=${ZCSR_TESTS_DIR}/data")
zcsr_add_engine_test(input test_input.c)
zcsr_add_engine_test(inputext test_inputext.c)
if(ZCSR_LINUX_BACKEND STREQUAL "x11")
    include(CheckIncludeFile)
    check_include_file("X11/extensions/XTest.h" ZCSR_HAVE_XTEST_H)
    find_library(ZCSR_XTST_LIBRARY Xtst)
    if(ZCSR_HAVE_XTEST_H AND ZCSR_XTST_LIBRARY)
        zcsr_add_engine_test(inputext_x11 test_inputext_x11.c)
        target_link_libraries(inputext_x11_test PRIVATE ${ZCSR_XTST_LIBRARY})
        set_tests_properties(inputext_x11 PROPERTIES RESOURCE_LOCK zcsr_gui_display)
    else()
        message(STATUS "inputext_x11_test skipped: X11/extensions/XTest.h or libXtst not found")
    endif()
endif()
zcsr_add_engine_test(log test_log.c)
zcsr_add_engine_test(log_default test_log_default.c)
zcsr_add_engine_test(log_file test_log_file.c)
zcsr_add_engine_test(log_crash test_log_crash.c)
zcsr_add_engine_test(log_event test_log_event.c)
zcsr_add_engine_test(log_fuzz test_log_fuzz.c)
zcsr_add_engine_test(platform test_platform.c)
set_tests_properties(platform PROPERTIES RESOURCE_LOCK zcsr_gui_display)
zcsr_add_engine_test(signal test_signal.c)
zcsr_add_engine_test(scene_camera test_scene_camera.c)
zcsr_add_engine_test(scene test_scene.c)
zcsr_add_engine_test(scene_render test_scene_render.c)
zcsr_add_engine_test(state_reuse test_state_reuse.c)
zcsr_add_engine_test(state test_state.c)

# Music T0 contracts: MP3 / AAC / M4A / WAV / FLAC / OGG Vorbis / Ogg Opus.
# The data dir is argv[1]; the shared helper music_t0_helper.c is compiled into
# every target. FFmpeg-backed formats (AAC/M4A/Opus) resolve their backend
# symbols through zcsr_music.
set(ZCSR_MUSIC_T0_SUFFIXES mp3 aac m4a wav flac ogg opus)
# AAC / M4A / Opus decode through the FFmpeg backend; without it these T0
# contracts cannot run and are registered as explicit skips rather than built
# against a missing backend.
set(ZCSR_FFMPEG_MUSIC_SUFFIXES aac m4a opus)
foreach(mus_suffix ${ZCSR_MUSIC_T0_SUFFIXES})
    if(mus_suffix STREQUAL "flac")
        set(mus_src "test_music_flac_t0.c")
        # T0 FLAC owns the spec CTest name `music_flac`; the executable target
        # keeps the _t0 suffix so it does not collide with the legacy target.
        set(mus_tgt "music_flac")
        set(mus_exe "music_flac_t0_test")
    else()
        set(mus_src "test_music_${mus_suffix}.c")
        set(mus_tgt "music_${mus_suffix}")
        set(mus_exe "music_${mus_suffix}_test")
    endif()
    if(NOT EXISTS "${ZCSR_TESTS_DIR}/${mus_src}")
        continue()
    endif()
    if(mus_suffix IN_LIST ZCSR_FFMPEG_MUSIC_SUFFIXES AND NOT ZCSR_FFMPEG_FOUND)
        zcsr_add_skip_test(${mus_tgt}
            "FFmpeg backend unavailable (ZCSR_FFMPEG_FOUND=FALSE): ${mus_suffix} decode disabled at configure time"
            "audio")
        continue()
    endif()
    add_executable(${mus_exe}
        "${ZCSR_TESTS_DIR}/${mus_src}"
        "${ZCSR_TESTS_DIR}/music_t0_helper.c")
    target_include_directories(${mus_exe} PRIVATE
        "${ZCSR_TESTS_DIR}" "${PROJECT_SOURCE_DIR}/include"
        "${PROJECT_SOURCE_DIR}/modules/music")
    target_link_libraries(${mus_exe} PRIVATE ${ZCSR_ENGINE_TEST_LIBS})
    if(MSVC)
        target_compile_options(${mus_exe} PRIVATE /UNDEBUG)
        target_link_libraries(${mus_exe} PRIVATE
            user32 gdi32 opengl32 winmm ole32 uuid avrt mmdevapi
            mfplat mfreadwrite mfuuid propsys xinput comctl32)
    else()
        target_compile_options(${mus_exe} PRIVATE -UNDEBUG)
    endif()
    add_test(NAME ${mus_tgt} COMMAND ${mus_exe} "${ZCSR_TESTS_DIR}/data")
    set_tests_properties(${mus_tgt} PROPERTIES
        TIMEOUT 8
        LABELS "module;audio;fast"
        RESOURCE_LOCK zcsr_audio_device
        ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0")
    zcsr_copy_ffmpeg_runtime(${mus_exe})
endforeach()

# Legacy FLAC contract (sample.flac, pre-T0 regression): kept under a distinct
# CTest name so it never collides with the T0 `music_flac` entry above.
if(EXISTS "${ZCSR_TESTS_DIR}/test_music_flac.c")
    add_executable(music_flac_legacy_test "${ZCSR_TESTS_DIR}/test_music_flac.c")
    target_include_directories(music_flac_legacy_test PRIVATE
        "${ZCSR_TESTS_DIR}" "${PROJECT_SOURCE_DIR}/include")
    target_link_libraries(music_flac_legacy_test PRIVATE ${ZCSR_ENGINE_TEST_LIBS})
    if(MSVC)
        target_compile_options(music_flac_legacy_test PRIVATE /UNDEBUG)
        target_link_libraries(music_flac_legacy_test PRIVATE
            user32 gdi32 opengl32 winmm ole32 uuid avrt mmdevapi)
    else()
        target_compile_options(music_flac_legacy_test PRIVATE -UNDEBUG)
    endif()
    add_test(NAME music_flac_legacy COMMAND music_flac_legacy_test
        "${ZCSR_TESTS_DIR}/data")
    set_tests_properties(music_flac_legacy PROPERTIES
        TIMEOUT 8
        LABELS "module;audio;fast"
        RESOURCE_LOCK zcsr_audio_device)
endif()

# Music negative contract: garbage / truncated / mislabeled inputs fail closed
# (detect false / load NULL) without crashing.
if(EXISTS "${ZCSR_TESTS_DIR}/test_music_negative.c")
    add_executable(music_negative_test
        "${ZCSR_TESTS_DIR}/test_music_negative.c"
        "${ZCSR_TESTS_DIR}/music_t0_helper.c")
    target_include_directories(music_negative_test PRIVATE
        "${ZCSR_TESTS_DIR}" "${PROJECT_SOURCE_DIR}/include"
        "${PROJECT_SOURCE_DIR}/modules/music")
    target_link_libraries(music_negative_test PRIVATE ${ZCSR_ENGINE_TEST_LIBS})
    if(MSVC)
        target_compile_options(music_negative_test PRIVATE /UNDEBUG)
        target_link_libraries(music_negative_test PRIVATE
            user32 gdi32 opengl32 winmm ole32 uuid avrt mmdevapi
            mfplat mfreadwrite mfuuid propsys xinput comctl32)
    else()
        target_compile_options(music_negative_test PRIVATE -UNDEBUG)
    endif()
    add_test(NAME music_negative COMMAND music_negative_test "${ZCSR_TESTS_DIR}/data")
    set_tests_properties(music_negative PROPERTIES
        TIMEOUT 8
        LABELS "module;audio;fast"
        RESOURCE_LOCK zcsr_audio_device
        ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0")
    zcsr_copy_ffmpeg_runtime(music_negative_test)
endif()

# music_flac_codec: standalone TU that defines DR_FLAC_IMPLEMENTATION and links
# dr_flac directly; no mixer, no audio device.
if(EXISTS "${ZCSR_TESTS_DIR}/test_music_flac_codec.c")
    add_executable(music_flac_codec_test "${ZCSR_TESTS_DIR}/test_music_flac_codec.c")
    target_include_directories(music_flac_codec_test PRIVATE
        "${PROJECT_SOURCE_DIR}/include" "${PROJECT_SOURCE_DIR}/modules/music")
    if(MSVC)
        target_compile_options(music_flac_codec_test PRIVATE /UNDEBUG)
    endif()
    if(WIN32)
        target_link_libraries(music_flac_codec_test PRIVATE ole32)
    endif()
    add_test(NAME music_flac_codec COMMAND music_flac_codec_test "${ZCSR_TESTS_DIR}/data")
    set_tests_properties(music_flac_codec PROPERTIES
        TIMEOUT 8
        LABELS "module;audio;fast")
endif()

# Video T0 contracts: MP4/H.264 and WebM/VP9. The fixture dir is passed both
# as argv[1] and through ZCSR_VIDEO_DATA_DIR (the tests check argv first, then
# the env). The CTest working directory is the per-test run_<name> folder, so
# neither fallback ".". 60s hard cap: decode + seek + failure-path contract.
#
# Real-backend gate (no video_null fallback may stand in for a real decoder).
# Both contracts' caps checks require the WebM/VP9 decoder to be reachable in
# addition to MP4/H.264; on this codebase that combined VP9/WebM capability is
# provided only by the FFmpeg backend. Media Foundation alone exposes H.264 but
# not VP9 here, so both video T0 tests gate on ZCSR_FFMPEG_FOUND. On Linux this
# is also mandatory: no FFmpeg means no real video backend, never video_null.
if(TARGET zcsr_video)
    set(_vid_backend_ok "${ZCSR_FFMPEG_FOUND}")

    foreach(vid_t0 video_mp4_h264 video_webm_vp9)
        if(NOT EXISTS "${ZCSR_TESTS_DIR}/test_${vid_t0}.c")
            continue()
        endif()
        if(NOT _vid_backend_ok)
            zcsr_add_skip_test(${vid_t0}
                "FFmpeg backend unavailable (ZCSR_FFMPEG_FOUND=FALSE): video caps require a reachable WebM/VP9 decoder; MF alone does not satisfy the contract"
                "video")
            continue()
        endif()
        add_executable(${vid_t0}_test "${ZCSR_TESTS_DIR}/test_${vid_t0}.c")
        target_include_directories(${vid_t0}_test PRIVATE
            "${ZCSR_TESTS_DIR}" "${PROJECT_SOURCE_DIR}/include")
        target_link_libraries(${vid_t0}_test PRIVATE ${ZCSR_ENGINE_TEST_LIBS})
        if(MSVC)
            target_compile_options(${vid_t0}_test PRIVATE /UNDEBUG)
            target_link_libraries(${vid_t0}_test PRIVATE
                user32 gdi32 opengl32 winmm ole32 uuid avrt mmdevapi
                mfplat mfreadwrite mfuuid propsys xinput comctl32)
        else()
            target_compile_options(${vid_t0}_test PRIVATE -UNDEBUG)
        endif()
        add_test(NAME ${vid_t0} COMMAND ${vid_t0}_test
            "${PROJECT_SOURCE_DIR}/tests/video_data")
        set_tests_properties(${vid_t0} PROPERTIES
            TIMEOUT 60
            LABELS "module;video;fast"
            ENVIRONMENT "ZCSR_VIDEO_DATA_DIR=${PROJECT_SOURCE_DIR}/tests/video_data;ASAN_OPTIONS=detect_leaks=0")
        zcsr_copy_ffmpeg_runtime(${vid_t0}_test)
    endforeach()
endif()

# --- Resolved orphan contract tests (gap-4 disposition; see the orphan report) ---
# Pure-CPU / headless contracts that link the real engine modules and run with
# no window, audio endpoint, or GPU surface.
zcsr_add_engine_test(assets_path assets_path_test.c)
zcsr_add_engine_test(file_service_paths test_file_service_paths.c)
zcsr_add_engine_test(mouse_edges test_mouse_edges.c)
zcsr_add_engine_test(music_logic test_music_logic.c)

# Hardware endpoint enumeration smoke. Labeled `hardware` and locked to the
# audio resource. Fail-closed: a backend without endpoints reports count 0 and
# the test still passes; it never opens a playback stream.
add_executable(audio_devices_test "${ZCSR_TESTS_DIR}/audio_devices_test.c")
target_include_directories(audio_devices_test PRIVATE
    "${ZCSR_TESTS_DIR}" "${PROJECT_SOURCE_DIR}/include")
target_link_libraries(audio_devices_test PRIVATE ${ZCSR_ENGINE_TEST_LIBS})
if(MSVC)
    target_compile_options(audio_devices_test PRIVATE /UNDEBUG)
    target_link_libraries(audio_devices_test PRIVATE
        user32 gdi32 opengl32 winmm ole32 uuid avrt mmdevapi
        mfplat mfreadwrite mfuuid propsys xinput comctl32)
else()
    target_compile_options(audio_devices_test PRIVATE -UNDEBUG)
endif()
add_test(NAME audio_devices COMMAND audio_devices_test)
set_tests_properties(audio_devices PROPERTIES
    TIMEOUT 8
    LABELS "module;audio;hardware"
    RESOURCE_LOCK zcsr_audio_device)

# Hardware-dependent endpoint switch: needs >=2 output endpoints and an active
# playback session, starts the mixer and switches the live output for ~4.4s.
# Registered as an explicit skip (hardware label); not run in headless/CI or on
# single-endpoint sessions. Source retained behind this gate.
zcsr_add_skip_test(audio_switch
    "hardware-dependent: needs >=2 audio output endpoints and an active playback session; skipped in headless/CI"
    "audio;hardware")
