. (Join-Path $PSScriptRoot '../scripts/Core.ps1')
$base=$script:ModRoot;$work=Get-ModChild $base ('.work/layout-'+[guid]::NewGuid().ToString('N'));$game=Join-Path $work 'wobble'
try{
    foreach($dir in @('scripts','assets/hd','manifests')){Copy-ModTree (Join-Path $base $dir) (Join-Path $work $dir)}
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
    $artRoot=Join-Path $work 'assets/hd';$optional=Join-Path $artRoot 'hud-reticles.png'
    $reticleSource=if([IO.File]::Exists($optional)){$optional}else{Join-Path $artRoot 'hud-gauges.png'}
    [IO.File]::Copy($reticleSource,(Join-Path $artRoot 'unknown-preview.png'))
    $failed=$false;try{& (Join-Path $work 'scripts/Install-Textures.ps1') -SourcePath $artRoot -GameRoot $game -PassThru}catch{$failed=$_.Exception.Message -like '*Cannot map*'}
    if(-not $failed){throw 'Unmapped PNG should explain its filename instead of being silently ignored.'}
    [IO.File]::Delete((Join-Path $artRoot 'unknown-preview.png'))
    $expectedArt=@(Get-ChildItem -LiteralPath $artRoot -File -Filter '*.png').Count
    $result=& (Join-Path $work 'scripts/Install-Textures.ps1') -SourcePath $artRoot -GameRoot $game -PassThru
    if([IO.File]::Exists($optional)){
        if($result.checked -ne $expectedArt){throw 'Optional reticle PNG was not validated.'}
        $saved=$optional+'.saved';[IO.File]::Move($optional,$saved)
        try{
            $result=& (Join-Path $work 'scripts/Install-Textures.ps1') -SourcePath $artRoot -GameRoot $game -PassThru
            if($result.checked -ne ($expectedArt-1)){throw 'Missing optional reticles must retain SD fallback.'}
            [IO.File]::WriteAllText($optional,'invalid optional PNG')
            $failed=$false;try{& (Join-Path $work 'scripts/Install-Textures.ps1') -SourcePath $artRoot -GameRoot $game}catch{$failed=$true}
            if(-not $failed){throw 'Invalid reticle PNG was accepted.'}
        }finally{if([IO.File]::Exists($optional)){[IO.File]::Delete($optional)};[IO.File]::Move($saved,$optional)}
    }
    if(@(Get-ChildItem $hd -File).Count -ne 1){throw 'Artwork copies were recreated.'}
    Write-Host 'PASS: old generated files removed, custom artwork/files and saves preserved; optional reticles validate and retain SD fallback when absent; unknown PNG names are reported; PNG validation creates no duplicates.'
}finally{
    $full=[IO.Path]::GetFullPath($work);$allowed=[IO.Path]::GetFullPath((Join-Path $base '.work')).TrimEnd('\')+'\layout-'
    if(-not $full.StartsWith($allowed,[StringComparison]::OrdinalIgnoreCase)){throw 'Unsafe test cleanup.'}
    if([IO.Directory]::Exists($full)){[IO.Directory]::Delete($full,$true)}
}
