@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
cd /d "C:\OBS Project\plugins\obs-overlay"
cl obs-overlay.c ^
  /I"C:\OBS Project\libobs" ^
  /I"C:\OBS Project\frontend\api" ^
  /I"C:\OBS Project\build" ^
  /I"C:\OBS Project\build\config" ^
  /I"C:\OBS Project\deps\w32-pthreads" ^
  /MD /O2 ^
  /link ^
  "C:\OBS Project\build\libobs\Release\obs.lib" ^
  "C:\OBS Project\build\frontend\api\Release\obs-frontend-api.lib" ^
  "C:\OBS Project\build\deps\w32-pthreads\Release\w32-pthreads.lib" ^
  user32.lib gdi32.lib ^
  /DLL /OUT:obs-overlay.dll
