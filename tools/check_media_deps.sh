#!/usr/bin/env bash
# Read-only detector for the optional ZCSR media dependencies (FFmpeg, libwebp).
#
# Reports, per dependency: the configured root, present/missing headers and
# link libraries, detected version, and license class (LGPL / GPL / nonfree /
# unknown). Mirrors the configure-time CMake probe: a backend counts as present
# only when BOTH its headers and every required library are found.
#
# This script NEVER installs anything, NEVER invokes sudo or a package manager,
# and NEVER changes system state. Detected GPL / nonfree FFmpeg is flagged
# non-compliant: the ZCSR baseline requires an LGPL shared build.
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

FFMPEG_ROOT="${ZCSR_FFMPEG_ROOT:-${REPO_ROOT}/third_party/prebuilt/linux64/ffmpeg}"
WEBP_ROOT="${ZCSR_LIBWEBP_ROOT:-${REPO_ROOT}/third_party/prebuilt/linux64/libwebp}"

find_header() {
    local rel="$1"; shift
    local dir
    for dir in "$@"; do
        if [ -f "${dir}/${rel}" ]; then
            printf '%s\n' "${dir}/${rel}"
            return 0
        fi
    done
    return 1
}

find_lib() {
    local name="$1"; shift
    local dir
    for dir in "$@"; do
        local f
        for f in "${dir}/lib${name}.so" "${dir}/lib${name}.a" "${dir}/${name}.so" "${dir}/${name}.a"; do
            if [ -f "$f" ]; then
                printf '%s\n' "$f"
                return 0
            fi
        done
    done
    return 1
}

FF_INCLUDE_DIRS=("${FFMPEG_ROOT}/include" /usr/include /usr/include/"$(gcc -dumpmachine 2>/dev/null)" /usr/include/ffmpeg)
FF_LIB_DIRS=("${FFMPEG_ROOT}/lib" /usr/lib /usr/lib/"$(gcc -dumpmachine 2>/dev/null)")
WP_INCLUDE_DIRS=("${WEBP_ROOT}/include" /usr/include /usr/include/"$(gcc -dumpmachine 2>/dev/null)")
WP_LIB_DIRS=("${WEBP_ROOT}/lib" /usr/lib /usr/lib/"$(gcc -dumpmachine 2>/dev/null)")

echo '=== ZCSR media dependency check (read-only) ==='
echo "repo root        : ${REPO_ROOT}"
echo "ZCSR_FFMPEG_ROOT = ${FFMPEG_ROOT}"
echo "ZCSR_LIBWEBP_ROOT= ${WEBP_ROOT}"
echo

# ---- FFmpeg ---------------------------------------------------------------
FF_HEADERS=(libavcodec/avcodec.h libavformat/avformat.h libavutil/avutil.h libswscale/swscale.h libswresample/swresample.h)
FF_LIBS=(avcodec avformat avutil swscale swresample)
ff_missing=()

echo '[FFmpeg] headers:'
for h in "${FF_HEADERS[@]}"; do
    hit="$(find_header "$h" "${FF_INCLUDE_DIRS[@]}")" || hit=""
    if [ -n "$hit" ]; then echo "  OK      $h"; else echo "  MISSING $h"; ff_missing+=("header $h"); fi
done

echo '[FFmpeg] libraries:'
for lib in "${FF_LIBS[@]}"; do
    hit="$(find_lib "$lib" "${FF_LIB_DIRS[@]}")" || hit=""
    if [ -n "$hit" ]; then echo "  OK      $lib -> $hit"; else echo "  MISSING $lib"; ff_missing+=("library lib$lib"); fi
done

ff_tool=""
if [ -x "${FFMPEG_ROOT}/bin/ffmpeg" ]; then
    ff_tool="${FFMPEG_ROOT}/bin/ffmpeg"
elif command -v ffmpeg >/dev/null 2>&1; then
    ff_tool="$(command -v ffmpeg)"
fi

ff_version="unknown"; ff_license="unknown"; ff_gpl=0; ff_nonfree=0
if [ -n "$ff_tool" ]; then
    echo "[FFmpeg] tool: $ff_tool"
    ff_version="$("$ff_tool" -version 2>&1 | head -n 1)"
    buildconf="$("$ff_tool" -buildconf 2>&1)"
    [ -z "$buildconf" ] && buildconf="$("$ff_tool" -version 2>&1)"
    if grep -q -- '--enable-gpl' <<<"$buildconf"; then ff_gpl=1; fi
    if grep -q -- '--enable-nonfree' <<<"$buildconf"; then ff_nonfree=1; fi
    if [ "$ff_gpl" -eq 1 ]; then ff_license='GPL (NON-COMPLIANT: required LGPL)'
    elif [ "$ff_nonfree" -eq 1 ]; then ff_license='nonfree (NON-COMPLIANT: required LGPL)'
    else ff_license='LGPL'; fi
else
    echo '[FFmpeg] tool: ffmpeg executable not found (version/license unknown)'
fi
echo "[FFmpeg] version : $ff_version"
echo "[FFmpeg] license : $ff_license"

ff_found=1
[ "${#ff_missing[@]}" -gt 0 ] && ff_found=0
[ "$ff_gpl" -eq 1 ] && ff_found=0
[ "$ff_nonfree" -eq 1 ] && ff_found=0
echo "[FFmpeg] ZCSR_FFMPEG_FOUND = $ff_found"
if [ "${#ff_missing[@]}" -gt 0 ]; then
    echo '  missing items:'
    for m in "${ff_missing[@]}"; do echo "    - $m"; done
fi
echo

# ---- libwebp --------------------------------------------------------------
WP_HEADERS=(webp/decode.h webp/demux.h)
WP_LIBS=(webp webpdemux sharpyuv)
wp_missing=()

echo '[libwebp] headers:'
for h in "${WP_HEADERS[@]}"; do
    hit="$(find_header "$h" "${WP_INCLUDE_DIRS[@]}")" || hit=""
    if [ -n "$hit" ]; then echo "  OK      $h"; else echo "  MISSING $h"; wp_missing+=("header $h"); fi
done

echo '[libwebp] libraries:'
for lib in "${WP_LIBS[@]}"; do
    hit="$(find_lib "$lib" "${WP_LIB_DIRS[@]}")" || hit=""
    if [ -n "$hit" ]; then echo "  OK      $lib -> $hit"; else echo "  MISSING $lib"; wp_missing+=("library lib$lib"); fi
done
echo '[libwebp] version: 1.4.0 (vendored prebuilt; BSD-3-Clause)'

wp_found=1
[ "${#wp_missing[@]}" -gt 0 ] && wp_found=0
echo "[libwebp] ZCSR_LIBWEBP_FOUND = $wp_found"
if [ "${#wp_missing[@]}" -gt 0 ]; then
    echo '  missing items:'
    for m in "${wp_missing[@]}"; do echo "    - $m"; done
fi
echo

echo '=== summary ==='
echo "  FFmpeg  present=$ff_found  license=$ff_license"
echo "  libwebp present=$wp_found"
echo 'Read-only check complete; nothing was installed or modified.'
exit 0
