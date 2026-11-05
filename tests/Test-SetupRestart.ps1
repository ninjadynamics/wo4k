param([string]$Compiler='')
. (Join-Path $PSScriptRoot '../scripts/Core.ps1')
. (Join-Path $PSScriptRoot '../scripts/Ui-Helpers.ps1')
$root=$script:ModRoot;$work=Join-Path $root ('.work/setup-restart-'+[guid]::NewGuid().ToString('N'))
$fixture=Join-Path $work 'updated setup';$parent=$null;$restart=$null;$release=$null
function Assert($Condition,[string]$Message){if(-not $Condition){throw $Message}}
try{
    [IO.Directory]::CreateDirectory((Join-Path $fixture 'scripts'))|Out-Null
    foreach($name in @('Restart-Setup.ps1','Diagnostics.ps1')){[IO.File]::Copy((Join-Path $root ('scripts/'+$name)),(Join-Path $fixture ('scripts/'+$name)))}
    # A harmless GUI-subsystem executable records its launch, checking that
    # the old parent really exited first. No window or game is opened.
    $source=Join-Path $work 'restart-recorder.c'
    [IO.File]::WriteAllText($source,@'
#include <windows.h>
#include <stdlib.h>
int WINAPI WinMain(HINSTANCE a,HINSTANCE b,LPSTR c,int d){
 char pid[32]={0};DWORD n;HANDLE f=CreateFileW(L"parent.txt",GENERIC_READ,FILE_SHARE_READ,0,OPEN_EXISTING,0,0);
 if(f==INVALID_HANDLE_VALUE)return 2;ReadFile(f,pid,31,&n,0);CloseHandle(f);
 HANDLE parent=OpenProcess(SYNCHRONIZE,FALSE,(DWORD)atoi(pid));
 if(parent){DWORD state=WaitForSingleObject(parent,0);CloseHandle(parent);if(state!=WAIT_OBJECT_0)return 3;}
 f=CreateFileW(L"restarted.txt",GENERIC_WRITE,0,0,CREATE_ALWAYS,0,0);if(f==INVALID_HANDLE_VALUE)return 4;
 WriteFile(f,"updated setup started after parent exit",39,&n,0);CloseHandle(f);return 0;
}
'@,[Text.Encoding]::ASCII)
    if(-not $Compiler){$Compiler=(Get-Command gcc.exe -ErrorAction Stop).Source}
    & $Compiler -O2 -s -static -mwindows $source -o (Join-Path $fixture 'Setup.exe')
    Assert ($LASTEXITCODE -eq 0) 'Restart recorder compilation failed.'
    $parentScript=Join-Path $work 'old-ui.ps1';$ready=Join-Path $work 'parent-ready';$release=Join-Path $work 'release-parent'
    [IO.File]::WriteAllText($parentScript,'param([string]$Ready,[string]$Release);[IO.File]::WriteAllText($Ready,"ready");$until=[DateTime]::UtcNow.AddSeconds(20);while(-not [IO.File]::Exists($Release)){if([DateTime]::UtcNow -gt $until){exit 8};Start-Sleep -Milliseconds 20}')
    $parent=Start-ModUiProcess -FilePath $script:ModPowerShell -ArgumentList @('-NoProfile','-ExecutionPolicy','Bypass','-File',('"'+$parentScript+'"'),'-Ready',('"'+$ready+'"'),'-Release',('"'+$release+'"'))
    $until=[DateTime]::UtcNow.AddSeconds(5)
    while(-not [IO.File]::Exists($ready) -and [DateTime]::UtcNow -lt $until){Start-Sleep -Milliseconds 20}
    Assert ([IO.File]::Exists($ready)) 'Parent fixture did not start.'
    [IO.File]::WriteAllText((Join-Path $fixture 'parent.txt'),[string]$parent.Id)
    $restart=Start-ModSetupRestart $fixture $parent.Id -NoErrorDialog
    $until=[DateTime]::UtcNow.AddSeconds(5);$waiting=$false
    while(-not $waiting -and [DateTime]::UtcNow -lt $until){
        foreach($log in @(Get-ChildItem -LiteralPath (Join-Path $fixture 'logs') -Filter '*.log' -ErrorAction SilentlyContinue)){
            if([IO.File]::ReadAllText($log.FullName).Contains('"waiting"')){$waiting=$true}
        }
        if(-not $waiting){Start-Sleep -Milliseconds 20}
    }
    Assert ($waiting -and -not $parent.HasExited -and -not [IO.File]::Exists((Join-Path $fixture 'restarted.txt'))) 'Restart did not wait for the existing UI to exit.'
    [IO.File]::WriteAllText($release,'exit');$parent.WaitForExit()
    Assert ($restart.WaitForExit(10000) -and $restart.ExitCode -eq 0) 'Restart helper did not finish successfully.'
    $until=[DateTime]::UtcNow.AddSeconds(5)
    while(-not [IO.File]::Exists((Join-Path $fixture 'restarted.txt')) -and [DateTime]::UtcNow -lt $until){Start-Sleep -Milliseconds 20}
    Assert ([IO.File]::Exists((Join-Path $fixture 'restarted.txt'))) 'The updated Setup executable was not launched after the old UI exited.'
    Write-Host 'PASS: the production restart helper waits for the previous process, then starts the updated executable in the correct folder; no window or game launched.'
}finally{
    if($release){[IO.File]::WriteAllText($release,'exit')}
    foreach($process in @($parent,$restart)){if($process){$null=$process.WaitForExit(5000);$process.Dispose()}}
    $full=[IO.Path]::GetFullPath($work);$prefix=[IO.Path]::GetFullPath((Join-Path $root '.work')).TrimEnd('\')+'\setup-restart-'
    if(-not $full.StartsWith($prefix,[StringComparison]::OrdinalIgnoreCase)){throw 'Unsafe Setup restart test cleanup.'}
    if([IO.Directory]::Exists($full)){[IO.Directory]::Delete($full,$true)}
}
