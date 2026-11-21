if(NOT DEFINED CLANG_TIDY OR NOT DEFINED INCLUDE_DIR OR
   NOT DEFINED BAD_SRC OR NOT DEFINED GOOD_SRC)
  message(FATAL_ERROR "run_clang_tidy.cmake: missing required -D argument")
endif()

set(CHECKS "-*,bugprone-format-string,cert-printf")

execute_process(
  COMMAND "${CLANG_TIDY}" "${BAD_SRC}"
          "--checks=${CHECKS}" "--warnings-as-errors=*"
          -- "-I${INCLUDE_DIR}" -std=c11
  RESULT_VARIABLE bad_rc OUTPUT_VARIABLE bad_out ERROR_VARIABLE bad_err)
if(bad_rc EQUAL 0)
  message(FATAL_ERROR "clang_tidy: bad_format.c produced no error\n${bad_out}")
endif()

execute_process(
  COMMAND "${CLANG_TIDY}" "${GOOD_SRC}"
          "--checks=${CHECKS}" "--warnings-as-errors=*"
          -- "-I${INCLUDE_DIR}" -std=c11
  RESULT_VARIABLE good_rc OUTPUT_VARIABLE good_out ERROR_VARIABLE good_err)
if(NOT good_rc EQUAL 0)
  message(FATAL_ERROR "clang_tidy: good_format.c failed\n${good_out}\n${good_err}")
endif()

message(STATUS "clang_tidy: PASS (bad rejected, good accepted)")
