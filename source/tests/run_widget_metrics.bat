@echo off
setlocal
for %%I in ("%~dp0..\..") do set "ROOT=%%~fI"
set "QT=%ROOT%\..\_ThirdParty\Qt\5.12.5\msvc2017_64"
if not exist "%ROOT%\build\tests" mkdir "%ROOT%\build\tests"
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
if not errorlevel 0 exit /b 1
cl /nologo /O2 /MD /W4 /EHsc /std:c++17 /Zc:__cplusplus /permissive- /utf-8 /I"%QT%\include" "%~dp0widget_metrics.cpp" /Fo"%ROOT%\build\tests\widget_metrics.obj" /Fe:"%ROOT%\build\tests\widget_metrics.exe" /link "%QT%\lib\Qt5Core.lib" "%QT%\lib\Qt5Gui.lib" "%QT%\lib\Qt5Widgets.lib" user32.lib shell32.lib version.lib
if errorlevel 1 exit /b 1
if not errorlevel 0 exit /b 1
set "PATH=%QT%\bin;%PATH%"
set "QT_QPA_PLATFORM=offscreen"
set "QT_QPA_FONTDIR=%WINDIR%\Fonts"
set "QT_QPA_PLATFORM_PLUGIN_PATH=%QT%\plugins\platforms"
"%ROOT%\build\tests\widget_metrics.exe" "%ROOT%\build\tests\widget_metrics.png"
exit /b %ERRORLEVEL%
