#!/bin/bash
set -uo pipefail

if [[ "$(uname -s)" != "Darwin" ]]; then
    echo "ERROR: collect_macos_logs.sh only runs on macOS." >&2
    exit 2
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
# Logs and scratch live outside the source tree under a sibling zcsr-build-macos
# root; the verify_macos binary is read from the out-of-tree build produced by
# verify_macos_linear.sh (override its location if that build used another dir).
BUILD_DIR="${ZCSR_MACOS_LOG_DIR:-$ROOT_DIR/../zcsr-build-macos/logs}"
case "$BUILD_DIR" in
    "$ROOT_DIR"/*|"$ROOT_DIR")
        echo "ERROR: BUILD_DIR '${BUILD_DIR}' is inside the source tree (${ROOT_DIR}); refused." >&2
        exit 1
        ;;
esac
VERIFY_BIN="${ZCSR_MACOS_VERIFY_BIN:-$ROOT_DIR/../zcsr-build-macos/verify_linear/verify_macos}"

if [[ ! -x "${VERIFY_BIN}" ]]; then
    echo "ERROR: ${VERIFY_BIN} does not exist or is not executable." >&2
    echo "Build it first with tools/verify_macos_linear.sh (out-of-tree build)." >&2
    exit 1
fi

STAMP="$(date +%Y%m%d_%H%M%S)"
OUT_LOG="${BUILD_DIR}/macos_verify_${STAMP}.log"
TMP_DIR="$(mktemp -d "${BUILD_DIR}/.macos-logs.XXXXXX")"

VERIFY_LOG="${TMP_DIR}/verify_macos.log"
CORE_AUDIO_LOG="${TMP_DIR}/coreaudio.log"
PROFILER_BEFORE="${TMP_DIR}/system_profiler_before.txt"
PROFILER_AFTER="${TMP_DIR}/system_profiler_after.txt"
IOREG_BEFORE="${TMP_DIR}/ioreg_before.txt"
IOREG_AFTER="${TMP_DIR}/ioreg_after.txt"

CORE_PID=""
VERIFY_PID=""
VERIFY_STATUS=0

cleanup() {
    if [[ -n "${CORE_PID}" ]]; then
        kill -INT "${CORE_PID}" 2>/dev/null || true
        wait "${CORE_PID}" 2>/dev/null || true
        CORE_PID=""
    fi

    if [[ -n "${VERIFY_PID}" ]]; then
        kill -TERM "${VERIFY_PID}" 2>/dev/null || true
        wait "${VERIFY_PID}" 2>/dev/null || true
        VERIFY_PID=""
    fi

    rm -rf "${TMP_DIR}"
}

trap cleanup EXIT INT TERM

mkdir -p "${BUILD_DIR}"

echo "[collect] Capturing pre-run audio device information..."

system_profiler SPAudioDataType \
    > "${PROFILER_BEFORE}" 2>&1 || true

ioreg -l 2>&1 | grep -i audio \
    > "${IOREG_BEFORE}" || true

echo "[collect] Starting CoreAudio unified log capture..."

log stream \
    --style compact \
    --predicate 'subsystem == "com.apple.coreaudio"' \
    > "${CORE_AUDIO_LOG}" 2>&1 &
CORE_PID=$!

sleep 1

echo "[collect] Starting ${VERIFY_BIN}..."
"${VERIFY_BIN}" > "${VERIFY_LOG}" 2>&1 &
VERIFY_PID=$!

set +e
wait "${VERIFY_PID}"
VERIFY_STATUS=$?
set -e
VERIFY_PID=""

echo "[collect] Verification exited with status ${VERIFY_STATUS}."

if [[ -n "${CORE_PID}" ]]; then
    kill -INT "${CORE_PID}" 2>/dev/null || true
    wait "${CORE_PID}" 2>/dev/null || true
    CORE_PID=""
fi

echo "[collect] Capturing post-run audio device information..."

system_profiler SPAudioDataType \
    > "${PROFILER_AFTER}" 2>&1 || true

ioreg -l 2>&1 | grep -i audio \
    > "${IOREG_AFTER}" || true

{
    echo "============================================================"
    echo "zcsr macOS AudioUnit verification"
    echo "Timestamp: ${STAMP}"
    echo "Verify exit status: ${VERIFY_STATUS}"
    echo "Verify executable: ${VERIFY_BIN}"
    echo "============================================================"

    echo
    echo "================ verify_macos output ======================="
    cat "${VERIFY_LOG}" 2>/dev/null || true

    echo
    echo "================ CoreAudio unified log ====================="
    cat "${CORE_AUDIO_LOG}" 2>/dev/null || true

    echo
    echo "================ system_profiler BEFORE ===================="
    cat "${PROFILER_BEFORE}" 2>/dev/null || true

    echo
    echo "================ system_profiler AFTER ====================="
    cat "${PROFILER_AFTER}" 2>/dev/null || true

    echo
    echo "================ ioreg audio BEFORE ========================"
    cat "${IOREG_BEFORE}" 2>/dev/null || true

    echo
    echo "================ ioreg audio AFTER ========================="
    cat "${IOREG_AFTER}" 2>/dev/null || true

    echo
    echo "================ end of collection ========================="
} > "${OUT_LOG}"

echo "[collect] Log saved to:"
echo "${OUT_LOG}"

exit "${VERIFY_STATUS}"
