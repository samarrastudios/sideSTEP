# Turns on STEP thumbnails and the preview pane in File Explorer for the current user,
# straight from the build folder (no admin needed). For everyday use prefer install.ps1.
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$dll = Join-Path $root 'build\Release\sideSTEPThumbs.dll'
if (-not (Test-Path $dll)) { Write-Host "Build first: scripts\build.ps1" -ForegroundColor Red; exit 1 }
$p = Start-Process regsvr32.exe -ArgumentList '/s', "`"$dll`"" -Wait -PassThru
if ($p.ExitCode -ne 0) { Write-Host "Registration failed (regsvr32 exit code $($p.ExitCode))." -ForegroundColor Red; exit 1 }
# Thumbnail handlers that come from packaged (Store) apps beat registry ones like ours.
$rival = Get-AppxPackage -Name 'AelyoSoftworks.STEPExplorerPreview' -ErrorAction SilentlyContinue
if ($rival) {
    Write-Host "Note: the Store app 'STEP Explorer Preview' is installed and Windows will use its thumbnails for .step/.stp/.p21 instead of sideSTEP's. Uninstall it (Settings > Apps) to see sideSTEP thumbnails." -ForegroundColor Yellow
}
Write-Host "Registered. Open a folder of STEP files and switch to Large icons." -ForegroundColor Green
Write-Host "If a file still shows its old icon, Windows has cached it: run Disk Cleanup > Thumbnails, or copy the file to a new name."
