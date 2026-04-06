@echo off

call "%~dp0\setup.bat"

rmdir /s /q build >nul 2>&1

cmake --preset %PRESET_NAME%
cmake --build build/%PRESET_NAME%