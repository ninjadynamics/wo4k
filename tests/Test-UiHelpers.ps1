. (Join-Path $PSScriptRoot '../scripts/Diagnostics.ps1')
. (Join-Path $PSScriptRoot '../scripts/Core.ps1')
. (Join-Path $PSScriptRoot '../scripts/Ui-Helpers.ps1')
Add-Type -AssemblyName PresentationFramework
$root=$script:ModRoot;$work=Join-Path $root ('.work/ui-helpers-'+[guid]::NewGuid().ToString('N'))
function Assert($Condition,[string]$Message){if(-not $Condition){throw $Message}}
try{
    [IO.Directory]::CreateDirectory($work)|Out-Null
    # Prove the displayed bitmap survives replacement of its source. A URI
    # BitmapFrame with default caching holds the read handle and fails here.
    $icon=Join-Path $work 'setup.ico';$source=Join-Path $root 'assets/icons/setup.ico'
    if(-not [IO.File]::Exists($source)){$source=Join-Path $root 'assets/icons/setup.png'}
    [IO.File]::Copy($source,$icon)
    $bitmap=Read-ModUiBitmap $icon
    $handle=[IO.File]::Open($icon,[IO.FileMode]::Open,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None);$handle.Dispose()
    [IO.File]::WriteAllBytes($icon,[byte[]](1,2,3))
    Assert ($bitmap.IsFrozen -and $bitmap.PixelWidth -gt 0) 'The loaded icon depends on its source remaining open.'
    $status=Join-Path $work 'status.json';Write-ModJson $status @{state='complete';message='Installation checked and ready.';percent=100}
    $scriptFile=Join-Path $work 'fast-worker.ps1';[IO.File]::WriteAllText($scriptFile,'exit 0')
    for($i=0;$i -lt 3;++$i){
        $process=Start-ModUiProcess -FilePath $script:ModPowerShell -ArgumentList @('-NoProfile','-ExecutionPolicy','Bypass','-File',('"'+$scriptFile+'"'))
        $process.WaitForExit();$result=Get-ModUiWorkerResult $process $status 'installation check'
        Assert ($result.success -and $result.exit_code -eq 0) 'A successful real worker was misreported as failing.';$process.Dispose()
    }
    # Reproduce the reported PowerShell 5.1 null-exit-code object explicitly.
    $unknown=[pscustomobject]@{ExitCode=$null};$unknown|Add-Member ScriptMethod WaitForExit {}
    $result=Get-ModUiWorkerResult $unknown $status 'installation check'
    Assert ($result.success -and $result.message -eq 'Installation checked and ready.') 'A completion receipt became a false failure.'
    $failed=[pscustomobject]@{ExitCode=9};$failed|Add-Member ScriptMethod WaitForExit {}
    Assert (-not (Get-ModUiWorkerResult $failed $status 'installation check').success) 'A real process failure was ignored.'
    Write-ModJson $status @{state='error';message='One or more track textures are missing or damaged. Run Install / repair.';log_path='fixture.log';error_shown=$true}
    $result=Get-ModUiWorkerResult $unknown $status 'installation check'
    Assert (-not $result.success -and $result.message -like 'One or more track textures*' -and $result.error_shown) 'The specific worker explanation or popup suppression was lost.'
    foreach($case in @(
        @('Exception calling "Open": file is being used by another process.','*file that is still open*'),
        @('Missing Windows game data: TRACK01/TRACK.WAD','One or more track files*'),
        @('Corrupt track texture NRACE01.TGA','One or more track or sprite textures*'),
        @('Missing sound effect: ENGINES.WAV','One or more sound effects*'),
        @('The configured Psx soundtrack needs its source.','The selected soundtrack is missing*'),
        @('Downloaded ZIP checksum failed.','The downloaded update is incomplete*'),
        @('The Windows game needs repair and its source is unavailable.','The original Windows game source*')
    )){Assert ((Get-WoFailureMessage $case[0] 'Installation failed') -like $case[1]) ('Unhelpful explanation for: '+$case[0])}
    $log=Start-WoLog 'ui-error-test' $work -SkipHardware
    $script:uiFailureStatusPath=$status
    function Show-WoErrorDialog([string]$Text){
        $script:dialogText=$Text
        $script:dialogStatus=Get-Content -LiteralPath $script:uiFailureStatusPath -Raw|ConvertFrom-Json
    }
    try{throw 'Corrupt track texture: textures/sd/Pics/Hi-res/NRACE01.TGA'}catch{Complete-WoFailure $_ 'Installation check failed' -StatusFile $status}
    Assert ($script:dialogStatus.state -eq 'error' -and $script:dialogStatus.message -like 'One or more track or sprite textures*') 'Setup received no explanation until after its worker dialog closed.'
    Assert ($script:dialogText -notmatch 'NRACE01|textures/sd|Exception calling' -and $script:dialogText -like '*One or more track or sprite textures*') 'The popup exposed internal error details instead of the affected category.'
    Assert ([IO.File]::ReadAllText($log).Contains('NRACE01.TGA')) 'Technical evidence was lost from the diagnostic log.'
    Write-Host 'PASS: displayed icon releases its file, real worker exit codes survive, completed null-exit-code workers succeed, real failures remain failures, and specific actionable explanations stay visible.'
}finally{
    $full=[IO.Path]::GetFullPath($work);$prefix=[IO.Path]::GetFullPath((Join-Path $root '.work')).TrimEnd('\')+'\ui-helpers-'
    if(-not $full.StartsWith($prefix,[StringComparison]::OrdinalIgnoreCase)){throw 'Unsafe UI helper test cleanup.'}
    if([IO.Directory]::Exists($full)){[IO.Directory]::Delete($full,$true)}
}
