# setup-windows.ps1 - One-click setup for Windows build environment
# This installs vcpkg, Qt dependencies, and builds the project
#
# Run: powershell -ExecutionPolicy Bypass -File setup-windows.ps1

$ErrorActionPreference = "Stop"
$ProjectDir = $PSScriptRoot

Write-Host "=== UltimateMangaReader Windows Setup ===" -ForegroundColor Cyan
Write-Host ""

# Step 1: Check for Visual Studio / Build Tools
$vsWhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$hasVS = $false
if (Test-Path $vsWhere) {
    $vsPath = & $vsWhere -latest -property installationPath 2>$null
    if ($vsPath) { $hasVS = $true }
}

if (-not $hasVS) {
    Write-Host "WARNING: Visual Studio or Build Tools not detected." -ForegroundColor Yellow
    Write-Host "Install 'Desktop development with C++' workload from:" -ForegroundColor Yellow
    Write-Host "  https://visualstudio.microsoft.com/downloads/" -ForegroundColor Cyan
    Write-Host ""
}

# Step 2: Install/update vcpkg
$VcpkgDir = "$ProjectDir\vcpkg"
if (-not (Test-Path "$VcpkgDir\vcpkg.exe")) {
    Write-Host "Installing vcpkg..." -ForegroundColor Yellow
    if (Test-Path $VcpkgDir) { Remove-Item -Recurse -Force $VcpkgDir }
    git clone https://github.com/microsoft/vcpkg.git "$VcpkgDir"
    & "$VcpkgDir\bootstrap-vcpkg.bat" -disableMetrics
} else {
    Write-Host "vcpkg already installed." -ForegroundColor Green
}

# Step 3: Install C libraries via vcpkg
Write-Host "Installing libjpeg-turbo, libpng, openssl via vcpkg..." -ForegroundColor Yellow
& "$VcpkgDir\vcpkg.exe" install libjpeg-turbo:x64-windows libpng:x64-windows openssl:x64-windows

# Step 4: Check for Qt
$QtDir = ""
$possibleQtPaths = @(
    "C:\Qt\5.15.2\msvc2019_64",
    "C:\Qt\5.15.2\msvc2022_64",
    "C:\Qt\5.15.2\mingw81_64",
    "C:\Qt\6.5.3\msvc2019_64",
    "C:\Qt\6.6.1\msvc2019_64",
    "C:\Qt\6.7.0\msvc2019_64",
    "$env:USERPROFILE\Qt\5.15.2\msvc2019_64",
    "$env:USERPROFILE\Qt\5.15.2\mingw81_64"
)
foreach ($p in $possibleQtPaths) {
    if (Test-Path "$p\bin\qmake.exe") {
        $QtDir = $p
        break
    }
}

if (-not $QtDir) {
    Write-Host ""
    Write-Host "Qt 5.15+ not found!" -ForegroundColor Red
    Write-Host "Please install Qt from: https://www.qt.io/download-qt-installer" -ForegroundColor Yellow
    Write-Host "Select: Qt 5.15.2 -> MSVC 2019 64-bit (or MinGW)" -ForegroundColor Yellow
    Write-Host ""
    Write-Host "After installing Qt, run:" -ForegroundColor Cyan
    Write-Host "  .\build-windows.ps1 -QtDir 'C:\Qt\5.15.2\msvc2019_64' -VcpkgDir '$VcpkgDir'" -ForegroundColor White
    exit 1
}

Write-Host ""
Write-Host "Qt found at: $QtDir" -ForegroundColor Green
Write-Host ""

# Step 5: Build
Write-Host "Building..." -ForegroundColor Yellow
& "$ProjectDir\build-windows.ps1" -QtDir $QtDir -VcpkgDir $VcpkgDir

Write-Host ""
Write-Host "=== Setup Complete ===" -ForegroundColor Cyan
