param([string]$Version='')
. (Join-Path $PSScriptRoot 'Core.ps1')
if(-not $Version){$Version=[IO.File]::ReadAllText((Join-Path $script:ModRoot 'VERSION')).Trim()}
$entries=New-Object Collections.Generic.List[object]
$top=@('Setup.ps1','Play.ps1','Setup.exe','Play.exe','README.md','LICENSE','.gitignore','release.url','source.url','Makefile','VERSION','compile.ps1','HOW-TO-COMPILE.md')
$folders=@('scripts','src','runtime','assets','manifests','licenses','docs','tests')
$files=@();foreach($name in $top){$files+=Get-Item -LiteralPath (Join-Path $script:ModRoot $name)}
foreach($dir in $folders){$files+=Get-ChildItem -LiteralPath (Join-Path $script:ModRoot $dir) -File -Recurse | Where-Object {$_.FullName -notmatch '\\src\\runtime\\build\\' -or $_.Name -in 'render_hooks.h','render_refs.h'}}
# Include only explicitly generated binary differences, never original sources.
$files+=Get-ChildItem -LiteralPath (Join-Path $script:ModRoot 'patches') -File -Filter '*.wodiff'
foreach($file in $files){
    $rel=$file.FullName.Substring($script:ModRoot.Length+1).Replace('\','/')
    if($file.Extension -in '.mpg','.mpeg','.mp4','.avi','.wmv','.wav','.mp3','.iso','.mdf','.mds','.bin','.tga','.tim','.cmp','.ppm','.wad','.swp','.dat','.py','.cmd','.bat','.obj','.lib'){throw "Forbidden release payload: $rel"}
    if($file.Extension -eq '.exe' -and $rel -notin 'Setup.exe','Play.exe','runtime/wo2097-controller.exe','runtime/audio-clean.exe'){throw "Unexpected executable: $rel"}
    if($file.Extension -eq '.dll' -and $rel -notin 'runtime/wohr.dll','runtime/ddraw.dll','runtime/libiconv-2.dll'){throw "Unexpected library: $rel"}
    $entries.Add([pscustomobject]@{path=$rel;bytes=$file.Length;sha256=(Get-ModHash $file.FullName)})
}
$dist=Join-Path $script:ModRoot 'dist';[IO.Directory]::CreateDirectory($dist)|Out-Null
$inventory=Join-Path $dist 'release-files.json';Write-ModJson $inventory ([ordered]@{version=$Version;files=@($entries.ToArray())})
$name='wo4k-'+$Version+'.zip';$zipPath=Join-Path $dist $name
if([IO.File]::Exists($zipPath)){[IO.File]::Delete($zipPath)}
$archive=[IO.Compression.ZipFile]::Open($zipPath,[IO.Compression.ZipArchiveMode]::Create)
try{foreach($entry in $entries){[IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive,(Get-ModChild $script:ModRoot $entry.path),$entry.path,[IO.Compression.CompressionLevel]::Optimal)|Out-Null};[IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive,$inventory,'release-files.json',[IO.Compression.CompressionLevel]::Optimal)|Out-Null}finally{$archive.Dispose()}
$hash=Get-ModHash $zipPath;[IO.File]::WriteAllText(($zipPath+'.sha256'),($hash+'  '+$name+"`r`n"),[Text.Encoding]::ASCII)
Write-Host "Packed $($entries.Count) allowlisted files: $zipPath"
Write-Host "SHA256 $hash"
