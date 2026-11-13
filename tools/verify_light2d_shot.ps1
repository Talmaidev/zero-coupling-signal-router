param([int]$RunMs = 800, [int]$MaxWaitMs = 20000)
Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class Win32 {
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  public struct RECT { public int L; public int T; public int R; public int B; }
}
"@
$exe = "C:\Users\chen\Documents\GitHub\zero-coupling-signal-router\build-msvc\zcsr_test.exe"
$out = "C:\Users\chen\Documents\GitHub\zero-coupling-signal-router\build-msvc\shot_win.png"
$p = Start-Process -FilePath $exe -ArgumentList "autoquit" -PassThru `
     -RedirectStandardOutput "C:\Users\chen\Documents\GitHub\zero-coupling-signal-router\build-msvc\shot_out.log" `
     -RedirectStandardError "C:\Users\chen\Documents\GitHub\zero-coupling-signal-router\build-msvc\shot_err.log"
Start-Sleep -Milliseconds $RunMs
[Win32]::SetForegroundWindow($p.MainWindowHandle) | Out-Null
$p.Refresh()
$h = $p.MainWindowHandle
$r = New-Object Win32+RECT
[Win32]::GetWindowRect($h, [ref]$r) | Out-Null
$w = $r.R - $r.L
$hh = $r.B - $r.T
if ($w -lt 50 -or $hh -lt 50) { Write-Output "BADRECT $w x $hh"; Stop-Process -Id $p.Id -Force; exit 1 }

# STATE-TRIGGERED CAPTURE (frame-rate independent): poll the flame pixel
# (client 256,216) until it is warm (module active) in two consecutive
# samples 250ms apart - this also skips the autoquit settings-overlay
# window, where the scene is dimmed. Falls back to capture-after-timeout.
$probeX = [int](256 + 8); $probeY = [int](216 + 31)
$deadline = (Get-Date).AddMilliseconds($MaxWaitMs)
$warmStreak = 0
$captured = $false
while ((Get-Date) -lt $deadline) {
  Start-Sleep -Milliseconds 250
  if ($p.HasExited) { break }
  $probe = New-Object System.Drawing.Bitmap(1, 1)
  $pg = [System.Drawing.Graphics]::FromImage($probe)
  $pg.CopyFromScreen($r.L + $probeX, $r.T + $probeY, 0, 0, $probe.Size)
  $pg.Dispose()
  $c = $probe.GetPixel(0, 0)
  $probe.Dispose()
  if ($c.R -gt 50 -and $c.R -gt $c.B) { $warmStreak++ } else { $warmStreak = 0 }
  if ($warmStreak -ge 2) { $captured = $true; break }
}
if (-not $captured) { Write-Output "WARN: module-warm window not detected in ${MaxWaitMs}ms; capturing anyway" }

$bmp = New-Object System.Drawing.Bitmap($w, $hh)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($r.L, $r.T, 0, 0, $bmp.Size)
$g.Dispose()
$bmp.Save($out, [System.Drawing.Imaging.ImageFormat]::Png)
# calibration: find the brightest warm pixel (the flame) -> true client origin
$bestX = -1; $bestY = -1; $bestV = -1
for ($yy = 0; $yy -lt $hh; $yy += 2) {
  for ($xx = 0; $xx -lt $w; $xx += 2) {
    $c = $bmp.GetPixel($xx, $yy)
    if ($c.R -gt 200 -and $c.G -gt 140) {
      $v = $c.R + $c.G
      if ($v -gt $bestV) { $bestV = $v; $bestX = $xx; $bestY = $yy }
    }
  }
}
Write-Output "flame-brightest at bitmap ($bestX,$bestY) val=$bestV (client target 256,216 -> offset $(256 - $bestX),$(216 - $bestY))"
# pixel probes straight from the in-memory capture (client offset ~8,31)
$probes = @(
  @(256,216,"flame spot k=0.5 (client y=216)"),
  @(126,422,"rug A TL quadrant (u.25,v.25 -> red)"),
  @(174,470,"rug A BR quadrant (u.75,v.75 -> yellow)"),
  @(106,402,"rug A transparent frame (discard -> floor)"),
  @(338,432,"rug B sub-rect UV (all BR yellow)"),
  @(386,480,"rug B second point (yellow)"),
  @(100,64,"inset1 pos center"),
  @(316,64,"inset2 nrm center"),
  @(1230,700,"background")
)
foreach ($pr in $probes) {
  $px = [int]($pr[0] + 8); $py = [int]($pr[1] + 31)
  if ($px -ge 0 -and $px -lt $w -and $py -ge 0 -and $py -lt $hh) {
    $c = $bmp.GetPixel($px, $py)
    Write-Output ("probe {0}: R={1} G={2} B={3}" -f $pr[2], $c.R, $c.G, $c.B)
  }
}
foreach ($row in @(360, 264)) {
  $line = "row $row :"
  for ($cx = 850; $cx -le 1200; $cx += 25) {
    $c = $bmp.GetPixel([int]($cx + 8), [int]($row + 31))
    $line += " [$cx]$($c.R),$($c.G),$($c.B)"
  }
  Write-Output $line
}
# coarse ASCII luminance grid
$cols = 120
$rows = [int]($cols * $hh / $w / 2.2)
$small = New-Object System.Drawing.Bitmap($cols, $rows)
$gs = [System.Drawing.Graphics]::FromImage($small)
$gs.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::Low
$gs.DrawImage($bmp, 0, 0, $cols, $rows)
$chars = " .:-=+*#%@".ToCharArray()
for ($y = 0; $y -lt $rows; $y++) {
  $line = ""
  for ($x = 0; $x -lt $cols; $x++) {
    $c = $small.GetPixel($x, $y)
    $lum = ($c.R * 0.3 + $c.G * 0.49 + $c.B * 0.11) / 255.0
    $idx = [int]($lum * 9.0)
    if ($idx -gt 9) { $idx = 9 }
    $line += $chars[$idx]
  }
  Write-Output $line
}
$gs.Dispose(); $small.Dispose(); $bmp.Dispose()
Stop-Process -Id $p.Id -Force
Write-Output "saved $out ${w}x${hh}"
Write-Output "--- exe stdout ---"
Get-Content "C:\Users\chen\Documents\GitHub\zero-coupling-signal-router\build-msvc\shot_out.log" -ErrorAction SilentlyContinue
Write-Output "--- exe stderr ---"
Get-Content "C:\Users\chen\Documents\GitHub\zero-coupling-signal-router\build-msvc\shot_err.log" -ErrorAction SilentlyContinue
