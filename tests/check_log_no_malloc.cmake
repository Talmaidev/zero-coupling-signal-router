if(NOT DEFINED LIB OR NOT DEFINED TOOL OR NOT DEFINED FLAVOR)
  message(FATAL_ERROR "check_log_no_malloc.cmake: missing -DLIB/-DTOOL/-DFLAVOR")
endif()

if(FLAVOR STREQUAL "windows")
  execute_process(COMMAND "${TOOL}" /symbols "${LIB}"
    RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
else()
  execute_process(COMMAND "${TOOL}" -u "${LIB}"
    RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
endif()

if(NOT rc EQUAL 0)
  message(FATAL_ERROR "symbol dump failed: ${err}")
endif()

# Match whole symbol names, not substrings: sanitizer runtimes legitimately
# reference __asan_stack_malloc_N / __ubsan_* when the library was instrumented,
# and a substring search would report those as "malloc referenced".
string(REPLACE "\r" "" out "${out}")
string(REPLACE "\n" ";" lines "${out}")
foreach(line IN LISTS lines)
  string(STRIP "${line}" line)
  if(line STREQUAL "")
    continue()
  endif()
  string(REGEX MATCH "[^ \t]+$" sym "${line}")
  string(TOLOWER "${sym}" sym)
  string(REGEX REPLACE "^_" "" sym "${sym}")
  if(sym STREQUAL "malloc" OR sym STREQUAL "calloc"
     OR sym STREQUAL "realloc" OR sym STREQUAL "free")
    message(FATAL_ERROR "log_no_malloc: '${sym}' referenced by ${LIB}")
  endif()
endforeach()

message(STATUS "log_no_malloc: PASS (no malloc/calloc/realloc/free in zcsr_core)")
