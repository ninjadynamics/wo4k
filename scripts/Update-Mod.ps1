param([string]$StatusFile,[switch]$FunctionsOnly,[switch]$NoErrorDialog)
. (Join-Path $PSScriptRoot 'Diagnostics.ps1')
if(-not $FunctionsOnly){$null=Start-WoLog 'update' (Join-Path $PSScriptRoot '..')}
try {
. (Join-Path $PSScriptRoot 'Core.ps1')
. (Join-Path $PSScriptRoot 'Links.ps1')
function Set-UpdateStatus([int]$Percent,[string]$Message,[string]$State='busy') {
    Write-Host $Message
    Write-WoLog 'update_stage' @{percent=$Percent;message=$Message;state=$State}
    if($StatusFile){Write-ModJson $StatusFile ([ordered]@{percent=$Percent;message=$Message;state=$State})}
}
function Invoke-ModCurl([string]$Url,[string]$Destination) {
    $uri=$null
    if(-not [Uri]::TryCreate($Url,[UriKind]::Absolute,[ref]$uri) -or $uri.Scheme -ne 'https' -or $uri.UserInfo -or $Url -match '[\x00-\x20"<>\\]'){throw 'Invalid release download URL.'}
    $curl=Join-Path ([Environment]::SystemDirectory) 'curl.exe'
    if(-not [IO.File]::Exists($curl)){throw 'Windows curl.exe is unavailable.'}
    [WoBeta.Files]::Parent($Destination)
    # No shell is involved. All arguments are single quoted Windows argv items;
    # paths cannot contain quotes and URLs were checked above.
    $arguments=@('-q','--fail','--silent','--show-error','--location','--proto','=https','--proto-redir','=https','--connect-timeout','15','--max-time','240','--max-filesize','268435456','--retry','2','--user-agent','WO4K-Setup','--header','Accept: application/vnd.github+json','--output',$Destination,'--url',$uri.AbsoluteUri)
    $start=New-Object Diagnostics.ProcessStartInfo
    $start.FileName=$curl;$start.UseShellExecute=$false;$start.CreateNoWindow=$true
    $start.RedirectStandardError=$true;$start.RedirectStandardOutput=$true
    $start.Arguments=($arguments|ForEach-Object {'"'+$_+'"'}) -join ' '
    $process=New-Object Diagnostics.Process;$process.StartInfo=$start
    try {
        if(-not $process.Start()){throw 'Could not start curl.'}
        $errors=$process.StandardError.ReadToEnd();$process.StandardOutput.ReadToEnd()|Out-Null;$process.WaitForExit()
        if($process.ExitCode -ne 0){throw ('Download failed (curl '+$process.ExitCode+'): '+$errors.Trim())}
    } finally {$process.Dispose()}
}
function Compare-ModReleaseVersion([string]$A,[string]$B) {
    $pattern='^v?(\d+\.\d+\.\d+)(?:-([0-9A-Za-z.-]+))?(?:\+[0-9A-Za-z.-]+)?$'
    if($A -notmatch $pattern){throw 'Invalid release version.'};$aBase=[version]$Matches[1];$aPre=$Matches[2]
    if($B -notmatch $pattern){throw 'Invalid installed version.'};$bBase=[version]$Matches[1];$bPre=$Matches[2]
    $order=$aBase.CompareTo($bBase);if($order){return $order}
    if(-not $aPre -and -not $bPre){return 0};if(-not $aPre){return 1};if(-not $bPre){return -1}
    $aParts=$aPre.Split('.');$bParts=$bPre.Split('.')
    for($i=0;$i -lt [Math]::Min($aParts.Count,$bParts.Count);$i++){
        $left=$aParts[$i];$right=$bParts[$i]
        $ln=$left -match '^\d+$';$rn=$right -match '^\d+$'
        if($ln -and $rn){$order=([decimal]$left).CompareTo([decimal]$right)}elseif($ln){$order=-1}elseif($rn){$order=1}else{$order=[string]::CompareOrdinal($left,$right)}
        if($order){return $order}
    }
    return $aParts.Count.CompareTo($bParts.Count)
}
function Get-ModUpdateDownload([string]$Url,[string]$Session) {
    $endpoint=Get-ModReleaseEndpoint $Url;if(-not $endpoint){throw 'No valid release URL is configured.'}
    if(([Uri]$endpoint).AbsolutePath -match '(?i)\.zip$'){return [pscustomobject]@{url=$endpoint;sha256=$null;checksum_url=$null}}
    $metadata=Join-Path $Session 'release.json';Invoke-ModCurl $endpoint $metadata
    $release=Get-Content -LiteralPath $metadata -Encoding UTF8 -Raw|ConvertFrom-Json
    if($release.draft){throw 'The release is still a draft.'}
    $assets=@($release.assets|Where-Object {$_.name -match '(?i)^wo(?:4k|[-_]beta[-_]mod)[A-Za-z0-9._-]*\.zip$'})
    if($assets.Count -ne 1){throw 'The latest release must contain exactly one WO4K mod ZIP asset.'}
    $asset=$assets[0];$hash=$null;$checksum=$null
    if($asset.PSObject.Properties['digest'] -and $asset.digest -match '^sha256:([0-9a-fA-F]{64})$'){$hash=$Matches[1].ToLowerInvariant()}
    $sidecars=@($release.assets|Where-Object {$_.name -ceq ($asset.name+'.sha256')})
    if($sidecars.Count -eq 1){$checksum=$sidecars[0].browser_download_url}
    return [pscustomobject]@{url=$asset.browser_download_url;sha256=$hash;checksum_url=$checksum}
}
function Read-ModUpdatePackage([string]$Zip,[string]$Stage) {
    $archive=[IO.Compression.ZipFile]::OpenRead($Zip)
    try {
        $seen=@{};$total=[long]0
        foreach($entry in $archive.Entries){
            $path=$entry.FullName.Replace('\','/')
            if($path.EndsWith('/')){continue}
            if($seen.ContainsKey($path)){throw 'Duplicate update package entry.'};$seen[$path]=$true
            $total+=$entry.Length;if($total -gt 512MB -or $seen.Count -gt 2000){throw 'Update package exceeds its size limit.'}
        }
    } finally {$archive.Dispose()}
    Expand-ModZip $Zip $Stage
    $file=Join-Path $Stage 'release-files.json'
    if(-not [IO.File]::Exists($file)){throw 'The release ZIP has no file inventory.'}
    $inventory=Get-Content -LiteralPath $file -Encoding UTF8 -Raw|ConvertFrom-Json
    Compare-ModReleaseVersion $inventory.version '0.0.0'|Out-Null
    $declared=@{};$declared['release-files.json']=$true
    $top=@('Setup.ps1','Play.ps1','Setup.exe','Play.exe','README.md','LICENSE','.gitignore','release.url','source.url','Makefile','VERSION','compile.ps1','HOW-TO-COMPILE.md')
    foreach($entry in $inventory.files){
        $path=[string]$entry.path
        if(-not $path -or $path -match '(^/|\\|:|(^|/)\.\.?(/|$)|[\x00-\x1f])' -or $declared.ContainsKey($path)){throw 'Invalid release inventory path.'}
        if($path -notin $top -and $path -notmatch '^(scripts|src|runtime|assets|manifests|licenses|docs|tests|patches)/'){throw "Unexpected update payload: $path"}
        $extension=[IO.Path]::GetExtension($path).ToLowerInvariant()
        if($extension -in '.mpg','.mpeg','.mp4','.avi','.wmv','.wav','.mp3','.iso','.mdf','.mds','.bin','.tga','.tim','.cmp','.ppm','.wad','.swp','.dat','.py','.cmd','.bat','.obj','.lib'){throw "Forbidden update payload: $path"}
        if($extension -eq '.exe' -and $path -notin 'Setup.exe','Play.exe','runtime/wo2097-controller.exe','runtime/audio-clean.exe'){throw 'Unexpected executable in update.'}
        if($extension -eq '.dll' -and $path -notin 'runtime/wohr.dll','runtime/ddraw.dll','runtime/libiconv-2.dll'){throw 'Unexpected library in update.'}
        $payload=Get-ModChild $Stage $path
        if(-not [IO.File]::Exists($payload) -or $entry.sha256 -notmatch '^[0-9a-fA-F]{64}$' -or (Get-ModHash $payload) -ne $entry.sha256 -or (Get-Item -LiteralPath $payload).Length -ne $entry.bytes){throw "Release checksum failed: $path"}
        $declared[$path]=$true
    }
    if($declared.Count -ne $seen.Count){throw 'Release ZIP contains undeclared files.'}
    foreach($path in $seen.Keys){if(-not $declared.ContainsKey($path)){throw 'Release ZIP contains undeclared files.'}}
    foreach($required in @('Setup.ps1','Play.ps1','Setup.exe','Play.exe','scripts/Core.ps1','scripts/Diagnostics.ps1','runtime/wohr.dll','manifests/patches.json')){if(-not $declared.ContainsKey($required)){throw "Incomplete release: $required"}}
    return $inventory
}
function Assert-ModUpdateLocalPath([string]$Root,[string]$Path) {
    $full=[IO.Path]::GetFullPath($Path)
    if(-not $full.StartsWith($Root+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Update target is outside the mod folder.'}
    while($full -and $full.Length -ge $Root.Length){
        if(([IO.File]::Exists($full) -or [IO.Directory]::Exists($full)) -and ([IO.File]::GetAttributes($full) -band [IO.FileAttributes]::ReparsePoint)){throw 'An update path contains a filesystem link.'}
        $full=[IO.Path]::GetDirectoryName($full)
    }
}
function Install-ModUpdate([string]$Root,[string]$Stage,$Inventory,[string]$Session) {
    $Root=[IO.Path]::GetFullPath($Root).TrimEnd('\')
    $game=Join-Path $Root 'wobble';$installed=[IO.File]::Exists((Join-Path $game 'Wipeout2097.exe'))
    foreach($process in @(Get-Process Wipeout2097 -ErrorAction SilentlyContinue)){
        if(-not $process.Path -or $process.Path.StartsWith($Root+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Close the game before updating the mod.'}
    }
    if($installed){
        $patches=Get-Content -LiteralPath (Join-Path $Stage 'manifests/patches.json') -Encoding UTF8 -Raw|ConvertFrom-Json
        if((Get-ModHash (Join-Path $game 'Wipeout2097.exe')) -ne $patches.output_sha256){throw "This release requires Install / repair with your original game source. The downloaded ZIP is retained in $Session"}
    }
    $plan=New-Object Collections.Generic.List[object]
    foreach($entry in $Inventory.files){
        if($entry.path -in 'release.url','source.url' -and [IO.File]::Exists((Get-ModChild $Root $entry.path))){continue}
        $plan.Add([pscustomobject]@{source=(Get-ModChild $Stage $entry.path);relative=$entry.path;hash=$entry.sha256})
    }
    if($installed){
        foreach($entry in $Inventory.files|Where-Object {$_.path -match '^runtime/(wohr\.dll|ddraw\.dll|wo2097-controller\.exe|lib[^/]+\.dll)$'}){$plan.Add([pscustomobject]@{source=(Get-ModChild $Stage $entry.path);relative=('wobble/'+[IO.Path]::GetFileName($entry.path));hash=$entry.sha256})}
        foreach($name in @('w2097-hd.cg','w2097-hd.cgp')){$path=Join-Path $Stage ('runtime/'+$name);if([IO.File]::Exists($path)){$plan.Add([pscustomobject]@{source=$path;relative=('wobble/common-shaders-master/w2097/'+$name);hash=(Get-ModHash $path)})}}
        $receiptPath=Join-Path $game 'installation.json'
        if([IO.File]::Exists($receiptPath)){
            $receipt=Get-Content -LiteralPath $receiptPath -Encoding UTF8 -Raw|ConvertFrom-Json
            $receipt.version=$Inventory.version;$nextReceipt=Join-Path $Session 'installation.json';Write-ModJson $nextReceipt $receipt
            $plan.Add([pscustomobject]@{source=$nextReceipt;relative='wobble/installation.json';hash=(Get-ModHash $nextReceipt)})
        }
    }
    # The inventory is committed last. Source/disc/audio/save/config paths never
    # enter the plan. Only files with new content need copying or rollback.
    $plan.Add([pscustomobject]@{source=(Join-Path $Stage 'release-files.json');relative='release-files.json';hash=(Get-ModHash (Join-Path $Stage 'release-files.json'))})
    $backup=Join-Path $Root ('backups/update-'+(Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
    $changes=New-Object Collections.Generic.List[object]
    foreach($entry in $plan){
        $target=Get-ModChild $Root $entry.relative
        Assert-ModUpdateLocalPath $Root $target
        if([IO.File]::Exists($target) -and (Get-ModHash $target) -eq $entry.hash){continue}
        $exists=[IO.File]::Exists($target)
        if($exists){$handle=[IO.File]::Open($target,[IO.FileMode]::Open,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None);$handle.Dispose();$copy=Get-ModChild $backup $entry.relative;Assert-ModUpdateLocalPath $Root $copy;[WoBeta.Files]::Parent($copy);[IO.File]::Copy($target,$copy,$false)}
        $changes.Add([pscustomobject]@{entry=$entry;target=$target;existed=$exists})
    }
    $applied=New-Object Collections.Generic.List[object]
    try {
        foreach($change in $changes){
            Assert-ModUpdateLocalPath $Root $change.target
            [WoBeta.Files]::Parent($change.target);$applied.Add($change)
            [IO.File]::Copy($change.entry.source,$change.target,$true)
            if((Get-ModHash $change.target) -ne $change.entry.hash){throw 'Updated file verification failed.'}
        }
    } catch {
        for($i=$applied.Count-1;$i -ge 0;$i--){$change=$applied[$i];if($change.existed){[IO.File]::Copy((Get-ModChild $backup $change.entry.relative),$change.target,$true)}elseif([IO.File]::Exists($change.target)){[IO.File]::Delete($change.target)}}
        throw
    }
    return $changes.Count
}
if($FunctionsOnly){return}
$session=Join-Path $script:ModRoot ('.work/update-'+[guid]::NewGuid().ToString('N').Substring(0,16))
    $url=Get-ModConfiguredUrl (Join-Path $script:ModRoot 'release.url') Release
    if(-not $url){throw 'Update is unavailable: release.url is missing, empty or invalid.'}
    [IO.Directory]::CreateDirectory($session)|Out-Null
    Set-UpdateStatus 5 'Checking the latest release…'
    $download=Get-ModUpdateDownload $url $session
    $zip=Join-Path $session 'release.zip';Set-UpdateStatus 20 'Downloading the latest mod release…';Invoke-ModCurl $download.url $zip
    $expected=$download.sha256
    if($download.checksum_url){$checksum=Join-Path $session 'release.sha256';Invoke-ModCurl $download.checksum_url $checksum;$text=[IO.File]::ReadAllText($checksum).Trim();if($text -notmatch '^([0-9a-fA-F]{64})(?:\s+[^\r\n]+)?$'){throw 'Invalid release checksum file.'};$sidecarHash=$Matches[1].ToLowerInvariant();if($expected -and $expected -ne $sidecarHash){throw 'Release checksums disagree.'};$expected=$sidecarHash}
    if($expected -and (Get-ModHash $zip) -ne $expected){throw 'Downloaded ZIP checksum failed.'}
    Set-UpdateStatus 55 'Verifying the release package…'
    $stage=Join-Path $session 'package';$inventory=Read-ModUpdatePackage $zip $stage
    $current=Join-Path $script:ModRoot 'release-files.json'
    if([IO.File]::Exists($current)){$previous=Get-Content -LiteralPath $current -Encoding UTF8 -Raw|ConvertFrom-Json;if((Compare-ModReleaseVersion $inventory.version $previous.version) -le 0){Set-UpdateStatus 100 ('You already have the latest release ('+$previous.version+').') complete;return}}
    Set-UpdateStatus 75 'Installing the update; keeping your game files and preferences…'
    $count=Install-ModUpdate $script:ModRoot $stage $inventory $session
    Set-UpdateStatus 100 ('Updated to '+$inventory.version+'. Reopen Setup to load the updated interface.') complete
} catch {
    if(-not $FunctionsOnly){Complete-WoFailure $_ 'Update failed' -StatusFile $StatusFile -NoDialog:$NoErrorDialog}
    throw
}
