. (Join-Path $PSScriptRoot 'Disc-Audio.ps1')
if(-not ('WoBeta.TextureSources' -as [type])){Add-Type -Path (Join-Path $PSScriptRoot 'TextureSources.cs')}

function Find-ModPs1Disc([string]$Source) {
    $descriptors=if([IO.Directory]::Exists($Source)){
        @(Get-ChildItem -LiteralPath $Source -File -Recurse | Where-Object {$_.Extension -in '.cue','.mds','.iso','.bin'} | Sort-Object @{Expression={if($_.Extension -in '.cue','.mds'){0}else{1}}} | Select-Object -ExpandProperty FullName)
    }else{@($Source)}
    foreach($descriptor in $descriptors){
        $image=$descriptor;[long]$offset=0
        if([IO.Path]::GetExtension($descriptor) -in '.cue','.mds','.mdf' -or ([IO.Path]::GetExtension($descriptor) -eq '.bin' -and [IO.File]::Exists([IO.Path]::ChangeExtension($descriptor,'.cue')))){
            $tracks=@(Get-ModDiscTracks $descriptor);$data=@($tracks|Where-Object {-not $_.Audio})
            if(-not $data.Count){continue};$image=$data[0].Image;$offset=$data[0].Offset
        }
        $disc=$null
        try{
            $disc=New-Object WoBeta.Disc($image,$offset)
            $boot=@($disc.Entries | Where-Object {-not $_.Directory -and $_.Path -ieq 'SYSTEM.CNF'})
            if(-not $boot.Count){continue}
            $text=[Text.Encoding]::ASCII.GetString($disc.Read($boot[0].Lba,$boot[0].Length))
            if($text -notmatch '(?i)SLES[_-]003\.27'){continue}
            if(-not @($disc.Entries|Where-Object {$_.Path -ieq 'WIPEOUT2/TEXTURES/EXPLODE.CMP'}).Count){continue}
            return [pscustomobject]@{Image=$image;Offset=$offset;Edition='WipEout 2097 Europe SLES-00327'}
        }catch{Write-Verbose $_.Exception.Message}finally{if($disc){$disc.Dispose()}}
    }
    return $null
}

function Import-ModPs1Textures([string]$Source,[string]$Destination,[string]$WorkRoot,$Plan) {
    $input=Resolve-ModInput $Source $WorkRoot
    $found=Find-ModPs1Disc $input
    if(-not $found){
        if(Find-ModMusicFolder $input Psx){return $false}
        if([IO.File]::Exists($input) -and [IO.Path]::GetExtension($input) -in '.cue','.mds','.mdf'){
            if(@(Get-ModDiscTracks $input | Where-Object Audio).Count -eq 11){return $false}
        }
        throw 'No supported PlayStation texture data found. Use the European WipEout 2097 disc (SLES-00327); an extracted music folder supplies music only.'
    }
    $reuse=@{};foreach($archive in $Plan.archives){if($archive.selection -eq 'pc_byte_identical'){$reuse[$archive.file]=$true}}
    $disc=New-Object WoBeta.Disc($found.Image,$found.Offset);$count=0
    try{
        foreach($entry in $disc.Entries){
            if($entry.Directory -or $entry.Path -notmatch '^(?i)WIPEOUT2/((COMMON|TEXTURES|TRACK\d{2})/.+\.(CMP|TIM|TTF|TEX))$'){continue}
            $relative=$Matches[1]
            if($reuse.ContainsKey($relative)){continue}
            $disc.Extract($entry,(Get-ModChild $Destination $relative));++$count
        }
    }finally{$disc.Dispose()}
    Write-ModJson (Get-ModChild $Destination 'source.json') @{edition=$found.Edition;files=$count;configured_source=[IO.Path]::GetFullPath($Source);selection='Fixed project-time map; identical PC originals reused'}
    return $true
}

