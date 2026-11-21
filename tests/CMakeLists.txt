# Repository-native engine contract tests.
# These targets build against the engine's own module targets; they do not
# depend on game_test or any external harness.

# Tests use assert() as their oracle; keep assertions active in Release builds.
if(MSVC)
    add_compile_options(/UNDEBUG)
else()
    add_compile_options(-UNDEBUG)
endif()

add_executable(core_test test_core.c)
target_link_libraries(core_test PRIVATE zcsr_core)
add_test(NAME core COMMAND core_test)

add_executable(input_keymap_test test_input_keymap.c)
target_link_libraries(input_keymap_test PRIVATE zcsr_input zcsr_platform zcsr_core)
if(WIN32)
    target_link_libraries(input_keymap_test PRIVATE user32 gdi32)
endif()
add_test(NAME input_keymap COMMAND input_keymap_test)

if(WIN32)
    # Real OS message path: PostMessage WM_KEYDOWN/WM_KEYUP into a Win32
    # surface and drain via zcsr_input_pump(); not a synthetic queue injection.
    add_executable(input_win32_pump_test test_input_win32_pump.c)
    target_link_libraries(input_win32_pump_test PRIVATE
        zcsr_input zcsr_platform zcsr_core user32 gdi32)
    add_test(NAME input_win32_pump COMMAND input_win32_pump_test)
    set_tests_properties(input_win32_pump PROPERTIES TIMEOUT 30 SKIP_RETURN_CODE 77)
endif()

add_executable(instance_alpha_sort_test test_instance_alpha_sort.c)
target_link_libraries(instance_alpha_sort_test PRIVATE
    zcsr_glrender zcsr_platform zcsr_font zcsr_atlas zcsr_image zcsr_core)
if(WIN32)
    target_link_libraries(instance_alpha_sort_test PRIVATE user32 gdi32 opengl32)
endif()
add_test(NAME instance_alpha_sort COMMAND instance_alpha_sort_test)

add_executable(ui_hit_test test_ui_hit.c)
target_link_libraries(ui_hit_test PRIVATE zcsr_ui zcsr_core)
add_test(NAME ui_hit COMMAND ui_hit_test)

add_executable(music_api_test test_music_api.c)
target_link_libraries(music_api_test PRIVATE zcsr_music zcsr_audiomix zcsr_core)
if(WIN32)
    target_link_libraries(music_api_test PRIVATE ole32)
endif()
add_test(NAME music_api COMMAND music_api_test "${CMAKE_CURRENT_SOURCE_DIR}/data")
# Same rationale as the audiomix test: libpipewire's global allocations outlive
# pw_deinit() and are reported as <unknown module> leaks. ASan/UBSan stay on.
set_tests_properties(music_api PROPERTIES ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0")

# Engine module tests (real module/platform dependencies, not pure-CPU fakes).
add_subdirectory(engine)

add_executable(collision_save_test test_collision_save.c)
target_link_libraries(collision_save_test PRIVATE zcsr_collision zcsr_save zcsr_core)
add_test(NAME collision_save COMMAND collision_save_test)

add_executable(viewport_test test_viewport.c)
target_link_libraries(viewport_test PRIVATE zcsr_viewport zcsr_core)
add_test(NAME viewport COMMAND viewport_test)
