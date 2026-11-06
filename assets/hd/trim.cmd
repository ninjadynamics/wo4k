@echo off
rem Double-click to losslessly recompress the PNGs beside this script.
rem Pixels, alpha, bit depth and colour-profile chunks are preserved; editing
rem metadata (including Photoshop/XMP data) is discarded. Originals are
rem replaced only after verification, and only if the result is smaller.
rem This runs deband -z; build it once from the repository root with:
rem     mingw32-make deband
setlocal
set "deband=%~dp0..\..\tools\deband\deband.exe"
set "trim_exit=1"
if not exist "%deband%" (
    echo deband.exe was not found. Build it from the repository root with:
    echo     mingw32-make deband
    goto finish
)
"%deband%" -z "%~dp0."
set "trim_exit=%errorlevel%"
:finish
if /i not "%~1"=="--no-pause" pause
exit /b %trim_exit%
