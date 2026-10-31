# Offline fixtures exercise URL handling and the real update transaction.
. (Join-Path $PSScriptRoot '../scripts/Update-Mod.ps1') -FunctionsOnly
$root=$script:ModRoot;$work=Join-Path $root ('.work/update-tests-'+[guid]::NewGuid().ToString('N').Substring(0,16))
[IO.Directory]::CreateDirectory($work)|Out-Null
$config=Join-Path $work 'link.txt'
function Assert-Result([bool]$Condition,[string]$Message){if(-not $Condition){throw $Message}}
Assert-Result (-not (Get-ModConfiguredUrl $config Release)) 'Missing release configuration should disable Update.'
foreach($text in @('','  ',"https://github.com/owner/repo/releases/latest`nhttps://evil.example/archive.zip",'not a URL','http://example.test/file.zip','file:///C:/file.zip','https://user:password@example.test/file.zip','https://example.test/file.zip#fragment','https://github.com/owner/repo/issues')){
    [IO.File]::WriteAllText($config,$text,(New-Object Text.UTF8Encoding($false)))
    Assert-Result (-not (Get-ModConfiguredUrl $config Release)) ('Accepted invalid release configuration: '+$text)
}
[IO.File]::WriteAllBytes($config,[byte[]](0xc3,0x28));Assert-Result (-not (Get-ModConfiguredUrl $config Release)) 'Invalid UTF-8 must disable Update.'
foreach($text in @('https://github.com/owner/repo/releases','https://github.com/owner/repo/releases/latest','https://api.github.com/repos/owner/repo/releases/latest','https://example.test/wo4k.zip')){
    [IO.File]::WriteAllText($config,$text);Assert-Result ([bool](Get-ModConfiguredUrl $config Release)) ('Rejected valid release URL: '+$text)
}
[IO.File]::WriteAllText($config,'https://github.com/owner/repo');Assert-Result ([bool](Get-ModConfiguredUrl $config Source)) 'Valid source link was disabled.'
[IO.File]::WriteAllText($config,'https://example.test/owner/repo');Assert-Result (-not (Get-ModConfiguredUrl $config Source)) 'Non-GitHub source link was enabled.'
Assert-Result ((Get-ModReleaseEndpoint 'https://github.com/owner/repo/releases/latest') -eq 'https://api.github.com/repos/owner/repo/releases/latest') 'GitHub page was not resolved to its latest-release API.'
Assert-Result ((Compare-ModReleaseVersion '0.1.0-beta.11' '0.1.0-beta.2') -gt 0) 'Numeric beta ordering failed.'
Assert-Result ((Compare-ModReleaseVersion '0.1.0' '0.1.0-beta.11') -gt 0) 'Stable release ordering failed.'
Assert-Result ((Compare-ModReleaseVersion '0.1.0-beta.2' '0.2.0') -lt 0) 'Downgrade detection failed.'
Assert-Result ((Compare-ModReleaseVersion 'v0.1.0-beta.2' '0.1.0-beta.2') -eq 0) 'Version equality failed.'

$package=Join-Path $work 'payload';$fixture=Join-Path $work 'installed mod Ω';$game=Join-Path $fixture 'wobble'
foreach($directory in @($package,$fixture,$game)){[IO.Directory]::CreateDirectory($directory)|Out-Null}
$payload=[ordered]@{
    'Setup.ps1'='new setup';'Play.ps1'='new play';'Setup.exe'='launcher fixture';'Play.exe'='launcher fixture';'scripts/Core.ps1'='new core';
    'runtime/wohr.dll'='new runtime';'release.url'='';'source.url'=''
}
$native=Join-Path $game 'Wipeout2097.exe';[IO.File]::WriteAllText($native,'game fixture')
$patches=Join-Path $package 'manifests/patches.json';Write-ModJson $patches @{output_sha256=(Get-ModHash $native)}
$entries=New-Object Collections.Generic.List[object]
foreach($item in $payload.GetEnumerator()){$file=Get-ModChild $package $item.Key;[WoBeta.Files]::Parent($file);[IO.File]::WriteAllText($file,$item.Value);$entries.Add([pscustomobject]@{path=$item.Key;bytes=(Get-Item -LiteralPath $file).Length;sha256=(Get-ModHash $file)})}
$entries.Add([pscustomobject]@{path='manifests/patches.json';bytes=(Get-Item -LiteralPath $patches).Length;sha256=(Get-ModHash $patches)})
Write-ModJson (Join-Path $package 'release-files.json') @{version='0.1.0-beta.3';files=@($entries.ToArray())}
$zip=Join-Path $work 'release.zip';[IO.Compression.ZipFile]::CreateFromDirectory($package,$zip)
$verified=Join-Path $work 'verified';$inventory=Read-ModUpdatePackage $zip $verified

