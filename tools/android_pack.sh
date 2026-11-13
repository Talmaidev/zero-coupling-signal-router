#!/usr/bin/env bash
# zcsr Android APK packager — aapt2 link + .so (Stored) + zipalign + apksigner.
#
# Argument-less; configured via environment variables (defaults in brackets):
#   ANDROID_SDK_ROOT / ANDROID_HOME   SDK root (required)
#   ANDROID_BUILD_TOOLS_DIR           build-tools dir (auto-detect latest)
#   ZCSR_ANDROID_PLATFORM             platform dir, e.g. android-36 [android-36]
#   ZCSR_ANDROID_SO                   path to libzcsr_phase0.so [build/android/out/arm64-v8a/libzcsr_phase0.so]
#   ZCSR_ANDROID_BUILD_DIR            staging dir [build/android/t3_1c]
#   ZCSR_ANDROID_APK                  output APK [build/android/zcsr_phase1c.apk]
#   ZCSR_ANDROID_VERSION_CODE         override versionCode [from AndroidManifest.xml]
#   ZCSR_ANDROID_VERSION_NAME         override versionName [from AndroidManifest.xml]
#   ZCSR_DEBUG_STOREPASS              keystore password (required, env only)
#
# Output: $ZCSR_ANDROID_APK (signed v2+v3). versionCode/versionName default to
# manifest values and can be overridden via ZCSR_ANDROID_VERSION_CODE / _NAME.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
to_abs() {
    case "$1" in
        /*) printf '%s\n' "$1" ;;
        *)  printf '%s/%s\n' "$ROOT" "$1" ;;
    esac
}

SDK_ROOT="${ANDROID_SDK_ROOT:-${ANDROID_HOME:-}}"
if [[ -z "$SDK_ROOT" || ! -d "$SDK_ROOT" ]]; then
    echo "ERROR: set ANDROID_SDK_ROOT (or ANDROID_HOME)" >&2
    exit 1
fi
SDK_ROOT="$(to_abs "$SDK_ROOT")"

BUILD_TOOLS_DIR="${ANDROID_BUILD_TOOLS_DIR:-}"
if [[ -z "$BUILD_TOOLS_DIR" ]]; then
    BUILD_TOOLS_DIR="$(
        find "$SDK_ROOT/build-tools" -mindepth 1 -maxdepth 1 -type d \
            | sort -V | tail -n 1
    )"
fi

AAPT2="$BUILD_TOOLS_DIR/aapt2"
ZIPALIGN="$BUILD_TOOLS_DIR/zipalign"
APKSIGNER="$BUILD_TOOLS_DIR/apksigner"
ANDROID_JAR="$SDK_ROOT/platforms/${ZCSR_ANDROID_PLATFORM:-android-36}/android.jar"

SO_PATH="$(to_abs "${ZCSR_ANDROID_SO:-build/android/out/arm64-v8a/libzcsr_phase0.so}")"
BUILD_DIR="$(to_abs "${ZCSR_ANDROID_BUILD_DIR:-build/android/t3_1c}")"
APK_OUT="$(to_abs "${ZCSR_ANDROID_APK:-build/android/zcsr_phase1c.apk}")"

KEYSTORE="$ROOT/android/keystore/zcsr-debug.keystore"
KEY_ALIAS="zcsr-debug"
: "${ZCSR_DEBUG_STOREPASS:?Set ZCSR_DEBUG_STOREPASS in the environment}"

MANIFEST="$ROOT/android/AndroidManifest.xml"
manifest_attr() {
    sed -nE "s/.*android:$1=\"([^\"]+)\".*/\1/p" "$MANIFEST" | head -n 1
}
MANIFEST_VERSION_CODE="$(manifest_attr versionCode)"
MANIFEST_VERSION_NAME="$(manifest_attr versionName)"
VERSION_CODE="${ZCSR_ANDROID_VERSION_CODE:-$MANIFEST_VERSION_CODE}"
VERSION_NAME="${ZCSR_ANDROID_VERSION_NAME:-$MANIFEST_VERSION_NAME}"
if [[ ! "$VERSION_CODE" =~ ^[1-9][0-9]*$ ]]; then
    echo "ERROR: invalid Android versionCode: '$VERSION_CODE'" >&2
    exit 1
fi
if [[ -z "$VERSION_NAME" || "$VERSION_NAME" == @* ]]; then
    echo "ERROR: invalid Android versionName: '$VERSION_NAME'" >&2
    exit 1
fi
if [[ -z "${ZCSR_ANDROID_VERSION_CODE:-}" ]]; then
    echo "WARN: using manifest versionCode=$VERSION_CODE; set ZCSR_ANDROID_VERSION_CODE for device updates." >&2
fi
echo "Packaging Android versionCode=$VERSION_CODE versionName=$VERSION_NAME"

for path in \
    "$ANDROID_JAR" "$AAPT2" "$ZIPALIGN" "$APKSIGNER" \
    "$SO_PATH" "$ROOT/android/AndroidManifest.xml" "$KEYSTORE"; do
    if [[ ! -e "$path" ]]; then
        echo "ERROR: required input missing: $path" >&2
        exit 1
    fi
done

command -v zip >/dev/null
command -v unzip >/dev/null

mkdir -p "$BUILD_DIR" "$(dirname "$APK_OUT")"
rm -rf -- "$BUILD_DIR/pack-stage"
STAGE="$BUILD_DIR/pack-stage"
BASE_APK="$STAGE/base.apk"
ALIGNED_APK="$STAGE/aligned.apk"
mkdir -p "$STAGE/lib/arm64-v8a"

echo "[1/6] aapt2 link"
"$AAPT2" link \
    -o "$BASE_APK" \
    -I "$ANDROID_JAR" \
    --manifest "$ROOT/android/AndroidManifest.xml" \
    --min-sdk-version 24 \
    --replace-version \
    --version-code "$VERSION_CODE" \
    --version-name "$VERSION_NAME"

echo "[2/6] copy .so"
cp "$SO_PATH" "$STAGE/lib/arm64-v8a/libzcsr_phase0.so"

echo "[3/6] add .so to APK (absolute archive path)"
(
    cd "$STAGE"
    zip -q -0 "$BASE_APK" "lib/arm64-v8a/libzcsr_phase0.so"
)

echo "[4/6] zipalign -P 16"
"$ZIPALIGN" -P 16 -f -v 4 "$BASE_APK" "$ALIGNED_APK"

echo "[5/6] apksigner"
"$APKSIGNER" sign \
    --ks "$KEYSTORE" \
    --ks-type PKCS12 \
    --ks-key-alias "$KEY_ALIAS" \
    --ks-pass env:ZCSR_DEBUG_STOREPASS \
    --key-pass env:ZCSR_DEBUG_STOREPASS \
    --min-sdk-version 24 \
    --v4-signing-enabled false \
    --out "$APK_OUT" \
    "$ALIGNED_APK"

echo "[6/6] verify"
unzip -t "$APK_OUT" >/dev/null
"$ZIPALIGN" -c -P 16 -v 4 "$APK_OUT"
unzip -Z1 "$APK_OUT" | grep -Fx "lib/arm64-v8a/libzcsr_phase0.so" >/dev/null
"$APKSIGNER" verify --verbose --print-certs "$APK_OUT"
echo "PASS: APK created at $APK_OUT"
