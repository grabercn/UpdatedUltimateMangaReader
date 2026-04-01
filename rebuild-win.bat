@echo off
echo === Clean Rebuild ===
if exist "%~dp0build-windows" rmdir /s /q "%~dp0build-windows"
call "%~dp0build-win.bat"
