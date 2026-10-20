@echo off
setlocal
echo ================================================
echo  Sympathy - Build installer (Inno Setup)
echo ================================================
echo.

set "ISCC=C:\Program Files (x86)\Inno Setup 6\ISCC.exe"
if not exist "%ISCC%" (
    echo ERROR: Inno Setup 6 not found at:
    echo        %ISCC%
    echo        Install it from https://jrsoftware.org/isinfo.php
    exit /b 1
)

if not exist "..\build\dist\Sympathy.clap" (
    echo ERROR: build\dist\Sympathy.clap not found.
    echo        Run build.bat first.
    exit /b 1
)
if not exist "..\build\dist\Sympathy.vst3" (
    echo ERROR: build\dist\Sympathy.vst3 not found.
    echo        Run build.bat first.
    exit /b 1
)

"%ISCC%" "Sympathy.iss"
if errorlevel 1 (
    echo ERROR: Inno Setup compile failed.
    exit /b 1
)

echo.
echo Installer built. See installer\output\
endlocal
