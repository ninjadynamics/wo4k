function Get-ModSourceMessage([string]$GamePath,[string]$PsxPath,[bool]$GameVerified,[bool]$PsxVerified,[bool]$SourceError=$false,[string]$IssueMessage='',[bool]$InstalledReady=$false) {
    if($IssueMessage){return $IssueMessage}
    if($SourceError){return 'The last operation did not complete. Run Install / repair to check the game files again.'}
    if(-not $GamePath){return 'Please provide the path(s) to your WipeOut 2097 / XL game files.'}
    if($GamePath -and -not $GameVerified){
        if(-not (Test-Path -LiteralPath $GamePath)){
            if($InstalledReady){return 'The installed game is ready. Its Windows source is unavailable; locate the original ZIP, disc or folder before reinstalling.'}
            return 'The Windows game source is missing. Choose the current location of your PC game ZIP, disc or folder.'
        }
        return 'The Windows source could not be verified. Select an original supported WipEout 2097 / XL PC game ZIP, disc or folder.'
    }
    if($PsxPath -and -not $PsxVerified){
        if(-not (Test-Path -LiteralPath $PsxPath)){
            if($InstalledReady){return 'The installed game is ready. Its PlayStation source is unavailable; locate it to restore missing artwork or music.'}
            return 'The PlayStation source is missing. Choose its current location, or clear the optional field.'
        }
        return 'The PlayStation source could not be verified. Select the European WipEout 2097 disc or a complete extracted soundtrack folder.'
    }
    if(-not $PsxPath){return 'Add the PlayStation version for original artwork and its soundtrack.'}
    return 'Ready to launch, all systems go!'
}
