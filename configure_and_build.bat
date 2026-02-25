@echo off
set CMAKE="C:\Program Files\Microsoft Visual Studio\18\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
set CEF_ROOT=C:/OBS Project/.deps

echo === Clearing stale cmake cache ===
del /f /q "c:\OBS Project\build\CMakeCache.txt" 2>nul
rmdir /s /q "c:\OBS Project\build\CMakeFiles" 2>nul

echo === Step 1: Generating Visual Studio solution in build directory ===
%CMAKE% -S "c:\OBS Project" -B "c:\OBS Project\build" -G "Visual Studio 18 2026" ^
  -DCMAKE_PREFIX_PATH="C:/OBS Project/.deps/obs-deps-2025-08-23-x64;C:/OBS Project/.deps/obs-deps-qt6-2025-08-23-x64" ^
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
%CMAKE% --build "c:\OBS Project\build" --config Release --parallel 8

echo.
echo Build finished with exit code %errorlevel%
