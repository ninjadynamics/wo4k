param([switch]$Diagnostics,[switch]$RefreshOnly)
$ErrorActionPreference='Stop'
$game=Join-Path $PSScriptRoot 'wobble';$exe=Join-Path $game 'Wipeout2097.exe'
if(-not (Test-Path -LiteralPath $exe)){Add-Type -AssemblyName PresentationFramework;[Windows.MessageBox]::Show('Run Setup first to supply your Windows game files.','WipEout 2097')|Out-Null;return}
[IO.Directory]::CreateDirectory((Join-Path $PSScriptRoot 'logs'))|Out-Null
try {
    & (Join-Path $PSScriptRoot 'scripts/Refresh-Assets.ps1') -GameRoot $game
} catch {
    if($RefreshOnly){throw}
    Add-Type -AssemblyName PresentationFramework
    [Windows.MessageBox]::Show(('Artwork/audio refresh could not finish. '+$_.Exception.Message),'WipEout 2097 / XL')|Out-Null
    return
}
if($RefreshOnly){return}
$env:WO2097_RENDER_TEST='1';$env:WO2097_DISTANCE_TEST='1';$env:WO2097_REFRESH_TEST='1';$env:WO2097_SIM60_TEST='0'
$child=Start-Process -FilePath $exe -WorkingDirectory $game -PassThru
$child.WaitForExit()
if(Test-Path -LiteralPath (Join-Path $game 'Debug.txt')){Move-Item -LiteralPath (Join-Path $game 'Debug.txt') -Destination (Join-Path $PSScriptRoot 'logs/game-debug.txt') -Force}
