. (Join-Path $PSScriptRoot 'Core.ps1')
. (Join-Path $PSScriptRoot 'Game-Settings.ps1')
if(-not ('WoBeta.ConfigurationFile' -as [type])){
Add-Type -TypeDefinition @'
using System;using System.IO;using System.Text;
namespace WoBeta { public static class ConfigurationFile {
 public static void Write(string path,string json){
  for(string cursor=Path.GetFullPath(path);cursor!=null;cursor=Path.GetDirectoryName(cursor))
   if((File.Exists(cursor)||Directory.Exists(cursor))&&(File.GetAttributes(cursor)&FileAttributes.ReparsePoint)!=0)throw new IOException("Configuration refuses filesystem links.");
  Directory.CreateDirectory(Path.GetDirectoryName(path));string temp=path+"."+Guid.NewGuid().ToString("N")+".tmp";
  try{File.WriteAllText(temp,json,new UTF8Encoding(false));if(File.Exists(path))File.Replace(temp,path,null);else File.Move(temp,path);}
  finally{if(File.Exists(temp))File.Delete(temp);}
 }
}}
'@
}
function Get-ModProperty($Object,[string]$Name,$Default=$null){
    if($null -ne $Object -and $Object.PSObject.Properties[$Name]){return $Object.$Name};return $Default
}
function Get-ModPcFileReceipt([string]$GameRoot,[string]$Root=$script:ModRoot){
    $manifest=Get-Content -LiteralPath (Join-Path $Root 'manifests/game-files.json') -Raw -Encoding UTF8|ConvertFrom-Json
    $records=@(foreach($relative in $manifest.required){
        $path=Get-ModChild $GameRoot $relative
        if(-not [IO.File]::Exists($path)){throw ('Missing Windows game data: '+$relative)}
        [pscustomobject]@{file=$relative;sha256=(Get-ModHash $path)}
    })
    $audio=Get-Content -LiteralPath (Join-Path $Root 'manifests/patches.json') -Raw -Encoding UTF8|ConvertFrom-Json
    foreach($entry in $audio.audio){
        $relative='audio/source/'+$entry.name;$path=Get-ModChild $GameRoot $relative
        if(-not [IO.File]::Exists($path)){throw ('Missing original sound effect: '+$entry.name)}
        $records+=[pscustomobject]@{file=$relative;sha256=(Get-ModHash $path)}
    }
    return $records
}
function Resolve-ModConfiguredPath([string]$Root,[string]$Path){
    if(-not $Path){return ''}
    if([IO.Path]::IsPathRooted($Path)){return [IO.Path]::GetFullPath($Path)}
    return Get-ModChild $Root $Path
}
function ConvertTo-ModPortablePath([string]$Root,[string]$Path){
    if(-not $Path){return ''};$full=Resolve-ModConfiguredPath $Root $Path;$base=[IO.Path]::GetFullPath($Root).TrimEnd('\')
    if($full.StartsWith($base+'\',[StringComparison]::OrdinalIgnoreCase)){return $full.Substring($base.Length+1).Replace('\','/')};return $full
}
function Get-ModConfiguration([string]$Root=$script:ModRoot){
    $game=Join-Path $Root 'wobble';$receipt=$null;$saved=$null
    $file=Join-Path $game 'installation.json';if([IO.File]::Exists($file)){$receipt=Get-Content -LiteralPath $file -Raw -Encoding UTF8|ConvertFrom-Json}
    $file=Join-Path $Root '.state/setup.json';if([IO.File]::Exists($file)){$saved=Get-Content -LiteralPath $file -Raw -Encoding UTF8|ConvertFrom-Json;if($saved.format -ne 1){throw 'Unsupported saved Setup configuration.'}}
    if(-not $saved){$saved=Get-ModProperty $receipt 'configuration'}
    # Upgrade old installations that retained only their install request.
    if(-not $saved -and $receipt){
        $requests=@(Get-ChildItem -LiteralPath (Join-Path $Root '.work') -Filter 'request-*.json' -File -ErrorAction SilentlyContinue|Sort-Object LastWriteTimeUtc -Descending)
        foreach($request in $requests){
            try{$candidate=Get-Content -LiteralPath $request.FullName -Raw -Encoding UTF8|ConvertFrom-Json
                if($candidate.GameSource -and $request.LastWriteTimeUtc -le ([datetime]$receipt.installed_at).ToUniversalTime()){$saved=[pscustomobject]@{GameSource=$candidate.GameSource;PcMusicSource=$candidate.PcMusicSource;PsxMusicSource=$candidate.PsxMusicSource};break}
            }catch{continue}
        }
    }
    $gameSource=Get-ModProperty $saved 'GameSource' $(if([IO.Directory]::Exists($game)){'wobble'}else{''})
    $psx=Get-ModProperty $saved 'PsxMusicSource' ''
    if(-not $saved){
        $origin=Join-Path $game 'textures/ps1-source/source.json'
        if([IO.File]::Exists($origin)){$psx=Get-ModProperty (Get-Content -LiteralPath $origin -Raw -Encoding UTF8|ConvertFrom-Json) 'configured_source' ''}
    }
    $result=[pscustomobject][ordered]@{
        format=1;revision=[int](Get-ModProperty $saved 'revision' 0)
        GameSource=(Resolve-ModConfiguredPath $Root $gameSource)
        PcMusicSource=(Resolve-ModConfiguredPath $Root (Get-ModProperty $saved 'PcMusicSource' ''))
        PsxMusicSource=(Resolve-ModConfiguredPath $Root $psx)
        DefaultSoundtrack=[string](Get-ModProperty $saved 'DefaultSoundtrack' (Get-ModProperty $receipt 'default_soundtrack' 'Pc'))
        HdTextures=[bool](Get-ModProperty $saved 'HdTextures' (Get-ModProperty $receipt 'hd_textures' $true))
        CleanAudio=[bool](Get-ModProperty $saved 'CleanAudio' (Get-ModProperty $receipt 'clean_audio' $true))
        SoundMode=[int](Get-ModProperty $saved 'SoundMode' (Get-ModSoundMode (Join-Path $game 'Wipeout2097.ini')))
        FullscreenMode=[string](Get-ModProperty $saved 'FullscreenMode' (Get-ModFullscreenMode (Join-Path $game 'DDrawCompat-Wipeout2097.ini')))
    }
    if($result.DefaultSoundtrack -notin 'Pc','Psx','None' -or $result.SoundMode -notin 1,2,5 -or $result.FullscreenMode -notin 'borderless','exclusive'){throw 'Invalid saved Setup preferences.'}
    return $result
}
function Save-ModConfiguration($Configuration,[string]$Root=$script:ModRoot){
    $stored=[ordered]@{format=1;revision=1}
    foreach($name in @('GameSource','PcMusicSource','PsxMusicSource')){$stored[$name]=ConvertTo-ModPortablePath $Root ([string](Get-ModProperty $Configuration $name ''))}
    foreach($name in @('DefaultSoundtrack','HdTextures','CleanAudio','SoundMode','FullscreenMode')){$stored[$name]=Get-ModProperty $Configuration $name}
    if($stored.DefaultSoundtrack -notin 'Pc','Psx','None' -or $stored.SoundMode -notin 1,2,5 -or $stored.FullscreenMode -notin 'borderless','exclusive' -or $stored.HdTextures -isnot [bool] -or $stored.CleanAudio -isnot [bool]){throw 'Invalid Setup preferences.'}
    $file=Get-ModChild $Root '.state/setup.json';$previous=$null
    if([IO.File]::Exists($file)){$previous=Get-Content -LiteralPath $file -Raw -Encoding UTF8|ConvertFrom-Json}
    if($previous){
        $same=$true;foreach($key in $stored.Keys){if($key -eq 'revision'){continue};if((Get-ModProperty $previous $key) -cne $stored[$key]){$same=$false}}
        if($same){return Get-ModConfiguration $Root}
        $stored.revision=[int](Get-ModProperty $previous 'revision' 0)+1
    }
    [WoBeta.ConfigurationFile]::Write($file,(ConvertTo-Json -InputObject $stored -Depth 8))
    return Get-ModConfiguration $Root
}
function Get-ModSourceStamp([string]$Path){
    if(-not $Path){return ''}
    if(-not (Test-Path -LiteralPath $Path)){return $Path+'|unavailable'}
    $item=Get-Item -LiteralPath $Path
    if($item.PSIsContainer){
        $parts=@(Get-ChildItem -LiteralPath $Path -File -Recurse -Depth 2 | Where-Object {$_.Extension -in '.iso','.cue','.bin','.mds','.mdf','.zip','.wav','.mp3' -or $_.Name -ieq 'tracks.ini'} | Sort-Object FullName | ForEach-Object {$_.FullName+'|'+$_.Length+'|'+$_.LastWriteTimeUtc.Ticks})
        return $item.FullName+'|folder|'+($parts -join '|')
    }
    $stamp=$item.FullName+'|'+$item.Length+'|'+$item.LastWriteTimeUtc.Ticks
    if($item.Extension -in '.cue','.mds' -and (Get-Command Get-ModDiscTracks -ErrorAction SilentlyContinue)){
        foreach($image in @(Get-ModDiscTracks $Path|Select-Object -ExpandProperty Image -Unique)){
            if([IO.File]::Exists($image)){$track=Get-Item -LiteralPath $image;$stamp+='|'+$track.FullName+'|'+$track.Length+'|'+$track.LastWriteTimeUtc.Ticks}else{$stamp+='|'+$image+'|unavailable'}
        }
    }
    return $stamp
}
