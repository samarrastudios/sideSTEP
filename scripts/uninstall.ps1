# Removes a sideSTEP install made by install.ps1. Runs from the install folder
# (Settings > Apps calls it there) or from the repo's scripts folder.
$dest = Join-Path $env:LOCALAPPDATA 'Programs\sideSTEP'
$dllName = 'sideSTEPThumbs.dll'
$dll = Join-Path $dest $dllName
if (Test-Path $dll) { Start-Process regsvr32.exe -ArgumentList '/u', '/s', "`"$dll`"" -Wait }
$viewer = Join-Path $dest 'sideSTEP.exe'
if (Test-Path $viewer) {
    Get-Process sideSTEP -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $viewer } | Stop-Process -Force
    Start-Process $viewer -ArgumentList '--unregister' -Wait
}
Remove-Item (Join-Path ([Environment]::GetFolderPath('Programs')) 'sideSTEP.lnk') -Force -ErrorAction SilentlyContinue
Get-Process dllhost, prevhost -ErrorAction SilentlyContinue | ForEach-Object {
    try { if ($_.Modules.ModuleName -contains $dllName) { $_ | Stop-Process -Force } } catch {}
}
Remove-Item 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\sideSTEP' -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item 'HKCU:\Software\sideSTEP' -Recurse -Force -ErrorAction SilentlyContinue
if (Test-Path $dest) {
    # This script may be running from inside $dest, so step out and delete from a detached shell.
    Set-Location $env:TEMP
    Start-Process cmd.exe -ArgumentList '/c', "timeout /t 2 /nobreak >nul & rmdir /s /q `"$dest`"" -WindowStyle Hidden
}
Write-Host "sideSTEP uninstalled."
