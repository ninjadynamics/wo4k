@echo off
rem The authoring studio: a page for choosing textures to draw anew, each
rem once for all its copies (src\tools\build_auth.c). It opens in the
rem browser; keep this window open while you use it. Maintainer tool;
rem never shipped.
setlocal
set "root=%~dp0.."
set "tool=%root%\build\tools\wo4k-build.exe"
if not exist "%tool%" (
    echo %tool% is not built yet. Run: mingw32-make tools
    pause
    exit /b 1
)
cd /d "%root%"
"%tool%" auth
if errorlevel 1 pause
