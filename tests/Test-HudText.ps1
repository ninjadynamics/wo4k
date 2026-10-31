param([string]$CompilerDirectory='C:/mingw64/bin')
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'));$out=Join-Path $root '.work/test-hud-text'
New-Item -ItemType Directory -Path $out -Force|Out-Null
@'
LIBRARY KERNEL32.dll
EXPORTS
VirtualAlloc@16
ExitProcess@4
'@|Set-Content -LiteralPath (Join-Path $out 'kernel32.def') -Encoding Ascii
& "$CompilerDirectory/llvm-dlltool.exe" -m i386 -k -d "$out/kernel32.def" -l "$out/kernel32.lib";if($LASTEXITCODE){throw 'HUD test imports failed.'}
& "$CompilerDirectory/clang.exe" --target=i686-w64-windows-gnu -isystem 'C:/mingw64/x86_64-w64-mingw32/include' -Os -fno-builtin -fno-stack-protector -Wall -Wextra -Wno-unused-function -c "$PSScriptRoot/test_hud_text.c" -o "$out/test_hud_text.obj";if($LASTEXITCODE){throw 'HUD test compilation failed.'}
& "$CompilerDirectory/lld-link.exe" /machine:x86 /subsystem:console /entry:test_entry@0 /nodefaultlib "/out:$out/test_hud_text.exe" "$out/test_hud_text.obj" "$out/kernel32.lib";if($LASTEXITCODE){throw 'HUD test link failed.'}
& "$out/test_hud_text.exe";if($LASTEXITCODE){throw "HUD centering test failed: $LASTEXITCODE"}
Write-Host 'PASS: production text hooks center eight notification layouts across 4:3, 16:9, ultrawide, 32:9 and portrait; corner/menu anchors and overlay heights retained.'
