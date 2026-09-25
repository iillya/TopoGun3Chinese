@echo off
setlocal
for %%I in ("%~dp0..\..") do set "ROOT=%%~fI"
if not exist "%ROOT%\build\tests" mkdir "%ROOT%\build\tests"
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
cl /nologo /O2 /MT /W4 /EHsc /std:c++17 /utf-8 "%~dp0compatibility.cpp" /Fo"%ROOT%\build\tests\compatibility.obj" /Fe:"%ROOT%\build\tests\compatibility.exe" /link version.lib
if errorlevel 1 exit /b 1
"%ROOT%\build\tests\compatibility.exe" "%ROOT%\build\out\TopoGunChineseHook.dll" "%ROOT%\..\_ThirdParty\Qt\5.12.5\msvc2017_64\bin"
exit /b %ERRORLEVEL%
