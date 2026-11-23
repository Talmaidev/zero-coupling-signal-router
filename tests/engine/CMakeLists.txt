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

set(ZCSR_TESTS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/..")

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
zcsr_add_engine_test(glrender test_glrender.c)
set_tests_properties(glrender PROPERTIES RESOURCE_LOCK zcsr_gui_display)
zcsr_add_engine_test(hsm test_hsm.c)
zcsr_add_engine_test(image test_image.c)
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
