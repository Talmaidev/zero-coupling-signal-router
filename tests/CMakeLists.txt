# Repository-native engine contract tests.
# These targets build against the engine's own module targets; they do not
# depend on game_test or any external harness.
add_executable(core_test test_core.c)
target_link_libraries(core_test PRIVATE zcsr_core)
add_test(NAME core COMMAND core_test)

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
