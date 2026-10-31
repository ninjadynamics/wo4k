param([string]$GameRoot=(Join-Path $PSScriptRoot '../wobble'),[string]$CompilerDirectory='C:/mingw64/bin')
$ErrorActionPreference='Stop';$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'));$out=Join-Path $root '.work/test-paths';New-Item -ItemType Directory -Path $out -Force|Out-Null
$definition=Get-Content -Encoding UTF8 -LiteralPath (Join-Path $root 'src/runtime/build/kernel32.def') -Raw
($definition+"`r`nExitProcess@4`r`nCreateFileA@28`r`n")|Set-Content -LiteralPath (Join-Path $out 'kernel32.def') -Encoding Ascii
& "$CompilerDirectory/llvm-dlltool.exe" -m i386 -k -d "$out/kernel32.def" -l "$out/kernel32.lib";if($LASTEXITCODE){throw 'Test imports failed.'}
& "$CompilerDirectory/clang.exe" --target=i686-w64-windows-gnu -isystem 'C:/mingw64/x86_64-w64-mingw32/include' -Os -fno-builtin -fno-stack-protector -Wall -Wextra -c "$PSScriptRoot/test_paths.c" -o "$out/test_paths.obj";if($LASTEXITCODE){throw 'Path test compilation failed.'}
& "$CompilerDirectory/lld-link.exe" /machine:x86 /subsystem:console /entry:test_entry@0 /nodefaultlib "/out:$out/test_paths.exe" "$out/test_paths.obj" "$out/kernel32.lib";if($LASTEXITCODE){throw 'Path test link failed.'}
$env:WO_TEST_GAME_ROOT=[IO.Path]::GetFullPath($GameRoot);& "$out/test_paths.exe";if($LASTEXITCODE){throw "Native portable-path test failed: $LASTEXITCODE"}
Write-Host 'PASS: actual patched-EXE import table redirects 12 native asset reads, including doubled sound separators; absolute/UNC/parent paths remain unchanged.'
