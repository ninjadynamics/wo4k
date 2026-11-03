# Native sound modes: 1=Mono, 2=Stereo, 5=Dolby Surround.
function Get-ModFullscreenMode([string]$SettingsPath) {
    if([IO.File]::Exists($SettingsPath)){
        foreach($line in [IO.File]::ReadAllLines($SettingsPath)){
            if($line -match '^\s*FullscreenMode\s*=\s*exclusive(?:\(\s*[01]\s*\))?\s*(?:#.*)?$'){return 'exclusive'}
            if($line -match '^\s*FullscreenMode\s*=\s*borderless\s*(?:#.*)?$'){return 'borderless'}
        }
    }
    return 'borderless'
}
function Set-ModFullscreenMode([string]$SettingsPath,[ValidateSet('borderless','exclusive')][string]$FullscreenMode) {
    $lines=New-Object Collections.Generic.List[string]
    if([IO.File]::Exists($SettingsPath)){foreach($line in [IO.File]::ReadAllLines($SettingsPath)){$lines.Add($line)}}
    $replaced=$false
    for($i=$lines.Count-1;$i -ge 0;$i--){if($lines[$i] -match '^\s*FullscreenMode\s*='){
        if($replaced){$lines.RemoveAt($i)}else{$lines[$i]='FullscreenMode = '+$FullscreenMode.ToLowerInvariant();$replaced=$true}
    }}
    if(-not $replaced){$lines.Add('FullscreenMode = '+$FullscreenMode.ToLowerInvariant())}
    $parent=[IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($SettingsPath));[IO.Directory]::CreateDirectory($parent)|Out-Null
    [IO.File]::WriteAllLines($SettingsPath,$lines,(New-Object Text.UTF8Encoding($false)))
}
function Get-ModSoundMode([string]$SettingsPath) {
    if(-not [IO.File]::Exists($SettingsPath)){return 2}
    $audio=$false
    foreach($line in [IO.File]::ReadAllLines($SettingsPath)){
        if($line -match '^\s*\[([^\]]+)\]\s*$'){$audio=$matches[1] -ieq 'Audio';continue}
        if($audio -and $line -match '^\s*SoundMode\s*=\s*(1|2|5)\s*$'){return [int]$matches[1]}
    }
    return 2
}
function Set-ModSoundMode([string]$SettingsPath,[ValidateSet(1,2,5)][int]$SoundMode) {
    $lines=New-Object Collections.Generic.List[string]
    if([IO.File]::Exists($SettingsPath)){foreach($line in [IO.File]::ReadAllLines($SettingsPath)){$lines.Add($line)}}
    $section=-1;$end=$lines.Count
    for($i=0;$i -lt $lines.Count;$i++){
        if($lines[$i] -match '^\s*\[([^\]]+)\]\s*$'){
            if($section -ge 0){$end=$i;break}
            if($matches[1] -ieq 'Audio'){$section=$i}
        }
    }
    if($section -lt 0){if($lines.Count -and $lines[$lines.Count-1]){$lines.Add('')};$lines.Add('[Audio]');$lines.Add('SoundMode='+$SoundMode)}
    else{
        $replaced=$false
        for($i=$end-1;$i -gt $section;$i--){if($lines[$i] -match '^\s*SoundMode\s*='){
            if($replaced){$lines.RemoveAt($i)}else{$lines[$i]='SoundMode='+$SoundMode;$replaced=$true}
        }}
        if(-not $replaced){$lines.Insert($end,'SoundMode='+$SoundMode)}
    }
    $parent=[IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($SettingsPath));[IO.Directory]::CreateDirectory($parent)|Out-Null
    [IO.File]::WriteAllLines($SettingsPath,$lines,(New-Object Text.UTF8Encoding($false)))
}
