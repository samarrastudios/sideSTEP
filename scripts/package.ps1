# Builds sideSTEP and packages it as a setup program:
#   build\installer\sideSTEP-Setup-<version>.exe
# Needs Inno Setup 6 (winget install JRSoftware.InnoSetup) as well as what scripts\build.ps1 needs.
# -SkipBuild packages whatever is already in build\Release.
param([switch]$SkipBuild)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent

function Fail($msg) { Write-Host "`n$msg" -ForegroundColor Red; exit 1 }

if (-not $SkipBuild) {
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'build.ps1')
    if ($LASTEXITCODE) { Fail "Build failed; nothing was packaged." }
}

# Inno Setup's compiler.
$iscc = @("$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe", "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe", "$env:ProgramFiles\Inno Setup 6\ISCC.exe") |
    Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $iscc) { Fail "Inno Setup 6 not found. Install it with: winget install JRSoftware.InnoSetup" }

# The Visual C++ runtime DLLs that ship beside the app.
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs = & $vswhere -latest -products * -property installationPath
$redist = Get-ChildItem (Join-Path $vs 'VC\Redist\MSVC\*\x64\Microsoft.VC*.CRT') -Directory -ErrorAction SilentlyContinue |
    Sort-Object FullName | Select-Object -Last 1
if (-not $redist) { Fail "Visual C++ redistributable files not found under $vs\VC\Redist. Add the C++ workload in the Visual Studio Installer." }

# The version comes from the binary that is being packaged.
$exe = Join-Path $root 'build\Release\sideSTEP.exe'
if (-not (Test-Path $exe)) { Fail "build\Release\sideSTEP.exe is missing. Run without -SkipBuild." }
$version = (Get-Item $exe).VersionInfo.ProductVersion

& $iscc "/DAppVersion=$version" "/DVCRedist=$($redist.FullName)" /Qp (Join-Path $root 'installer\sideSTEP.iss')
if ($LASTEXITCODE) { Fail "Packaging failed (see the Inno Setup output above)." }

$setup = Join-Path $root "build\installer\sideSTEP-Setup-$version.exe"
$hash = (Get-FileHash $setup -Algorithm SHA256).Hash
Write-Host "`nPackaged:" -ForegroundColor Green
Write-Host "  $setup"
Write-Host ("  {0:N1} MB, SHA-256 {1}" -f ((Get-Item $setup).Length / 1MB), $hash)
Write-Host "It is not code-signed, so Windows SmartScreen will warn people the first time they run it."
