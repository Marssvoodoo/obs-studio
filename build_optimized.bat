@echo off
call "%~dp0scripts\build-windows-x64.bat" %*
exit /b %ERRORLEVEL%
