@echo off
rem Make a set of upscaled textures for assets\hd from the installer's
rem debanded ones (assets\sd), with WO4K's own neural upscaler
rem (src\core\wo_upscale.h); each upscaled picture is then debanded once
rem more. Maintainer tool; never shipped.
rem
rem   tools\neural-upscale.cmd      ask for the scale
rem   tools\neural-upscale.cmd 4    4x (or 2) without asking or pausing
rem
rem The pictures the authored folder (assets\auth) has are left out, and so
rem is Valparaiso's waterfall. Play indexes the set on its next launch.
setlocal
set "root=%~dp0.."
set "tool=%root%\build\tools\wo4k-build.exe"
set "result=1"

if not exist "%tool%" (
    echo %tool% is not built yet. Run: mingw32-make tools
    goto finish
)
if not exist "%root%\assets\sd\*.png" (
    echo There are no debanded textures in %root%\assets\sd.
    echo Run Setup or Play once so that they are made.
    goto finish
)

rem A page of the game's textures is as large as its largest picture's
rem scale: at 2x it takes 2 MB of video memory, at 4x 8 MB.
set "scale=%~1"
if not defined scale (
    set "scale=4"
    set /p "scale=Scale, 2 or 4 [4]: "
)
"%tool%" upscale "%root%\assets\sd" "%root%\assets\hd" %scale% "%root%\assets\auth"
set "result=%errorlevel%"

:finish
if "%~1"=="" pause
exit /b %result%
