param([string]$GameRoot=(Join-Path $PSScriptRoot '../wobble'),[switch]$PassThru)
. (Join-Path $PSScriptRoot 'Core.ps1')
$game=[IO.Path]::GetFullPath($GameRoot).TrimEnd('\');$changed=0;$preserved=0
foreach($process in @(Get-Process Wipeout2097 -ErrorAction SilentlyContinue)){
    if(-not $process.Path -or $process.Path.StartsWith($game+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Close this game before updating its layout.'}
}
$cursor=$game
while($cursor){
    if([IO.Directory]::Exists($cursor) -and ([IO.File]::GetAttributes($cursor) -band [IO.FileAttributes]::ReparsePoint)){throw 'Layout cleanup refuses linked directories.'}
    $parent=[IO.Directory]::GetParent($cursor);$cursor=if($parent){$parent.FullName}else{$null}
}
$legacy=Get-ModChild $game 'textures/hd'
foreach($path in @((Get-ModChild $game 'textures'),$legacy)){
    if([IO.Directory]::Exists($path) -and ([IO.File]::GetAttributes($path) -band [IO.FileAttributes]::ReparsePoint)){throw 'Layout cleanup refuses linked directories.'}
}
# Only old installer outputs; custom PNG edits are saved, unknown files retained.
foreach($name in @('font-and-numbers','hud-gauges','notice','title')){
    $png=Get-ModChild $legacy ($name+'.png');$canonical=Join-Path $script:ModRoot ('assets/hd/'+$name+'.png')
    if(-not [IO.File]::Exists($canonical)){continue}
    if([IO.File]::Exists($png)){
        if((Get-ModHash $png) -ne (Get-ModHash $canonical)){
            $saved=Get-ModChild $script:ModRoot ('backups/artwork-migration/'+(Get-ModHash $png)+'-'+$name+'.png');[WoBeta.Files]::Parent($saved)
            if(-not [IO.File]::Exists($saved)){[IO.File]::Copy($png,$saved,$false)};++$preserved
        }
        [IO.File]::Delete($png);++$changed
    }
    foreach($extension in @('.bmp','.rgba')){
        $generated=Get-ModChild $legacy ($name+$extension)
        if([IO.File]::Exists($generated)){[IO.File]::Delete($generated);++$changed}
    }
}
$state=Get-ModChild $legacy 'install-state.json';if([IO.File]::Exists($state)){[IO.File]::Delete($state);++$changed}
if([IO.Directory]::Exists($legacy) -and -not [IO.Directory]::EnumerateFileSystemEntries($legacy).GetEnumerator().MoveNext()){[IO.Directory]::Delete($legacy,$false)}
$result=[pscustomobject]@{layout=2;removed=$changed;custom_artwork_preserved=$preserved}
if($PassThru){$result}
