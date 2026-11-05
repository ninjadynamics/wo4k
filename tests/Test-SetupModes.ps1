param([string]$SetupPath)
$ErrorActionPreference='Stop'
function Assert($Condition,[string]$Message){if(-not $Condition){throw $Message}}
if($SetupPath){
    # Run the actual WPF controls and click handler, without showing a window
    # or allowing installation, launch, discovery or preference writes.
    . $SetupPath -ValidateUi
    function Visible-Actions {return @($window.FindName('Actions').Children|Where-Object Visibility -eq Visible|ForEach-Object {[string]$_.Content}) -join ','}
    Assert ((Visible-Actions) -eq 'Update,Install') 'First installation must expose exactly Update and Install.'
    Assert ($window.FindName('SourceFields').Visibility -eq 'Visible') 'First installation hid the source selectors.'
    # Exercise the actual discovery-completion timer that previously left the
    # Looking... message behind after enabling the buttons again.
    $script:scan=[pscustomobject]@{HasExited=$true};$script:scan|Add-Member ScriptMethod Dispose {}
    $script:scanResult=Join-Path $script:ModRoot '.work/fixture-discovery.json';$script:scanMode='All'
    Write-ModJson $script:scanResult @{Game='';Psx=''}
    $script:status.Text='Looking for your game files...';Set-GuiState Searching
    $script:modeFrame=New-Object Windows.Threading.DispatcherFrame
    $script:pumpCount=0;$pump=New-Object Windows.Threading.DispatcherTimer;$pump.Interval=[TimeSpan]::FromMilliseconds(50)
    $pump.Add_Tick({++$script:pumpCount;if(-not $script:scan -or $script:pumpCount -ge 60){$script:modeFrame.Continue=$false}})
    $timer.Start();$pump.Start()
    try{[Windows.Threading.Dispatcher]::PushFrame($script:modeFrame)}finally{$pump.Stop();$timer.Stop();$sourceTimer.Stop()}
    Assert (-not $script:scan -and $script:status.Text -eq 'Ready') 'Completed discovery left a stale progress message.'
    $script:gameVerified=$true;Update-SourceMessage
    Assert ($window.FindName('SourceHint').Text -eq 'Sources are ready. Choose Install to set up the game.') 'First-time setup claimed the game could launch before installation.'
    $game=Join-Path $script:ModRoot 'wobble';[IO.Directory]::CreateDirectory($game)|Out-Null
    [IO.File]::WriteAllText((Join-Path $game 'Wipeout2097.exe'),'fixture: never launch')
    Write-ModJson (Join-Path $game 'installation.json') @{default_soundtrack='None';hd_textures=$true;clean_audio=$true}
    $script:installedReady=$true;Set-GuiState Idle
    Assert ((Visible-Actions) -eq 'Update,Repair,Play') 'Installed controls did not switch to Update, Repair and Play.'
    Assert ($window.FindName('SourceFields').Visibility -eq 'Collapsed') 'Successful first installation did not hide the selectors.'
    $script:install.RaiseEvent((New-Object Windows.RoutedEventArgs([Windows.Controls.Button]::ClickEvent)))
    Assert ($script:repairMode -and $window.FindName('SourceFields').Visibility -eq 'Visible') 'Repair did not reveal both source selectors.'
    Assert ((Visible-Actions) -eq 'Update,Cancel,Install' -and -not $script:worker) 'Repair did not show Cancel and Install without starting work.'
    Assert ($script:install.Background.ToString() -eq '#FFF2C94C') 'Cancel must be yellow.'
    # Activate the production hover trigger deterministically without moving
    # the user's mouse. Local dark text on Cancel must not override its white
    # hovered label, nor the icon/label content inside other buttons.
    $button=$script:install;$normalTemplate=$button.Template
    [xml]$hoverMarkup=[Windows.Markup.XamlWriter]::Save($normalTemplate)
    foreach($trigger in $hoverMarkup.SelectNodes('//*[local-name()="Trigger"]')){
        if($trigger.GetAttribute('Property') -like '*IsMouseOver'){
            foreach($valueNode in @($trigger.ChildNodes|Where-Object LocalName -eq 'Trigger.Value')){$trigger.RemoveChild($valueNode)|Out-Null}
            $trigger.SetAttribute('Value','False')
        }
    }
    $button.Template=[Windows.Markup.XamlReader]::Load((New-Object Xml.XmlNodeReader $hoverMarkup))
    $button.ApplyTemplate()|Out-Null;$button.Measure((New-Object Windows.Size(160,40)));$button.Arrange((New-Object Windows.Rect(0,0,160,40)));$button.UpdateLayout()
    $presenter=$button.Template.FindName('ButtonContent',$button)
    Assert ([Windows.Documents.TextElement]::GetForeground($presenter).Color -eq [Windows.Media.Colors]::White) 'Hover did not turn the actual button label white.'
    function Assert-WhiteButtonText($element){
        $found=0
        if($element -is [Windows.Controls.TextBlock] -or $element -is [Windows.Controls.AccessText]){
            Assert ([Windows.Documents.TextElement]::GetForeground($element).Color -eq [Windows.Media.Colors]::White) 'Rendered highlighted text is not white.';$found++
        }
        for($child=0;$child -lt [Windows.Media.VisualTreeHelper]::GetChildrenCount($element);++$child){$found+=Assert-WhiteButtonText ([Windows.Media.VisualTreeHelper]::GetChild($element,$child))}
        return $found
    }
    Assert ((Assert-WhiteButtonText $presenter) -gt 0) 'No rendered button label was checked.'
    $button.Template=$normalTemplate
    $expectedSource=$script:repairConfiguration.GameSource
    $script:fields.Game.Text='unapplied source edit';$sourceTimer.Stop()
    $script:install.RaiseEvent((New-Object Windows.RoutedEventArgs([Windows.Controls.Button]::ClickEvent)))
    Assert (-not $script:repairMode -and (Visible-Actions) -eq 'Update,Repair,Play' -and -not $script:worker) 'Cancel did not return to the installed view without starting work.'
    Assert ($script:fields.Game.Text -eq $expectedSource) 'Cancel kept unapplied source edits.'
    $script:install.RaiseEvent((New-Object Windows.RoutedEventArgs([Windows.Controls.Button]::ClickEvent)))
    # The second click must perform a real repair operation, using the common
    # reconciliation worker rather than asking for offline legacy music paths.
    $script:fields.Game.Text=$game;$sourceTimer.Stop()
    function Start-ModUiProcess([string]$FilePath,[string[]]$ArgumentList,[string]$ErrorLog){
        $script:modeWorkerArguments=$ArgumentList;return [pscustomobject]@{HasExited=$false}
    }
    $script:play.RaiseEvent((New-Object Windows.RoutedEventArgs([Windows.Controls.Button]::ClickEvent)))
    Assert ($script:modeWorkerArguments -contains '-ForceRepair' -and ($script:modeWorkerArguments -join ' ') -like '*Reconcile-Installation.ps1*') 'Install in Repair mode bypassed the shared repair worker.'
    $script:worker=$null;Set-GuiState Idle
    Assert ((Visible-Actions) -eq 'Update,Cancel,Install' -and $window.FindName('SourceFields').Visibility -eq 'Visible') 'Repair source visibility did not last for the session.'
    $script:issueMessage='One or more track textures are damaged. Review the source files.';$script:sourceError=$true;Set-GuiState Idle
    Assert ($script:status.Text -eq $script:issueMessage -and $window.FindName('SourceHint').Text -eq $script:issueMessage) 'Ready hid an actual failure.'
    $script:sourceError=$false;$script:issueMessage='';Set-GuiState Idle
    Assert ($script:status.Text -eq 'Ready') 'Cleared failure left stale status text.'
    # Drive the real completion timer: only a successful Update restarts UI.
    $script:restartInvocations=0
    function Restart-SetupAfterUpdate {$script:restartInvocations++}
    foreach($success in @($true,$false)){
        $before=$script:restartInvocations
        $script:workerKind='update';$script:workerStatus=Join-Path $script:ModRoot '.work/fixture-update.json'
        Write-ModJson $script:workerStatus @{state=$(if($success){'complete'}else{'error'});message=$(if($success){'Updated.'}else{'Update could not be completed.'});percent=100;error_shown=$true}
        $script:worker=[pscustomobject]@{HasExited=$true;ExitCode=$(if($success){0}else{1})}
        $script:worker|Add-Member ScriptMethod WaitForExit {}; $script:worker|Add-Member ScriptMethod Dispose {}
        $script:modeFrame=New-Object Windows.Threading.DispatcherFrame;$script:pumpCount=0
        $pump=New-Object Windows.Threading.DispatcherTimer;$pump.Interval=[TimeSpan]::FromMilliseconds(50)
        $pump.Add_Tick({++$script:pumpCount;if(-not $script:worker -or $script:pumpCount -ge 60){$script:modeFrame.Continue=$false}})
        $timer.Start();$pump.Start();try{[Windows.Threading.Dispatcher]::PushFrame($script:modeFrame)}finally{$pump.Stop();$timer.Stop()}
        Assert (-not $script:worker -and $script:restartInvocations -eq ($before+[int]$success)) 'Setup restarted on failure or failed to restart after a successful Update.'
    }
    # A new Setup session must restore the installed layout without persisting
    # the previous session's Repair toggle.
    . $SetupPath -ValidateUi
    Assert (-not $script:repairMode -and (Visible-Actions) -eq 'Update,Repair,Play') 'Repair mode persisted into a new Setup session.'
    Assert ($window.FindName('SourceFields').Visibility -eq 'Collapsed') 'A new session kept the source fields open.'
    Assert ($window.FindName('NinjaLogo').HorizontalAlignment -eq 'Left' -and $window.FindName('NinjaLogo').Margin.Left -eq 0) 'The Ninja logo is not aligned to the left edge.'
    Assert (-not [IO.File]::Exists((Join-Path $script:ModRoot '.state/setup.json'))) 'UI validation wrote session state into saved configuration.'
    Write-Host 'PASS: first install has two actions; installed mode hides sources and has three; real Repair click reveals sources without starting work; session restart hides them; completed discovery becomes Ready and real errors stay visible.'
    return
}
. (Join-Path $PSScriptRoot '../scripts/Core.ps1')
$testRepo=$script:ModRoot;$work=Join-Path $testRepo ('.work/setup-modes-'+[guid]::NewGuid().ToString('N'))
try{
    Copy-ModTree (Join-Path $testRepo 'scripts') (Join-Path $work 'scripts')
    [IO.File]::Copy((Join-Path $testRepo 'Setup.ps1'),(Join-Path $work 'Setup.ps1'))
    & powershell.exe -NoProfile -STA -ExecutionPolicy Bypass -File $PSCommandPath -SetupPath (Join-Path $work 'Setup.ps1')
    if($LASTEXITCODE){throw 'Setup mode transition test failed.'}
}finally{
    $full=[IO.Path]::GetFullPath($work);$prefix=[IO.Path]::GetFullPath((Join-Path $testRepo '.work')).TrimEnd('\')+'\setup-modes-'
    if(-not $full.StartsWith($prefix,[StringComparison]::OrdinalIgnoreCase)){throw 'Unsafe Setup mode test cleanup.'}
    if([IO.Directory]::Exists($full)){[IO.Directory]::Delete($full,$true)}
}
