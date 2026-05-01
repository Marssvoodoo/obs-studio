@echo off
REM Resolve MSBuild via vswhere instead of a hardcoded VS path.
REM (Previous version pointed at "Visual Studio\18\Professional" which
REM does not exist — VS 2022 is version 17.) Falls back to a 17 path
REM if vswhere is unavailable. Override by setting MSBUILD before calling.

if "%MSBUILD%"=="" (
  for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe`) do set MSBUILD="%%i"
)
if "%MSBUILD%"=="" (
  set MSBUILD="C:\Program Files\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe"
)

REM Default to "build" (matches configure_and_build.bat / build_optimized.bat).
REM Alternates seen in this tree: build_x64\obs-studio.sln, build_x86\obs-studio.sln.
REM Override via:  set SLN=...  before calling.
if "%SLN%"=="" set SLN="%~dp0build\obs-studio.sln"

echo Building OBS Studio (Release Win32) with %MSBUILD%
%MSBUILD% %SLN% /p:Configuration=Release /p:Platform=Win32 /m:8 /v:m
echo Build finished with exit code %errorlevel%
