#!/usr/bin/env bash
# ZCSR Android arm64 device verification driver.
#
# Requires a connected, authorized device and a signed APK built by
# tools/android_pack.sh. This script installs the APK, launches it, exercises
# touch and HOME/resume, captures screenshots and a filtered logcat, then
# asserts the native markers that prove texture, batch, atlas, instancing,
# text, input, and AAudio initialization.
#
# Environment:
#   ANDROID_SDK_ROOT / ANDROID_HOME   SDK root (or pass ADB directly)
#   ADB                               adb executable override
#   ANDROID_SERIAL                    select one device when several are attached
#   ZCSR_ANDROID_APK                  signed APK (required)
#   ZCSR_ANDROID_LOG_DIR              output directory (defaults outside repo)
#   ZCSR_ANDROID_PACKAGE              package [com.zcsr.phase0]
#   ZCSR_ANDROID_ACTIVITY             activity [android.app.NativeActivity]
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
APK="${ZCSR_ANDROID_APK:-}"
if [[ -z "${APK}" || ! -f "${APK}" ]]; then
    echo "ERROR: set ZCSR_ANDROID_APK to a signed APK." >&2
    exit 2
fi
APK="$(cd "$(dirname "${APK}")" && pwd)/$(basename "${APK}")"

LOG_DIR="${ZCSR_ANDROID_LOG_DIR:-$(cd "${ROOT}/../.." && pwd)/build/zcsr/android/logs}"
PKG="${ZCSR_ANDROID_PACKAGE:-com.zcsr.phase0}"
ACTIVITY="${ZCSR_ANDROID_ACTIVITY:-android.app.NativeActivity}"
mkdir -p "${LOG_DIR}"
STAMP="$(date +%Y%m%d_%H%M%S)"
SUMMARY="${LOG_DIR}/android_summary_${STAMP}.log"
FULL_LOG="${LOG_DIR}/android_logcat_${STAMP}.log"
: > "${SUMMARY}"

if [[ -n "${ADB:-}" ]]; then
    ADB_BIN="${ADB}"
elif [[ -n "${ANDROID_SDK_ROOT:-}" ]]; then
    ADB_BIN="${ANDROID_SDK_ROOT}/platform-tools/adb"
elif [[ -n "${ANDROID_HOME:-}" ]]; then
    ADB_BIN="${ANDROID_HOME}/platform-tools/adb"
else
    ADB_BIN="adb"
fi
if [[ ! -x "${ADB_BIN}" ]] && ! command -v "${ADB_BIN}" >/dev/null; then
    echo "ERROR: adb not found: ${ADB_BIN}" >&2
    exit 2
fi
ADB_CMD=("${ADB_BIN}")
if [[ -n "${ANDROID_SERIAL:-}" ]]; then
    ADB_CMD+=(-s "${ANDROID_SERIAL}")
fi

if ! "${ADB_CMD[@]}" get-state >/dev/null 2>&1; then
    echo "ERROR: no authorized device; run 'adb devices' and accept USB debugging." >&2
    exit 2
fi

{
    echo "ZCSR Android arm64 verification"
    echo "timestamp=${STAMP}"
    echo "apk=${APK} ($(stat -c%s "${APK}") bytes)"
    echo "package=${PKG}/${ACTIVITY}"
    echo "log_dir=${LOG_DIR}"
    echo
    echo "--- device ---"
    "${ADB_CMD[@]}" shell getprop ro.product.manufacturer || true
    "${ADB_CMD[@]}" shell getprop ro.product.model || true
    "${ADB_CMD[@]}" shell getprop ro.build.version.release || true
    "${ADB_CMD[@]}" shell getprop ro.build.version.sdk || true
    "${ADB_CMD[@]}" shell getprop ro.product.cpu.abi || true
    "${ADB_CMD[@]}" shell wm size || true
    "${ADB_CMD[@]}" shell wm density || true
} | tee -a "${SUMMARY}"

echo "===== install =====" | tee -a "${SUMMARY}"
"${ADB_CMD[@]}" install -r -d "${APK}" 2>&1 | tee -a "${SUMMARY}"

