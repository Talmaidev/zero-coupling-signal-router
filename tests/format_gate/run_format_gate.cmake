# Format-gate driver: the negative fixture must fail to compile and the
# positive fixture must compile. Works with clang-cl (/W4 /WX) and with
# clang/GCC (-Wformat -Werror).
if(NOT DEFINED COMPILER OR NOT DEFINED FLAVOR OR NOT DEFINED INCLUDE_DIR OR
   NOT DEFINED BAD_SRC OR NOT DEFINED GOOD_SRC OR NOT DEFINED OUT_DIR)
  message(FATAL_ERROR "run_format_gate.cmake: missing required -D argument")
endif()

# The intermediate objects (.obj/.o) written below must live inside the build
# tree, never in the source tree. The caller passes BINARY_DIR (the build
# root); fall back to CMAKE_BINARY_DIR when the driver is invoked directly.
if(NOT DEFINED BINARY_DIR)
  set(BINARY_DIR "${CMAKE_BINARY_DIR}")
endif()
if(NOT BINARY_DIR)
  message(FATAL_ERROR
    "run_format_gate.cmake: BINARY_DIR is not defined; OUT_DIR must live "
    "under the build tree, refusing to write into the source tree")
endif()

file(TO_CMAKE_PATH "${BINARY_DIR}" _fg_bin)
file(TO_CMAKE_PATH "${OUT_DIR}" _fg_out)
if(WIN32)
  string(TOLOWER "${_fg_bin}" _fg_bin_cmp)
  string(TOLOWER "${_fg_out}" _fg_out_cmp)
else()
  set(_fg_bin_cmp "${_fg_bin}")
  set(_fg_out_cmp "${_fg_out}")
endif()
string(LENGTH "${_fg_bin_cmp}" _fg_bin_len)
string(SUBSTRING "${_fg_out_cmp}" 0 ${_fg_bin_len} _fg_out_head)
set(_fg_outside FALSE)
if(_fg_out_cmp STREQUAL _fg_bin_cmp)
  # OUT_DIR == the build root: still inside the build tree.
  set(_fg_outside FALSE)
elseif(_fg_out_head STREQUAL _fg_bin_cmp)
  string(SUBSTRING "${_fg_out_cmp}" ${_fg_bin_len} 1 _fg_sep)
  if(NOT _fg_sep STREQUAL "/" AND NOT _fg_sep STREQUAL "\\")
    set(_fg_outside TRUE)
  endif()
else()
  set(_fg_outside TRUE)
endif()
if(_fg_outside)
  message(FATAL_ERROR
    "format_gate: OUT_DIR (${OUT_DIR}) must live under CMAKE_BINARY_DIR "
    "(${BINARY_DIR}); refusing to write objects into the source tree")
endif()
unset(_fg_bin)
unset(_fg_out)
unset(_fg_bin_cmp)
unset(_fg_out_cmp)
unset(_fg_bin_len)
unset(_fg_out_head)
unset(_fg_sep)
unset(_fg_outside)

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
