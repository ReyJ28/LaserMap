# LaserMap installer. Right-click > Run with PowerShell. Close Resolume first.
$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path

if (Get-Process -Name 'Resolume*' -ErrorAction SilentlyContinue) {
  Write-Host 'Resolume is running. Close it, then run this installer again.' -ForegroundColor Yellow
  Read-Host 'Press Enter to close'; exit 1
}

# 1) The Resolume plugin
$fx = Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'Resolume Arena\Extra Effects'
New-Item -ItemType Directory -Force $fx | Out-Null
Copy-Item (Join-Path $here 'LaserMap.dll') $fx -Force
Write-Host "Plugin installed:  $fx\LaserMap.dll" -ForegroundColor Green

# 2) The BEYOND helper (the plugin starts it by itself)
$app = Join-Path $env:LOCALAPPDATA 'LaserMap'
New-Item -ItemType Directory -Force $app | Out-Null
Copy-Item (Join-Path $here 'lasermap_bridge.exe') $app -Force
Write-Host "Helper installed:  $app\lasermap_bridge.exe" -ForegroundColor Green

# 3) Is Pangolin's SDK file where the helper can find it?
$roots = @($env:ProgramFiles, ${env:ProgramFiles(x86)}, $env:ProgramW6432, $env:ProgramData) | Where-Object { $_ } | Select-Object -Unique
$dll = $null
foreach ($r in $roots) {
  $dirs = Get-ChildItem $r -Directory -ErrorAction SilentlyContinue | Where-Object { $_.Name -match 'Pangolin|BEYOND' }
  foreach ($d in $dirs) {
    $dll = Get-ChildItem $d.FullName -Recurse -Filter 'BEYONDIO.dll' -Depth 3 -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($dll) { break }
  }
  if ($dll) { break }
}
if ($dll) { Write-Host "Found BEYONDIO.dll: $($dll.FullName)" -ForegroundColor Green }
else {
  Write-Host 'BEYONDIO.dll was not found in your BEYOND install.' -ForegroundColor Yellow
  Write-Host "Copy Pangolin's BEYONDIO.dll into $app (it is not included here)."
}

Write-Host ''
Write-Host 'Done. Next:' -ForegroundColor Cyan
Write-Host '  1. Start BEYOND (laser output OFF, low power on the zone).'
Write-Host '  2. Start Resolume, add the LaserMap effect to a playing clip.'
Write-Host '  3. Tick Send To BEYOND, set Zone, tick Armed, tick Grid. Warp the zone in BEYOND until the grid fits.'
Read-Host 'Press Enter to close'
