param([string]$GameRoot=(Join-Path $PSScriptRoot '../wobble'),[string]$CompilerDirectory='C:/mingw64/bin')
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'));$out=Join-Path $root '.work/test-render-init'
New-Item -ItemType Directory -Path $out -Force|Out-Null
$definition=Get-Content -LiteralPath (Join-Path $root 'src/runtime/build/kernel32.def') -Encoding UTF8 -Raw
($definition+"`r`nExitProcess@4`r`n")|Set-Content -LiteralPath (Join-Path $out 'kernel32.def') -Encoding Ascii
& "$CompilerDirectory/llvm-dlltool.exe" -m i386 -k -d "$out/kernel32.def" -l "$out/kernel32.lib";if($LASTEXITCODE){throw 'Render init imports failed.'}
& "$CompilerDirectory/clang.exe" --target=i686-w64-windows-gnu -isystem 'C:/mingw64/x86_64-w64-mingw32/include' -Os -fno-builtin -fno-stack-protector -Wall -Wextra -Wno-unused-parameter -c "$PSScriptRoot/test_render_init.c" -o "$out/test_render_init.obj";if($LASTEXITCODE){throw 'Render init test compile failed.'}
& "$CompilerDirectory/lld-link.exe" /machine:x86 /base:0x10000000 /dynamicbase:no /subsystem:console /entry:test_entry@0 /nodefaultlib "/out:$out/test_render_init.exe" "$out/test_render_init.obj" "$out/kernel32.lib" (Join-Path $root 'src/runtime/build/user32.lib') (Join-Path $root 'src/runtime/build/ole32.lib');if($LASTEXITCODE){throw 'Render init test link failed.'}
$env:WO_TEST_GAME_ROOT=[IO.Path]::GetFullPath($GameRoot)
$env:WO_TEST_SETTINGS=Join-Path $out 'test-settings.ini'
$env:WO_TEST_BOUNDS=Join-Path $out 'title-bounds.bin'
$env:WO_TEST_FRAME_ART_ROOT=Join-Path $root 'assets/hd'
$env:WO2097_RENDER_TEST='1';$env:WO2097_DISTANCE_TEST='1';$env:WO2097_REFRESH_TEST='1';$env:WO2097_SIM60_TEST='0'
& "$out/test_render_init.exe"
if($LASTEXITCODE -eq 10){throw 'Integration failure reproduced: full renderer installed, but race UI hooks never activated.'}
if($LASTEXITCODE){throw "Renderer initialization integration test failed: $LASTEXITCODE"}
$bounds=[IO.File]::ReadAllBytes($env:WO_TEST_BOUNDS)
Write-Host ('PASS: original native title vertices are on-screen: '+[BitConverter]::ToSingle($bounds,0)+','+[BitConverter]::ToSingle($bounds,4)+' to '+[BitConverter]::ToSingle($bounds,8)+','+[BitConverter]::ToSingle($bounds,12)+' in 640x480; original buffers execute before flip, without consuming the queue.')
Write-Host 'PASS: startup never calls the legacy control constructor or enters its event loop; the real game window starts borderless and stays hidden until graphics initialization completes. Mono/Stereo/Surround, saved/fallback display modes, native8-bit fallback, signature guards and failure handling pass.'
Write-Host 'PASS: the supported EXE header routine (including its original x152 second-half argument) emits exactly one HD rectangle across five aspect ratios; all254+210 atlas pixels are retained. Mixed header/widget packets bind and restore the correct textures, fades and tag cleanup pass, and the same native routine retains both halves with SD fallback.'
Write-Host 'PASS: all fourteen original menu frame tiles cover their full HD UV ranges across five display shapes, with continuous subdivision UVs and unchanged SD fallback; frame sampling clamps and restores inherited wrap/mirror before subsequent draws.'
Write-Host 'PASS: PNG pixel edges drive side-column placement; actual submitted side/cap border edges match within0.002 display pixels across five screen shapes. Footer dividers align with the main columns, highlights stop at the artwork borders, duplicate native white bands are omitted, and missing HD art retains the original outlines.'
Write-Host 'PASS: premultiplied upload removes hidden transparent RGB; translucent UI vertices preserve opacity and SD sharing; generic native names and joined-logo region composition pass. HD reticles bind to native HUD slot24 with alpha blending; absent optional art keeps the SD handle. All four ending triggers use original flags without changing progress; intro runs once before title; HD title uses the native background pass and lettering ignores scene depth; title hooks initialize; actual x86 register thunk uses wall-clock ticks, controller prompt/Start input switch correctly; full renderer initializes race UI; actual patched entry points select Full trails, hold clock-based flash across every frame, toggle FPS with backtick, and pass F1 to native camera handling.'
