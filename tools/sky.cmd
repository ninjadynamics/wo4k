@echo off
rem The sky page of the authoring studio: how each track's sky picture goes
rem round its sky (once, as one panorama, or more), the size to draw it at,
rem and a picture to start from (src\tools\build_sky.c). It opens in the
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
"%tool%" sky
if errorlevel 1 pause
