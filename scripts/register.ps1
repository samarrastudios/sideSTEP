# Turns on STEP thumbnails in File Explorer for the current user (no admin needed).
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$dll = Join-Path $root 'build\Release\sideSTEPThumbs.dll'
if (-not (Test-Path $dll)) { Write-Host "Build first: scripts\build.ps1" -ForegroundColor Red; exit 1 }
$p = Start-Process regsvr32.exe -ArgumentList '/s', "`"$dll`"" -Wait -PassThru
if ($p.ExitCode -ne 0) { Write-Host "Registration failed (regsvr32 exit code $($p.ExitCode))." -ForegroundColor Red; exit 1 }
Write-Host "Registered. Open a folder of STEP files and switch to Large icons." -ForegroundColor Green
Write-Host "If a file still shows its old icon, Windows has cached it: run Disk Cleanup > Thumbnails, or copy the file to a new name."
