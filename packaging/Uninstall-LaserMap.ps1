# Removes LaserMap (plugin and helper). Close Resolume first. Your Documents\LaserMap logs/recordings are kept.
$fx = Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'Resolume Arena\Extra Effects\LaserMap.dll'
Get-Process -Name 'lasermap_bridge' -ErrorAction SilentlyContinue | Stop-Process -Force
Remove-Item $fx -Force -ErrorAction SilentlyContinue
Remove-Item (Join-Path $env:LOCALAPPDATA 'LaserMap') -Recurse -Force -ErrorAction SilentlyContinue
Write-Host 'LaserMap removed.' -ForegroundColor Green
Read-Host 'Press Enter to close'