echo "===== launch and exercise =====" | tee -a "${SUMMARY}"
"${ADB_CMD[@]}" logcat -c
"${ADB_CMD[@]}" shell am force-stop "${PKG}"
"${ADB_CMD[@]}" shell am start -W -n "${PKG}/${ACTIVITY}" 2>&1 | tee -a "${SUMMARY}"

sleep 1
"${ADB_CMD[@]}" exec-out screencap -p > "${LOG_DIR}/android_scene_batch_${STAMP}.png" 2>/dev/null || true
sleep 2
"${ADB_CMD[@]}" exec-out screencap -p > "${LOG_DIR}/android_scene_atlas_${STAMP}.png" 2>/dev/null || true
sleep 2
"${ADB_CMD[@]}" exec-out screencap -p > "${LOG_DIR}/android_scene_instanced_${STAMP}.png" 2>/dev/null || true
sleep 2
"${ADB_CMD[@]}" exec-out screencap -p > "${LOG_DIR}/android_scene_text_${STAMP}.png" 2>/dev/null || true

# Touch and drag: these must surface as native 1E touch events.
"${ADB_CMD[@]}" shell input tap 500 1200 || true
sleep 1
"${ADB_CMD[@]}" shell input swipe 500 1600 500 600 300 || true
sleep 1

# HOME -> relaunch verifies APP_CMD_TERM_WINDOW/INIT_WINDOW recovery.
"${ADB_CMD[@]}" shell input keyevent KEYCODE_HOME || true
sleep 2
"${ADB_CMD[@]}" shell am start -W -n "${PKG}/${ACTIVITY}" 2>&1 | tee -a "${SUMMARY}"
sleep 3
"${ADB_CMD[@]}" exec-out screencap -p > "${LOG_DIR}/android_resume_${STAMP}.png" 2>/dev/null || true

"${ADB_CMD[@]}" shell pidof "${PKG}" > "${LOG_DIR}/android_pid_${STAMP}.txt" 2>/dev/null || true
"${ADB_CMD[@]}" logcat -d -v threadtime 'zcsr:V' 'zcsr-gl:V' 'AndroidRuntime:E' '*:S' > "${FULL_LOG}" 2>&1 || true

check() {
    local label="$1" pattern="$2"
    if grep -Fq -- "${pattern}" "${FULL_LOG}"; then
        echo "PASS ${label}" | tee -a "${SUMMARY}"
    else
        echo "FAIL ${label}: missing '${pattern}'" | tee -a "${SUMMARY}"
        FAILED=1
    fi
}
FAILED=0
check "boot"                  "android boot ok"
check "texture"               "1C.0 texture="
check "sprite batch"          "1C.0 sprite batch PASS"
check "atlas regions"         "1C.1 atlas PASS"
check "instanced batch"       "1C.2 instanced PASS"
check "text pass"             "1C.3 text PASS"
check "input extension"       "1E input extension initialized"
check "keyboard state"        "1E keyboard state initialized"
check "touch event"           "1E touch:"
check "AAudio WAV registered" "1D audio: WAV registered"
check "resume GL init"        "GLES version:"

PID="$(cat "${LOG_DIR}/android_pid_${STAMP}.txt" 2>/dev/null || true)"
if [[ -n "${PID}" ]]; then
    echo "PASS process alive pid=${PID}" | tee -a "${SUMMARY}"
else
    echo "FAIL process not alive after resume" | tee -a "${SUMMARY}"
    FAILED=1
fi

if grep -Fq -- "FATAL EXCEPTION" "${FULL_LOG}" || grep -Fq -- "AndroidRuntime: FATAL" "${FULL_LOG}"; then
    echo "FAIL Java fatal exception found" | tee -a "${SUMMARY}"
    FAILED=1
else
    echo "PASS no Java fatal exception" | tee -a "${SUMMARY}"
fi

{
    echo
    echo "===== Android summary ====="
    echo "logcat=${FULL_LOG}"
    echo "screenshots=${LOG_DIR}/android_*_${STAMP}.png"
    if [[ "${FAILED}" -eq 0 ]]; then
        echo "RESULT: PASS"
    else
        echo "RESULT: FAIL"
    fi
} | tee -a "${SUMMARY}"

exit "${FAILED}"
