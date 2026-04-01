@echo off
echo === UltimateMangaReader Windows Build ===
echo.

call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" x64
if errorlevel 1 (
    echo ERROR: Could not set up MSVC environment
    exit /b 1
)

if not exist "%~dp0build-windows" mkdir "%~dp0build-windows"
cd /d "%~dp0build-windows"

REM Only run qmake if Makefile doesn't exist (incremental build)
if not exist "Makefile" (
    echo Running qmake...
    C:\Qt\5.15.2\msvc2019_64\bin\qmake.exe "%~dp0UltimateMangaReader.pro" CONFIG+=desktop
    if errorlevel 1 (
        echo QMAKE FAILED
        exit /b 1
    )
)

REM Use jom (parallel make) if available, else nmake
if exist "%~dp0tools\jom.exe" (
    echo Building with jom [parallel]...
    "%~dp0tools\jom.exe" -j %NUMBER_OF_PROCESSORS%
) else (
    echo Building with nmake [single-threaded]...
    nmake
)
if errorlevel 1 (
    echo BUILD FAILED
    exit /b 1
)

echo.
echo === Copying DLLs ===
copy /Y "%~dp0vcpkg\installed\x64-windows\bin\turbojpeg.dll" release\ 2>nul
copy /Y "%~dp0vcpkg\installed\x64-windows\bin\jpeg62.dll" release\ 2>nul
copy /Y "%~dp0vcpkg\installed\x64-windows\bin\libpng16.dll" release\ 2>nul
copy /Y "%~dp0vcpkg\installed\x64-windows\bin\zlib1.dll" release\ 2>nul
REM Qt 5.15 requires OpenSSL 1.1.1 (libssl-1_1-x64.dll), NOT OpenSSL 3.x
if exist "%~dp0ssl\libssl-1_1-x64.dll" (
    copy /Y "%~dp0ssl\libssl-1_1-x64.dll" release\ 2>nul
    copy /Y "%~dp0ssl\libcrypto-1_1-x64.dll" release\ 2>nul
) else (
    echo WARNING: OpenSSL 1.1.1 DLLs not found in ssl\ folder. HTTPS will not work.
)

echo Running windeployqt...
C:\Qt\5.15.2\msvc2019_64\bin\windeployqt.exe release\UltimateMangaReader.exe --no-translations --no-compiler-runtime 2>nul

echo.
echo === BUILD SUCCESS ===
echo Executable: %cd%\release\UltimateMangaReader.exe
