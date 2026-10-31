param([string]$Compiler='C:/mingw64/bin/gcc.exe')
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
& $Compiler -std=c11 -O2 -s -Wall -Wextra -static -municode (Join-Path $root 'src/audio-clean.c') -lm -o (Join-Path $root 'runtime/audio-clean.exe')
if($LASTEXITCODE){throw 'Audio processor build failed.'}
