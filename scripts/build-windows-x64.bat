@echo off
setlocal EnableExtensions

set "PROJECT_ROOT=%~dp0.."
for %%I in ("%PROJECT_ROOT%") do set "PROJECT_ROOT=%%~fI"
set "CONFIGURATION=%~1"
if not defined CONFIGURATION set "CONFIGURATION=RelWithDebInfo"

set "CMAKE_EXE="
for /f "delims=" %%I in ('where cmake.exe 2^>nul') do if not defined CMAKE_EXE set "CMAKE_EXE=%%I"
if defined CMAKE_EXE goto cmake_found

set "VSWHERE_EXE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE_EXE%" goto cmake_missing
for /f "usebackq delims=" %%I in (`"%VSWHERE_EXE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -find Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe`) do if not defined CMAKE_EXE set "CMAKE_EXE=%%I"
if not defined CMAKE_EXE goto cmake_missing

:cmake_found
for %%I in ("%CMAKE_EXE%") do set "CTEST_EXE=%%~dpIctest.exe"
if not exist "%CTEST_EXE%" (
  echo ERROR: ctest.exe was not found beside "%CMAKE_EXE%".
  exit /b 1
)

echo Configuring OBS with the windows-x64 preset...
pushd "%PROJECT_ROOT%"
"%CMAKE_EXE%" --preset windows-x64
if not errorlevel 1 goto configure_complete
set "RESULT=%ERRORLEVEL%"
popd
exit /b %RESULT%
:configure_complete

echo Building OBS %CONFIGURATION% x64...
"%CMAKE_EXE%" --build build_x64 --config "%CONFIGURATION%" --parallel
if not errorlevel 1 goto build_complete
set "RESULT=%ERRORLEVEL%"
popd
exit /b %RESULT%
:build_complete

echo Running OBS regression tests...
"%CTEST_EXE%" --test-dir build_x64 -C "%CONFIGURATION%" --output-on-failure
set "RESULT=%ERRORLEVEL%"
popd
exit /b %RESULT%

:cmake_missing
echo ERROR: A valid CMake installation was not found in PATH or Visual Studio.
exit /b 1
