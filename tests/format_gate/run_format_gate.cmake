# Format-gate driver: the negative fixture must fail to compile and the
# positive fixture must compile. Works with clang-cl (/W4 /WX) and with
# clang/GCC (-Wformat -Werror).
if(NOT DEFINED COMPILER OR NOT DEFINED FLAVOR OR NOT DEFINED INCLUDE_DIR OR
   NOT DEFINED BAD_SRC OR NOT DEFINED GOOD_SRC OR NOT DEFINED OUT_DIR)
  message(FATAL_ERROR "run_format_gate.cmake: missing required -D argument")
endif()

file(MAKE_DIRECTORY "${OUT_DIR}")

if(FLAVOR STREQUAL "clang-cl")
  set(BAD_CMD "${COMPILER}" /nologo /std:c11 /W4 /WX "/I${INCLUDE_DIR}"
      /c "${BAD_SRC}" "/Fo${OUT_DIR}/bad.obj")
  set(GOOD_CMD "${COMPILER}" /nologo /std:c11 /W4 /WX "/I${INCLUDE_DIR}"
      /c "${GOOD_SRC}" "/Fo${OUT_DIR}/good.obj")
else()
  set(BAD_CMD "${COMPILER}" -std=c11 -Wall -Wextra -Werror -Wformat
      "-I${INCLUDE_DIR}" -c "${BAD_SRC}" -o "${OUT_DIR}/bad.o")
  set(GOOD_CMD "${COMPILER}" -std=c11 -Wall -Wextra -Werror -Wformat
      "-I${INCLUDE_DIR}" -c "${GOOD_SRC}" -o "${OUT_DIR}/good.o")
endif()

execute_process(COMMAND ${BAD_CMD}
    RESULT_VARIABLE bad_rc OUTPUT_VARIABLE bad_out ERROR_VARIABLE bad_err)
if(bad_rc EQUAL 0)
  message(FATAL_ERROR "format_gate: bad_format.c compiled; the gate is not active")
endif()

execute_process(COMMAND ${GOOD_CMD}
    RESULT_VARIABLE good_rc OUTPUT_VARIABLE good_out ERROR_VARIABLE good_err)
if(NOT good_rc EQUAL 0)
  message(FATAL_ERROR "format_gate: good_format.c failed:\n${good_err}")
endif()

message(STATUS "format_gate: PASS (int64_t + %d rejected, PRId64 accepted)")
