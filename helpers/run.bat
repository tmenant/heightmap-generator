@echo off

call "%~dp0\build.bat"

if %ERRORLEVEL% neq 0 (
    echo build failed.
    exit /b 1
)

cls

build\%PRESET_NAME%\src\hmapgen.exe %*