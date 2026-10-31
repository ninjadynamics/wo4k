param([Parameter(Mandatory=$true)][string]$PcImage,[string]$PsxArchive)
. (Join-Path $PSScriptRoot '../scripts/Sources.ps1')
$root=$script:ModRoot;$zip=Join-Path $root ('dist/wo4k-'+[IO.File]::ReadAllText((Join-Path $root 'VERSION')).Trim()+'.zip')
if(-not [IO.File]::Exists($zip)){throw 'Build the release ZIP first.'}
$work=Join-Path $root ('.work/clean-'+[guid]::NewGuid().ToString('N').Substring(0,16));$clean=Join-Path $work 'unpacked mod Ω'
Expand-ModZip $zip $clean
$inventory=Get-Content -Encoding UTF8 -LiteralPath (Join-Path $clean 'release-files.json') -Raw|ConvertFrom-Json
foreach($entry in $inventory.files){if((Get-ModHash (Get-ModChild $clean $entry.path)) -ne $entry.sha256){throw "Packaged checksum failed: $($entry.path)"}}
& powershell.exe -NoProfile -STA -ExecutionPolicy Bypass -File (Join-Path $clean 'Setup.ps1') -ValidateUi
if($LASTEXITCODE){throw 'Packaged GUI did not validate.'}
$native=Get-ModNativeRoot $PcImage (Join-Path $work 'native')
$sourceZip=Join-Path $work 'Original PC Files Ω.zip';[IO.Compression.ZipFile]::CreateFromDirectory($native,$sourceZip,[IO.Compression.CompressionLevel]::Optimal,$false)
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $clean 'scripts/Install-Mod.ps1') -GameSource $sourceZip -PcMusicSource $PcImage
if($LASTEXITCODE){throw 'Clean ZIP-source PC install failed.'}
$receipt=Get-Content -Encoding UTF8 -LiteralPath (Join-Path $clean 'wobble/installation.json') -Raw|ConvertFrom-Json
if($receipt.default_soundtrack -ne 'Pc' -or $receipt.playstation_available){throw 'Minimum installation should be PC-only.'}
if($PsxArchive){
    $request=Join-Path $clean '.work/gui-request.json'
    Write-ModJson $request ([ordered]@{GameSource=$sourceZip;PcMusicSource=$PcImage;PsxMusicSource=$PsxArchive;DefaultSoundtrack='Psx';HdTextures=$true;CleanAudio=$true})
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $clean 'scripts/Install-Mod.ps1') -RequestFile $request
    if($LASTEXITCODE){throw 'Clean optional PS1 install failed.'}
    $receipt=Get-Content -Encoding UTF8 -LiteralPath (Join-Path $clean 'wobble/installation.json') -Raw|ConvertFrom-Json
    if(-not $receipt.playstation_available -or $receipt.default_soundtrack -ne 'Psx'){throw 'Optional PS1/default selection failed.'}
}
$count=@(Get-ChildItem -LiteralPath (Join-Path $clean 'wobble/audio') -File -Filter '*.wav').Count
if($count -ne 39){throw 'Clean installation has an incomplete effects bank.'}
$leftovers=@(Get-ChildItem -LiteralPath (Join-Path $clean '.work') -Directory -Filter 'i-*');if($leftovers.Count){throw 'Successful install left temporary extraction sessions.'}
$result=[ordered]@{state='passed';unicode_gui_request=$true;successful_session_cleanup=$true;package_sha256=(Get-ModHash $zip);packaged_files=$inventory.files.Count;pc_game_zip=$true;pc_mds_audio=$true;optional_psx=[bool]$PsxArchive;native_audio_files=$count;fresh_root=$clean;desktop_play_test='pending'}
Write-ModJson (Join-Path $root '.work/clean-package-report.json') $result
Write-Host 'PASS: unpacked release validates and installs from source ZIP + PC disc; optional PS1 bank/default tested when provided.'
