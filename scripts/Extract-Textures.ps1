param([Parameter(Mandatory=$true)][string]$SourcePath,[Parameter(Mandatory=$true)][string]$Destination,[switch]$NoPng)
. (Join-Path $PSScriptRoot 'Sources.ps1')
$source=[IO.Path]::GetFullPath($SourcePath);$records=New-Object Collections.Generic.List[object]
if(-not [IO.Directory]::Exists($source)){$source=Get-ModNativeRoot $source (Join-Path $script:ModRoot ('.work/texture-source-'+[guid]::NewGuid().ToString('N')))}
if([IO.Directory]::Exists((Join-Path $source 'textures/sd'))){Copy-ModTree (Join-Path $source 'textures/sd') $Destination}
foreach($dir in @('Pics','Blitsurf','Textures')) {
    $input=Join-Path $source $dir;if(-not [IO.Directory]::Exists($input)){$input=Join-Path $source ('textures/sd/'+$dir)}
    if(-not [IO.Directory]::Exists($input)){continue}
    $out=Get-ModChild $Destination $dir;Copy-ModTree $input $out
}
# Native track/ship texture containers stay with their game data; also expose
# them under SD for inspection/export without changing the game's WAD layout.
foreach($dir in Get-ChildItem -LiteralPath $source -Directory | Where-Object {$_.Name -match '^(?i)(Track\d{2}|Common)$'}) {
    foreach($file in Get-ChildItem -LiteralPath $dir.FullName -File | Where-Object {$_.Extension -in '.cmp','.tim','.tex'}) {
        $to=Get-ModChild $Destination ($dir.Name+'/'+$file.Name);[WoBeta.Files]::Parent($to);[IO.File]::Copy($file.FullName,$to,$true)
    }
}
foreach($file in Get-ChildItem -LiteralPath $Destination -File -Recurse) {
    if($file.FullName -like '*\exports\*'){continue}
    $rel=$file.FullName.Substring([IO.Path]::GetFullPath($Destination).TrimEnd('\').Length+1)
    $records.Add([pscustomobject]@{file=$rel;sha256=(Get-ModHash $file.FullName)})
    if($NoPng){continue}
    $png=Get-ModChild $Destination ('exports/'+$rel+'.png')
    switch($file.Extension.ToLowerInvariant()) {
        '.tga' {[WoBeta.Files]::Tga($file.FullName,$png)}
        '.tim' {[WoBeta.Files]::Tim([IO.File]::ReadAllBytes($file.FullName),$png)}
        '.cmp' {[WoBeta.Files]::Cmp($file.FullName,($png+'.parts'))}
    }
}
Write-ModJson (Get-ModChild $Destination 'sources.json') @($records.ToArray())
return [IO.Path]::GetFullPath($Destination)
