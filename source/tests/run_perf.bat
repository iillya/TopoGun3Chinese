@echo off
setlocal
for %%I in ("%~dp0..\..") do set "ROOT=%%~fI"
set "QT=%ROOT%\..\_ThirdParty\Qt\5.12.5\msvc2017_64"
if not exist "%ROOT%\build\tests" mkdir "%ROOT%\build\tests"
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
if not errorlevel 0 exit /b 1
cl /nologo /O2 /MD /W4 /EHsc /std:c++17 /Zc:__cplusplus /utf-8 /I"%QT%\include" "%~dp0performance.cpp" /Fo"%ROOT%\build\tests\performance.obj" /Fe:"%ROOT%\build\tests\performance.exe" /link "%QT%\lib\Qt5Core.lib" "%QT%\lib\Qt5Gui.lib" "%QT%\lib\Qt5Widgets.lib" user32.lib shell32.lib version.lib
if errorlevel 1 exit /b 1
if not errorlevel 0 exit /b 1
set "PATH=%QT%\bin;%PATH%"
"%ROOT%\build\tests\performance.exe" "%ROOT%\source"
exit /b %ERRORLEVEL%
