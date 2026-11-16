# Turns STEP thumbnails and the preview pane back off and releases the DLL.
$root = Split-Path $PSScriptRoot -Parent
$dll = Join-Path $root 'build\Release\sideSTEPThumbs.dll'
if (Test-Path $dll) { Start-Process regsvr32.exe -ArgumentList '/u', '/s', "`"$dll`"" -Wait }
Get-Process dllhost, prevhost -ErrorAction SilentlyContinue | ForEach-Object {
    try { if ($_.Modules.ModuleName -contains 'sideSTEPThumbs.dll') { $_ | Stop-Process -Force } } catch {}
}
Write-Host "Unregistered."
