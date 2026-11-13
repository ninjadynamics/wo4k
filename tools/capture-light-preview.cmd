@echo off
rem Take the three screenshots Setup's "Adjust shadows / highlights" preview is
rem made from. Maintainer tool; never shipped.
rem
rem   1. Run this. Play starts the game as usual.
rem   2. Race, and at a view worth showing press F9. The scene holds still
rem      for a few seconds while three screenshots are taken (lit as the
rem      game lights it, fully lit, unlit), then the race runs on.
rem   3. Leave the game, and run
rem        python tools\Build-LightPreview.py
rem      which makes assets\ui\light-preview-*.png from the three newest
rem      screenshots.
setlocal
set "root=%~dp0.."
if not exist "%root%\Play.exe" (
    echo Play.exe is not in %root%.
    pause
    exit /b 1
)
set WO4K_LIGHT_CAPTURE=1
"%root%\Play.exe"
