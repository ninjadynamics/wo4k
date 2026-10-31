param()
. (Join-Path $PSScriptRoot 'Core.ps1')
$lock=Get-Content -LiteralPath (Join-Path $script:ModRoot 'manifests/dependencies.json') -Encoding UTF8 -Raw|ConvertFrom-Json
$work=Join-Path $script:ModRoot '.work/deps';$archives=Join-Path $work 'archives';[IO.Directory]::CreateDirectory($archives)|Out-Null
$savedPath=$env:PATH
try{foreach($entry in $lock){
    $marker=Join-Path $work $entry.marker
    if([IO.File]::Exists($marker) -and (Get-ModHash $marker) -eq $entry.marker_sha256){continue}
    $archive=Join-Path $archives $entry.archive
    if(-not [IO.File]::Exists($archive) -or (Get-ModHash $archive) -ne $entry.sha256){
        & (Join-Path ([Environment]::SystemDirectory) 'curl.exe') -q --fail --location --show-error --proto '=https' --proto-redir '=https' --connect-timeout 20 --max-time 240 --output $archive $entry.url
        if($LASTEXITCODE){throw "Dependency download failed: $($entry.name)"}
    }
    if((Get-ModHash $archive) -ne $entry.sha256){throw "Dependency checksum failed: $($entry.name)"}
    if($entry.archive -like '*.zip') {Expand-ModZip $archive $work}
    else{
        $zstd=Get-ChildItem -LiteralPath $work -Recurse -File -Filter zstd.exe|Select-Object -First 1
        if(-not $zstd){throw 'Portable zstd dependency is missing.'}
        $env:PATH=$zstd.DirectoryName+';'+$savedPath
        & (Join-Path ([Environment]::SystemDirectory) 'tar.exe') -xf $archive -C $work
        if($LASTEXITCODE){throw "Dependency extraction failed: $($entry.name)"}
    }
    if(-not [IO.File]::Exists($marker) -or (Get-ModHash $marker) -ne $entry.marker_sha256){throw "Extracted dependency checksum failed: $($entry.name)"}
}
foreach($pair in @(@('ddraw.dll','runtime/ddraw.dll'),@('mingw64/bin/libiconv-2.dll','runtime/libiconv-2.dll'))){
    [IO.File]::Copy((Join-Path $work $pair[0]),(Join-Path $script:ModRoot $pair[1]),$true)
}
}finally{$env:PATH=$savedPath}
Write-Host 'Verified build dependencies in .work/deps; nothing installed globally.'
