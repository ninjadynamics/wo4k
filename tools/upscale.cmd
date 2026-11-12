@echo off
rem Upscale the installer's debanded textures (assets\sd) into assets\hd with
rem Upscayl, choosing the model. The game uses a PNG in assets\hd wherever
rem assets\auth has none of that name. Maintainer tool; never shipped.
rem
rem   tools\upscale.cmd            ask for the model
rem   tools\upscale.cmd --no-pause the same, without the final keypress
setlocal enabledelayedexpansion
set "root=%~dp0.."
set "upscayl=C:\Program Files\Upscayl\resources\bin\upscayl-bin.exe"
set "models=C:\Program Files\Upscayl\resources\models"
set "input=%root%\assets\sd"
set "output=%root%\assets\hd"
set "scale=5"
set "result=1"

if not exist "%upscayl%" (
    echo upscayl-bin.exe was not found at:
    echo     %upscayl%
    goto finish
)
if not exist "%input%\*.png" (
    echo There are no debanded textures in %input%.
    echo Run Setup or Play once so that they are made.
    goto finish
)

set count=0
for %%m in ("%models%\*.param") do (
    set /a count+=1
    set "model!count!=%%~nm"
)
if %count%==0 (
    echo No models were found in %models%.
    goto finish
)
echo Models:
for /l %%i in (1,1,%count%) do (
    if "!model%%i!"=="upscayl-standard-4x" (echo   %%i. !model%%i!  ^(default^)& set "default=%%i") else echo   %%i. !model%%i!
)
if not defined default set "default=1"
set "choice=%default%"
set /p "choice=Model number [%default%]: "
if not defined model%choice% (
    echo There is no model %choice%.
    goto finish
)
set "name=!model%choice%!"

if not exist "%output%" mkdir "%output%"
if exist "%output%\*.png" (
    echo.
    echo %output% already has PNGs. Those with the same names will be replaced.
    set "go=n"
    set /p "go=Continue? [y/N]: "
    if /i not "!go!"=="y" goto finish
)
echo.
echo Upscaling %input% with %name% at %scale%x ...
"%upscayl%" -i "%input%" -o "%output%" -s %scale% -m "%models%" -n %name% -f png
set "result=%errorlevel%"

rem The waterfall is animated from its palette; a still picture of it would
rem only be painted over.
if exist "%output%\track13-scene-cmp-16.png" del "%output%\track13-scene-cmp-16.png"

:finish
if /i not "%~1"=="--no-pause" pause
exit /b %result%
