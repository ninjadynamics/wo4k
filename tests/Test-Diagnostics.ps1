param([string]$Compiler='')
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
. (Join-Path $root 'scripts/Diagnostics.ps1')
$work=Join-Path $root ('.work/diagnostics-'+[guid]::NewGuid().ToString('N'))
[IO.Directory]::CreateDirectory($work)|Out-Null
function Assert([bool]$Condition,[string]$Message){if(-not $Condition){throw $Message}}
try {
    # Prove queries select only support fields, even if the provider returns extras.
    function Get-CimInstance($ClassName,$Property,$OperationTimeoutSec,$ErrorAction){
        $allowed=@{Win32_OperatingSystem=@('Caption','Version','BuildNumber','OSArchitecture');Win32_Processor=@('Name','NumberOfCores','NumberOfLogicalProcessors');Win32_ComputerSystem=@('TotalPhysicalMemory');Win32_VideoController=@('Name','DriverVersion')}
        Assert ($allowed.ContainsKey($ClassName)) 'Unexpected hardware inventory query.'
        Assert (($Property -join ',') -eq ($allowed[$ClassName] -join ',')) 'Query includes identifying fields.'
        [pscustomobject]@{Caption='Windows 11';Version='10.0.26100';BuildNumber='26100';OSArchitecture='64-bit';Name='Test CPU or GPU';NumberOfCores=8;NumberOfLogicalProcessors=16;TotalPhysicalMemory=32GB;DriverVersion='1.2.3';SerialNumber='PRIVATE_SERIAL';UserName='PRIVATE_USER';UUID='PRIVATE_UUID';MACAddress='PRIVATE_MAC'}
    }
    $log=Start-WoLog 'test' $work
    Write-WoLog 'redaction' @{message=('File at '+$env:USERPROFILE+'\private\x and '+$work+'\file')}
    $entries=@(Get-Content -LiteralPath $log|ForEach-Object {ConvertFrom-Json $_})
    $spec=($entries|Where-Object event -eq system).data
    Assert ($spec.windows -eq 'Windows 11' -and $spec.ram_gib -eq 32 -and $spec.cpu[0].cores -eq 8 -and $spec.gpu[0].driver -eq '1.2.3') 'OS/CPU/RAM/GPU support fields missing.'
    $text=[IO.File]::ReadAllText($log)
    Assert ($text -notmatch 'PRIVATE_(SERIAL|USER|UUID|MAC)') 'Private inventory fields leaked.'
    Assert (-not $text.Contains($env:USERPROFILE.Replace('\','\\')) -and -not $text.Contains($work.Replace('\','\\'))) 'Personal paths were not redacted in JSON.'
    Assert ($text.Contains('%USERPROFILE%') -and $text.Contains('%MODROOT%')) 'Path placeholders missing.'
    function Show-WoErrorDialog([string]$Text){$script:dialogText=$Text}
    $oldMarker=$env:WO4K_ERROR_MARKER;$env:WO4K_ERROR_MARKER=Join-Path $work 'reported'
    Assert (Show-WoFailure 'Test failure') 'Popup was not shown.'
    Assert ($script:dialogText.Contains($log) -and $script:dialogText.Contains('Test failure') -and [IO.File]::Exists($env:WO4K_ERROR_MARKER)) 'Popup must identify its log; duplicate-error marker missing.'
    $env:WO4K_ERROR_MARKER=$oldMarker
    foreach($pair in @(@(-1073741515,'DLL'),@(-1073741819,'access violation'),@(-1073740760,'signature'))){Assert ((Get-WoExitDescription $pair[0]).Contains($pair[1])) 'Native failure hint missing.'}
    # Exercise the actual Play script with real child processes, without game data.
    $fixture=Join-Path $work 'mod Ω with spaces';$scripts=Join-Path $fixture 'scripts';$game=Join-Path $fixture 'wobble'
    [IO.Directory]::CreateDirectory($scripts)|Out-Null;[IO.Directory]::CreateDirectory($game)|Out-Null
    [IO.File]::Copy((Join-Path $root 'Play.ps1'),(Join-Path $fixture 'Play.ps1'))
    $helper=[IO.File]::ReadAllText((Join-Path $root 'scripts/Diagnostics.ps1'))+"`r`nfunction Get-WoSystemInfo {return @{windows='Windows 11';cpu='Fixture';ram_gib=32;gpu='Fixture'}}`r`n"
    [IO.File]::WriteAllText((Join-Path $scripts 'Diagnostics.ps1'),$helper,(New-Object Text.UTF8Encoding($true)))
    [IO.File]::WriteAllText((Join-Path $scripts 'Refresh-Assets.ps1'),'# Fixture assets are already current.')
    [IO.File]::WriteAllText((Join-Path $fixture 'VERSION'),'0.1.0-test')
    [IO.File]::WriteAllText((Join-Path $game 'installation.json'),' {"version":"test","hd_textures":true,"clean_audio":true}')
    foreach($name in @('wohr.dll','ddraw.dll','wo2097-controller.exe','libiconv-2.dll')){[IO.File]::WriteAllText((Join-Path $game $name),'dependency fixture')}
    if(-not $Compiler){$Compiler=(Get-Command gcc.exe -ErrorAction Stop).Source}
    $native=Join-Path $work 'exit.c';[IO.File]::WriteAllText($native,'#include <windows.h>'+"`n"+'int main(void){ExitProcess(EXIT_CODE);return 0;}')
    function Run-Play([int]$Expected){
        & powershell.exe -NoLogo -NoProfile -STA -ExecutionPolicy Bypass -File (Join-Path $fixture 'Play.ps1') -NoErrorDialog
        Assert ($LASTEXITCODE -eq $Expected) ('Unexpected Play exit status: '+$LASTEXITCODE)
        $latest=Get-ChildItem -LiteralPath (Join-Path $fixture 'logs') -Filter 'play-*.log'|Sort-Object LastWriteTimeUtc -Descending|Select-Object -First 1
        return @(Get-Content -LiteralPath $latest.FullName|ForEach-Object {ConvertFrom-Json $_})
    }
    foreach($case in @(@('0',0),@('0xC0000005u',1))){
        & $Compiler -s -static $native ('-DEXIT_CODE='+$case[0]) -o (Join-Path $work 'fixture.exe');Assert ($LASTEXITCODE -eq 0) 'Fixture compile failed.'
        [IO.File]::Copy((Join-Path $work 'fixture.exe'),(Join-Path $game 'Wipeout2097.exe'),$true)
        $events=Run-Play $case[1]
        Assert (@($events|Where-Object event -eq game_exit).Count -eq 1) 'Game exit code was not captured.'
        if($case[1]){Assert ((($events|Where-Object event -eq failure).data.message).Contains('0xC0000005')) 'Crash exit code was hidden.'}else{Assert (@($events|Where-Object event -eq failure).Count -eq 0) 'Normal exit incorrectly reported a failure.'}
    }
    [IO.File]::Delete((Join-Path $game 'wohr.dll'));$events=Run-Play 1
    Assert ((($events|Where-Object event -eq failure).data.message).Contains('wohr.dll')) 'Missing DLL failure was hidden.'
    [IO.File]::WriteAllText((Join-Path $scripts 'Refresh-Assets.ps1'),"throw 'Fixture refresh failure'");$events=Run-Play 1
    Assert ((($events|Where-Object event -eq failure).data.message).Contains('Fixture refresh failure')) 'Refresh failure was hidden.'
    [IO.File]::WriteAllText((Join-Path $scripts 'Refresh-Assets.ps1'),'# ready')
    [IO.File]::WriteAllText((Join-Path $game 'wohr.dll'),'fixture')
    [IO.File]::WriteAllText((Join-Path $game 'Wipeout2097.exe'),'invalid executable');$events=Run-Play 1
    Assert (@($events|Where-Object event -eq failure).Count -eq 1) 'Process creation failure was hidden.'
    # Run production native bootstrap with only MessageBox replaced by a recorder.
    $bootstrap=Join-Path $work 'bootstrap';[IO.Directory]::CreateDirectory($bootstrap)|Out-Null
    $driver=Join-Path $bootstrap 'driver.c';$launcher=(Join-Path $root 'src/launcher.c').Replace('\','/')
    $code="#include <windows.h>`nstatic int WINAPI capture(HWND w,LPCWSTR t,LPCWSTR c,UINT f){HANDLE h=CreateFileW(L`"popup.txt`",GENERIC_WRITE,0,0,CREATE_ALWAYS,0,0);DWORD n;WriteFile(h,t,(DWORD)lstrlenW(t)*2,&n,0);CloseHandle(h);return IDOK;}`n#define MessageBoxW capture`n#define WO_PLAY`n#include `"$launcher`""
    [IO.File]::WriteAllText($driver,$code)
    $launcherExe=Join-Path $bootstrap 'Play.exe'
    & $Compiler -s -static -municode -mwindows $driver -o $launcherExe;Assert ($LASTEXITCODE -eq 0) 'Native bootstrap fixture build failed.'
    $fixtureScript=Join-Path $bootstrap 'Play.ps1';[IO.File]::WriteAllText($fixtureScript,'param( this is broken')
    $process=Start-Process -FilePath $launcherExe -WorkingDirectory $bootstrap -PassThru;$null=$process.Handle;$process.WaitForExit()
    Assert ($process.ExitCode -ne 0) 'Bootstrap swallowed a parser failure.';$process.Dispose()
    $popup=[Text.Encoding]::Unicode.GetString([IO.File]::ReadAllBytes((Join-Path $bootstrap 'popup.txt')))
    Assert ($popup.Contains('Diagnostic log:') -and $popup.Contains('bootstrap-play-')) 'Bootstrap popup omits its log location.'
    $bootstrapLog=Get-ChildItem -LiteralPath (Join-Path $bootstrap 'logs') -Filter '*.log'|Select-Object -First 1
    $raw=[IO.File]::ReadAllText($bootstrapLog.FullName)
    Assert ($raw.Contains('powershell_exit') -and $raw.Contains('windows_build') -and $raw.Contains('ParserError')) 'Bootstrap does not capture early script failure and OS build.'
    Assert (@(Get-ChildItem -LiteralPath (Join-Path $bootstrap 'logs') -Filter '*.reported').Count -eq 0) 'Temporary error marker remains.'
    # A script-reported failure gets one dialog, not a second native one.
    [IO.File]::Delete((Join-Path $bootstrap 'popup.txt'))
    [IO.File]::WriteAllText($fixtureScript,'[IO.File]::WriteAllText($env:WO4K_ERROR_MARKER,"reported");exit 1')
    $process=Start-Process -FilePath $launcherExe -WorkingDirectory $bootstrap -PassThru;$null=$process.Handle;$process.WaitForExit();$process.Dispose()
    Assert (-not [IO.File]::Exists((Join-Path $bootstrap 'popup.txt'))) 'Native launcher repeated an already reported error.'
    # Setup must release its executable once the UI announces readiness.
    [IO.File]::WriteAllText($driver,$code.Replace('#define WO_PLAY',''))
    $setupExe=Join-Path $bootstrap 'Setup.exe'
    & $Compiler -s -static -municode -mwindows $driver -o $setupExe;Assert ($LASTEXITCODE -eq 0) 'Setup bootstrap fixture build failed.'
    [IO.File]::WriteAllText((Join-Path $bootstrap 'Setup.ps1'),'[IO.File]::WriteAllText($env:WO4K_READY_MARKER,"ready");Start-Sleep -Seconds 2')
    $watch=[Diagnostics.Stopwatch]::StartNew();$process=Start-Process -FilePath $setupExe -WorkingDirectory $bootstrap -PassThru;$null=$process.Handle;$process.WaitForExit();$watch.Stop()
    Assert ($process.ExitCode -eq 0 -and $watch.Elapsed.TotalSeconds -lt 2) 'Setup launcher remains locked while the UI runs.';$process.Dispose()
    $stream=[IO.File]::Open($setupExe,[IO.FileMode]::Open,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None);$stream.Dispose()
    Start-Sleep -Seconds 2
    Assert (@(Get-ChildItem -LiteralPath (Join-Path $bootstrap 'logs') -Filter '*.ready').Count -eq 0) 'Temporary readiness marker remains.'
    # Installer/updater must report failures that occur before their Core loads.
    foreach($operation in @('Install-Mod','Update-Mod')){
        [IO.File]::Copy((Join-Path $root ('scripts/'+$operation+'.ps1')),(Join-Path $scripts ($operation+'.ps1')))
        $status=Join-Path $fixture ($operation+'.json')
        $priorPreference=$ErrorActionPreference;$ErrorActionPreference='Continue'
        & powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $scripts ($operation+'.ps1')) -StatusFile $status -NoErrorDialog 2>$null
        $ErrorActionPreference=$priorPreference
        Assert ($LASTEXITCODE -ne 0 -and [IO.File]::Exists($status)) 'Worker startup error was not logged.'
        $state=Get-Content -LiteralPath $status -Encoding UTF8 -Raw|ConvertFrom-Json
        Assert ($state.state -eq 'error' -and [IO.File]::Exists($state.log_path)) 'Worker error omits a valid log location.'
    }
    Write-Host 'PASS: minimal hardware allowlist, path redaction, popup log path, crash/missing DLL/refresh/process/parser failures, real child exit codes and native bootstrap diagnostics.'
} finally {
    $env:WO4K_ERROR_MARKER=$oldMarker
    $full=[IO.Path]::GetFullPath($work);$prefix=[IO.Path]::GetFullPath((Join-Path $root '.work')).TrimEnd('\')+'\diagnostics-'
    if(-not $full.StartsWith($prefix,[StringComparison]::OrdinalIgnoreCase)){throw 'Unsafe diagnostics test cleanup.'}
    if([IO.Directory]::Exists($full)){[IO.Directory]::Delete($full,$true)}
}