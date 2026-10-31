param([string]$CompilerDirectory='C:\mingw64\bin',[string]$HeaderDirectory='C:\mingw64\x86_64-w64-mingw32\include')
$ErrorActionPreference='Stop'
$bin = $CompilerDirectory
$headers = $HeaderDirectory
$runtimeSource = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../src/runtime'))
$out = Join-Path $runtimeSource 'build'
New-Item -ItemType Directory -Path $out -Force | Out-Null
@'
LIBRARY KERNEL32.dll
EXPORTS
GetModuleFileNameW@12
GetModuleHandleW@4
SetCurrentDirectoryW@4
GetFileAttributesW@4
GetPrivateProfileIntW@16
WritePrivateProfileStringW@16
GetEnvironmentVariableW@12
VirtualAlloc@16
VirtualFree@12
ReadFile@20
GetFileSizeEx@8
VirtualProtect@16
GetCurrentProcess@0
FlushInstructionCache@12
QueryPerformanceCounter@4
QueryPerformanceFrequency@4
Sleep@4
CreateFileW@28
WriteFile@20
CloseHandle@4
GetCurrentProcessId@0
CreateFileMappingW@24
MapViewOfFile@20
CreateProcessW@40
GetTickCount@0
'@ | Set-Content -LiteralPath (Join-Path $out 'kernel32.def') -Encoding Ascii
@'
LIBRARY WINMM.dll
EXPORTS
mciSendCommandA@16
mciSendStringW@16
joyGetPosEx@8
joyGetNumDevs@0
joyGetDevCapsA@12
timeKillEvent@4
timeGetDevCaps@8
timeSetEvent@20
timeGetTime@0
'@ | Set-Content -LiteralPath (Join-Path $out 'winmm.def') -Encoding Ascii
@'
LIBRARY USER32.dll
EXPORTS
GetSystemMetrics@4
GetForegroundWindow@0
GetWindowThreadProcessId@8
'@ | Set-Content -LiteralPath (Join-Path $out 'user32.def') -Encoding Ascii
foreach ($lib in @('kernel32','winmm','user32')) {
    & "$bin\llvm-dlltool.exe" -m i386 -k -d "$out\$lib.def" -l "$out\$lib.lib"
    if ($LASTEXITCODE) { throw "Import library failed: $lib" }
}

foreach ($unit in @('wohd','render_patch','simulation_patch')) {
    & "$bin\clang.exe" --target=i686-w64-windows-gnu -isystem $headers -Os -fno-builtin -fno-stack-protector -Wall -Wextra -Wno-unused-parameter -c "$runtimeSource\$unit.c" -o "$out\$unit.obj"
    if ($LASTEXITCODE) { throw "Compile failed: $unit" }
}
& "$bin\lld-link.exe" /dll /machine:x86 /entry:DllMain@12 /nodefaultlib "/def:$runtimeSource\wohd.def" "/out:$out\wohd.dll" "$out\wohd.obj" "$out\render_patch.obj" "$out\simulation_patch.obj" "$out\kernel32.lib" "$out\winmm.lib" "$out\user32.lib"
if ($LASTEXITCODE) { throw 'Link failed' }
Copy-Item -LiteralPath "$out\wohd.dll" -Destination (Join-Path $PSScriptRoot '../runtime/wohr.dll') -Force
Write-Host 'Built portable x86 mod runtime.'
