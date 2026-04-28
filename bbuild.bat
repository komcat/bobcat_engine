@echo off
setlocal
set VSDIR=C:\Program Files\Microsoft Visual Studio\18\Community
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >NUL
if errorlevel 1 (
    echo vcvars64 failed
    exit /b 1
)
"%VSDIR%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build out\build\x64-debug-hardware --target bobcat_engine
exit /b %errorlevel%
