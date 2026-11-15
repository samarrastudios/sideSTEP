# Builds sideSTEP (thumbnail DLL + command-line tester) from a normal PowerShell window.
# First run downloads vcpkg and compiles OpenCASCADE, which takes a while (20-60 min).
# Later runs take seconds.
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent

function Fail($msg) { Write-Host "`n$msg" -ForegroundColor Red; exit 1 }

# --- prerequisites -----------------------------------------------------------
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path $vswhere)) {
    Fail "Visual Studio (or Build Tools) not found. Install 'Build Tools for Visual Studio' with the 'Desktop development with C++' workload, then re-run."
}
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { Fail "Visual Studio is installed but the C++ workload is missing. Add 'Desktop development with C++' in the Visual Studio Installer." }
if (-not (Get-Command git -ErrorAction SilentlyContinue)) { Fail "Git is not on PATH. Install Git for Windows, then re-run." }

$cmake = (Get-Command cmake -ErrorAction SilentlyContinue).Source
if (-not $cmake) {
    $cmake = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
    if (-not (Test-Path $cmake)) { Fail "CMake not found. Add 'C++ CMake tools for Windows' in the Visual Studio Installer." }
}

# --- vcpkg (dependency manager; fetches and builds OpenCASCADE) --------------
$vcpkg = if ($env:VCPKG_ROOT -and (Test-Path (Join-Path $env:VCPKG_ROOT 'vcpkg.exe'))) { $env:VCPKG_ROOT } else { Join-Path $root 'vcpkg' }
if (-not (Test-Path (Join-Path $vcpkg 'vcpkg.exe'))) {
    if (-not (Test-Path $vcpkg)) { git clone https://github.com/microsoft/vcpkg.git $vcpkg; if ($LASTEXITCODE) { Fail "vcpkg clone failed." } }
    & (Join-Path $vcpkg 'bootstrap-vcpkg.bat') -disableMetrics
    if ($LASTEXITCODE) { Fail "vcpkg bootstrap failed." }
}

# --- release any loaded copy of the DLL so the linker can overwrite it -------
Get-Process dllhost, prevhost -ErrorAction SilentlyContinue | ForEach-Object {
    try { if ($_.Modules.ModuleName -contains 'sideSTEPThumbs.dll') { $_ | Stop-Process -Force } } catch {}
}

# --- configure + build -------------------------------------------------------
$build = Join-Path $root 'build'
& $cmake -S $root -B $build -A x64 `
    "-DCMAKE_TOOLCHAIN_FILE=$vcpkg\scripts\buildsystems\vcpkg.cmake" `
    -DVCPKG_TARGET_TRIPLET=x64-windows-static-md
if ($LASTEXITCODE) { Fail "CMake configure failed (see output above)." }
& $cmake --build $build --config Release --parallel
if ($LASTEXITCODE) { Fail "Build failed (see output above)." }

Write-Host "`nBuilt:" -ForegroundColor Green
Get-ChildItem (Join-Path $build 'Release') -Include *.dll, *.exe -Recurse | ForEach-Object { Write-Host "  $($_.FullName)" }
Write-Host "`nNext: scripts\test.ps1 (renders the sample files to PNG), then scripts\register.ps1"
