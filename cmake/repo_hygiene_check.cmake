# repo_hygiene_check.cmake — read-only source-tree hygiene gate (CTest: repo_hygiene)
#
# Cross-platform (Windows / Linux / macOS), pure CMake script mode: no shell,
# no PowerShell, no external tool. Scans only SOURCE_DIR (the current source
# tree) and fails (non-zero exit) when build/configure/stray products appear
# outside the explicit allowlist.
#
# The script NEVER creates, modifies or deletes anything in the source tree.
# It enumerates paths with file(GLOB) only; any scratch/log would have to live
# in ${CMAKE_BINARY_DIR}, and this gate writes nothing at all.
#
# Inputs (passed by add_test):
#   -DSOURCE_DIR=<repo source root>
#
# Violation rules:
#   D1  build/output directory: `build`, `out`, `build_*`, `build-*`,
#       `out_*`, `out-*`
#   D2  CMakeFiles/ directory (CMake configure scratch)
#   D3  dependency download scratch: `_dl/`, `_src/`, `*prebuilt*_dl*`
#   D4  CTest runtime scratch: `Testing/`
#   F1  CMakeCache.txt (an in-repo configure was run)
#   F2  stray intermediate/runtime binary: *.obj, *.pdb, *.ilk, *.exe
#
# Allowlist (explicit relative prefixes, forward slashes):
#   .git/                    VCS metadata
#   third_party/prebuilt/    vendored prebuilt binaries
#   third_party/             vendored third-party source
#   tests/data/              versioned audio/image/font fixtures (t0.wav ...)
#   tests/video_data/        versioned video fixtures

if(NOT SOURCE_DIR)
    message(FATAL_ERROR "repo_hygiene_check: SOURCE_DIR is not set")
endif()

file(TO_CMAKE_PATH "${SOURCE_DIR}" ZCSR_HG_SRC)

set(ZCSR_HG_ALLOW_PREFIXES
    ".git/"
    "third_party/prebuilt/"
    "third_party/"
    "tests/data/"
    "tests/video_data/"
)

function(zcsr_hg_allowlisted rel out)
    set(_allowed FALSE)
    foreach(_prefix IN LISTS ZCSR_HG_ALLOW_PREFIXES)
        string(FIND "${rel}" "${_prefix}" _idx)
        if(_idx EQUAL 0)
            set(_allowed TRUE)
            break()
        endif()
    endforeach()
    set(${out} "${_allowed}" PARENT_SCOPE)
endfunction()

function(zcsr_hg_dir_name_violation dname reason_out)
    if(dname STREQUAL "build" OR dname STREQUAL "out" OR
       dname MATCHES "^build[_-]" OR dname MATCHES "^out[_-]")
        set(${reason_out} "D1 build/output directory '${dname}/' (build trees must live outside the source tree)" PARENT_SCOPE)
    elseif(dname STREQUAL "CMakeFiles")
        set(${reason_out} "D2 CMakeFiles/ directory (CMake configure scratch in the source tree)" PARENT_SCOPE)
    elseif(dname STREQUAL "_dl" OR dname STREQUAL "_src" OR dname MATCHES "prebuilt.*_dl")
        set(${reason_out} "D3 dependency download scratch directory '${dname}/' (FetchContent _dl/_src)" PARENT_SCOPE)
    elseif(dname STREQUAL "Testing")
        set(${reason_out} "D4 CTest runtime scratch directory 'Testing/'" PARENT_SCOPE)
    else()
        set(${reason_out} "" PARENT_SCOPE)
    endif()
endfunction()

set(ZCSR_HG_VIOLATIONS "")

# ---- enumerate directories (include empty dirs via LIST_DIRECTORIES) -------
file(GLOB_RECURSE ZCSR_HG_DIRS RELATIVE "${ZCSR_HG_SRC}" LIST_DIRECTORIES true
    "${ZCSR_HG_SRC}/*")

foreach(_d IN LISTS ZCSR_HG_DIRS)
    if(NOT IS_DIRECTORY "${ZCSR_HG_SRC}/${_d}")
        continue()
    endif()
    zcsr_hg_allowlisted("${_d}/" _allow)
    if(_allow)
        continue()
    endif()
    get_filename_component(_dname "${_d}" NAME)
    zcsr_hg_dir_name_violation("${_dname}" _reason)
    if(_reason)
        list(APPEND ZCSR_HG_VIOLATIONS "${ZCSR_HG_SRC}/${_d}  [${_reason}]")
    endif()
endforeach()

# ---- enumerate files --------------------------------------------------------
file(GLOB_RECURSE ZCSR_HG_FILES RELATIVE "${ZCSR_HG_SRC}"
    "${ZCSR_HG_SRC}/*")

foreach(_f IN LISTS ZCSR_HG_FILES)
    if(IS_DIRECTORY "${ZCSR_HG_SRC}/${_f}")
        continue()
    endif()
    zcsr_hg_allowlisted("${_f}" _allow)
    if(_allow)
        continue()
    endif()

    get_filename_component(_name "${_f}" NAME)
    get_filename_component(_ext  "${_f}" EXT)
    string(TOLOWER "${_ext}" _ext)

    set(_reason "")
    if(_name STREQUAL "CMakeCache.txt")
        set(_reason "F1 CMakeCache.txt (in-repo configure was run; build tree must live outside the source tree)")
    elseif(_ext STREQUAL ".obj" OR _ext STREQUAL ".pdb" OR
           _ext STREQUAL ".ilk" OR _ext STREQUAL ".exe")
        set(_reason "F2 stray intermediate/runtime binary '${_name}' (${_ext}; compiler/linker output belongs in the build tree)")
    endif()

    if(_reason)
        list(APPEND ZCSR_HG_VIOLATIONS "${ZCSR_HG_SRC}/${_f}  [${_reason}]")
    endif()
endforeach()

list(LENGTH ZCSR_HG_VIOLATIONS _count)
if(_count GREATER 0)
    message(STATUS "repo_hygiene: ${_count} violation(s) in ${ZCSR_HG_SRC}")
    foreach(_v IN LISTS ZCSR_HG_VIOLATIONS)
        message(STATUS "  ${_v}")
    endforeach()
    message(FATAL_ERROR
        "repo_hygiene gate failed: the source tree contains build/configure/stray "
        "products. Move all build trees outside the source tree; see "
        "BUILD_LAYOUT.md (build layout is enforced, not advisory).")
else()
    message(STATUS "repo_hygiene: source tree clean (${ZCSR_HG_SRC})")
endif()
