function Get-ModSourceMessage([string]$GamePath,[string]$PsxPath,[bool]$GameVerified,[bool]$PsxVerified,[bool]$SourceError=$false) {
    if($SourceError -or ($GamePath -and -not $GameVerified) -or ($PsxPath -and -not $PsxVerified)) {return "There’s an issue with one or more game files"}
    if(-not $GamePath){return 'Please provide the path(s) to your WipeOut 2097 / XL game files.'}
    if(-not $PsxPath){return 'Add the PlayStation version of WipeOut 2097 / XL to enjoy its soundtrack in-game.'}
    return 'Ready to launch, all systems go!'
}
