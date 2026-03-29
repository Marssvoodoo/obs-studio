@echo off
echo ============================================
echo OBS Studio - Optimized Build Script
echo ============================================
echo.

REM Auto-detect cmake
where cmake >nul 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: cmake not found in PATH.
    echo Run this from "Developer Command Prompt for VS" or add cmake to PATH.
    pause
    exit /b 1
)

REM Set dependency paths relative to project root
set "PROJECT_ROOT=%~dp0"
set "DEPS_DIR=%PROJECT_ROOT%.deps"

if not exist "%DEPS_DIR%" (
    echo ERROR: Dependencies not found at %DEPS_DIR%
    echo Run the dependency setup first.
    pause
    exit /b 1
)

REM Check if build directory exists
if not exist "build" (
    echo Creating build directory...
    mkdir build
)

cd build

echo.
echo ============================================
echo Step 1: Running CMake Configuration
echo ============================================
echo.

cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_CONFIGURATION_TYPES=Release ^
  -DCMAKE_PREFIX_PATH="%DEPS_DIR%/obs-deps-2025-08-23-x64;%DEPS_DIR%/obs-deps-qt6-2025-08-23-x64" ^
  -DOBS_VERSION_OVERRIDE="31.0.0" ^
  -DENABLE_BROWSER=ON

if %ERRORLEVEL% NEQ 0 (
    echo.
    echo ERROR: CMake configuration failed!
    echo.
    pause
    exit /b 1
)

echo.
echo ============================================
echo Step 2: Building OBS (This may take a while)
echo ============================================
echo.

cmake --build . --config Release --parallel %NUMBER_OF_PROCESSORS%

if %ERRORLEVEL% NEQ 0 (
    echo.
    echo ERROR: Build failed!
    echo Check the output above for error messages.
    echo.
    pause
    exit /b 1
)

echo.
echo ============================================
echo BUILD SUCCESSFUL!
echo ============================================
echo.
echo The optimized OBS build is in: build\rundir\Release\bin\64bit
echo.
pause
