@echo off
setlocal
call "%~dp0source\build.bat"
set "RESULT=%ERRORLEVEL%"
if /I not "%~1"=="--no-pause" pause
exit /b %RESULT%
