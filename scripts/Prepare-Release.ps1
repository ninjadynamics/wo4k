param([Parameter(Mandatory=$true)][string]$SourceGameRoot,[string]$DiscGameRoot)
. (Join-Path $PSScriptRoot 'Core.ps1')
$SourceGameRoot=[IO.Path]::GetFullPath($SourceGameRoot)
$target=Join-Path $SourceGameRoot 'Wipeout2_HighTest.exe';$output=Get-ModHash $target
$variants=New-Object Collections.Generic.List[object]
$sources=@((Join-Path $SourceGameRoot 'Wipeout2.exe'))
if($DiscGameRoot){$sources+=Join-Path $DiscGameRoot 'D3D.EXE'}
foreach($source in $sources){$hash=Get-ModHash $source;$relative='patches/exe-'+$hash.Substring(0,12)+'.wodiff';[WoBeta.Files]::Delta($source,$target,(Get-ModChild $script:ModRoot $relative))|Out-Null;$variants.Add([pscustomobject]@{source_sha256=$hash;source_size=(Get-Item -LiteralPath $source).Length;patch=$relative})}
$audio=New-Object Collections.Generic.List[object]
foreach($file in Get-ChildItem -LiteralPath (Join-Path $SourceGameRoot 'tmp/baselines/sound-original-22k') -File -Filter '*.wav') {
    $hashes=@((Get-ModHash $file.FullName));if($DiscGameRoot){$discSound=Join-Path $DiscGameRoot ('Sound/'+$file.Name);if([IO.File]::Exists($discSound)){$hashes+=Get-ModHash $discSound}}
    $audio.Add([pscustomobject]@{name=$file.Name;source_sha256=(Get-ModHash $file.FullName);source_hashes=@($hashes|Select-Object -Unique);output_sha256=(Get-ModHash (Join-Path $SourceGameRoot ('Sound/'+$file.Name)))})
}
Write-ModJson (Get-ModChild $script:ModRoot 'manifests/patches.json') ([ordered]@{format=1;output_sha256=$output;pc_mds_sha256='063523a8cd02a4189b7c87cb3c531513af1b13c6438cd335dcdbe58fe5c3f13a';executables=@($variants.ToArray());audio=@($audio.ToArray())})
[IO.File]::Copy((Join-Path $SourceGameRoot 'ddraw.dll'),(Join-Path $script:ModRoot 'runtime/ddraw.dll'),$true)

& (Join-Path $PSScriptRoot 'Export-PatchRecipes.ps1')
