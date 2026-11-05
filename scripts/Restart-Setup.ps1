param([Parameter(Mandatory=$true)][int]$ParentProcessId,[switch]$NoErrorDialog)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'Diagnostics.ps1')
$restartRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$null=Start-WoLog 'setup-restart' $restartRoot -SkipHardware
try{
    if($ParentProcessId -eq $PID){throw 'Setup restart cannot wait for itself.'}
    $parent=Get-Process -Id $ParentProcessId -ErrorAction SilentlyContinue
    if($parent){
        Write-WoLog 'waiting' 'Waiting for the previous Setup process to exit.'
        try{if(-not $parent.WaitForExit(30000)){throw 'The previous Setup process did not exit.'}}
        finally{$parent.Dispose()}
    }
    $launcher=Join-Path $restartRoot 'Setup.exe'
    if(-not [IO.File]::Exists($launcher)){throw 'The updated Setup.exe is missing.'}
    # Relaunch the interactive app only after its old PowerShell UI is gone.
    Start-Process -FilePath $launcher -WorkingDirectory $restartRoot|Out-Null
    Write-WoLog 'complete' 'The updated Setup was started.'
}catch{Complete-WoFailure $_ 'Restarting Setup after update' -NoDialog:$NoErrorDialog;exit 1}
