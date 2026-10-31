. (Join-Path $PSScriptRoot 'Core.ps1')
$art=New-Object Collections.Generic.List[object]
foreach($file in Get-ChildItem -LiteralPath (Join-Path $script:ModRoot 'assets/hd') -File -Filter '*.png'){
    $image=New-Object Drawing.Bitmap($file.FullName)
    try{$art.Add([pscustomobject]@{file=('assets/hd/'+$file.Name);width=$image.Width;height=$image.Height;sha256=(Get-ModHash $file.FullName);license='MIT for mod contributions';provenance='Contributor-enhanced artwork based on user-supplied original WipEout artwork; original creators retain attribution.'})}finally{$image.Dispose()}
}
Write-ModJson (Join-Path $script:ModRoot 'manifests/artwork.json') @($art.ToArray())
$libraries=@(
    [ordered]@{name='DDrawCompat';version=(Get-Item -LiteralPath (Join-Path $script:ModRoot 'runtime/ddraw.dll')).VersionInfo.FileVersion;file='runtime/ddraw.dll';license='0BSD';source='https://github.com/narzoul/DDrawCompat';license_file='licenses/DDrawCompat.txt'},
    [ordered]@{name='SDL3 controller host';version='3.4.16';file='runtime/wo2097-controller.exe';license='zlib (SDL3), MIT (host)';source='https://github.com/libsdl-org/SDL';license_file='licenses/SDL3.txt'},
    [ordered]@{name='GNU libiconv';version='1.18';file='runtime/libiconv-2.dll';license='LGPL-2.1-or-later';source='https://ftp.gnu.org/pub/gnu/libiconv/libiconv-1.18.tar.gz';license_file='licenses/libiconv-LGPL.txt'}
)
foreach($entry in $libraries){$entry.sha256=Get-ModHash (Join-Path $script:ModRoot $entry.file)}
Write-ModJson (Join-Path $script:ModRoot 'manifests/third-party.json') $libraries
