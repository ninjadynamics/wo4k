. (Join-Path $PSScriptRoot '../scripts/Source-Status.ps1')
$cases=@(
    @{game='';psx='';gv=$false;pv=$false;error=$false;expected='Please provide the path(s) to your WipeOut 2097 / XL game files.'},
    @{game='pc.zip';psx='';gv=$true;pv=$false;error=$false;expected='Add the PlayStation version for original artwork and its soundtrack.'},
    @{game='pc.zip';psx='ps1.zip';gv=$true;pv=$true;error=$false;expected='Ready to launch, all systems go!'},
    @{game='bad.zip';psx='';gv=$false;pv=$false;error=$false;expected="There’s an issue with one or more game files"},
    @{game='pc.zip';psx='bad.zip';gv=$true;pv=$false;error=$false;expected="There’s an issue with one or more game files"},
    @{game='pc.zip';psx='ps1.zip';gv=$true;pv=$true;error=$true;expected="There’s an issue with one or more game files"}
)
foreach($case in $cases){
    $actual=Get-ModSourceMessage $case.game $case.psx $case.gv $case.pv $case.error
    if($actual -cne $case.expected){throw "Incorrect source status: $actual"}
}
Write-Host 'PASS: empty, optional-PS1, ready, unverified and installation-error source messages.'
