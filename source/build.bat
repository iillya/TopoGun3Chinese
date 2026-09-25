@echo off
setlocal EnableExtensions
for %%I in ("%~dp0..") do set "ROOT=%%~fI"
set "QT=%ROOT%\..\_ThirdParty\Qt\5.12.5\msvc2017_64"
set "OUT=%ROOT%\build\out"
set "OBJ=%ROOT%\build\obj"
if not exist "%QT%\lib\Qt5Widgets.lib" exit /b 1
if not exist "%OUT%\translations" mkdir "%OUT%\translations"
if not exist "%OBJ%" mkdir "%OBJ%"
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
rc /nologo /I"%ROOT%\icon" /fo "%OBJ%\app_icon.res" "%ROOT%\icon\app_icon.rc"
if errorlevel 1 exit /b 1
cl /nologo /O2 /MD /W4 /LD /EHsc /std:c++17 /Zc:__cplusplus /permissive- /utf-8 /I"%QT%\include" "%~dp0hook.cpp" /Fo"%OBJ%\hook.obj" /Fe:"%OUT%\TopoGunChineseHook.dll" /link /IMPLIB:"%OBJ%\hook.lib" "%OBJ%\app_icon.res" "%QT%\lib\Qt5Core.lib" "%QT%\lib\Qt5Gui.lib" "%QT%\lib\Qt5Widgets.lib" user32.lib shell32.lib version.lib
if errorlevel 1 exit /b 1
cl /nologo /O2 /MT /W4 /EHsc /std:c++17 /utf-8 "%~dp0launcher.cpp" "%OBJ%\app_icon.res" /Fo"%OBJ%\launcher.obj" /Fe:"%OUT%\TopoGunChineseLauncher.exe" /link /SUBSYSTEM:WINDOWS user32.lib version.lib
if errorlevel 1 exit /b 1
copy /Y "%~dp0translations\*_zh.json" "%OUT%\translations\" >nul
if errorlevel 1 exit /b 1
if /I "%~1"=="--runtime-only" exit /b 0
call "%~dp0build_inno.bat" --package-only
exit /b %ERRORLEVEL%
