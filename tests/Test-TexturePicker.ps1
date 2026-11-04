param([string]$Node,[string]$Browser)
$ErrorActionPreference='Stop'
if(-not $Node){$Node=(Get-Command node.exe -ErrorAction Stop).Source}
if(-not $Browser){
    $candidates=@()
    foreach($base in @(${env:ProgramFiles(x86)},$env:ProgramFiles)){
        if($base){$candidates+=Join-Path $base 'Microsoft/Edge/Application/msedge.exe';$candidates+=Join-Path $base 'Google/Chrome/Application/chrome.exe'}
    }
    $Browser=@($candidates|Where-Object {[IO.File]::Exists($_)}|Select-Object -First 1)[0]
    if(-not $Browser){throw 'Specify -Browser with the path to Edge or Chrome.'}
}
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$work=Join-Path $root ('.work/texture-picker-test-'+[guid]::NewGuid().ToString('N'))
[IO.Directory]::CreateDirectory($work)|Out-Null
$profile=Join-Path $work 'browser-profile'
$process=Start-Process -FilePath $Browser -WindowStyle Hidden -PassThru -ArgumentList @('--headless=new','--disable-gpu','--no-first-run','--no-default-browser-check','--disable-background-networking','--disable-extensions','--remote-debugging-port=9237','--remote-debugging-address=127.0.0.1',('--user-data-dir="'+$profile+'"'),'--window-size=1440,1080','--force-device-scale-factor=1','about:blank')
try{
    & $Node (Join-Path $PSScriptRoot 'test_texture_picker.mjs') (Join-Path $root 'exports/ps1-texture-choices/index.html') $work 9237
    if($LASTEXITCODE){throw ('Texture picker browser test failed: '+$LASTEXITCODE)}
}finally{
    $running=Get-Process -Id $process.Id -ErrorAction SilentlyContinue
    if($running -and $running.Path -eq $Browser){Stop-Process -Id $process.Id -ErrorAction SilentlyContinue}
}