function Test-ModNativeTextures([string]$GameRoot,$Plan,[switch]$RequirePs1){
    $path=Join-Path $GameRoot 'textures/selection.json'
    if(-not [IO.File]::Exists($path)){return $false}
    try{$state=Get-Content -LiteralPath $path -Raw -Encoding UTF8|ConvertFrom-Json}catch{return $false}
    if(-not $state.PSObject.Properties['files'] -or $state.revision -ne $Plan.revision -or ($RequirePs1 -and -not $state.ps1_textures)){return $false}
    foreach($group in @($Plan.operations|Group-Object atlas)){
        $expected=@($state.files|Where-Object file -eq $group.Name)
        if($expected.Count -ne 1){return $false}
        $file=Get-ModChild (Join-Path $GameRoot 'textures/sd') $group.Name
        if(-not [IO.File]::Exists($file) -or (Get-ModHash $file) -ne $expected[0].sha256){return $false}
    }
    return $true
}
function Install-ModNativeTextures([string]$GameRoot,[string]$PsxSource,[string]$WorkRoot,[string]$RetainFrom,[switch]$Incremental,[switch]$RequirePs1) {
    $GameRoot=[IO.Path]::GetFullPath($GameRoot)
    $plan=Get-Content -LiteralPath (Join-Path $script:ModRoot 'manifests/texture-selection.json') -Raw -Encoding UTF8 | ConvertFrom-Json
    if($plan.format -ne 1){throw 'Unsupported native texture selection map.'}
    $ps1=Join-Path $GameRoot 'textures/ps1-source';$original=Join-Path $GameRoot 'textures/native-source'
    $receipt=Join-Path $GameRoot 'textures/selection.json'
    if($Incremental -and -not $PsxSource -and -not $RetainFrom -and [IO.File]::Exists($receipt)){
        if(Test-ModNativeTextures $GameRoot $plan -RequirePs1:$RequirePs1){return Get-Content -LiteralPath $receipt -Raw -Encoding UTF8|ConvertFrom-Json}
    }
    # Setup staging and repair retain both imported disc assets and pristine
    # packed sheets. A later launch never patches an already-patched sheet.
    if($RetainFrom){foreach($relative in @('textures/ps1-source','textures/native-source')){
        $old=Join-Path $RetainFrom $relative
        if([IO.Directory]::Exists($old)){Copy-ModTree $old (Get-ModChild $GameRoot $relative)}
    }}
    if($PsxSource){$null=Import-ModPs1Textures $PsxSource $ps1 $WorkRoot $plan}
    if($RequirePs1 -and -not [IO.File]::Exists((Join-Path $ps1 'source.json'))){throw 'PlayStation artwork needs its configured disc source. Open Setup to restore its path.'}
    $decoder=New-Object WoBeta.TextureSources($GameRoot,$ps1)
    $pc=0;$ps=0;$pending=0;$sheets=0;$outputs=New-Object Collections.Generic.List[object]
    foreach($group in @($plan.operations | Group-Object atlas)){
        $target=Get-ModChild (Join-Path $GameRoot 'textures/sd') $group.Name
        if(-not [IO.File]::Exists($target)){continue}
        $pristine=Get-ModChild $original $group.Name
        if(-not [IO.File]::Exists($pristine)){[WoBeta.Files]::Parent($pristine);[IO.File]::Copy($target,$pristine)}
        $bytes=[IO.File]::ReadAllBytes($pristine)
        foreach($op in $group.Group){
            $src=$op.source
            $isPs1=[bool]$src.PSObject.Properties['ps1']
            if($isPs1 -and -not [IO.File]::Exists((Get-ModChild $ps1 $src.ps1))){++$pending;continue}
            if($src.PSObject.Properties['material']){
                $pixels=$decoder.Road($src.wad,$src.entry,$src.table,[int]$src.material,[int]$src.level)
            }else{
                $path=if($isPs1){$src.ps1}elseif($src.PSObject.Properties['path']){$src.path}else{''}
                $wad=if($src.PSObject.Properties['wad']){$src.wad}else{''}
                $entry=if($src.PSObject.Properties['entry']){$src.entry}else{''}
                $pixels=$decoder.Frame($path,$wad,$entry,$isPs1,[int]$src.frame)
            }
            [WoBeta.TextureSources]::PatchTga($bytes,$pixels,[int]$op.x,[int]$op.y,[int]$op.width,[int]$op.height)
            if($isPs1){++$ps}else{++$pc}
        }
        # Build the whole sheet before replacing it. No partially decoded image
        # can become visible if a source/container is malformed.
        $temporary=$target+'.wo4k-new';[IO.File]::WriteAllBytes($temporary,$bytes)
        try{[WoBeta.TextureSources]::Commit($temporary,$target)}finally{if([IO.File]::Exists($temporary)){[IO.File]::Delete($temporary)}}
        ++$sheets
        $outputs.Add([pscustomobject]@{file=$group.Name;sha256=(Get-ModHash $target)})
    }
    if($RequirePs1 -and $pending){throw 'The imported PlayStation texture cache is incomplete. Restore the configured disc source in Setup.'}
    $result=[pscustomobject]@{format=2;revision=$plan.revision;selection='ps1-preferred';pc_regions=$pc;ps1_regions=$ps;pending_ps1_regions=$pending;sheets=$sheets;ps1_textures=[IO.File]::Exists((Join-Path $ps1 'source.json'));files=@($outputs.ToArray())}
    Write-ModJson (Join-Path $GameRoot 'textures/selection.json') $result
    return $result
}
