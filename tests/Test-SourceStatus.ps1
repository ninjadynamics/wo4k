. (Join-Path $PSScriptRoot '../scripts/Source-Status.ps1')
$cases=@(
    @{game='';psx='';gv=$false;pv=$false;error=$false;expected='Please provide the path(s) to your WipeOut 2097 / XL game files.'},
    @{game='pc.zip';psx='';gv=$true;pv=$false;error=$false;expected='Add the PlayStation version for original artwork and its soundtrack.'},
    @{game='pc.zip';psx='ps1.zip';gv=$true;pv=$true;error=$false;expected='Ready to launch, all systems go!'},
    @{game='bad.zip';psx='';gv=$false;pv=$false;error=$false;expected='The Windows game source is missing. Choose the current location of your PC game ZIP, disc or folder.'},
    @{game='pc.zip';psx='bad.zip';gv=$true;pv=$false;error=$false;expected='The PlayStation source is missing. Choose its current location, or clear the optional field.'},
    @{game='pc.zip';psx='ps1.zip';gv=$true;pv=$true;error=$true;expected='The last operation did not complete. Run Install / repair to check the game files again.'}
)
foreach($case in $cases){
    $actual=Get-ModSourceMessage $case.game $case.psx $case.gv $case.pv $case.error
    if($actual -cne $case.expected){throw "Incorrect source status: $actual"}
}
Write-Host 'PASS: empty, optional-PS1, ready, unverified and installation-error source messages.'

$detail='One or more track textures are missing or damaged. Run Install / repair.'
if((Get-ModSourceMessage 'pc.zip' 'ps1.zip' $true $true $true $detail) -ne $detail){throw 'The banner hid a specific operation failure.'}
$ready=Get-ModSourceMessage 'missing-pc.zip' '' $false $false $false '' $true
if($ready -notlike 'The installed game is ready.*Windows source is unavailable*'){throw 'An offline original source was reported as damaged installed game data.'}
