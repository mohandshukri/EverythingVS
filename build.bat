@echo off
setlocal

set "CONFIG=%~1"
set "PLATFORM=%~2"
if "%CONFIG%"=="" set "CONFIG=Release"
if "%PLATFORM%"=="" set "PLATFORM=x64"

echo === Building EverythingVS (%CONFIG% ^| %PLATFORM%) ===

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo [ERROR] Visual Studio Installer not found. Open EverythingVS.sln directly in Visual Studio 2022.
    if not defined CI pause
    exit /b 1
)

set "MSBUILD="
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe`) do (
    set "MSBUILD=%%i"
)

if "%MSBUILD%"=="" (
    echo [ERROR] MSBuild.exe not found. Please install "Desktop development with C++" in Visual Studio 2022.
    if not defined CI pause
    exit /b 1
)

"%MSBUILD%" "%~dp0EverythingVS.sln" /p:Configuration=%CONFIG% /p:Platform=%PLATFORM% /m /nologo
if errorlevel 1 (
    echo [ERROR] Build failed.
    if not defined CI pause
    exit /b 1
)

echo.
echo [OK] Build succeeded! Output: bin\%PLATFORM%\%CONFIG%\EverythingVS.exe
if not defined CI pause
exit /b 0
