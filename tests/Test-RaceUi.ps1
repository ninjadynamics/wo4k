param([string]$GameRoot=(Join-Path $PSScriptRoot '../wobble'),[string]$CompilerDirectory='C:/mingw64/bin')
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'));$out=Join-Path $root '.work/test-race-ui'
New-Item -ItemType Directory -Path $out -Force|Out-Null
# Check the exact complete instructions used by the new two trampolines.
$exe=[IO.File]::ReadAllBytes((Join-Path $GameRoot 'Wipeout2097.exe'))
$pe=[BitConverter]::ToInt32($exe,0x3c);$sectionCount=[BitConverter]::ToUInt16($exe,$pe+6);$sections=$pe+24+[BitConverter]::ToUInt16($exe,$pe+20)
foreach($hook in @(@{rva=0x283f0;bytes=[byte[]](0x66,0xa1,0x6a,0x49,0xa6,0)},@{rva=0x31c50;bytes=[byte[]](0x0f,0xbf,0x05,0x20,0x48,0xa6,0)})){
    $found=$false
    for($i=0;$i -lt $sectionCount;$i++){$at=$sections+$i*40;$base=[BitConverter]::ToUInt32($exe,$at+12);$size=[BitConverter]::ToUInt32($exe,$at+16);if($hook.rva -ge $base -and $hook.rva+$hook.bytes.Length -le $base+$size){$offset=[BitConverter]::ToUInt32($exe,$at+20)+$hook.rva-$base;for($j=0;$j -lt $hook.bytes.Length;$j++){if($exe[$offset+$j] -ne $hook.bytes[$j]){throw 'Native race UI entry bytes do not match.'}};$found=$true;break}}
    if(-not $found){throw 'Native race UI function not found.'}
}
@'
LIBRARY KERNEL32.dll
EXPORTS
VirtualAlloc@16
ExitProcess@4
'@|Set-Content -LiteralPath (Join-Path $out 'kernel32.def') -Encoding Ascii
& "$CompilerDirectory/llvm-dlltool.exe" -m i386 -k -d "$out/kernel32.def" -l "$out/kernel32.lib";if($LASTEXITCODE){throw 'Race UI test imports failed.'}
& "$CompilerDirectory/clang.exe" --target=i686-w64-windows-gnu -isystem 'C:/mingw64/x86_64-w64-mingw32/include' -Os -fno-builtin -fno-stack-protector -Wall -Wextra -Wno-unused-function -c "$PSScriptRoot/test_race_ui.c" -o "$out/test_race_ui.obj";if($LASTEXITCODE){throw 'Race UI test compile failed.'}
& "$CompilerDirectory/lld-link.exe" /machine:x86 /subsystem:console /entry:test_entry@0 /nodefaultlib "/out:$out/test_race_ui.exe" "$out/test_race_ui.obj" "$out/kernel32.lib";if($LASTEXITCODE){throw 'Race UI test link failed.'}
& "$out/test_race_ui.exe";if($LASTEXITCODE){throw "Race UI test failed: $LASTEXITCODE"}
Write-Host 'PASS: native hook instructions, Full trails/default and saved F4 modes, backtick FPS toggle, native F1 camera, and identical wall-clock Extended Time flashing at 30/60/165/240 FPS.'
