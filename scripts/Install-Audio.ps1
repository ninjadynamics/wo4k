param([Parameter(Mandatory=$true)][string]$SourcePath,[Parameter(Mandatory=$true)][string]$GameRoot,[switch]$Original,[switch]$Incremental,[switch]$PassThru)
. (Join-Path $PSScriptRoot 'Core.ps1')
$manifest=Get-Content -Encoding UTF8 -LiteralPath (Join-Path $script:ModRoot 'manifests/patches.json') -Raw | ConvertFrom-Json
$source=[IO.Path]::GetFullPath($SourcePath).TrimEnd('\')
$cache=Get-ModChild $GameRoot 'audio/source'
$stateFile=Get-ModChild $GameRoot 'audio/install-state.json';$previous=@{}
if($Incremental -and [IO.File]::Exists($stateFile)){try{$state=Get-Content -LiteralPath $stateFile -Encoding UTF8 -Raw|ConvertFrom-Json;foreach($entry in $state.files){if($entry.PSObject.Properties['source_sha256'] -and $entry.PSObject.Properties['processor'] -and $entry.PSObject.Properties['output_sha256']){$previous[$entry.name]=$entry}}}catch{$previous=@{}}}
$processor=if($Original){'original'}else{Get-ModHash (Join-Path $script:ModRoot 'runtime/audio-clean.exe')}
$inputs=@{};$hashes=@{}
# Validate the whole bank before changing any effects. Keep local inputs so
# launch-time refresh never denoises its own output.
foreach($entry in $manifest.audio){
    $input=Join-Path $source $entry.name
    if(-not [IO.File]::Exists($input)){throw "Missing sound effect: $($entry.name)"}
    $hash=Get-ModHash $input
    if(-not $Original -and $hash -ne $entry.output_sha256 -and $hash -notin @($entry.source_hashes)){throw "Unsupported sound effect checksum: $($entry.name). Use -Original to retain custom effects."}
    $inputs[$entry.name]=$input;$hashes[$entry.name]=$hash
}
$next=New-Object Collections.Generic.List[object];$updated=0
foreach($entry in $manifest.audio) {
    $input=$inputs[$entry.name];$hash=$hashes[$entry.name];$target=Get-ModChild $GameRoot ('audio/'+$entry.name)
    $pristine=Join-Path $cache $entry.name
    if(-not [string]::Equals($input,$pristine,[StringComparison]::OrdinalIgnoreCase)){
        [WoBeta.Files]::Parent($pristine)
        if(-not [IO.File]::Exists($pristine) -or (Get-ModHash $pristine) -ne $hash){[IO.File]::Copy($input,$pristine,$true)}
        $input=$pristine
    }
    $old=$previous[$entry.name]
    if($Incremental -and $old -and $old.source_sha256 -eq $hash -and $old.processor -eq $processor -and [IO.File]::Exists($target) -and (Get-ModHash $target) -eq $old.output_sha256){$next.Add($old);continue}
    if($Original -or $hash -eq $entry.output_sha256){[WoBeta.Files]::Parent($target);[IO.File]::Copy($input,$target,$true)}
    elseif($hash -in @($entry.source_hashes)){
        [WoBeta.Files]::Parent($target)
        & (Join-Path $script:ModRoot 'runtime/audio-clean.exe') $input $target
        if($LASTEXITCODE){throw "Audio processing failed for $($entry.name): $LASTEXITCODE"}
    }
    $next.Add([pscustomobject]@{name=$entry.name;source_sha256=$hash;processor=$processor;output_sha256=(Get-ModHash $target)});++$updated
}
Write-ModJson $stateFile @{files=@($next.ToArray())}
Write-Host "Verified $($manifest.audio.Count) sound effects; refreshed $updated."
if($PassThru){[pscustomobject]@{checked=$manifest.audio.Count;updated=$updated}}
