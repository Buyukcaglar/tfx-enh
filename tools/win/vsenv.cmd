@echo off
rem Runs a command inside the MSVC x64 developer environment.
rem Usage: tools\win\vsenv.cmd <command> [args...]
rem TFX_VS selects the install (default: the newest with the C++ tools).
setlocal
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not defined TFX_VS (
  for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "TFX_VS=%%i"
)
if not defined TFX_VS (
  echo vsenv: no Visual Studio with the C++ x64 tools found 1>&2
  exit /b 2
)
call "%TFX_VS%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 2
set "PATH=%TFX_VS%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;%TFX_VS%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%PATH%"
%*
