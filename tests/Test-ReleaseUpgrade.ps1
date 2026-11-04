param([Parameter(Mandatory=$true)][string]$Package,[Parameter(Mandatory=$true)][string]$GameSource,[string]$PublishedRepository)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if(-not $PublishedRepository){$PublishedRepository=$root}
$work=Join-Path $root ('.work/release-smoke-'+[guid]::NewGuid().ToString('N'))
$fixture=Join-Path $work 'installed mod';$stage=Join-Path $work 'update package'
Add-Type -AssemblyName System.IO.Compression.FileSystem
try{
    [IO.Compression.ZipFile]::ExtractToDirectory($Package,$fixture)
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $fixture 'scripts/Install-Mod.ps1') -GameSource $GameSource -DefaultSoundtrack None -NoErrorDialog
    if($LASTEXITCODE){throw 'Clean-room installation failed.'}
    $game=Join-Path $fixture 'wobble'
    [IO.File]::WriteAllText((Join-Path $game 'SAVEGAME.BIN'),'retained save fixture')
    $settings=[IO.File]::ReadAllText((Join-Path $game 'Wipeout2097.ini'))
    # Use the actual published beta.15 updater, not the newly edited implementation.
    foreach($file in @('Update-Mod.ps1','Core.ps1','Links.ps1','Install-Textures.ps1')){
        $lines=& git -C $PublishedRepository show ('v0.1.0-beta.15:scripts/'+$file)
        if($LASTEXITCODE){throw 'Cannot read published updater source.'}
        [IO.File]::WriteAllText((Join-Path $fixture ('scripts/'+$file)),($lines -join "`n"),(New-Object Text.UTF8Encoding($true)))
    }
    . (Join-Path $fixture 'scripts/Update-Mod.ps1') -FunctionsOnly
    $inventory=Read-ModUpdatePackage $Package $stage
    $null=Install-ModUpdate $fixture $stage $inventory $work
    if(-not [IO.File]::Exists((Join-Path $fixture 'scripts/Diagnostics.ps1'))){throw 'Update omitted diagnostics helper.'}
    if([IO.File]::ReadAllText((Join-Path $game 'SAVEGAME.BIN')) -ne 'retained save fixture' -or [IO.File]::ReadAllText((Join-Path $game 'Wipeout2097.ini')) -ne $settings){throw 'Update changed saves/settings.'}
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $fixture 'Play.ps1') -RefreshOnly -NoErrorDialog
    if($LASTEXITCODE){throw 'Updated Play refresh failed.'}
    if([IO.Directory]::Exists((Join-Path $game 'textures/hd'))){throw 'Updated Play left duplicate artwork.'}
    $logs=@(Get-ChildItem -LiteralPath (Join-Path $fixture 'logs') -Filter 'play-*.log')
    if(-not $logs.Count -or -not ([IO.File]::ReadAllText($logs[0].FullName)).Contains('Assets refreshed')){throw 'Updated Play did not produce diagnostics.'}
    Write-Host 'PASS: release ZIP installs from owned PC data; actual beta.15 updater accepts it, refreshes runtime/scripts, preserves saves/settings and produces logs on Play; legacy HD copies are cleaned.'
}finally{
    $full=[IO.Path]::GetFullPath($work);$prefix=[IO.Path]::GetFullPath((Join-Path $root '.work')).TrimEnd('\')+'\release-smoke-'
    if(-not $full.StartsWith($prefix,[StringComparison]::OrdinalIgnoreCase)){throw 'Unsafe smoke-test cleanup.'}
    if([IO.Directory]::Exists($full)){[IO.Directory]::Delete($full,$true)}
}