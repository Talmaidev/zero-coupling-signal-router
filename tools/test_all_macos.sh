#!/usr/bin/env bash
# ZCSR 1.0 unified macOS verification driver.
#
# Run on a real Mac with a GUI session (not a headless CI box). It builds and
# tests all five repositories out of tree, then runs the Editor smoke path.
# It never writes into a source checkout.
#
# Layout assumed (override the root if yours differs):
#   <workspace>/GitHub/{zero-coupling-signal-router,UIM,zcsr_runtime,zcsr_tools,zcsr_editor}
#   <workspace>/build/zcsr/macos-verify/
#
# Environment:
#   ZCSR_REPO_ROOT        parent directory containing the five repos
#   ZCSR_BUILD_ROOT       out-of-tree build root
#   ZCSR_MACOS_LOG_DIR    log output directory (defaults to build root/logs)
#   ZCSR_DEPS_ROOT        optional prebuilt root with ffmpeg/ and libwebp/
#   ZCSR_FFMPEG_ROOT      optional explicit FFmpeg root
#   ZCSR_LIBWEBP_ROOT     optional explicit libwebp root
#
# Usage:
#   bash tools/test_all_macos.sh
#   ZCSR_REPO_ROOT=/path/to/GitHub ZCSR_BUILD_ROOT=/tmp/zcsr-build bash tools/test_all_macos.sh
set -euo pipefail

if [[ "$(uname -s)" != "Darwin" ]]; then
    echo "ERROR: test_all_macos.sh only runs on macOS." >&2
    exit 2
fi

ENGINE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
REPO_ROOT="${ZCSR_REPO_ROOT:-$(cd "${ENGINE_DIR}/.." && pwd)}"
BUILD_ROOT="${ZCSR_BUILD_ROOT:-$(cd "${ENGINE_DIR}/../.." && pwd)/build/zcsr/macos-verify}"
LOG_DIR="${ZCSR_MACOS_LOG_DIR:-${BUILD_ROOT}/logs}"
UIM_DIR="${REPO_ROOT}/UIM"
RUNTIME_DIR="${REPO_ROOT}/zcsr_runtime"
TOOLS_DIR="${REPO_ROOT}/zcsr_tools"
EDITOR_DIR="${REPO_ROOT}/zcsr_editor"

