# Explicit names keep drop-in replacements local to the game's movie directory.
function Find-ModVideo([string]$GameRoot,[string]$Movie,[switch]$OriginalOnly) {
    $base=switch($Movie){'intro'{'WXLINTR'} 'xtro1'{'XTRO1'} 'xtro2'{'XTRO2'} 'xtro3'{'XTRO3'} 'xtro4'{'XTRO4'} default{throw 'Unknown game video.'}}
    $names=@($base+'.mp4')
    if($Movie -eq 'intro'){$names=@('intro.mp4')+$names}
    if($OriginalOnly){$names=@()}
    $names+=($base+'.MPG')
    foreach($name in $names){$candidate=Join-Path $GameRoot ('movies/'+$name);if([IO.File]::Exists($candidate)){return [IO.Path]::GetFullPath($candidate)}}
    return $null
}
