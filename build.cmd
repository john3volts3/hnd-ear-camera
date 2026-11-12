@echo off
rem Builds all targets in Release with the Visual Studio Build Tools (x64).
rem Usage: build.cmd [clean]
setlocal
set "ROOT=%~dp0"
set "BUILD=%ROOT%build"

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo vswhere.exe not found: install Visual Studio Build Tools 2022 with the C++ workload.
    exit /b 1
)
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSINSTALL=%%i"
if not defined VSINSTALL (
    echo No Visual Studio installation with the C++ x64 tools was found.
    exit /b 1
)

call "%VSINSTALL%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1

if /i "%~1"=="clean" if exist "%BUILD%" rmdir /s /q "%BUILD%"

cmake -S "%ROOT%." -B "%BUILD%" -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release || exit /b 1
cmake --build "%BUILD%" || exit /b 1
echo.
echo Build OK: %BUILD%\bin
