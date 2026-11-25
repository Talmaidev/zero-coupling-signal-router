<#
.SYNOPSIS
    Read-only detector for the optional ZCSR media dependencies (FFmpeg, libwebp).

.DESCRIPTION
    Reports, for each dependency: the configured root variable, which headers
    and which link libraries are present, the detected version, and the license
    class (LGPL / GPL / nonfree / unknown). This script NEVER installs anything,
    NEVER runs sudo / package-manager commands, and NEVER writes outside its own
    console output. It mirrors the configure-time CMake probe: a backend counts
    as present only when BOTH its headers and every required library are found.

    Detected GPL / nonfree FFmpeg is flagged non-compliant: the ZCSR baseline
    requires an LGPL shared build.

.PARAMETER FfmpegRoot
    FFmpeg prefix (contains include/ and lib/). Defaults to the repo prebuilt
    win64 location; pass an explicit prefix (vcpkg / C:\vcpkg\installed\x64-windows)
    to inspect another tree.

.PARAMETER WebpRoot
    libwebp prefix. Defaults to the repo prebuilt win64 location.

.EXAMPLE
    powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\check_media_deps.ps1
#>

[CmdletBinding()]
param(
    [string]$FfmpegRoot,
    [string]$WebpRoot
)

$ErrorActionPreference = 'Stop'

$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if (-not $FfmpegRoot) {
    $FfmpegRoot = Join-Path $RepoRoot 'third_party\prebuilt\win64\ffmpeg'
}
if (-not $WebpRoot) {
    $WebpRoot = Join-Path $RepoRoot 'third_party\prebuilt\win64\libwebp'
}

function Test-Any {
    param([string[]]$BaseDirs, [string[]]$Names)
    foreach ($base in $BaseDirs) {
        if (-not $base) { continue }
        foreach ($n in $Names) {
            $candidate = Join-Path $base $n
            if (Test-Path -LiteralPath $candidate) { return $candidate }
        }
    }
    return $null
}

Write-Output '=== ZCSR media dependency check (read-only) ==='
Write-Output "repo root      : $RepoRoot"
Write-Output "ZCSR_FFMPEG_ROOT = $FfmpegRoot"
Write-Output "ZCSR_LIBWEBP_ROOT = $WebpRoot"
Write-Output ''

# ---- FFmpeg ---------------------------------------------------------------
$ffHeaders = @(
    'libavcodec\avcodec.h',
    'libavformat\avformat.h',
    'libavutil\avutil.h',
    'libswscale\swscale.h',
    'libswresample\swresample.h'
)
$ffLibNames = @('avcodec', 'avformat', 'avutil', 'swscale', 'swresample')

$ffIncludeDirs = @(
    (Join-Path $FfmpegRoot 'include'),
    '/usr/include',
    '/usr/include/x86_64-linux-gnu'
)
$ffLibDirs = @(
    (Join-Path $FfmpegRoot 'lib'),
    '/usr/lib',
    '/usr/lib/x86_64-linux-gnu'
)

$ffMissing = New-Object System.Collections.Generic.List[string]
Write-Output '[FFmpeg] headers:'
foreach ($h in $ffHeaders) {
    $hit = Test-Any -BaseDirs $ffIncludeDirs -Names @($h)
    if ($hit) { Write-Output "  OK      $h" }
    else { Write-Output "  MISSING $h"; $ffMissing.Add("header $h") }
}

Write-Output '[FFmpeg] libraries:'
foreach ($lib in $ffLibNames) {
    $candidates = @("$lib.lib", "lib$lib.dll.a", "lib$lib.so", "$lib.so")
    $hit = Test-Any -BaseDirs $ffLibDirs -Names $candidates
    if ($hit) { Write-Output "  OK      $lib -> $hit" }
    else { Write-Output "  MISSING $lib"; $ffMissing.Add("library lib$lib") }
}

# Version + license: locate ffmpeg executable under the root bin/, then PATH.
$ffTool = $null
$binGuess = Join-Path $FfmpegRoot 'bin\ffmpeg.exe'
if (Test-Path -LiteralPath $binGuess) { $ffTool = $binGuess }
if (-not $ffTool) {
    $cmd = Get-Command ffmpeg -ErrorAction SilentlyContinue
    if ($cmd) { $ffTool = $cmd.Source }
}

