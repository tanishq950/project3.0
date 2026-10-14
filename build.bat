@echo off
setlocal
cd /d "%~dp0"
call "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
cl /nologo /W3 /O2 paint.c /link /SUBSYSTEM:WINDOWS
if errorlevel 1 exit /b 1
echo Built paint.exe
