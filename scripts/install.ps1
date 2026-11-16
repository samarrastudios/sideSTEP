# Installs sideSTEP for the current user (no admin): copies the built files to
# %LOCALAPPDATA%\Programs\sideSTEP, turns on Explorer thumbnails and the preview
# pane from there, offers the viewer as an app for STEP files, and adds an entry
# under Settings > Apps > Installed apps so it can be removed.
# This is the stop-gap until the setup .exe exists; it needs scripts\build.ps1 first.
# -NoPreviewHelper: do not turn on the background helper that removes Windows' "downloaded from
# the internet" mark from STEP files (it can be switched on later from the viewer's File menu).
param([switch]$NoPreviewHelper)
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

# A host that was just stopped can hold the file for a moment longer; retry briefly.
for ($try = 1; ; $try++) {
    try { Copy-Item (Join-Path $src $dllName) $dest -Force; break }
    catch { if ($try -ge 10) { throw }; Start-Sleep -Milliseconds 500 }
}
Copy-Item (Join-Path $src 'sidestep-thumb.exe') $dest -Force
$viewer = Join-Path $src 'sideSTEP.exe'
$hasViewer = Test-Path $viewer
if ($hasViewer) {
    Get-Process sideSTEP -ErrorAction SilentlyContinue | Where-Object { $_.Path -like "$dest\*" } | Stop-Process -Force
    # A helper that was just stopped can hold the file for a moment longer; retry briefly.
    for ($try = 1; ; $try++) {
        try { Copy-Item $viewer $dest -Force; break }
        catch { if ($try -ge 10) { throw }; Start-Sleep -Milliseconds 500 }
    }
}
Copy-Item (Join-Path $root 'assets\brand\icon\sidestep.ico') $dest -Force
Copy-Item (Join-Path $root 'LICENSE') (Join-Path $dest 'LICENSE.txt') -Force
Copy-Item (Join-Path $PSScriptRoot 'uninstall.ps1') $dest -Force

$dll = Join-Path $dest $dllName
$p = Start-Process regsvr32.exe -ArgumentList '/s', "`"$dll`"" -Wait -PassThru
if ($p.ExitCode -ne 0) { Write-Host "Registration failed (regsvr32 exit code $($p.ExitCode))." -ForegroundColor Red; exit 1 }

if ($hasViewer) {
    # The viewer registers itself as an app that opens .step/.stp/.p21 and gets a Start menu shortcut.
    Start-Process (Join-Path $dest 'sideSTEP.exe') -ArgumentList '--register' -Wait
    # Background helper (also started at every sign-in): removes Windows' "downloaded from the
    # internet" mark from STEP files, which otherwise blocks the Explorer preview pane.
    # No -Wait here: PowerShell would also wait for the helper this starts, which never exits.
    $switch = if ($NoPreviewHelper) { '--helper-off' } else { '--helper-on' }
    (Start-Process (Join-Path $dest 'sideSTEP.exe') -ArgumentList $switch -PassThru).WaitForExit(10000) | Out-Null
    $lnk = Join-Path ([Environment]::GetFolderPath('Programs')) 'sideSTEP.lnk'
    $sc = (New-Object -ComObject WScript.Shell).CreateShortcut($lnk)
    $sc.TargetPath = Join-Path $dest 'sideSTEP.exe'
    $sc.Description = 'STEP files, seen without the CAD package.'
    $sc.Save()
}

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
if ($hasViewer) {
    Write-Host "To open STEP files in sideSTEP by default: right-click a .step file > Open with > Choose another app > sideSTEP > Always. (Windows only lets you make that choice yourself.)"
}
$rival = Get-AppxPackage -Name 'AelyoSoftworks.STEPExplorerPreview' -ErrorAction SilentlyContinue
if ($rival) {
    Write-Host "Note: the Store app 'STEP Explorer Preview' is installed and Windows will use its thumbnails for .step/.stp/.p21 instead of sideSTEP's. Uninstall it (Settings > Apps) to see sideSTEP thumbnails." -ForegroundColor Yellow
}
