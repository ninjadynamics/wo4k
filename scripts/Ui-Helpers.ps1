# WPF must finish reading an image before the updater replaces its file.
function Read-ModUiBitmap([string]$Path) {
    $stream=[IO.File]::Open($Path,[IO.FileMode]::Open,[IO.FileAccess]::Read,([IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete))
    try{
        $bitmap=[Windows.Media.Imaging.BitmapFrame]::Create($stream,[Windows.Media.Imaging.BitmapCreateOptions]::PreservePixelFormat,[Windows.Media.Imaging.BitmapCacheOption]::OnLoad)
        $bitmap.Freeze();return $bitmap
    }finally{$stream.Dispose()}
}
function Start-ModUiProcess([string]$FilePath,[string[]]$ArgumentList,[string]$ErrorLog) {
    $options=@{FilePath=$FilePath;ArgumentList=$ArgumentList;WindowStyle='Hidden';PassThru=$true}
    if($ErrorLog){$options.RedirectStandardError=$ErrorLog}
    $process=Start-Process @options
    # PowerShell 5.1 otherwise loses ExitCode when the first handle lookup is
    # after the background process has already exited.
    $null=$process.Handle
    return $process
}
function Start-ModSetupRestart([string]$Root,[int]$ParentProcessId,[switch]$NoErrorDialog) {
    $helper=Join-Path $Root 'scripts/Restart-Setup.ps1'
    if(-not [IO.File]::Exists($helper)){throw 'The Setup restart helper is missing.'}
    $arguments=@('-NoProfile','-ExecutionPolicy','Bypass','-File',('"'+$helper+'"'),'-ParentProcessId',[string]$ParentProcessId)
    if($NoErrorDialog){$arguments+='-NoErrorDialog'}
    return Start-ModUiProcess -FilePath $script:ModPowerShell -ArgumentList $arguments
}
function Get-ModUiWorkerResult($Process,[string]$StatusFile,[string]$Operation) {
    $Process.WaitForExit();$code=$Process.ExitCode;$status=$null
    if([IO.File]::Exists($StatusFile)){try{$status=Get-Content -LiteralPath $StatusFile -Raw -Encoding UTF8|ConvertFrom-Json}catch{}}
    $state=if($status -and $status.PSObject.Properties['state']){[string]$status.state}else{''}
    $success=$state -eq 'complete' -and ($null -eq $code -or $code -eq 0)
    $message=if($status -and $status.PSObject.Properties['message']){[string]$status.message}else{''}
    if(-not $success){
        $message=if($state -eq 'error' -and $message){Get-WoFailureMessage $message $Operation}else{'Setup could not finish the '+$Operation+'. Run Install / repair to check the installation again.'}
    }
    $log=if($status -and $status.PSObject.Properties['log_path']){[string]$status.log_path}else{''}
    $shown=$status -and $status.PSObject.Properties['error_shown'] -and [bool]$status.error_shown
    return [pscustomobject]@{success=$success;exit_code=$code;message=$message;log_path=$log;error_shown=[bool]$shown;state=$state}
}