$script:fixtureMetadata=[ordered]@{draft=$false;assets=@(@{name='wo-beta-mod-0.1.0-beta.3.zip';browser_download_url='https://example.test/release.zip';digest=('sha256:'+(Get-ModHash $zip))},@{name='wo-beta-mod-0.1.0-beta.3.zip.sha256';browser_download_url='https://example.test/release.zip.sha256'})}
$savedCurl=${function:Invoke-ModCurl}
try {
    function Invoke-ModCurl([string]$Url,[string]$Destination){Write-ModJson $Destination $script:fixtureMetadata}
    $download=Get-ModUpdateDownload 'https://github.com/owner/repo/releases/latest' $work
    Assert-Result ($download.sha256 -eq (Get-ModHash $zip) -and $download.checksum_url -eq 'https://example.test/release.zip.sha256') 'Release asset/digest/checksum discovery failed.'
    $script:fixtureMetadata.assets+=@{name='wo4k-second.zip';browser_download_url='https://example.test/second.zip'}
    $failed=$false;try{Get-ModUpdateDownload 'https://github.com/owner/repo/releases/latest' $work|Out-Null}catch{$failed=$true};Assert-Result $failed 'Ambiguous ZIP release was accepted.'
} finally {Set-Item Function:Invoke-ModCurl $savedCurl}

$keep=[ordered]@{'release.url'='https://example.test/custom.zip';'source.url'='https://github.com/custom/project';'wobble/CONFIG.DAT'='my controls';'wobble/Wipeout2097.ini'='my preferences';'wobble/music/pc/track02.wav'='my music';'wobble/audio/ENGINES.WAV'='my effects'}
foreach($item in $keep.GetEnumerator()){$file=Get-ModChild $fixture $item.Key;[WoBeta.Files]::Parent($file);[IO.File]::WriteAllText($file,$item.Value)}
[IO.File]::WriteAllText((Join-Path $fixture 'Setup.ps1'),'old setup');[IO.File]::WriteAllText((Join-Path $game 'wohr.dll'),'old runtime')
Write-ModJson (Join-Path $game 'installation.json') @{version='0.1.0-beta.2';hd_textures=$false;default_soundtrack='Psx';clean_audio=$true}
$count=Install-ModUpdate $fixture $verified $inventory $work
Assert-Result ($count -gt 0 -and [IO.File]::ReadAllText((Join-Path $game 'wohr.dll')) -eq 'new runtime') 'Installed runtime was not updated.'
foreach($item in $keep.GetEnumerator()){Assert-Result ([IO.File]::ReadAllText((Get-ModChild $fixture $item.Key)) -eq $item.Value) ('Update changed user data: '+$item.Key)}
$receipt=Get-Content -LiteralPath (Join-Path $game 'installation.json') -Encoding UTF8 -Raw|ConvertFrom-Json
Assert-Result ($receipt.version -eq '0.1.0-beta.3' -and $receipt.default_soundtrack -eq 'Psx') 'Update lost soundtrack preference.'
$backups=@(Get-ChildItem -LiteralPath (Join-Path $fixture 'backups') -Directory)
Assert-Result ($backups.Count -eq 1 -and [IO.File]::ReadAllText((Join-Path $backups[0].FullName 'Setup.ps1')) -eq 'old setup') 'Changed files were not backed up.'
$fresh=Join-Path $work 'fresh mod';[IO.Directory]::CreateDirectory($fresh)|Out-Null
Install-ModUpdate $fresh $verified $inventory $work|Out-Null
Assert-Result ([IO.File]::Exists((Join-Path $fresh 'release.url')) -and [IO.File]::Exists((Join-Path $fresh 'source.url'))) 'Missing URL templates were not installed.'

