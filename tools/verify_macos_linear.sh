#!/bin/bash
# macOS 1.0 gate for the unified linear frame. Run on a real Mac with a GUI
# session (not a headless CI box): the glrender test creates an NSOpenGL
# surface and reads the real backbuffer.
set -euo pipefail

if [[ "$(uname -s)" != "Darwin" ]]; then
    echo "ERROR: verify_macos_linear.sh only runs on macOS." >&2
    exit 2
fi

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build_macos"
LOG="${BUILD_DIR}/verify_macos_linear.log"

mkdir -p "${BUILD_DIR}"

echo "[mac] configuring ${BUILD_DIR}"
cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0 \
    2>&1 | tee "${LOG}"

echo "[mac] building"
cmake --build "${BUILD_DIR}" -j "$(sysctl -n hw.logicalcpu)" 2>&1 | tee -a "${LOG}"

echo "[mac] ctest"
ctest --test-dir "${BUILD_DIR}" -C Release --output-on-failure 2>&1 | tee -a "${LOG}"

echo "[mac] direct glrender test (real NSOpenGL backbuffer)"
TEST_BIN="${BUILD_DIR}/tests/engine/glrender_test"
if [[ ! -x "${TEST_BIN}" ]]; then
    TEST_BIN="${BUILD_DIR}/tests/engine/Release/glrender_test"
fi
if [[ ! -x "${TEST_BIN}" ]]; then
    echo "ERROR: glrender_test binary not found under ${BUILD_DIR}/tests/engine." >&2
    exit 1
fi
"${TEST_BIN}" 2>&1 | tee -a "${LOG}"

if ! grep -q "PASS: unified linear frame round-trip and linear blending" "${LOG}"; then
    echo "ERROR: unified-linear golden did not pass; macOS is not a 1.0 candidate." >&2
    exit 1
fi
if ! grep -q "PASS: GL renderer smoke" "${LOG}"; then
    echo "ERROR: GL renderer smoke did not pass; macOS is not a 1.0 candidate." >&2
    exit 1
fi

echo "[mac] PASS: unified linear frame + smoke; log=${LOG}"
