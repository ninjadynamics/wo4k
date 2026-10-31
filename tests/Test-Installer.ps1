param([switch]$UiOnly)
. (Join-Path $PSScriptRoot '../scripts/Core.ps1')
$root=$script:ModRoot
foreach($file in Get-ChildItem -LiteralPath $root -File -Recurse -Filter '*.ps1' | Where-Object {$_.FullName -notlike '*\.work\*' -and $_.FullName -notlike '*\wobble\*' -and $_.FullName -notlike '*\backups\*'}){
    $tokens=$null;$errors=$null;[Management.Automation.Language.Parser]::ParseFile($file.FullName,[ref]$tokens,[ref]$errors)|Out-Null
    if($errors.Count){throw ($file.Name+': '+($errors|Out-String))}
}
foreach($state in @('Idle','Searching','Updating')){& powershell.exe -NoProfile -STA -ExecutionPolicy Bypass -File (Join-Path $root 'Setup.ps1') -ValidateUi -PreviewState $state;if($LASTEXITCODE){throw 'UI state validation failed.'}}
if($UiOnly){return}
$work=Join-Path $root ('.work/installer-tests-'+[guid]::NewGuid().ToString('N'));[IO.Directory]::CreateDirectory($work)|Out-Null
$empty=Join-Path $work 'empty';[IO.Directory]::CreateDirectory($empty)|Out-Null;$out=Join-Path $work 'empty-result.json'
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root 'scripts/Discover-Sources.ps1') -OutputFile $out -SearchRoot $empty
if($LASTEXITCODE){throw 'Empty-source discovery failed.'};$found=Get-Content -Encoding UTF8 -LiteralPath $out -Raw|ConvertFrom-Json
if($found.Game -or $found.Pc -or $found.Psx){throw 'Empty discovery must remain empty.'}
$zip=Join-Path $work 'unsafe.zip';$archive=[IO.Compression.ZipFile]::Open($zip,[IO.Compression.ZipArchiveMode]::Create)
try{$entry=$archive.CreateEntry('../escaped.txt');$s=$entry.Open();try{$s.WriteByte(1)}finally{$s.Dispose()}}finally{$archive.Dispose()}
$rejected=$false;try{Expand-ModZip $zip (Join-Path $work 'extract')}catch{$rejected=$true};if(-not $rejected -or [IO.File]::Exists((Join-Path $work 'escaped.txt'))){throw 'ZIP traversal was not rejected.'}
$a=Join-Path $work 'a.dat';$b=Join-Path $work 'b.dat';$p=Join-Path $work 'delta.wodiff';$c=Join-Path $work 'c.dat'
[IO.File]::WriteAllBytes($a,[byte[]](1,2,3,4,5));[IO.File]::WriteAllBytes($b,[byte[]](1,4,3,9,5,6,7));[WoBeta.Files]::Delta($a,$b,$p)|Out-Null;[WoBeta.Files]::Apply($a,$p,$c)
if((Get-ModHash $c) -ne (Get-ModHash $b)){throw 'Binary difference roundtrip failed.'}
[IO.File]::WriteAllBytes($a,[byte[]](9,2,3,4,5));$rejected=$false;try{[WoBeta.Files]::Apply($a,$p,$c)}catch{$rejected=$true};if(-not $rejected){throw 'Wrong source checksum was accepted.'}
Write-Host 'PASS: script syntax, idle/search UI tooltips, silent empty discovery, traversal rejection and checksum-guarded binary differences.'