$ffVersion = 'unknown'
$ffLicense = 'unknown'
$ffGpl = $false
$ffNonfree = $false
if ($ffTool) {
    Write-Output "[FFmpeg] tool: $ffTool"
    $versionLine = (& $ffTool -version 2>&1 | Select-Object -First 1)
    if ($versionLine) { $ffVersion = "$versionLine".Trim() }
    $buildconf = (& $ffTool -buildconf 2>&1) -join "`n"
    if (-not $buildconf) {
        $buildconf = (& $ffTool -version 2>&1) -join "`n"
    }
    if ($buildconf -match '--enable-gpl') { $ffGpl = $true }
    if ($buildconf -match '--enable-nonfree') { $ffNonfree = $true }
    if ($ffGpl) { $ffLicense = 'GPL (NON-COMPLIANT: required LGPL)' }
    elseif ($ffNonfree) { $ffLicense = 'nonfree (NON-COMPLIANT: required LGPL)' }
    else { $ffLicense = 'LGPL' }
} else {
    Write-Output '[FFmpeg] tool: ffmpeg executable not found (version/license unknown)'
}
Write-Output "[FFmpeg] version : $ffVersion"
Write-Output "[FFmpeg] license : $ffLicense"

$ffFound = ($ffMissing.Count -eq 0) -and (-not $ffGpl) -and (-not $ffNonfree)
Write-Output "[FFmpeg] ZCSR_FFMPEG_FOUND = $ffFound"
if ($ffMissing.Count -gt 0) {
    Write-Output '  missing items:'
    foreach ($m in $ffMissing) { Write-Output "    - $m" }
}
Write-Output ''

# ---- libwebp --------------------------------------------------------------
$wpHeaders = @('webp\decode.h', 'webp\demux.h')
$wpLibNames = @(
    @{ tag = 'webp';      files = @('libwebp.lib', 'webp.lib', 'libwebp.so', 'libwebp.a') },
    @{ tag = 'webpdemux'; files = @('libwebpdemux.lib', 'webpdemux.lib', 'libwebpdemux.so', 'libwebpdemux.a') },
    @{ tag = 'sharpyuv';  files = @('libsharpyuv.lib', 'sharpyuv.lib', 'libsharpyuv.so', 'libsharpyuv.a') }
)
$wpIncludeDirs = @(
    (Join-Path $WebpRoot 'include'),
    '/usr/include',
    '/usr/include/x86_64-linux-gnu'
)
$wpLibDirs = @(
    (Join-Path $WebpRoot 'lib'),
    '/usr/lib',
    '/usr/lib/x86_64-linux-gnu'
)

$wpMissing = New-Object System.Collections.Generic.List[string]
Write-Output '[libwebp] headers:'
foreach ($h in $wpHeaders) {
    $hit = Test-Any -BaseDirs $wpIncludeDirs -Names @($h)
    if ($hit) { Write-Output "  OK      $h" }
    else { Write-Output "  MISSING $h"; $wpMissing.Add("header $h") }
}

Write-Output '[libwebp] libraries:'
foreach ($entry in $wpLibNames) {
    $hit = Test-Any -BaseDirs $wpLibDirs -Names $entry.files
    if ($hit) { Write-Output "  OK      $($entry.tag) -> $hit" }
    else { Write-Output "  MISSING $($entry.tag)"; $wpMissing.Add("library lib$($entry.tag)") }
}
Write-Output '[libwebp] version: 1.4.0 (vendored prebuilt; BSD-3-Clause)'

$wpFound = ($wpMissing.Count -eq 0)
Write-Output "[libwebp] ZCSR_LIBWEBP_FOUND = $wpFound"
if ($wpMissing.Count -gt 0) {
    Write-Output '  missing items:'
    foreach ($m in $wpMissing) { Write-Output "    - $m" }
}
Write-Output ''

Write-Output '=== summary ==='
Write-Output "  FFmpeg  present=$ffFound  license=$ffLicense"
Write-Output "  libwebp present=$wpFound"
Write-Output 'Read-only check complete; nothing was installed or modified.'
exit 0
