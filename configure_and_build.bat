@echo off
setlocal

REM Auto-detect cmake from PATH or VS installations
where cmake >nul 2>&1
if %ERRORLEVEL% EQU 0 (
    set "CMAKE=cmake"
) else (
    for /f "delims=" %%i in ('dir /b /s "C:\Program Files\Microsoft Visual Studio\*cmake.exe" 2^>nul') do set "CMAKE=%%i"
)
if not defined CMAKE (
    echo ERROR: cmake not found. Install Visual Studio with CMake or add cmake to PATH.
    exit /b 1
)

set "PROJECT_ROOT=%~dp0"
set "DEPS_DIR=%PROJECT_ROOT%.deps"
set "CEF_ROOT=%DEPS_DIR%"

echo === Clearing stale cmake cache ===
del /f /q "%PROJECT_ROOT%build\CMakeCache.txt" 2>nul
rmdir /s /q "%PROJECT_ROOT%build\CMakeFiles" 2>nul

echo === Step 1: Generating Visual Studio solution in build directory ===
"%CMAKE%" -S "%PROJECT_ROOT%" -B "%PROJECT_ROOT%build" ^
  -DCMAKE_PREFIX_PATH="%DEPS_DIR%/obs-deps-2025-08-23-x64;%DEPS_DIR%/obs-deps-qt6-2025-08-23-x64" ^
  -DVIRTUALCAM_GUID="A3FCE0F5-3493-419F-958A-ABA1250EC20B" ^
  -DENABLE_BROWSER=ON ^
  -DCEF_ROOT_DIR="%CEF_ROOT%" ^
  -DCEF_LIBRARY_WRAPPER_RELEASE="%CEF_ROOT%/build/libcef_dll_wrapper/libcef_dll_wrapper/Release/libcef_dll_wrapper.lib" ^
  -DCMAKE_BUILD_TYPE=Release

if %errorlevel% NEQ 0 (
    echo CMake configuration failed!
    exit /b 1
)

echo.
echo === Step 2: Building OBS Studio (Release x64) ===
"%CMAKE%" --build "%PROJECT_ROOT%build" --config Release --parallel %NUMBER_OF_PROCESSORS%

echo.
echo Build finished with exit code %errorlevel%
