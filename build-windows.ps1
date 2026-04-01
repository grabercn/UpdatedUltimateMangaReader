# build-windows.ps1 - Build UltimateMangaReader for Windows desktop testing
# Requires: Qt 5.15+ with MSVC or MinGW, OpenSSL, libjpeg-turbo, libpng
#
# Quick setup with vcpkg:
#   git clone https://github.com/microsoft/vcpkg.git
#   cd vcpkg && bootstrap-vcpkg.bat
#   vcpkg install libjpeg-turbo libpng openssl --triplet x64-windows
#
# Or install Qt via the online installer: https://www.qt.io/download-qt-installer
# Then set QTDIR below to your Qt installation path.

param(
    [string]$QtDir = "",
    [string]$VcpkgDir = "",
    [switch]$Clean,
    [switch]$Release
)

$ErrorActionPreference = "Stop"

Write-Host "=== UltimateMangaReader Windows Build ===" -ForegroundColor Cyan

# Auto-detect Qt
if (-not $QtDir) {
    $possibleQtPaths = @(
        "C:\Qt\5.15.2\msvc2019_64",
        "C:\Qt\5.15.2\mingw81_64",
        "C:\Qt\6.5.3\msvc2019_64",
        "C:\Qt\6.6.1\msvc2019_64",
        "$env:USERPROFILE\Qt\5.15.2\msvc2019_64",
        "$env:USERPROFILE\Qt\5.15.2\mingw81_64"
    )
    foreach ($p in $possibleQtPaths) {
        if (Test-Path "$p\bin\qmake.exe") {
            $QtDir = $p
            break
        }
    }
}

if (-not $QtDir -or -not (Test-Path "$QtDir\bin\qmake.exe")) {
    Write-Host "ERROR: Qt not found. Please install Qt 5.15+ and set -QtDir parameter." -ForegroundColor Red
    Write-Host "Download from: https://www.qt.io/download-qt-installer" -ForegroundColor Yellow
    Write-Host ""
    Write-Host "Example: .\build-windows.ps1 -QtDir 'C:\Qt\5.15.2\msvc2019_64'" -ForegroundColor Yellow
    exit 1
}

Write-Host "Using Qt: $QtDir" -ForegroundColor Green

$qmake = "$QtDir\bin\qmake.exe"
$projectDir = $PSScriptRoot

# Clean if requested
if ($Clean) {
    Write-Host "Cleaning..." -ForegroundColor Yellow
    if (Test-Path "$projectDir\build-windows") {
        Remove-Item -Recurse -Force "$projectDir\build-windows"
    }
}

# Create build directory
$buildDir = "$projectDir\build-windows"
if (-not (Test-Path $buildDir)) {
    New-Item -ItemType Directory -Path $buildDir | Out-Null
}

Set-Location $buildDir

# Configure
Write-Host "Configuring with qmake..." -ForegroundColor Yellow
$qmakeArgs = @("$projectDir\UltimateMangaReader.pro", "CONFIG+=desktop")
if ($Release) {
    $qmakeArgs += "CONFIG+=release"
} else {
    $qmakeArgs += "CONFIG+=debug"
}

if ($VcpkgDir -and (Test-Path $VcpkgDir)) {
    $vcpkgInclude = "$VcpkgDir\installed\x64-windows\include"
    $vcpkgLib = "$VcpkgDir\installed\x64-windows\lib"
    $qmakeArgs += "INCLUDEPATH+=$vcpkgInclude"
    $qmakeArgs += "LIBS+=-L$vcpkgLib"
}

& $qmake @qmakeArgs
if ($LASTEXITCODE -ne 0) {
    Write-Host "qmake failed!" -ForegroundColor Red
    exit 1
}

# Detect build tool (jom for MSVC, mingw32-make for MinGW)
$makeCmd = $null
if (Get-Command "jom" -ErrorAction SilentlyContinue) {
    $makeCmd = "jom"
} elseif (Get-Command "nmake" -ErrorAction SilentlyContinue) {
    $makeCmd = "nmake"
} elseif (Test-Path "$QtDir\..\..\..\Tools\mingw*\bin\mingw32-make.exe") {
    $mingwPath = (Get-ChildItem "$QtDir\..\..\..\Tools\mingw*\bin\mingw32-make.exe" | Select-Object -First 1).FullName
    $makeCmd = $mingwPath
} elseif (Get-Command "mingw32-make" -ErrorAction SilentlyContinue) {
    $makeCmd = "mingw32-make"
} elseif (Get-Command "make" -ErrorAction SilentlyContinue) {
    $makeCmd = "make"
}

if (-not $makeCmd) {
    Write-Host "ERROR: No build tool found (jom, nmake, mingw32-make, or make)" -ForegroundColor Red
    exit 1
}

Write-Host "Building with: $makeCmd" -ForegroundColor Yellow
& $makeCmd -j $env:NUMBER_OF_PROCESSORS
if ($LASTEXITCODE -ne 0) {
    Write-Host "Build failed!" -ForegroundColor Red
    exit 1
}

# Deploy Qt DLLs
$exePath = Get-ChildItem -Path $buildDir -Filter "UltimateMangaReader.exe" -Recurse | Select-Object -First 1
if ($exePath) {
    Write-Host "Deploying Qt dependencies..." -ForegroundColor Yellow
    $windeployqt = "$QtDir\bin\windeployqt.exe"
    if (Test-Path $windeployqt) {
        & $windeployqt $exePath.FullName --no-translations --no-compiler-runtime
    }
    Write-Host ""
    Write-Host "BUILD SUCCESSFUL!" -ForegroundColor Green
    Write-Host "Executable: $($exePath.FullName)" -ForegroundColor Cyan
} else {
    Write-Host "Build completed but executable not found in $buildDir" -ForegroundColor Yellow
}

Set-Location $projectDir
