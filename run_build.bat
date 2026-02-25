@echo off
set MSBUILD="C:\Program Files\Microsoft Visual Studio\18\Professional\MSBuild\Current\Bin\MSBuild.exe"
set SLN="c:\OBS Project\build_x86\obs-studio.sln"

echo Building OBS Studio (Release Win32)...
%MSBUILD% %SLN% /p:Configuration=Release /p:Platform=Win32 /m:8 /v:m
echo Build finished with exit code %errorlevel%