case "${BUILD_ROOT}" in
    "${ENGINE_DIR}"|"${ENGINE_DIR}"/*|"${REPO_ROOT}"|"${REPO_ROOT}"/*)
        echo "ERROR: build root '${BUILD_ROOT}' is inside a source checkout; refused." >&2
        exit 1
        ;;
esac

for d in "${ENGINE_DIR}" "${UIM_DIR}" "${RUNTIME_DIR}" "${TOOLS_DIR}" "${EDITOR_DIR}"; do
    if [[ ! -d "${d}" ]]; then
        echo "ERROR: repository not found: ${d}" >&2
        exit 1
    fi
done

mkdir -p "${BUILD_ROOT}" "${LOG_DIR}"
STAMP="$(date +%Y%m%d_%H%M%S)"
SUMMARY="${LOG_DIR}/macos_summary_${STAMP}.log"
MAIN_LOG="${LOG_DIR}/macos_all_${STAMP}.log"

# Every child command writes to its own log and the merged driver log.
run_step() {
    local name="$1"; shift
    local log="${LOG_DIR}/${name}_${STAMP}.log"
    echo "===== ${name} =====" | tee -a "${MAIN_LOG}"
    "$@" 2>&1 | tee -a "${MAIN_LOG}" "${log}"
    local rc="${PIPESTATUS[0]}"
    echo "----- ${name}: rc=${rc} -----" | tee -a "${MAIN_LOG}" "${SUMMARY}"
    return "${rc}"
}

ctest_count() {
    local log="$1"
    grep -E "tests passed|tests failed|Total Test time" "${log}" | tail -n 4 || true
}

DEPS_ARGS=()
if [[ -n "${ZCSR_DEPS_ROOT:-}" ]]; then
    DEPS_ARGS+=("-DZCSR_DEPS_ROOT=${ZCSR_DEPS_ROOT}")
fi
if [[ -n "${ZCSR_FFMPEG_ROOT:-}" ]]; then
    DEPS_ARGS+=("-DZCSR_FFMPEG_ROOT=${ZCSR_FFMPEG_ROOT}")
fi
if [[ -n "${ZCSR_LIBWEBP_ROOT:-}" ]]; then
    DEPS_ARGS+=("-DZCSR_LIBWEBP_ROOT=${ZCSR_LIBWEBP_ROOT}")
fi

{
    echo "ZCSR macOS unified verification"
    echo "timestamp=${STAMP}"
    echo "engine=${ENGINE_DIR}"
    echo "repo_root=${REPO_ROOT}"
    echo "build_root=${BUILD_ROOT}"
    echo "log_dir=${LOG_DIR}"
    echo
    echo "--- host ---"
    sw_vers
    uname -m
    xcodebuild -version
    cmake --version | head -n 1
    echo
    echo "--- display ---"
    system_profiler SPDisplaysDataType 2>/dev/null | sed -n '1,60p'
} | tee "${MAIN_LOG}" "${SUMMARY}"

FAILED=0

run_step engine_configure \
    cmake -S "${ENGINE_DIR}" -B "${BUILD_ROOT}/zero-coupling-signal-router" \
        -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF ${DEPS_ARGS[@]+"${DEPS_ARGS[@]}"} || FAILED=1
run_step engine_build \
    cmake --build "${BUILD_ROOT}/zero-coupling-signal-router" --config Release -j "$(sysctl -n hw.logicalcpu)" || FAILED=1
run_step engine_ctest \
    ctest --test-dir "${BUILD_ROOT}/zero-coupling-signal-router" -C Release --output-on-failure || FAILED=1

run_step uim_configure \
    cmake -S "${UIM_DIR}" -B "${BUILD_ROOT}/UIM" \
        -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF \
        -DZCSR_ENGINE_INCLUDE="${ENGINE_DIR}/include" \
        -DZCSR_UIM_WITH_MENU=ON -DZCSR_UIM_WITH_PAGE_MANAGER=ON || FAILED=1
run_step uim_build \
    cmake --build "${BUILD_ROOT}/UIM" --config Release -j "$(sysctl -n hw.logicalcpu)" || FAILED=1
run_step uim_ctest \
    ctest --test-dir "${BUILD_ROOT}/UIM" -C Release --output-on-failure || FAILED=1

run_step runtime_configure \
    cmake -S "${RUNTIME_DIR}" -B "${BUILD_ROOT}/zcsr_runtime" \
        -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF \
        -DZCSR_ENGINE_DIR="${ENGINE_DIR}" -DZCSR_UIM_DIR="${UIM_DIR}" \
        -DZCSR_RUNTIME_BUILD_PLAYER=ON ${DEPS_ARGS[@]+"${DEPS_ARGS[@]}"} || FAILED=1
run_step runtime_build \
    cmake --build "${BUILD_ROOT}/zcsr_runtime" --config Release -j "$(sysctl -n hw.logicalcpu)" || FAILED=1
run_step runtime_ctest \
    ctest --test-dir "${BUILD_ROOT}/zcsr_runtime" -C Release --output-on-failure || FAILED=1

run_step tools_configure \
    cmake -S "${TOOLS_DIR}" -B "${BUILD_ROOT}/zcsr_tools" \
        -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF \
        -DZCSR_ENGINE_DIR="${ENGINE_DIR}" -DZCSR_UIM_DIR="${UIM_DIR}" \
        -DZCSR_RUNTIME_DIR="${RUNTIME_DIR}" ${DEPS_ARGS[@]+"${DEPS_ARGS[@]}"} || FAILED=1
run_step tools_build \
    cmake --build "${BUILD_ROOT}/zcsr_tools" --config Release -j "$(sysctl -n hw.logicalcpu)" || FAILED=1
run_step tools_ctest \
    ctest --test-dir "${BUILD_ROOT}/zcsr_tools" -C Release --output-on-failure || FAILED=1

run_step editor_configure \
    cmake -S "${EDITOR_DIR}" -B "${BUILD_ROOT}/zcsr_editor" \
        -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF \
        -DZCSR_ENGINE_DIR="${ENGINE_DIR}" -DZCSR_UIM_DIR="${UIM_DIR}" \
        -DZCSR_RUNTIME_DIR="${RUNTIME_DIR}" \
        -DZCSR_EDITOR_REGISTER_SMOKE_TEST=ON \
        ${DEPS_ARGS[@]+"${DEPS_ARGS[@]}"} || FAILED=1
run_step editor_build \
    cmake --build "${BUILD_ROOT}/zcsr_editor" --config Release -j "$(sysctl -n hw.logicalcpu)" || FAILED=1
run_step editor_ctest \
    ctest --test-dir "${BUILD_ROOT}/zcsr_editor" -C Release --output-on-failure || FAILED=1

EDITOR_BIN="${BUILD_ROOT}/zcsr_editor/zcsr_editor"
if [[ ! -x "${EDITOR_BIN}" ]]; then
    EDITOR_BIN="${BUILD_ROOT}/zcsr_editor/Release/zcsr_editor"
fi
if [[ -x "${EDITOR_BIN}" ]]; then
    export DYLD_LIBRARY_PATH="${ZCSR_DEPS_ROOT:-${ZCSR_FFMPEG_ROOT:-}}/ffmpeg/lib:${DYLD_LIBRARY_PATH:-}"
    run_step editor_smoke \
        "${EDITOR_BIN}" --smoke-frames 30 --autoquit || FAILED=1
else
    echo "ERROR: zcsr_editor binary not found under ${BUILD_ROOT}/zcsr_editor" | tee -a "${MAIN_LOG}" "${SUMMARY}"
    FAILED=1
fi

{
    echo
    echo "===== macOS summary ====="
    echo "engine:"; ctest_count "${LOG_DIR}/engine_ctest_${STAMP}.log"
    echo "UIM:"; ctest_count "${LOG_DIR}/uim_ctest_${STAMP}.log"
    echo "runtime:"; ctest_count "${LOG_DIR}/runtime_ctest_${STAMP}.log"
    echo "tools:"; ctest_count "${LOG_DIR}/tools_ctest_${STAMP}.log"
    echo "editor:"; ctest_count "${LOG_DIR}/editor_ctest_${STAMP}.log"
    echo
    if [[ "${FAILED}" -eq 0 ]]; then
        echo "RESULT: PASS (all five repos built, tested, editor smoke exited 0)"
    else
        echo "RESULT: FAIL (one or more steps returned non-zero)"
    fi
    echo "main_log=${MAIN_LOG}"
} | tee -a "${MAIN_LOG}" "${SUMMARY}"

exit "${FAILED}"
