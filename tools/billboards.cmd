@echo off
rem Make the billboard contact sheet again from the installed game and the
rem current textures (assets\auth, assets\sd), then open it. Run it after
rem changing a texture: the page holds a copy of every picture. Your choices
rem stay, kept by the browser. Maintainer tool; never shipped.
setlocal
set "root=%~dp0.."
set "tool=%root%\build\tools\wo4k-build.exe"
if not exist "%tool%" (
    echo %tool% is not built yet. Run: mingw32-make tools
    pause
    exit /b 1
)
cd /d "%root%"
"%tool%" billboards wobble exports\billboards assets
if errorlevel 1 (
    pause
    exit /b 1
)
start "" "%root%\exports\billboards\index.html"
