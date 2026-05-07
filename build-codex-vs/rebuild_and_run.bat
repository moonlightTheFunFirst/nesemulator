@echo off
setlocal

set "BUILD_DIR=%~dp0"
set "CONFIG=Debug"
if "%BUILD_DIR:~-1%"=="\" set "BUILD_DIR=%BUILD_DIR:~0,-1%"

if /I "%~1"=="Debug" (
    set "CONFIG=%~1"
    shift
)
if /I "%~1"=="Release" (
    set "CONFIG=%~1"
    shift
)
if /I "%~1"=="RelWithDebInfo" (
    set "CONFIG=%~1"
    shift
)
if /I "%~1"=="MinSizeRel" (
    set "CONFIG=%~1"
    shift
)

set "EXE=%BUILD_DIR%\%CONFIG%\nesemu.exe"

echo Building nesemu [%CONFIG%]...
cmake --build "%BUILD_DIR%" --config "%CONFIG%" --target nesemu
if errorlevel 1 (
    echo Build failed.
    exit /b 1
)

taskkill /IM nesemu.exe /F >nul 2>nul

if not exist "%EXE%" (
    echo Executable not found: "%EXE%"
    exit /b 1
)

echo Starting "%EXE%" %*
start "" "%EXE%" %*
