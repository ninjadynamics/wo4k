param([Parameter(Mandatory=$true)][string]$SourcePath,[Parameter(Mandatory=$true)][string]$Destination)
. (Join-Path $PSScriptRoot 'Sources.ps1')
$folder=[IO.Path]::GetFullPath($SourcePath)
if(-not [IO.Directory]::Exists($folder)){$folder=Get-ModNativeRoot $folder (Join-Path $script:ModRoot ('.work/audio-source-'+[guid]::NewGuid().ToString('N')))}
if([IO.Directory]::Exists((Join-Path $folder 'audio/source'))){$folder=Join-Path $folder 'audio/source'}elseif([IO.Directory]::Exists((Join-Path $folder 'source'))){$folder=Join-Path $folder 'source'}elseif([IO.Directory]::Exists((Join-Path $folder 'Sound'))){$folder=Join-Path $folder 'Sound'}elseif([IO.Directory]::Exists((Join-Path $folder 'audio'))){$folder=Join-Path $folder 'audio'}
$files=@(Get-ChildItem -LiteralPath $folder -File -Filter '*.wav')
if(-not $files.Count){throw 'No sound effects found in the selected Sound/audio folder.'}
foreach($file in $files){$to=Get-ModChild $Destination $file.Name;[WoBeta.Files]::Parent($to);[IO.File]::Copy($file.FullName,$to,$true)}
return [IO.Path]::GetFullPath($Destination)
