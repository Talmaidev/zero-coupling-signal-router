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
    zcsr_overlay
    zcsr_platform
    zcsr_input
    zcsr_audio
    zcsr_gamelogic
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
    zcsr_app_shell
    zcsr_camera
    zcsr_camera3d
    zcsr_sprite
    zcsr_runtime_viewport
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
    set_tests_properties(${name} PROPERTIES TIMEOUT 120)
endfunction()

zcsr_add_engine_test(atlas test_atlas.c)
zcsr_add_engine_test(audio test_audio.c)
zcsr_add_engine_test(audiomix test_audiomix.c)
zcsr_add_engine_test(culling test_culling.c)
zcsr_add_engine_test(font test_font.c)
zcsr_add_engine_test(gamelogic test_gamelogic.c)
zcsr_add_engine_test(glrender test_glrender.c)
zcsr_add_engine_test(hsm test_hsm.c)
zcsr_add_engine_test(image test_image.c)
zcsr_add_engine_test(input test_input.c)
zcsr_add_engine_test(inputext test_inputext.c)
zcsr_add_engine_test(integration test_integration.c)
zcsr_add_engine_test(log test_log.c)
zcsr_add_engine_test(overlay test_overlay.c)
zcsr_add_engine_test(platform test_platform.c)
zcsr_add_engine_test(signal test_signal.c)
zcsr_add_engine_test(state_reuse test_state_reuse.c)
zcsr_add_engine_test(state test_state.c)
zcsr_add_engine_test(game_settings_overrides test_game_settings_overrides.c)
