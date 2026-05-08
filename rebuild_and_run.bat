@echo off
setlocal

pushd "%~dp0" >nul || exit /b 1

if not exist "build-cmake\CMakeCache.txt" (
    cmake -S . -B build-cmake -G Ninja
    if errorlevel 1 goto :fail
)

cmake --build build-cmake
if errorlevel 1 goto :fail

if not exist "build-cmake\nesemu.exe" (
    echo ERROR: build-cmake\nesemu.exe was not found.
    goto :fail
)

start "" "build-cmake\nesemu.exe" %*
popd >nul
exit /b 0

:fail
popd >nul
exit /b 1
