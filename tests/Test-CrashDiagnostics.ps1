param([string]$CompilerDirectory='C:/mingw64/bin')
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$out=Join-Path $root '.work/test-crash-diagnostics'
[IO.Directory]::CreateDirectory($out)|Out-Null
$definition=[IO.File]::ReadAllText((Join-Path $root 'src/runtime/build/kernel32.def'))+"`r`nExitProcess@4`r`nSetErrorMode@4`r`nRaiseException@16`r`n"
[IO.File]::WriteAllText((Join-Path $out 'kernel32.def'),$definition,[Text.Encoding]::ASCII)
& "$CompilerDirectory/llvm-dlltool.exe" -m i386 -k -d "$out/kernel32.def" -l "$out/kernel32.lib"
if($LASTEXITCODE){throw 'Crash fixture imports failed.'}
& "$CompilerDirectory/clang.exe" --target=i686-w64-windows-gnu -isystem "$CompilerDirectory/../x86_64-w64-mingw32/include" -Os -fno-builtin -fno-stack-protector -Wall -Wextra -c "$PSScriptRoot/test_crash_diagnostics.c" -o "$out/test.obj"
if($LASTEXITCODE){throw 'Crash fixture compile failed.'}
& "$CompilerDirectory/lld-link.exe" /machine:x86 /subsystem:console /entry:test_entry@0 /nodefaultlib "/out:$out/test.exe" "$out/test.obj" "$out/kernel32.lib"
if($LASTEXITCODE){throw 'Crash fixture link failed.'}
$oldLog=$env:WO_CRASH_TEST_LOG;$oldMode=$env:WO_CRASH_TEST_MODE
try{
    foreach($mode in @('handled','fatal')){
        $env:WO_CRASH_TEST_LOG=Join-Path $out ($mode+'.log');$env:WO_CRASH_TEST_MODE=$mode
        $start=New-Object Diagnostics.ProcessStartInfo
        $start.FileName=Join-Path $out 'test.exe';$start.WorkingDirectory=$out;$start.UseShellExecute=$false;$start.CreateNoWindow=$true
        $process=New-Object Diagnostics.Process;$process.StartInfo=$start
        try{
            if(-not $process.Start()){throw 'Crash fixture did not start.'}
            if(-not $process.WaitForExit(10000)){$process.Kill();$process.WaitForExit();throw 'Crash fixture timed out.'}
            $code=$process.ExitCode
        }finally{$process.Dispose()}
        $text=[IO.File]::ReadAllText($env:WO_CRASH_TEST_LOG)
        $version=[IO.File]::ReadAllText((Join-Path $root 'VERSION')).Trim()
        if(-not $text.Contains('mod_version '+$version)){throw 'Native exception log omits the compiled mod version.'}
        if($mode -eq 'handled'){
            if($code -ne 0 -or $text -notmatch 'handled_execution_continued' -or $text -match 'exception_unhandled'){throw 'Observer interfered with handled exceptions.'}
            if([regex]::Matches($text,'exception_first_chance').Count -ne 16){throw 'First-chance logging is not bounded.'}
        }else{
            if($code -ne -1073741819 -or $text -notmatch 'exception_unhandled 0xC0000005' -or $text -notmatch 'previous_filter_called'){throw 'Actual crash code/filter chaining was not preserved.'}
            if($text -notmatch 'fault_instruction 0x[0-9A-F]+ test.exe\+0x' -or $text -notmatch 'access_address 0x00000001' -or $text -notmatch 'access_kind_read0_write1_execute8 0x00000001'){throw 'Fault module/RVA and write address missing.'}
            if($text -notmatch 'process_virtual_available_bytes' -or $text -notmatch 'physical_available_bytes' -or $text -notmatch 'stack_code_candidate'){throw 'Memory or code-address breadcrumbs missing.'}
        }
        if($text.Contains($out) -or $text.Contains($env:USERPROFILE)){throw 'Native crash report leaked paths.'}
    }
    Write-Host 'PASS: actual x86 access violation records module/RVA, access type/address, registers, code candidates and available RAM/address space; original exit status/filter retained. Twenty handled exceptions continue normally; capture is bounded.'
}finally{$env:WO_CRASH_TEST_LOG=$oldLog;$env:WO_CRASH_TEST_MODE=$oldMode}
