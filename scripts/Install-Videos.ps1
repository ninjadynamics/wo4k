param([string]$GameRoot=(Join-Path $PSScriptRoot '../wobble'),[switch]$PassThru)
. (Join-Path $PSScriptRoot 'Core.ps1')
$encoder=Get-Command ffmpeg.exe -ErrorAction SilentlyContinue
$converted=0
function Invoke-VideoEncoder([string[]]$EncoderArguments){
    $savedPreference=$ErrorActionPreference
    try{$ErrorActionPreference='Continue';$messages=& $encoder.Source @EncoderArguments 2>&1;$code=$LASTEXITCODE}
    finally{$ErrorActionPreference=$savedPreference}
    [pscustomobject]@{code=$code;messages=($messages -join ' ')}
}
if($encoder){
    foreach($base in @('WXLINTR','XTRO1','XTRO2','XTRO3','XTRO4')){
        $original=Get-ModChild $GameRoot ('movies/'+$base+'.MPG')
        $output=Get-ModChild $GameRoot ('movies/'+$base+'.mp4')
        if(-not [IO.File]::Exists($original) -or [IO.File]::Exists($output)){continue}
        if($base -eq 'WXLINTR' -and [IO.File]::Exists((Get-ModChild $GameRoot 'movies/intro.mp4'))){continue}
        $temporary=Get-ModChild $GameRoot ('movies/.wo4k-convert-'+[guid]::NewGuid().ToString('N')+'.mp4')
        try{
            $encoded=Invoke-VideoEncoder @('-hide_banner','-loglevel','error','-nostdin','-y','-i',$original,'-map','0:v:0','-map','0:a:0?','-c:v','libx264','-preset','medium','-crf','18','-pix_fmt','yuv420p','-c:a','aac','-b:a','192k','-movflags','+faststart',$temporary)
            if($encoded.code -or -not [IO.File]::Exists($temporary) -or (Get-Item -LiteralPath $temporary).Length -lt 1024){throw ('Video conversion failed: '+$base+' '+$encoded.messages)}
            $validated=Invoke-VideoEncoder @('-hide_banner','-loglevel','error','-nostdin','-i',$temporary,'-map','0:v:0','-map','0:a:0?','-f','null','NUL')
            if($validated.code){throw ('Converted video failed decoding: '+$base)}
            [IO.File]::Move($temporary,$output)
            # Original source disc/ZIP remains untouched. Existing MP4s survive.
            [IO.File]::Delete($original);++$converted
        }finally{if([IO.File]::Exists($temporary)){[IO.File]::Delete($temporary)}}
    }
}
if($PassThru){[pscustomobject]@{converted=$converted;encoder_available=[bool]$encoder}}
