param([string]$GameRoot=(Join-Path $PSScriptRoot '../wobble'),[string]$PsxSource,[string]$WorkRoot,[switch]$PassThru)
. (Join-Path $PSScriptRoot 'Native-Textures.ps1')
$GameRoot=[IO.Path]::GetFullPath($GameRoot).TrimEnd('\')
foreach($process in @(Get-Process Wipeout2097 -ErrorAction SilentlyContinue)){
    if(-not $process.Path -or $process.Path.StartsWith($GameRoot+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Close the game before installing native texture improvements.'}
}
if(-not $WorkRoot){$WorkRoot=Join-Path $script:ModRoot ('.work/native-import-'+[guid]::NewGuid().ToString('N'))}
$result=Install-ModNativeTextures -GameRoot $GameRoot -PsxSource $PsxSource -WorkRoot $WorkRoot
$receiptPath=Join-Path $GameRoot 'installation.json'
if([IO.File]::Exists($receiptPath)){
    $receipt=Get-Content -LiteralPath $receiptPath -Raw -Encoding UTF8|ConvertFrom-Json
    $receipt|Add-Member -NotePropertyName playstation_textures -NotePropertyValue ([bool]$result.ps1_textures) -Force
    Write-ModJson $receiptPath $receipt
}
Write-Host ("Native artwork ready: $($result.pc_regions) regions from verified PC originals, $($result.ps1_regions) from the PS1 disc, $($result.pending_ps1_regions) awaiting an optional PS1 source.")
if($PassThru){$result}
