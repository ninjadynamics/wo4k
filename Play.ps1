param([switch]$Diagnostics,[switch]$RefreshOnly,[switch]$NoErrorDialog)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'scripts/Diagnostics.ps1')
$log=Start-WoLog 'play' $PSScriptRoot
$game=Join-Path $PSScriptRoot 'wobble';$exe=Join-Path $game 'Wipeout2097.exe';$stage='Checking the installation';$child=$null
try {
    Write-WoLog 'stage' $stage
    $stage='Refreshing artwork and audio';Write-WoLog 'stage' $stage
    $refresh=& (Join-Path $PSScriptRoot 'scripts/Refresh-Assets.ps1') -GameRoot $game -PassThru
    Write-WoLog 'artwork' @{checked=$refresh.art_checked;ignored=@($refresh.art_ignored);files=@($refresh.art_files)}
    if($RefreshOnly){Write-WoLog 'complete' 'Assets refreshed without starting the game.';return}
    $stage='Checking game dependencies';Write-WoLog 'stage' $stage
    foreach($name in @('Wipeout2097.exe','wohr.dll','ddraw.dll','wo2097-controller.exe','libiconv-2.dll')){
        $path=Join-Path $game $name
        if(-not [IO.File]::Exists($path)){throw "Missing $name. Run Install / repair. Check Windows Security if a file was removed."}
        Write-WoLog 'dependency' @{file=$name;bytes=(Get-Item -LiteralPath $path).Length;sha256=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash;signature=[string](Get-AuthenticodeSignature -LiteralPath $path).Status}
    }
    $receipt=Get-Content -LiteralPath (Join-Path $game 'installation.json') -Encoding UTF8 -Raw|ConvertFrom-Json
    Write-WoLog 'installation' @{version=$receipt.version;hd_art=[bool]$receipt.hd_textures;improved_audio=[bool]$receipt.clean_audio}
    $env:WO2097_HD_ART=if($receipt.hd_textures){'1'}else{'0'}
    $env:WO2097_RENDER_TEST='1';$env:WO2097_DISTANCE_TEST='1';$env:WO2097_REFRESH_TEST='1';$env:WO2097_SIM60_TEST='0'
    $graphics=Join-Path $game 'DDrawCompat-Wipeout2097.ini'
    $config=if([IO.File]::Exists($graphics)){[IO.File]::ReadAllText($graphics)}else{''}
    if($config -match '(?im)^LogLevel\s*='){$config=[regex]::Replace($config,'(?im)^LogLevel\s*=.*$','LogLevel = info')}else{$config+="`r`nLogLevel = info`r`n"}
    [IO.File]::WriteAllText($graphics,$config,[Text.Encoding]::ASCII)
    Write-WoGameConfiguration $game
    $stage='Starting the game';Write-WoLog 'stage' $stage
    $watch=[Diagnostics.Stopwatch]::StartNew()
    $start=New-Object Diagnostics.ProcessStartInfo
    $start.FileName=$exe;$start.WorkingDirectory=$game;$start.UseShellExecute=$false
    $child=New-Object Diagnostics.Process;$child.StartInfo=$start
    $startedUtc=[DateTime]::UtcNow
    if(-not $child.Start()){throw 'Windows could not start the game.'}
    Write-WoLog 'game_started' @{pid=$child.Id;process_bits=32;message='Waiting for the game to exit.'}
    $sample=0
    while(-not $child.WaitForExit(2000)){
        Write-WoProcessSnapshot $child -Modules:($sample -eq 0 -or $sample%15 -eq 0)
        $sample++
    }
    $code=$child.ExitCode;$watch.Stop()
    Write-WoLog 'game_exit' @{code=$code;hex=('0x'+[BitConverter]::ToUInt32([BitConverter]::GetBytes([int]$code),0).ToString('X8'));seconds=[Math]::Round($watch.Elapsed.TotalSeconds,2)}
    if($null -eq $code){throw 'Windows did not return a game exit status.'}
    if($code -ne 0){Save-WoWindowsCrashEvent $child.Id $startedUtc;throw (Get-WoExitDescription $code)}
    Write-WoLog 'complete' 'The game exited normally.'
} catch {
    Save-WoGameLogs $game
    Complete-WoFailure $_ $stage -NoDialog:$NoErrorDialog
    if($RefreshOnly){throw}
    exit 1
} finally {
    Save-WoGameLogs $game
    if($child){$child.Dispose()}
}
