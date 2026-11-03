$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot '../scripts/Game-Settings.ps1')
$work=Join-Path $PSScriptRoot ('../.work/game-settings-'+[guid]::NewGuid().ToString('N'))
$settings=Join-Path $work 'Wipeout2097.ini'
try{
    if((Get-ModSoundMode $settings) -ne 2){throw 'Missing settings must default to Stereo.'}
    Set-ModSoundMode $settings 5
    if((Get-ModSoundMode $settings) -ne 5){throw 'New settings did not persist Surround.'}
    $fixture="[Render]`r`nSoundMode=5`r`nShowFPS=1`r`n[Audio]`r`nSoundtrack=1`r`nSoundMode=9`r`nMusicVolume=73`r`n[Other]`r`nSoundMode=5`r`n"
    [IO.File]::WriteAllText($settings,$fixture)
    if((Get-ModSoundMode $settings) -ne 2){throw 'Invalid sound mode must default to Stereo.'}
    foreach($mode in 1,2,5){
        Set-ModSoundMode $settings $mode
        if((Get-ModSoundMode $settings) -ne $mode){throw 'Audio output did not roundtrip.'}
        if([IO.File]::ReadAllText($settings) -ne $fixture.Replace('SoundMode=9','SoundMode='+$mode)){throw 'Saving audio output changed another preference or section.'}
    }
    [IO.File]::WriteAllText($settings,"[Audio]`r`nSoundMode=1`r`nSoundMode=5`r`n")
    Set-ModSoundMode $settings 2
    if(([regex]::Matches([IO.File]::ReadAllText($settings),'SoundMode=')).Count -ne 1){throw 'Duplicate mode keys should collapse to one value.'}
    $graphics=Join-Path $work 'DDrawCompat-Wipeout2097.ini'
    if((Get-ModFullscreenMode $graphics) -ne 'borderless'){throw 'Missing graphics settings must default to Borderless.'}
    Set-ModFullscreenMode $graphics exclusive
    if((Get-ModFullscreenMode $graphics) -ne 'exclusive'){throw 'Exclusive choice did not persist.'}
    $graphicsFixture="# Keep graphics tuning`r`nDisplayResolution = desktop`r`nFullscreenMode = exclusive(0)`r`nVSync = on(1)`r`nTextureFilter = af16x`r`n"
    [IO.File]::WriteAllText($graphics,$graphicsFixture)
    if((Get-ModFullscreenMode $graphics) -ne 'exclusive'){throw 'Existing exclusive VSync override should restore as Exclusive.'}
    foreach($mode in 'borderless','exclusive'){
        Set-ModFullscreenMode $graphics $mode
        if((Get-ModFullscreenMode $graphics) -ne $mode){throw 'Fullscreen mode did not roundtrip.'}
        if([IO.File]::ReadAllText($graphics) -ne $graphicsFixture.Replace('exclusive(0)',$mode)){throw 'Fullscreen choice changed another graphics setting.'}
    }
    [IO.File]::WriteAllText($graphics,"FullscreenMode = invalid`r`n")
    if((Get-ModFullscreenMode $graphics) -ne 'borderless'){throw 'Invalid fullscreen mode must default to Borderless.'}
    [IO.File]::WriteAllText($graphics,"FullscreenMode = exclusive`r`nFullscreenMode = borderless`r`n")
    Set-ModFullscreenMode $graphics exclusive
    if(([regex]::Matches([IO.File]::ReadAllText($graphics),'FullscreenMode\s*=')).Count -ne 1){throw 'Duplicate fullscreen keys were not removed.'}
    Write-Host 'PASS: audio and fullscreen choices persist with safe defaults; existing exclusive VSync overrides are recognized; unrelated audio/graphics preferences are preserved and duplicate keys are removed.'
}finally{
    $full=[IO.Path]::GetFullPath($work);$allowed=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../.work')).TrimEnd('\')+'\game-settings-'
    if(-not $full.StartsWith($allowed,[StringComparison]::OrdinalIgnoreCase)){throw 'Unsafe settings-test cleanup.'}
    if([IO.Directory]::Exists($full)){[IO.Directory]::Delete($full,$true)}
}
