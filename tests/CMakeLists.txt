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

add_executable(color_pipeline_test test_color_pipeline.c)
target_link_libraries(color_pipeline_test PRIVATE zcsr_color_pipeline zcsr_core)
target_include_directories(color_pipeline_test PRIVATE "${PROJECT_SOURCE_DIR}/include")
add_test(NAME color_pipeline COMMAND color_pipeline_test)

add_executable(light2d_environment_test test_light2d_environment.c)
target_link_libraries(light2d_environment_test PRIVATE zcsr_light2d_environment)
add_test(NAME light2d_environment COMMAND light2d_environment_test)
set_tests_properties(light2d_environment PROPERTIES
    TIMEOUT 3
    LABELS "module;light2d;fast")

# Pure CPU shadow/projection math; documents in docs/LIGHT2D_* reference this
# target, so it must be a real CTest entry rather than an orphan source file.
add_executable(light2d_math_test test_light2d_math.c)
target_link_libraries(light2d_math_test PRIVATE zcsr_camera zcsr_core)
target_include_directories(light2d_math_test PRIVATE "${PROJECT_SOURCE_DIR}/include")
add_test(NAME light2d_math COMMAND light2d_math_test)
set_tests_properties(light2d_math PROPERTIES
    TIMEOUT 3
    LABELS "module;light2d;fast")

add_executable(scene_submit_test test_scene_submit.c)
target_link_libraries(scene_submit_test PRIVATE zcsr_scene_submit)
add_test(NAME scene_submit COMMAND scene_submit_test)
set_tests_properties(scene_submit PROPERTIES
    TIMEOUT 3
    LABELS "module;scene;fast")

add_executable(ui_runtime_test test_ui_runtime.c)
target_link_libraries(ui_runtime_test PRIVATE zcsr_ui zcsr_core)
target_include_directories(ui_runtime_test PRIVATE "${PROJECT_SOURCE_DIR}/include")
add_test(NAME ui_runtime COMMAND ui_runtime_test)

