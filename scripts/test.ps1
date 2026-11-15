# Renders every sample STEP file to a PNG using the same code path as the Explorer thumbnail.
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$exe = Join-Path $root 'build\Release\sidestep-thumb.exe'
if (-not (Test-Path $exe)) { Write-Host "Build first: scripts\build.ps1" -ForegroundColor Red; exit 1 }
$out = Join-Path $root 'build\test-output'
New-Item -ItemType Directory -Force $out | Out-Null
Get-ChildItem (Join-Path $root 'samples') -Include *.step, *.stp -Recurse | ForEach-Object {
    $png = Join-Path $out ($_.BaseName + '.png')
    Write-Host "$($_.Name): " -NoNewline
    & $exe $_.FullName $png 256
}
Write-Host "`nImages are in $out"
Invoke-Item $out