# A late copy failure must roll back all files already replaced.
[IO.File]::WriteAllText((Join-Path $verified 'Setup.ps1'),'next setup')
$rollback=Join-Path $work 'rollback';[IO.Directory]::CreateDirectory($rollback)|Out-Null;[IO.File]::WriteAllText((Join-Path $rollback 'Setup.ps1'),'retained setup')
$bad=[pscustomobject]@{version='0.1.0-beta.4';files=@([pscustomobject]@{path='Setup.ps1';sha256=(Get-ModHash (Join-Path $verified 'Setup.ps1'))},[pscustomobject]@{path='Play.ps1';sha256=('0'*64)})}
$failed=$false;try{Install-ModUpdate $rollback $verified $bad $work|Out-Null}catch{$failed=$true}
Assert-Result ($failed -and [IO.File]::ReadAllText((Join-Path $rollback 'Setup.ps1')) -eq 'retained setup' -and -not [IO.File]::Exists((Join-Path $rollback 'Play.ps1'))) 'Copy failure did not roll back the update.'

# Corrupted inventory payload and undeclared files never reach installation.
$badZip=Join-Path $work 'bad.zip';[IO.Compression.ZipFile]::CreateFromDirectory($verified,$badZip)
$failed=$false;try{Read-ModUpdatePackage $badZip (Join-Path $work 'bad-stage')|Out-Null}catch{$failed=$true};Assert-Result $failed 'Corrupted release passed validation.'
$extraZip=Join-Path $work 'extra.zip';[IO.File]::Copy($zip,$extraZip)
$archive=[IO.Compression.ZipFile]::Open($extraZip,[IO.Compression.ZipArchiveMode]::Update)
try{$extra=$archive.CreateEntry('unexpected.txt');$stream=$extra.Open();try{$stream.WriteByte(1)}finally{$stream.Dispose()}}finally{$archive.Dispose()}
$failed=$false;try{Read-ModUpdatePackage $extraZip (Join-Path $work 'extra-stage')|Out-Null}catch{$failed=$true};Assert-Result $failed 'Undeclared release payload was accepted.'

# Exercise the actual WPF/XAML with configured and disabled links, offline.
$uiRoot=Join-Path $work 'setup fixture';[IO.Directory]::CreateDirectory((Join-Path $uiRoot 'scripts'))|Out-Null
Copy-ModTree (Join-Path $root 'scripts') (Join-Path $uiRoot 'scripts')
[IO.File]::Copy((Join-Path $root 'Setup.ps1'),(Join-Path $uiRoot 'Setup.ps1'))
foreach($scenario in @('Missing','Empty','Corrupt','Valid')){
    if($scenario -ne 'Missing'){
        $releaseText=if($scenario -eq 'Valid'){'https://github.com/owner/repo/releases/latest'}elseif($scenario -eq 'Corrupt'){'damaged file'}else{''}
        $sourceText=if($scenario -eq 'Valid'){'https://github.com/owner/repo'}elseif($scenario -eq 'Corrupt'){'not github'}else{''}
        [IO.File]::WriteAllText((Join-Path $uiRoot 'release.url'),$releaseText);[IO.File]::WriteAllText((Join-Path $uiRoot 'source.url'),$sourceText)
    }
    & powershell.exe -NoProfile -STA -ExecutionPolicy Bypass -File (Join-Path $uiRoot 'Setup.ps1') -ValidateUi
    if($LASTEXITCODE){throw ('GUI URL configuration state failed: '+$scenario)}
}
Write-Host 'PASS: URL validity/disabled states, release metadata, semantic versions, package hashes, Unicode-root installation, backups, user-data preservation and rollback.'
