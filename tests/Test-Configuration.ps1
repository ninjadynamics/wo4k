. (Join-Path $PSScriptRoot '../scripts/Cleanup-ManagedFiles.ps1')
$root=$script:ModRoot;$work=Join-Path $root ('.work/configuration-'+[guid]::NewGuid().ToString('N'))
function Assert($Condition,[string]$Message){if(-not $Condition){throw $Message}}
try{
    [IO.Directory]::CreateDirectory($work)|Out-Null
    $config=Get-ModConfiguration $work
    $config.GameSource=Join-Path $work 'source';$config.PsxMusicSource=Join-Path $work 'PS1 disc.iso'
    $config.DefaultSoundtrack='None';$config.HdTextures=$false;$config.CleanAudio=$false;$config.SoundMode=5;$config.FullscreenMode='exclusive'
    $saved=Save-ModConfiguration $config $work;$again=Save-ModConfiguration $config $work
    Assert ($saved.revision -eq 1 -and $again.revision -eq 1) 'Unchanged configuration incremented its revision.'
    $raw=Get-Content -LiteralPath (Join-Path $work '.state/setup.json') -Raw|ConvertFrom-Json
    Assert ($raw.GameSource -eq 'source' -and $raw.PsxMusicSource -eq 'PS1 disc.iso') 'Portable source paths were not retained.'
    Assert ($again.SoundMode -eq 5 -and $again.FullscreenMode -eq 'exclusive' -and -not $again.CleanAudio -and -not $again.HdTextures) 'Setup preferences were not restored.'
    $config.DefaultSoundtrack='Psx';$saved=Save-ModConfiguration $config $work
    Assert ($saved.revision -eq 2) 'Changed configuration was not detected.'
    $old=Join-Path $work 'scripts/retired/old.ps1';$custom=Join-Path $work 'assets/retired.png';$keep=Join-Path $work 'wobble/SAVEGAME.BIN'
    foreach($path in @($old,$custom,$keep)){[WoBeta.Files]::Parent($path);[IO.File]::WriteAllText($path,'release bytes')}
    Write-ModJson (Join-Path $work 'release-files.json') @{format=1;version='old';files=@(@{path='scripts/retired/old.ps1';sha256=(Get-ModHash $old)},@{path='assets/retired.png';sha256=(Get-ModHash $custom)})}
    Save-ModReleaseOwnership $work
    [IO.File]::WriteAllText($custom,'my edited artwork')
    Write-ModJson (Join-Path $work 'release-files.json') @{format=1;version='new';files=@()}
    $result=Remove-ModRetiredFiles $work
    Assert ($result.removed -eq 1 -and $result.custom_files_preserved -eq 1) 'Retired managed files were not cleaned selectively.'
    Assert (-not [IO.Directory]::Exists((Join-Path $work 'scripts/retired'))) 'An empty obsolete directory remained.'
    Assert ([IO.File]::ReadAllText($custom) -eq 'my edited artwork' -and [IO.File]::ReadAllText($keep) -eq 'release bytes') 'Cleanup changed custom artwork or saves.'
    Write-ModJson (Join-Path $work '.state/release-inventory.json') @{files=@(@{path='assets/../wobble/SAVEGAME.BIN';sha256=(Get-ModHash $keep)})}
    $rejected=$false;try{$null=Remove-ModRetiredFiles $work}catch{$rejected=$true}
    Assert ($rejected -and [IO.File]::Exists($keep)) 'Cleanup accepted a traversal path.'
    Write-Host 'PASS: persistent portable sources/preferences, change revisions, manual-update ownership cleanup, custom artwork/save preservation, empty-directory cleanup and traversal rejection.'
}finally{
    $full=[IO.Path]::GetFullPath($work);$prefix=[IO.Path]::GetFullPath((Join-Path $root '.work')).TrimEnd('\')+'\configuration-'
    if(-not $full.StartsWith($prefix,[StringComparison]::OrdinalIgnoreCase)){throw 'Unsafe configuration test cleanup.'}
    if([IO.Directory]::Exists($full)){[IO.Directory]::Delete($full,$true)}
}
