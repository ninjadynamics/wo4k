param([ValidateSet('all','deps','runtime','controller','audio','launchers','patches','test','package','release','clean')][string]$Target='all',[string]$CompilerDirectory='',[string]$Version='')
$ErrorActionPreference='Stop';$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$sourceVersion=[IO.File]::ReadAllText((Join-Path $root 'VERSION')).Trim()
if(-not $Version){$Version=$sourceVersion}
$Version=$Version -replace '^v',''
if($Version -ne $sourceVersion){throw 'Update VERSION before overriding the release version; installer and package versions must match.'}
if($Version -notmatch '^\d+\.\d+\.\d+(?:-[0-9A-Za-z.-]+)?$'){throw 'Invalid build version.'}
if($Target -eq 'clean'){
    # Delete only named generated files, never local games, assets or toolchains.
    $generated=@('Setup.exe','Play.exe','release-files.json','runtime/wohr.dll','runtime/audio-clean.exe','runtime/wo2097-controller.exe','runtime/ddraw.dll','runtime/libiconv-2.dll','assets/icons/setup.ico','assets/icons/play.ico')
    foreach($relative in $generated){$path=Join-Path $root $relative;if([IO.File]::Exists($path)){[IO.File]::Delete($path)}}
    foreach($file in @(Get-ChildItem (Join-Path $root 'patches') -File -Filter '*.wodiff' -ErrorAction SilentlyContinue)){[IO.File]::Delete($file.FullName)}
    return
}
if($Target -notin 'deps','patches','package'){
    if(-not $CompilerDirectory){$CompilerDirectory=Split-Path (Get-Command gcc.exe -ErrorAction Stop).Source}
    $CompilerDirectory=[IO.Path]::GetFullPath($CompilerDirectory)
    $gcc=Join-Path $CompilerDirectory 'gcc.exe'
    $headerCandidates=@((Join-Path $CompilerDirectory '../x86_64-w64-mingw32/include'),(Join-Path $CompilerDirectory '../include'))
    $headers=@($headerCandidates|Where-Object {Test-Path -LiteralPath (Join-Path $_ 'windows.h')}|Select-Object -First 1)
    if(-not $headers.Count){throw 'Cannot find MinGW Windows headers beside the compiler.'}
}
function Run-BuildScript([string]$Name,[hashtable]$Arguments=@{}){& (Join-Path $PSScriptRoot $Name) @Arguments}
if($Target -in 'all','release','deps','controller') {Run-BuildScript 'Fetch-Dependencies.ps1'}
if($Target -in 'all','release','runtime'){Run-BuildScript 'Build-Runtime.ps1' @{CompilerDirectory=$CompilerDirectory;HeaderDirectory=$headers[0]}}
if($Target -in 'all','release','controller'){Run-BuildScript 'Build-Controller.ps1' @{Compiler=$gcc;SdlDirectory=(Join-Path $root '.work/deps/mingw64');LibraryDirectory=(Join-Path $root '.work/deps/mingw64/lib')}}
if($Target -in 'all','release','audio'){Run-BuildScript 'Build-Audio.ps1' @{Compiler=$gcc}}
if($Target -in 'all','release','launchers'){Run-BuildScript 'Build-Launchers.ps1' @{Compiler=$gcc}}
if($Target -in 'all','release','patches'){Run-BuildScript 'Build-Patches.ps1'}
if($Target -in 'all','release'){Run-BuildScript 'Update-Manifests.ps1'}
if($Target -in 'test','release'){
    foreach($test in @('Test-Diagnostics.ps1','Test-SourceStatus.ps1','Test-UiHelpers.ps1','Test-SetupModes.ps1','Test-SetupRestart.ps1','Test-GameSettings.ps1','Test-Configuration.ps1','Test-HdAssets.ps1','Test-Layout.ps1','Test-Installer.ps1','Test-Updates.ps1')){
        & powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root ('tests/'+$test))
        if($LASTEXITCODE){throw "Test failed: $test"}
    }
}
if($Target -in 'package','release'){Run-BuildScript 'Build-Release.ps1' @{Version=$Version}}
