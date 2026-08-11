@echo off
setlocal

REM Auto-detect cmake from PATH or VS installations.
REM NOTE: the VS glob below must NOT be "*cmake.exe" because that also
REM matches xdcmake.exe (XML Doc Comments Merge Tool), which happily
REM accepts -S/--build with zero effect and produces no real build.
where cmake.exe >nul 2>&1
if %ERRORLEVEL% EQU 0 (
    set "CMAKE=cmake"
) else (
    for /f "delims=" %%i in ('dir /b /s "C:\Program Files\Microsoft Visual Studio\cmake.exe" 2^>nul') do set "CMAKE=%%i"
)
if not defined CMAKE (
    echo ERROR: cmake not found. Install Visual Studio with CMake or add cmake to PATH.
    exit /b 1
)
echo Using cmake: %CMAKE%
"%CMAKE%" --version >nul 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: "%CMAKE%" is not a valid cmake executable.
    exit /b 1
)

set "PROJECT_ROOT=%~dp0"
REM Strip trailing backslash so quoted paths with spaces don't escape the closing quote
if "%PROJECT_ROOT:~-1%"=="\" set "PROJECT_ROOT=%PROJECT_ROOT:~0,-1%"
set "DEPS_DIR=%PROJECT_ROOT%\.deps"
set "CEF_ROOT=%DEPS_DIR%"
set "BUILD_DIR=%PROJECT_ROOT%\build"

echo === Clearing stale cmake cache ===
del /f /q "%BUILD_DIR%\CMakeCache.txt" 2>nul
rmdir /s /q "%BUILD_DIR%\CMakeFiles" 2>nul

echo === Step 1: Generating Visual Studio solution in build directory ===
"%CMAKE%" -S "%PROJECT_ROOT%" -B "%BUILD_DIR%" ^
  -DCMAKE_PREFIX_PATH="%DEPS_DIR%/obs-deps-2025-08-23-x64;%DEPS_DIR%/obs-deps-qt6-2025-08-23-x64" ^
  -DVIRTUALCAM_GUID="A3FCE0F5-3493-419F-958A-ABA1250EC20B" ^
  -DENABLE_BROWSER=ON ^
  -DCEF_ROOT_DIR="%CEF_ROOT%" ^
  -DCEF_LIBRARY_WRAPPER_RELEASE="%CEF_ROOT%/build/libcef_dll_wrapper/libcef_dll_wrapper/Release/libcef_dll_wrapper.lib" ^
  -DOBS_VERSION_OVERRIDE="32.2.1-perf" ^
  -DCMAKE_BUILD_TYPE=Release

if %errorlevel% NEQ 0 (
    echo CMake configuration failed!
    exit /b 1
)

echo.
echo === Step 2: Building OBS Studio (Release x64) ===
"%CMAKE%" --build "%BUILD_DIR%" --config Release --parallel %NUMBER_OF_PROCESSORS%

echo.
echo Build finished with exit code %errorlevel%
