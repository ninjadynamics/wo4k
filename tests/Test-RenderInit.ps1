param([string]$GameRoot=(Join-Path $PSScriptRoot '../wobble'),[string]$CompilerDirectory='C:/mingw64/bin')
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'));$out=Join-Path $root '.work/test-render-init'
New-Item -ItemType Directory -Path $out -Force|Out-Null
$definition=Get-Content -LiteralPath (Join-Path $root 'src/runtime/build/kernel32.def') -Encoding UTF8 -Raw
($definition+"`r`nExitProcess@4`r`nLoadLibraryW@4`r`nGetProcAddress@8`r`n")|Set-Content -LiteralPath (Join-Path $out 'kernel32.def') -Encoding Ascii
& "$CompilerDirectory/llvm-dlltool.exe" -m i386 -k -d "$out/kernel32.def" -l "$out/kernel32.lib";if($LASTEXITCODE){throw 'Render init imports failed.'}
& "$CompilerDirectory/clang.exe" --target=i686-w64-windows-gnu -isystem 'C:/mingw64/x86_64-w64-mingw32/include' -Os -fno-builtin -fno-stack-protector -Wall -Wextra -Wno-unused-parameter -c "$PSScriptRoot/test_render_init.c" -o "$out/test_render_init.obj";if($LASTEXITCODE){throw 'Render init test compile failed.'}
& "$CompilerDirectory/lld-link.exe" /machine:x86 /base:0x10000000 /dynamicbase:no /subsystem:console /entry:test_entry@0 /nodefaultlib "/out:$out/test_render_init.exe" "$out/test_render_init.obj" "$out/kernel32.lib" (Join-Path $root 'src/runtime/build/user32.lib') (Join-Path $root 'src/runtime/build/ole32.lib');if($LASTEXITCODE){throw 'Render init test link failed.'}
$env:WO_TEST_GAME_ROOT=[IO.Path]::GetFullPath($GameRoot)
$env:WO_TEST_SETTINGS=Join-Path $out 'test-settings.ini'
$env:WO_TEST_BOUNDS=Join-Path $out 'title-bounds.bin'
$env:WO_TEST_FRAME_ART_ROOT=Join-Path $root 'assets/hd'
$env:WO2097_RENDER_TEST='1';$env:WO2097_DISTANCE_TEST='1';$env:WO2097_REFRESH_TEST='1';$env:WO2097_SIM60_TEST='0'
$env:WO_TEST_DDRAW_WRAPPER=$null;$env:WO_TEST_MATERIAL_REPORT=$null
if($env:WO_TEST_GPU_MATERIALS -eq '1'){
    $env:WO_TEST_DDRAW_WRAPPER=Join-Path $root 'runtime/ddraw.dll'
    $env:WO_TEST_MATERIAL_REPORT=Join-Path $out 'world-material-gpu.bin'
    Set-Location -LiteralPath $out
    @('LogLevel = none','ResolutionScale = off','TextureFilter = bilinear','ColorKeyMethod = alphatest(1)')|Set-Content -LiteralPath (Join-Path $out 'DDrawCompat.ini') -Encoding Ascii
    $env:WO_TEST_EDGE_REF='1'
    & "$out/test_render_init.exe"
    if($LASTEXITCODE){throw "Original sprite-edge baseline failed: $LASTEXITCODE"}
    Copy-Item -LiteralPath $env:WO_TEST_MATERIAL_REPORT -Destination (Join-Path $out 'world-material-edge-original.bin') -Force
    @('LogLevel = none','ResolutionScale = off','TextureFilter = bilinear','ColorKeyMethod = alphatest(128)')|Set-Content -LiteralPath (Join-Path $out 'DDrawCompat.ini') -Encoding Ascii
    $env:WO_TEST_EDGE_REF='128'
}
& "$out/test_render_init.exe"
if($LASTEXITCODE -eq 10){throw 'Integration failure reproduced: full renderer installed, but race UI hooks never activated.'}
if($LASTEXITCODE){throw "Renderer initialization integration test failed: $LASTEXITCODE"}
if($env:WO_TEST_GPU_MATERIALS -eq '1'){Write-Host 'PASS: real offscreen GPU reproduces opaque-surface alpha-test holes AND black-backed color-keyed sprites. Production reset preserves opaque surfaces, source-alpha foliage and native black-key masking; the historical key-off produces a verified black rectangle. No presentation or display change.'}
if($env:WO_TEST_GPU_MATERIALS -eq '1'){Write-Host 'PASS: A/B GPU samples verify that the original filtered sprite edge is darkened, while alpha cutoff128 rejects that fringe; opaque pixels and transparent black masking still pass.'}
if($env:WO_TEST_GPU_MATERIALS -eq '1'){Write-Host 'PASS: explosion/smoke A/B pixels reproduce double-coverage dark halos; premultiplied effect packets preserve neutral edges and opaque interiors at 0/21/128/255 opacity, plus native additive destination blending.'}
if($env:WO_TEST_GPU_MATERIALS -eq '1'){Write-Host 'PASS: PS1 B+F explosions and exact clamp(B-F) smoke match real GPU pixels across two backgrounds, three fades and overlapping quads; black contributes zero and native blend factors are restored.'}
Write-Host 'PASS: the original x86 DrawExplode emits its real mode0 billboards; each camera/ship-translated emitter retains its PS1 blend, original silhouette and neutral128 modulation. Native FinishTransformBuffer processes the appended white smoke vertices and three-pass triangles without moving its placeholder.'
Write-Host 'PASS: each world buffer resets inherited explicit alpha testing and preserves native color keying, including with neutral lighting and soft shadows/effects disabled.'
Write-Host 'PASS: original x86 FinishTransformBuffer closes the rewritten stream through its retained PROCESSVERTICES pointer; vertex count, reset commands, triangle indices and final EXIT survive unchanged in position and meaning.'
$bounds=[IO.File]::ReadAllBytes($env:WO_TEST_BOUNDS)
Write-Host 'PASS: main/numpad +/- and Page Up/Down independently adjust/save shadows and highlights through the native key hook (0-300%, highlights neutral at100). Every 5% pair stays monotonic. Production material preparation preserves blended/shared RGB opacity, effects, alpha, geometry, HUD and menus; malformed streams and zero/default bypasses pass.'
Write-Host ('PASS: original native title vertices are on-screen: '+[BitConverter]::ToSingle($bounds,0)+','+[BitConverter]::ToSingle($bounds,4)+' to '+[BitConverter]::ToSingle($bounds,8)+','+[BitConverter]::ToSingle($bounds,12)+' in 640x480; original buffers execute before flip, without consuming the queue.')
Write-Host 'PASS: startup never calls the legacy control constructor or enters its event loop; the real game window starts borderless and stays hidden until graphics initialization completes. Mono/Stereo/Surround, saved/fallback display modes, native8-bit fallback, signature guards and failure handling pass.'
Write-Host 'PASS: the supported EXE header routine (including its original x152 second-half argument) emits exactly one HD rectangle across five aspect ratios; all254+210 atlas pixels are retained. Mixed header/widget packets bind and restore the correct textures, fades and tag cleanup pass, and the same native routine retains both halves with SD fallback.'
Write-Host 'PASS: all fourteen original menu frame tiles cover their full HD UV ranges across five display shapes, with continuous subdivision UVs and unchanged SD fallback; frame sampling clamps and restores inherited wrap/mirror before subsequent draws.'
Write-Host 'PASS: original native glyphs and initialized HUD quads align their visible PNG pixels to180px at4K (proportionally at4:3/1080p/ultrawide/portrait). Native DrawWeapon centers the complete indicator with every item and SD fallback; the real FPS pass shares the bottom margin.'
Write-Host 'PASS: PNG pixel edges drive side-column placement; actual submitted side/cap border edges match within0.002 display pixels across five screen shapes. Footer dividers align with the main columns, highlights stop at the artwork borders, duplicate native white bands are omitted, and missing HD art retains the original outlines.'
Write-Host 'PASS: premultiplied upload removes hidden transparent RGB; translucent UI vertices preserve opacity and SD sharing; generic native names and joined-logo region composition pass. HD reticles bind to native HUD slot24 with alpha blending; absent optional art keeps the SD handle. All four ending triggers use original flags without changing progress; intro runs once before title; HD title uses the native background pass and lettering ignores scene depth; title hooks initialize; actual x86 register thunk uses wall-clock ticks, controller prompt/Start input switch correctly; full renderer initializes race UI; actual patched entry points select Full trails, hold clock-based flash across every frame, toggle FPS with backtick, and pass F1 to native camera handling.'