if(MSVC)
    get_filename_component(ZCSR_MSVC_BIN "${CMAKE_AR}" DIRECTORY)
    find_program(ZCSR_DUMPBIN_EXECUTABLE dumpbin HINTS "${ZCSR_MSVC_BIN}" NO_DEFAULT_PATH)
    if(ZCSR_DUMPBIN_EXECUTABLE)
        add_test(NAME log_no_malloc
            COMMAND ${CMAKE_COMMAND}
                "-DLIB=$<TARGET_FILE:zcsr_core>"
                "-DTOOL=${ZCSR_DUMPBIN_EXECUTABLE}"
                "-DFLAVOR=windows"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/check_log_no_malloc.cmake")
        set_tests_properties(log_no_malloc PROPERTIES TIMEOUT 30)
    else()
        message(WARNING "log_no_malloc test not registered: dumpbin not found")
    endif()
endif()

# --- printf format gate (negative/positive fixture compile check) -----------
# Registered per docs/LOGGING_SPEC.md: on MSVC look for clang-cl and skip when
# it is absent; off MSVC drive the gate with CMAKE_C_COMPILER (-Wformat
# -Werror). Intermediate objects go to a per-gate folder under the build tree;
# the driver script asserts OUT_DIR is inside CMAKE_BINARY_DIR and refuses to
# write into the source tree otherwise.
if(ZCSR_ENABLE_FORMAT_GATE)
    set(_FG_DIR "${CMAKE_CURRENT_SOURCE_DIR}/format_gate")
    set(_FG_OUT_DIR "${CMAKE_BINARY_DIR}/format_gate")
    if(MSVC)
        find_program(ZCSR_CLANG_CL_EXECUTABLE clang-cl)
        if(ZCSR_CLANG_CL_EXECUTABLE)
            add_test(NAME format_gate
                COMMAND ${CMAKE_COMMAND}
                    -DCOMPILER=${ZCSR_CLANG_CL_EXECUTABLE}
                    -DFLAVOR=clang-cl
                    -DINCLUDE_DIR=${PROJECT_SOURCE_DIR}/include
                    -DBAD_SRC=${_FG_DIR}/bad_format.c
                    -DGOOD_SRC=${_FG_DIR}/good_format.c
                    -DOUT_DIR=${_FG_OUT_DIR}
                    -DBINARY_DIR=${CMAKE_BINARY_DIR}
                    -P ${_FG_DIR}/run_format_gate.cmake)
            set_tests_properties(format_gate PROPERTIES TIMEOUT 30)
        else()
            message(STATUS "format_gate test skipped: clang-cl not found (MSVC)")
        endif()
    else()
        add_test(NAME format_gate
            COMMAND ${CMAKE_COMMAND}
                -DCOMPILER=${CMAKE_C_COMPILER}
                -DFLAVOR=cc
                -DINCLUDE_DIR=${PROJECT_SOURCE_DIR}/include
                -DBAD_SRC=${_FG_DIR}/bad_format.c
                -DGOOD_SRC=${_FG_DIR}/good_format.c
                -DOUT_DIR=${_FG_OUT_DIR}
                -DBINARY_DIR=${CMAKE_BINARY_DIR}
                -P ${_FG_DIR}/run_format_gate.cmake)
        set_tests_properties(format_gate PROPERTIES TIMEOUT 30)
    endif()

    # clang-tidy variant: register only when the installed clang-tidy actually
    # lists bugprone-format-string. An incomplete clang-tidy build must not
    # register a test that cannot run the check (spec: detect before registering).
    find_program(ZCSR_CLANG_TIDY_EXECUTABLE clang-tidy
        HINTS "C:/Program Files/Microsoft Visual Studio/18/Professional/VC/Tools/Llvm/x64/bin")
    if(ZCSR_CLANG_TIDY_EXECUTABLE)
        execute_process(
            COMMAND ${ZCSR_CLANG_TIDY_EXECUTABLE} --list-checks
                    --checks=bugprone-format-string
            RESULT_VARIABLE _tidy_rc
            OUTPUT_VARIABLE _tidy_checks
            ERROR_VARIABLE _tidy_checks_err)
        if(_tidy_rc EQUAL 0 AND _tidy_checks MATCHES "bugprone-format-string")
            add_test(NAME clang_tidy_format
                COMMAND ${CMAKE_COMMAND}
                    -DCLANG_TIDY=${ZCSR_CLANG_TIDY_EXECUTABLE}
                    -DINCLUDE_DIR=${PROJECT_SOURCE_DIR}/include
                    -DBAD_SRC=${_FG_DIR}/bad_format.c
                    -DGOOD_SRC=${_FG_DIR}/good_format.c
                    -P ${_FG_DIR}/run_clang_tidy.cmake)
            set_tests_properties(clang_tidy_format PROPERTIES TIMEOUT 30)
        else()
            message(STATUS "clang_tidy_format skipped: clang-tidy lacks bugprone-format-string")
        endif()
    else()
        message(STATUS "clang_tidy_format skipped: clang-tidy not found")
    endif()
endif()

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
    set_tests_properties(input_win32_pump PROPERTIES
        TIMEOUT 30 SKIP_RETURN_CODE 77
        RESOURCE_LOCK zcsr_gui_display)
endif()

add_executable(instance_alpha_sort_test test_instance_alpha_sort.c)
target_link_libraries(instance_alpha_sort_test PRIVATE
    zcsr_glrender zcsr_culling zcsr_platform zcsr_font zcsr_atlas zcsr_image zcsr_core)
if(WIN32)
    target_link_libraries(instance_alpha_sort_test PRIVATE user32 gdi32 opengl32)
endif()
add_test(NAME instance_alpha_sort COMMAND instance_alpha_sort_test)
set_tests_properties(instance_alpha_sort PROPERTIES RESOURCE_LOCK zcsr_gui_display)

add_executable(ui_hit_test test_ui_hit.c)
target_link_libraries(ui_hit_test PRIVATE zcsr_ui zcsr_core)
add_test(NAME ui_hit COMMAND ui_hit_test)

add_executable(music_api_test test_music_api.c)
target_link_libraries(music_api_test PRIVATE zcsr_music zcsr_audiomix zcsr_core)
if(WIN32)
    target_link_libraries(music_api_test PRIVATE ole32)
endif()
# zcsr_music pulls the FFmpeg backend transitively; its runtime DLLs must land
# next to this executable (same POST_BUILD copy as the engine music/video tests).
zcsr_copy_ffmpeg_runtime(music_api_test)
add_test(NAME music_api COMMAND music_api_test "${CMAKE_CURRENT_SOURCE_DIR}/data")
# Same rationale as the audiomix test: libpipewire's global allocations outlive
# pw_deinit() and are reported as <unknown module> leaks. ASan/UBSan stay on.
set_tests_properties(music_api PROPERTIES
    ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0"
    TIMEOUT 8
    LABELS "module;audio;fast"
    RESOURCE_LOCK zcsr_audio_device)

# Engine module tests (real module/platform dependencies, not pure-CPU fakes).
add_subdirectory(engine)

add_executable(collision_save_test test_collision_save.c)
target_link_libraries(collision_save_test PRIVATE zcsr_collision zcsr_save zcsr_core)
add_test(NAME collision_save COMMAND collision_save_test)

add_executable(viewport_test test_viewport.c)
target_link_libraries(viewport_test PRIVATE zcsr_viewport zcsr_core)
add_test(NAME viewport COMMAND viewport_test)

# --- Repository hygiene gate (read-only, cross-platform cmake -P check) ---
# Scans this source tree for in-repo build/configure/stray leftovers. The gate
# writes nothing inside the source tree; it enumerates paths only. Registered
# under the "gate" label: run with `ctest -L gate`. The full 6-repo sweep on
# Windows remains the job of check_repo_hygiene.ps1; this CTest covers only the
# current repository so Linux checkouts (where the sibling repos are absent)
# never fail spuriously.
add_test(NAME repo_hygiene
    COMMAND ${CMAKE_COMMAND}
        -DSOURCE_DIR=${PROJECT_SOURCE_DIR}
        -P ${PROJECT_SOURCE_DIR}/cmake/repo_hygiene_check.cmake)
set_tests_properties(repo_hygiene PROPERTIES
    TIMEOUT 30
    LABELS "gate;hygiene")
