. (Join-Path $PSScriptRoot 'Configuration.ps1')
function Assert-ModManagedPath([string]$Root,[string]$Path){
    $base=[IO.Path]::GetFullPath($Root).TrimEnd('\');$full=[IO.Path]::GetFullPath($Path)
    if(-not $full.StartsWith($base+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Cleanup target leaves the mod folder.'}
    for($cursor=$full;$cursor -and $cursor.Length -ge $base.Length;$cursor=[IO.Path]::GetDirectoryName($cursor)){
        if((Test-Path -LiteralPath $cursor) -and ([IO.File]::GetAttributes($cursor) -band [IO.FileAttributes]::ReparsePoint)){throw 'Cleanup refuses filesystem links.'}
    }
}
function Save-ModReleaseOwnership([string]$Root){
    $source=Join-Path $Root 'release-files.json';$saved=Get-ModChild $Root '.state/release-inventory.json'
    if([IO.File]::Exists($source) -and -not [IO.File]::Exists($saved)){
        Assert-ModManagedPath $Root $saved
        $inventory=Get-Content -LiteralPath $source -Raw -Encoding UTF8|ConvertFrom-Json
        [WoBeta.ConfigurationFile]::Write($saved,(ConvertTo-Json -InputObject $inventory -Depth 12))
    }
}
function Remove-ModRetiredFiles([string]$Root){
    $file=Join-Path $Root 'release-files.json';$old=Join-Path $Root '.state/release-inventory.json';$removed=0;$kept=0
    if(-not [IO.File]::Exists($file)){return [pscustomobject]@{removed=0;custom_files_preserved=0}}
    $current=Get-Content -LiteralPath $file -Raw -Encoding UTF8|ConvertFrom-Json;$wanted=@{}
    foreach($entry in $current.files){$wanted[[string]$entry.path]=$true}
    if([IO.File]::Exists($old)){
        $prior=Get-Content -LiteralPath $old -Raw -Encoding UTF8|ConvertFrom-Json
        foreach($entry in $prior.files){
            $relative=[string]$entry.path
            if($relative -match '[\\:\x00-\x1f]' -or $relative.StartsWith('/') -or @($relative.Split('/')|Where-Object {$_ -in '','.','..'}).Count){throw 'Invalid path in saved release inventory.'}
            if($wanted.ContainsKey($relative) -or $relative -in 'release.url','source.url' -or $relative -notmatch '^(scripts|src|runtime|assets|manifests|licenses|docs|tests|patches)/' -or $entry.sha256 -notmatch '^[a-fA-F0-9]{64}$'){continue}
            $targets=@($relative)
            if($relative -match '^runtime/([^/]+\.dll|wo2097-controller\.exe)$'){$targets+=('wobble/'+[IO.Path]::GetFileName($relative))}
            foreach($target in $targets){
                $path=Get-ModChild $Root $target;Assert-ModManagedPath $Root $path
                if(-not [IO.File]::Exists($path)){continue}
                if((Get-ModHash $path) -ne $entry.sha256){++$kept;continue}
                [IO.File]::Delete($path);++$removed
                $parent=[IO.Path]::GetDirectoryName($path);$base=[IO.Path]::GetFullPath($Root).TrimEnd('\')
                while($parent -ne $base -and $parent.StartsWith($base+'\',[StringComparison]::OrdinalIgnoreCase)){
                    Assert-ModManagedPath $Root $parent
                    if(-not [IO.Directory]::Exists($parent) -or [IO.Directory]::EnumerateFileSystemEntries($parent).GetEnumerator().MoveNext()){break}
                    [IO.Directory]::Delete($parent,$false);$parent=[IO.Path]::GetDirectoryName($parent)
                }
            }
        }
    }
    $snapshot=Get-ModChild $Root '.state/release-inventory.json';Assert-ModManagedPath $Root $snapshot
    [WoBeta.ConfigurationFile]::Write($snapshot,(ConvertTo-Json -InputObject $current -Depth 12))
    return [pscustomobject]@{removed=$removed;custom_files_preserved=$kept}
}
