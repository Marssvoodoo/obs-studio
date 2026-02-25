@echo off
echo ============================================
echo OBS Studio - Optimized Build Script
echo ============================================
echo.

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

REM Configure with CMake (Release build for optimizations)
cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_CONFIGURATION_TYPES=Release

if %ERRORLEVEL% NEQ 0 (
    echo.
    echo ERROR: CMake configuration failed!
    echo.
    echo Common fixes:
    echo 1. Make sure CMake is installed and in PATH
    echo 2. Check that all dependencies are available
    echo 3. Run this from "Developer Command Prompt for VS"
    echo.
    pause
    exit /b 1
)

echo.
echo ============================================
echo Step 2: Building OBS (This may take a while)
echo ============================================
echo.

REM Build the project
cmake --build . --config Release -j

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
echo The optimized OBS build is in: build\rundir\Release\bin
echo.
echo To run OBS with optimizations:
echo   cd build\rundir\Release\bin
echo   obs64.exe
echo.
echo Next steps:
echo 1. Test the build to ensure it works
echo 2. Compare CPU usage with original build
echo 3. See NEXT_STEPS.md for performance testing
echo.
pause
