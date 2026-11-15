# Installs sideSTEP for the current user (no admin): copies the built files to
# %LOCALAPPDATA%\Programs\sideSTEP, turns on Explorer thumbnails from there, and
# adds an entry under Settings > Apps > Installed apps so it can be removed.
# This is the stop-gap until the setup .exe exists; it needs scripts\build.ps1 first.
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$src = Join-Path $root 'build\Release'
$dllName = 'sideSTEPThumbs.dll'
if (-not (Test-Path (Join-Path $src $dllName))) { Write-Host "Build first: scripts\build.ps1" -ForegroundColor Red; exit 1 }

$dest = Join-Path $env:LOCALAPPDATA 'Programs\sideSTEP'
New-Item -ItemType Directory -Force $dest | Out-Null

# Release any loaded copy so the files can be replaced.
Get-Process dllhost, prevhost -ErrorAction SilentlyContinue | ForEach-Object {
    try { if ($_.Modules.ModuleName -contains $dllName) { $_ | Stop-Process -Force } } catch {}
}

Copy-Item (Join-Path $src $dllName) $dest -Force
Copy-Item (Join-Path $src 'sidestep-thumb.exe') $dest -Force
Copy-Item (Join-Path $root 'assets\brand\icon\sidestep.ico') $dest -Force
Copy-Item (Join-Path $root 'LICENSE') (Join-Path $dest 'LICENSE.txt') -Force
Copy-Item (Join-Path $PSScriptRoot 'uninstall.ps1') $dest -Force

$dll = Join-Path $dest $dllName
$p = Start-Process regsvr32.exe -ArgumentList '/s', "`"$dll`"" -Wait -PassThru
if ($p.ExitCode -ne 0) { Write-Host "Registration failed (regsvr32 exit code $($p.ExitCode))." -ForegroundColor Red; exit 1 }

$version = (Get-Item $dll).VersionInfo.ProductVersion
$uninst = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\sideSTEP'
New-Item -Force $uninst | Out-Null
$values = @{
    DisplayName     = 'sideSTEP'
    DisplayVersion  = $version
    Publisher       = 'Adrian Samarra'
    DisplayIcon     = (Join-Path $dest 'sidestep.ico')
    InstallLocation = $dest
    URLInfoAbout    = 'https://github.com/samarrastudios/sideSTEP'
    UninstallString = "powershell.exe -NoProfile -ExecutionPolicy Bypass -File `"$(Join-Path $dest 'uninstall.ps1')`""
}
foreach ($k in $values.Keys) { Set-ItemProperty $uninst -Name $k -Value $values[$k] }
Set-ItemProperty $uninst -Name NoModify -Value 1 -Type DWord
Set-ItemProperty $uninst -Name NoRepair -Value 1 -Type DWord
$kb = [int]((Get-ChildItem $dest -File | Measure-Object Length -Sum).Sum / 1KB)
Set-ItemProperty $uninst -Name EstimatedSize -Value $kb -Type DWord

Write-Host "sideSTEP $version installed to $dest" -ForegroundColor Green
$rival = Get-AppxPackage -Name 'AelyoSoftworks.STEPExplorerPreview' -ErrorAction SilentlyContinue
if ($rival) {
    Write-Host "Note: the Store app 'STEP Explorer Preview' is installed and Windows will use its thumbnails for .step/.stp/.p21 instead of sideSTEP's. Uninstall it (Settings > Apps) to see sideSTEP thumbnails." -ForegroundColor Yellow
}
