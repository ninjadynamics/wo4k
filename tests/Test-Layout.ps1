. (Join-Path $PSScriptRoot '../scripts/Core.ps1')
$base=$script:ModRoot;$work=Get-ModChild $base ('.work/layout-'+[guid]::NewGuid().ToString('N'));$game=Join-Path $work 'wobble'
try{
    foreach($dir in @('scripts','assets/hd')){Copy-ModTree (Join-Path $base $dir) (Join-Path $work $dir)}
    $hd=Join-Path $game 'textures/hd';[IO.Directory]::CreateDirectory($hd)|Out-Null
    [IO.File]::Copy((Join-Path $base 'assets/hd/notice.png'),(Join-Path $hd 'notice.png'))
    [IO.File]::WriteAllText((Join-Path $hd 'title.png'),'custom image placeholder')
    foreach($file in @('notice.bmp','title.bmp','font-and-numbers.rgba','hud-gauges.rgba','install-state.json')){[IO.File]::WriteAllText((Join-Path $hd $file),'obsolete generated output')}
    [IO.File]::WriteAllText((Join-Path $hd 'my-custom-file.txt'),'keep me')
    [IO.File]::WriteAllText((Join-Path $game 'SAVEGAME.BIN'),'keep save')
    $result=& (Join-Path $work 'scripts/Repair-Layout.ps1') -GameRoot $game -PassThru
    if($result.removed -ne 7 -or $result.custom_artwork_preserved -ne 1){throw 'Legacy cleanup/migration failed.'}
    if(@(Get-ChildItem (Join-Path $work 'backups/artwork-migration') -File).Count -ne 1){throw 'Custom artwork was not preserved.'}
    if([IO.File]::ReadAllText((Join-Path $hd 'my-custom-file.txt')) -ne 'keep me' -or [IO.File]::ReadAllText((Join-Path $game 'SAVEGAME.BIN')) -ne 'keep save'){throw 'Custom files/save changed.'}
    $result=& (Join-Path $work 'scripts/Repair-Layout.ps1') -GameRoot $game -PassThru
    if($result.removed){throw 'Migration is not idempotent.'}
    & (Join-Path $work 'scripts/Install-Textures.ps1') -SourcePath (Join-Path $work 'assets/hd') -GameRoot $game
    if(@(Get-ChildItem $hd -File).Count -ne 1){throw 'Artwork copies were recreated.'}
    Write-Host 'PASS: old generated files removed, custom artwork/files and saves preserved; repeat migration and PNG validation create no duplicates.'
}finally{
    $full=[IO.Path]::GetFullPath($work);$allowed=[IO.Path]::GetFullPath((Join-Path $base '.work')).TrimEnd('\')+'\layout-'
    if(-not $full.StartsWith($allowed,[StringComparison]::OrdinalIgnoreCase)){throw 'Unsafe test cleanup.'}
    if([IO.Directory]::Exists($full)){[IO.Directory]::Delete($full,$true)}
}
