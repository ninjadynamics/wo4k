param([switch]$ValidateUi,[string]$PreviewPath,[ValidateSet('Idle','Searching','Updating')][string]$PreviewState='Idle')
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'scripts/Diagnostics.ps1')
$null=Start-WoLog 'setup' $PSScriptRoot -SkipHardware:$ValidateUi
try {
if([Threading.Thread]::CurrentThread.ApartmentState -ne 'STA'){throw 'Start Setup.exe, or run powershell.exe -STA -File Setup.ps1.'}
Add-Type -AssemblyName PresentationFramework,PresentationCore,WindowsBase,System.Windows.Forms
. (Join-Path $PSScriptRoot 'scripts/Core.ps1')
. (Join-Path $PSScriptRoot 'scripts/Configuration.ps1')
. (Join-Path $PSScriptRoot 'scripts/Links.ps1')
. (Join-Path $PSScriptRoot 'scripts/Source-Status.ps1')
. (Join-Path $PSScriptRoot 'scripts/Ui-Helpers.ps1')
# Import only function definitions; no filesystem search on the UI thread.
. (Join-Path $PSScriptRoot 'scripts/Discover-Sources.ps1') -OutputFile (Join-Path $PSScriptRoot '.work/unused.json')
[xml]$xaml=@'
<Window xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation" xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml" Title="WipEout 2097 / XL · Beta Mod" Width="820" Height="750" MinWidth="760" MinHeight="730" WindowStartupLocation="CenterScreen" Background="#0D111B" Foreground="#EEF2FB" FontFamily="Segoe UI">
 <Window.Resources>
  <Style TargetType="TextBlock"><Setter Property="TextWrapping" Value="Wrap"/><Setter Property="ToolTipService.ShowDuration" Value="60000"/></Style>
  <Style TargetType="Button">
   <Setter Property="Background" Value="#253148"/><Setter Property="Foreground" Value="#EEF2FB"/><Setter Property="Padding" Value="12,7"/><Setter Property="BorderThickness" Value="0"/><Setter Property="Cursor" Value="Hand"/>
   <Setter Property="Template"><Setter.Value><ControlTemplate TargetType="Button">
    <Border x:Name="Surface" Background="{TemplateBinding Background}" BorderBrush="#87909E" BorderThickness="0" CornerRadius="4" Padding="{TemplateBinding Padding}"><ContentPresenter x:Name="ButtonContent" HorizontalAlignment="Center" VerticalAlignment="Center" RecognizesAccessKey="True" TextElement.Foreground="{TemplateBinding Foreground}"/></Border>
    <ControlTemplate.Triggers>
     <Trigger Property="IsMouseOver" Value="True"><Setter TargetName="Surface" Property="Background" Value="#FF6633"/><Setter TargetName="ButtonContent" Property="TextElement.Foreground" Value="#FFFFFF"/></Trigger>
     <Trigger Property="IsPressed" Value="True"><Setter TargetName="Surface" Property="Background" Value="#FF875A"/><Setter TargetName="ButtonContent" Property="TextElement.Foreground" Value="#FFFFFF"/></Trigger>
     <Trigger Property="IsKeyboardFocused" Value="True"><Setter TargetName="Surface" Property="BorderThickness" Value="1"/></Trigger>
     <Trigger Property="IsEnabled" Value="False"><Setter TargetName="Surface" Property="Opacity" Value=".45"/></Trigger>
    </ControlTemplate.Triggers>
   </ControlTemplate></Setter.Value></Setter>
  </Style>
  <Style TargetType="TextBox"><Setter Property="Background" Value="#171F2D"/><Setter Property="Foreground" Value="#EDF3FF"/><Setter Property="BorderBrush" Value="#34425C"/><Setter Property="SelectionBrush" Value="#606773"/><Setter Property="Padding" Value="10,9"/><Setter Property="FontSize" Value="13"/>
   <Setter Property="Template"><Setter.Value><ControlTemplate TargetType="TextBox"><Border x:Name="Field" Background="{TemplateBinding Background}" BorderBrush="{TemplateBinding BorderBrush}" BorderThickness="1" CornerRadius="4"><ScrollViewer x:Name="PART_ContentHost" Margin="{TemplateBinding Padding}"/></Border><ControlTemplate.Triggers><Trigger Property="IsMouseOver" Value="True"><Setter TargetName="Field" Property="BorderBrush" Value="#687180"/></Trigger><Trigger Property="IsKeyboardFocused" Value="True"><Setter TargetName="Field" Property="BorderBrush" Value="#98A2B1"/></Trigger><Trigger Property="IsEnabled" Value="False"><Setter Property="Opacity" Value=".45"/></Trigger></ControlTemplate.Triggers></ControlTemplate></Setter.Value></Setter>
  </Style>
  <Style TargetType="CheckBox"><Setter Property="Foreground" Value="#D2DCEE"/><Setter Property="Margin" Value="0,9,0,0"/>
   <Setter Property="Template"><Setter.Value><ControlTemplate TargetType="CheckBox"><StackPanel Orientation="Horizontal"><Border x:Name="CheckSurface" Width="17" Height="17" Background="#171F2D" BorderBrush="#77808F" BorderThickness="1" CornerRadius="3" Margin="0,0,8,0"><Path x:Name="CheckMark" Data="M 3,8 L 6,11 L 12,4" Stroke="#66DBD0" StrokeThickness="2" Visibility="Collapsed"/></Border><ContentPresenter VerticalAlignment="Center" RecognizesAccessKey="True"/></StackPanel><ControlTemplate.Triggers><Trigger Property="IsChecked" Value="True"><Setter TargetName="CheckMark" Property="Visibility" Value="Visible"/></Trigger><Trigger Property="IsMouseOver" Value="True"><Setter TargetName="CheckSurface" Property="Background" Value="#343A46"/></Trigger><Trigger Property="IsKeyboardFocused" Value="True"><Setter TargetName="CheckSurface" Property="BorderBrush" Value="#CFD5DF"/></Trigger><Trigger Property="IsEnabled" Value="False"><Setter Property="Opacity" Value=".45"/></Trigger></ControlTemplate.Triggers></ControlTemplate></Setter.Value></Setter>
  </Style>
  <Style TargetType="ComboBoxItem"><Setter Property="Foreground" Value="#EEF2FB"/><Setter Property="Padding" Value="10,7"/><Setter Property="Template"><Setter.Value><ControlTemplate TargetType="ComboBoxItem"><Border x:Name="Choice" Background="#171F2D" Padding="{TemplateBinding Padding}"><ContentPresenter/></Border><ControlTemplate.Triggers><Trigger Property="IsSelected" Value="True"><Setter TargetName="Choice" Property="Background" Value="#4A263B"/></Trigger><Trigger Property="IsHighlighted" Value="True"><Setter TargetName="Choice" Property="Background" Value="#343A46"/></Trigger><Trigger Property="IsEnabled" Value="False"><Setter Property="Opacity" Value=".4"/></Trigger></ControlTemplate.Triggers></ControlTemplate></Setter.Value></Setter></Style>
  <Style TargetType="ComboBox"><Setter Property="Foreground" Value="#EEF2FB"/><Setter Property="Background" Value="#171F2D"/><Setter Property="Template"><Setter.Value><ControlTemplate TargetType="ComboBox"><Grid>
   <ToggleButton x:Name="DropToggle" Foreground="{TemplateBinding Foreground}" IsChecked="{Binding IsDropDownOpen, RelativeSource={RelativeSource TemplatedParent}, Mode=TwoWay}" Focusable="False"><ToggleButton.Template><ControlTemplate TargetType="ToggleButton"><Border x:Name="DropSurface" Background="#171F2D" BorderBrush="#34425C" BorderThickness="1" CornerRadius="4"><Grid><ContentPresenter Content="{Binding SelectionBoxItem, RelativeSource={RelativeSource AncestorType=ComboBox}}" ContentTemplate="{Binding SelectionBoxItemTemplate, RelativeSource={RelativeSource AncestorType=ComboBox}}" Margin="10,6,27,6"/><Path Data="M 0,0 L 4,4 L 8,0" Stroke="#D2DCEE" StrokeThickness="1.5" HorizontalAlignment="Right" VerticalAlignment="Center" Margin="0,0,10,0"/></Grid></Border><ControlTemplate.Triggers><Trigger Property="IsMouseOver" Value="True"><Setter TargetName="DropSurface" Property="Background" Value="#343A46"/></Trigger></ControlTemplate.Triggers></ControlTemplate></ToggleButton.Template></ToggleButton>
   <Popup x:Name="PART_Popup" IsOpen="{TemplateBinding IsDropDownOpen}" Placement="Bottom" AllowsTransparency="True" Focusable="False" PopupAnimation="Fade"><Border Background="#171F2D" BorderBrush="#606773" BorderThickness="1" MinWidth="{Binding ActualWidth, RelativeSource={RelativeSource TemplatedParent}}"><ScrollViewer><ItemsPresenter KeyboardNavigation.DirectionalNavigation="Contained"/></ScrollViewer></Border></Popup>
   </Grid><ControlTemplate.Triggers><Trigger Property="IsEnabled" Value="False"><Setter Property="Opacity" Value=".45"/></Trigger><Trigger Property="IsKeyboardFocusWithin" Value="True"><Setter TargetName="DropToggle" Property="Opacity" Value=".85"/></Trigger></ControlTemplate.Triggers></ControlTemplate></Setter.Value></Setter></Style>
  <Style TargetType="ContextMenu"><Setter Property="Background" Value="#171F2D"/><Setter Property="Foreground" Value="#EEF2FB"/><Setter Property="BorderBrush" Value="#606773"/></Style>
  <Style TargetType="MenuItem"><Setter Property="Foreground" Value="#EEF2FB"/><Setter Property="Template"><Setter.Value><ControlTemplate TargetType="MenuItem"><Border x:Name="MenuSurface" Background="#171F2D" Padding="12,8"><ContentPresenter ContentSource="Header" RecognizesAccessKey="True"/></Border><ControlTemplate.Triggers><Trigger Property="IsHighlighted" Value="True"><Setter TargetName="MenuSurface" Property="Background" Value="#343A46"/></Trigger></ControlTemplate.Triggers></ControlTemplate></Setter.Value></Setter></Style>
 </Window.Resources>
 <Grid Margin="32,24,32,24">
  <Grid.RowDefinitions><RowDefinition Height="Auto"/><RowDefinition Height="Auto"/><RowDefinition Height="*"/><RowDefinition Height="Auto"/></Grid.RowDefinitions>
  <StackPanel>
   <StackPanel Orientation="Horizontal" HorizontalAlignment="Left"><Image x:Name="Wo4kLogo" Width="300" Height="100" Stretch="Uniform" SnapsToDevicePixels="True" UseLayoutRounding="True" ToolTip="WO4K — modern enhancements for WipEout 2097 / XL."/><TextBlock Text="Beta" FontSize="12" FontWeight="Bold" Foreground="#66DBD0" VerticalAlignment="Center" Margin="12,0,0,0" ToolTip="Beta: the original PC game, enhanced for modern Windows."/></StackPanel>
   <Grid Margin="0,4,0,0"><Grid.ColumnDefinitions><ColumnDefinition Width="*"/><ColumnDefinition x:Name="GithubColumn" Width="121"/></Grid.ColumnDefinitions>
    <TextBlock Text="Adds widescreen, smooth animation, HD artwork and modern controls to WipeOut 2097 / XL." FontSize="15" Foreground="#A5B5CD" ToolTip="Play at your display's aspect ratio and refresh rate, with interpolated animation, extended track visibility, HD interface artwork, SDL3 gamepad support, locally improved sound effects and switchable PC/PlayStation soundtracks. The original 30 Hz game simulation is preserved."/>
    <Button x:Name="SourceLink" Grid.Column="1" Background="Transparent" Padding="0" HorizontalAlignment="Right" VerticalAlignment="Center" IsEnabled="False" AutomationProperties.Name="GitHub source"><Image x:Name="GithubLogo" Width="105" Height="24" Stretch="Uniform" SnapsToDevicePixels="True" UseLayoutRounding="True"/></Button>
   </Grid>
  </StackPanel>
  <Border Grid.Row="1" Background="#172335" CornerRadius="8" Padding="14,11" Margin="0,19,0,17" ToolTip="Game installation uses your local source files without mounting a disc. Only the Update button downloads a mod release.">
   <TextBlock x:Name="SourceHint" Text="Please provide the path(s) to your WipeOut 2097 / XL game files." Foreground="#BCD5E8" ToolTip="Use the PC ZIP/disc image for game data and any included PC music, or the small 11 MB PC version without music. Add a PlayStation source for optional PS1 music."/>
  </Border>
  <StackPanel Grid.Row="2">
   <StackPanel x:Name="SourceFields">
   <TextBlock Text="WINDOWS GAME" FontSize="11" FontWeight="Bold" Foreground="#A5B5CD" ToolTip="Required: PC ZIP/disc image or the small 11 MB PC version. Included PC music is extracted automatically; the small version can play without music or with optional PlayStation music."/>
   <Grid Margin="0,5,0,13"><Grid.ColumnDefinitions><ColumnDefinition Width="*"/><ColumnDefinition Width="58"/><ColumnDefinition Width="58"/></Grid.ColumnDefinitions>
    <TextBox x:Name="GamePath" ToolTip="PC ZIP, ISO/disc image or 11 MB PC version. Setup extracts included PC music automatically. Music is optional: the small version works silently or with the PlayStation soundtrack."/>
    <Button x:Name="GameBrowse" Grid.Column="1" Margin="5,0,0,0"><TextBlock Text="🔎" FontFamily="Segoe UI Emoji" FontSize="16"/></Button>
    <Button x:Name="GameMagic" Grid.Column="2" Margin="5,0,0,0"><TextBlock x:Name="GameWand" Text="✨" FontFamily="Segoe UI Emoji" FontSize="16" RenderTransformOrigin=".5,.5"><TextBlock.RenderTransform><RotateTransform/></TextBlock.RenderTransform></TextBlock></Button>
   </Grid>
   <TextBlock Text="PLAYSTATION GAME · OPTIONAL" FontSize="11" FontWeight="Bold" Foreground="#A5B5CD" ToolTip="Adds original PlayStation artwork from the European 2097 disc, plus its soundtrack when CD audio is included. A data-only image supplies artwork without music."/>
   <Grid Margin="0,5,0,14"><Grid.ColumnDefinitions><ColumnDefinition Width="*"/><ColumnDefinition Width="58"/><ColumnDefinition Width="58"/></Grid.ColumnDefinitions>
    <TextBox x:Name="PsxPath" ToolTip="Optional European PS1 game ZIP, ISO or CUE/BIN set for original textures and sprites. An existing PlayStation music folder supplies music only. Windows game files are still required."/>
    <Button x:Name="PsxBrowse" Grid.Column="1" Margin="5,0,0,0"><TextBlock Text="🔎" FontFamily="Segoe UI Emoji" FontSize="16"/></Button>
    <Button x:Name="PsxMagic" Grid.Column="2" Margin="5,0,0,0"><TextBlock x:Name="PsxWand" Text="✨" FontFamily="Segoe UI Emoji" FontSize="16" RenderTransformOrigin=".5,.5"><TextBlock.RenderTransform><RotateTransform/></TextBlock.RenderTransform></TextBlock></Button>
   </Grid>
   </StackPanel>
   <Grid><Grid.ColumnDefinitions><ColumnDefinition Width="*"/><ColumnDefinition Width="210"/></Grid.ColumnDefinitions>
    <TextBlock Text="Default soundtrack" VerticalAlignment="Center" Foreground="#C7D3E6" ToolTip="Choose installed PC or PlayStation music, or No music. Data-only discs supply artwork without CD audio. Switch soundtracks during play in Options / Audio."/>
    <ComboBox x:Name="Soundtrack" Grid.Column="1" SelectedIndex="2" Padding="8,5">
     <ComboBoxItem x:Name="PcChoice" IsEnabled="False" Content="PC"/>
     <ComboBoxItem x:Name="PsxChoice" Content="PlayStation" IsEnabled="False"/>
     <ComboBoxItem Content="No music"/>
    </ComboBox>
   </Grid>
   <TextBlock x:Name="SoundtrackHint" Text="Switch soundtracks anytime in the game's Audio options." FontSize="12" Foreground="#9FB0CA" Margin="0,6,0,0" ToolTip="When both soundtracks are installed, change between PC and PlayStation music on the fly in Options / Audio. Your choice is remembered."/>
   <Grid Margin="0,12,0,0"><Grid.ColumnDefinitions><ColumnDefinition Width="*"/><ColumnDefinition Width="210"/></Grid.ColumnDefinitions>
    <TextBlock Text="Audio output" VerticalAlignment="Center" Foreground="#C7D3E6" ToolTip="Choose Mono, Stereo or Dolby Surround. Stereo suits normal speakers and headphones; Dolby Surround needs a compatible decoder. Saved for the next launch."/>
    <ComboBox x:Name="AudioOutput" Grid.Column="1" SelectedIndex="1">
     <ComboBoxItem Content="Mono"/>
     <ComboBoxItem Content="Stereo"/>
     <ComboBoxItem Content="Dolby Surround"/>
    </ComboBox>
   </Grid>
   <Grid Margin="0,10,0,0"><Grid.ColumnDefinitions><ColumnDefinition Width="*"/><ColumnDefinition Width="210"/></Grid.ColumnDefinitions>
    <TextBlock Text="Fullscreen mode" VerticalAlignment="Center" Foreground="#C7D3E6" ToolTip="Borderless fills the screen and allows easy app switching. Exclusive uses exclusive fullscreen presentation. Saved for the next launch."/>
    <ComboBox x:Name="FullscreenMode" Grid.Column="1" SelectedIndex="0">
     <ComboBoxItem Content="Borderless"/>
     <ComboBoxItem Content="Exclusive"/>
    </ComboBox>
   </Grid>
   <CheckBox x:Name="HdTextures" Content="Install HD artwork" IsChecked="True" ToolTip="Install the supplied HD fonts, HUD, notice and title. Original SD artwork is retained for fallback."/>
   <CheckBox x:Name="CleanAudio" Content="Improve audio quality" IsChecked="True" ToolTip="Reduce noise in your original 22 kHz sound effects while preserving their volume. Audio is processed locally from your game files."/>
  </StackPanel>
  <StackPanel Grid.Row="3" Margin="0,18,0,0">
   <TextBlock x:Name="Status" Text="Please provide the path(s) to your WipeOut 2097 / XL game files." Foreground="#9FB0CA" FontSize="12" Margin="0,0,0,8" ToolTip="Current operation and its result. Failure details identify what needs attention; technical information stays in the diagnostic log."/>
   <ProgressBar x:Name="Progress" Height="2" Margin="0,0,0,28" Minimum="0" Maximum="100" Value="0" Foreground="#66DBD0" Background="#243149" ToolTip="Current discovery or installation progress. A moving bar means work is continuing in the background."/>
   <Grid><Grid.ColumnDefinitions><ColumnDefinition Width="*"/><ColumnDefinition Width="Auto"/></Grid.ColumnDefinitions>
    <Image x:Name="NinjaLogo" Width="256" Height="128" Stretch="Uniform" SnapsToDevicePixels="True" UseLayoutRounding="True" Margin="0,-32,24,0" HorizontalAlignment="Left" VerticalAlignment="Bottom" ToolTip="WO4K mod branding: Ninja Dynamics."/>
    <StackPanel x:Name="Actions" Grid.Column="1" Orientation="Horizontal" VerticalAlignment="Bottom">
     <Button x:Name="UpdateButton" Width="100" VerticalAlignment="Bottom" Content="Update" Margin="0,0,9,0" IsEnabled="False"/>
     <Button x:Name="InstallButton" Width="160" VerticalAlignment="Bottom" Content="Install" Background="#66DBD0" Foreground="#142032" FontWeight="SemiBold"/>
     <Button x:Name="PlayButton" Width="160" VerticalAlignment="Bottom" Content="Play" Background="#66DBD0" Foreground="#142032" FontWeight="SemiBold" Visibility="Collapsed" IsEnabled="False"/>
    </StackPanel>
   </Grid>
  </StackPanel>
 </Grid>
</Window>
'@
$window=[Windows.Markup.XamlReader]::Load((New-Object Xml.XmlNodeReader $xaml))
$setupIcon=Join-Path $PSScriptRoot 'assets/icons/setup.ico'
if([IO.File]::Exists($setupIcon)){$window.Icon=Read-ModUiBitmap $setupIcon}
foreach($item in @(@('Wo4kLogo','wo4k-logo-app.png'),@('GithubLogo','github.png'),@('NinjaLogo','ninja-logo.png'))){
    $path=Join-Path $PSScriptRoot ('assets/ui/'+$item[1])
    if([IO.File]::Exists($path)){
        $bitmap=New-Object Windows.Media.Imaging.BitmapImage
        $bitmap.BeginInit();$bitmap.CacheOption=[Windows.Media.Imaging.BitmapCacheOption]::OnLoad;$bitmap.UriSource=New-Object Uri($path,[UriKind]::Absolute);$bitmap.EndInit();$bitmap.Freeze()
        $window.FindName($item[0]).Source=$bitmap
    }
}
# Keep branding at its actual raster pixel dimensions, including on high-DPI screens.
function Set-BrandingPixelSizes {
    $dpi=[Windows.Media.VisualTreeHelper]::GetDpi($window)
    foreach($name in @('Wo4kLogo','GithubLogo','NinjaLogo')){
        $image=$window.FindName($name)
        if($image.Source){$image.Width=$image.Source.PixelWidth/$dpi.DpiScaleX;$image.Height=$image.Source.PixelHeight/$dpi.DpiScaleY}
    }
    # Align action-button centers with the wordmark, rather than the full ninja silhouette.
    $shift=[Math]::Max(0,((128-63.5)/$dpi.DpiScaleY-14)/2)
    $window.FindName('NinjaLogo').Margin=New-Object Windows.Thickness(0,-32,24,(-$shift))
    foreach($name in @('UpdateButton','PlayButton','InstallButton')){
        $right=if($name -eq 'PlayButton' -or ($name -eq 'InstallButton' -and $window.FindName('PlayButton').Visibility -ne 'Visible')){0}else{9}
        $window.FindName($name).Margin=New-Object Windows.Thickness(0,0,$right,$shift)
    }
    $window.FindName('GithubColumn').Width=New-Object Windows.GridLength($window.FindName('GithubLogo').Width+16)
    $window.MinWidth=[Math]::Max(760,64+360+180+24+$window.FindName('NinjaLogo').Width+16)
    if($window.Width -lt $window.MinWidth){$window.Width=$window.MinWidth}
}
Set-BrandingPixelSizes
$window.Add_SourceInitialized({Set-BrandingPixelSizes})
$window.Add_DpiChanged({Set-BrandingPixelSizes})
$script:fields=@{};$script:magic=@{};$script:wands=@{};$script:browse=@{}
foreach($kind in @('Game','Psx')){$script:fields[$kind]=$window.FindName($kind+'Path');$script:magic[$kind]=$window.FindName($kind+'Magic');$script:wands[$kind]=$window.FindName($kind+'Wand');$script:browse[$kind]=$window.FindName($kind+'Browse')}
$script:status=$window.FindName('Status');$script:progress=$window.FindName('Progress');$script:install=$window.FindName('InstallButton');$script:play=$window.FindName('PlayButton');$script:combo=$window.FindName('Soundtrack');$script:psxChoice=$window.FindName('PsxChoice')
$script:update=$window.FindName('UpdateButton');$script:sourceLink=$window.FindName('SourceLink');$script:guiState='Idle';$script:workerKind='installation'
$script:audioOutput=$window.FindName('AudioOutput')
$script:fullscreenMode=$window.FindName('FullscreenMode')
$script:scan=$null;$script:worker=$null;$script:scanMode='All';$script:scanResult='';$script:workerStatus='';$script:psxVerified=$false
$script:issueMessage='';$script:installedReady=$false
$script:repairMode=$false;$script:hasInstallation=$false;$script:sourceLayoutInitialized=$false;$script:startupStarted=$false;$script:restartingAfterUpdate=$false
$script:restoringConfiguration=$true
$script:savedConfiguration=Get-ModConfiguration $PSScriptRoot
$script:restorePsxDefault=$script:savedConfiguration.DefaultSoundtrack -eq 'Psx';$script:soundtrackTouched=$true;$script:gameVerified=$false;$script:sourceError=$false
$priorSettings=Join-Path $PSScriptRoot 'wobble/Wipeout2097.ini'
$priorGraphics=Join-Path $PSScriptRoot 'wobble/DDrawCompat-Wipeout2097.ini'
$script:fields.Game.Text=$script:savedConfiguration.GameSource;$script:fields.Psx.Text=$script:savedConfiguration.PsxMusicSource
$script:combo.SelectedIndex=[Array]::IndexOf([string[]]@('Pc','Psx','None'),$script:savedConfiguration.DefaultSoundtrack)
$script:audioOutput.SelectedIndex=[Array]::IndexOf([int[]]@(1,2,5),$script:savedConfiguration.SoundMode)
$script:fullscreenMode.SelectedIndex=[Array]::IndexOf([string[]]@('borderless','exclusive'),$script:savedConfiguration.FullscreenMode)
$window.FindName('HdTextures').IsChecked=$script:savedConfiguration.HdTextures;$window.FindName('CleanAudio').IsChecked=$script:savedConfiguration.CleanAudio
function Save-SetupPreferences {
    if($ValidateUi -or $PreviewPath -or $script:restoringConfiguration){return}
    $configuration=[pscustomobject]@{GameSource=$script:fields.Game.Text.Trim();PcMusicSource=$script:savedConfiguration.PcMusicSource;PsxMusicSource=$script:fields.Psx.Text.Trim();DefaultSoundtrack=@('Pc','Psx','None')[$script:combo.SelectedIndex];HdTextures=[bool]$window.FindName('HdTextures').IsChecked;CleanAudio=[bool]$window.FindName('CleanAudio').IsChecked;SoundMode=@(1,2,5)[$script:audioOutput.SelectedIndex];FullscreenMode=@('borderless','exclusive')[$script:fullscreenMode.SelectedIndex]}
    $script:savedConfiguration=Save-ModConfiguration $configuration $PSScriptRoot
}
function Start-InstallationCheck {
    if($script:worker -or $script:scan){return}
    $script:workerKind='installation check';$script:workerStatus=Join-Path $PSScriptRoot ('.work/check-status-'+[guid]::NewGuid().ToString('N')+'.json');[WoBeta.Files]::Parent($script:workerStatus)
    $script:status.Text='Checking the configured installation...';Set-GuiState Installing
    $script:worker=Start-ModUiProcess -FilePath $script:ModPowerShell -ArgumentList @('-NoProfile','-ExecutionPolicy','Bypass','-File',('"'+(Join-Path $PSScriptRoot 'scripts/Reconcile-Installation.ps1')+'"'),'-StatusFile',('"'+$script:workerStatus+'"')) -ErrorLog ($script:WoLogPath+'.worker.log')
}
function Update-InstallLayout {
    $script:hasInstallation=Test-Path -LiteralPath (Join-Path $PSScriptRoot 'wobble/installation.json')
    $sources=$window.FindName('SourceFields');$showSources=-not $script:hasInstallation -or $script:repairMode
    $changed=-not $script:sourceLayoutInitialized -or ($sources.Visibility -eq 'Visible') -ne $showSources
    $sources.Visibility=if($showSources){'Visible'}else{'Collapsed'}
    $script:play.Visibility=if($script:hasInstallation){'Visible'}else{'Collapsed'}
    $script:install.Content=if($script:repairMode){'Cancel'}elseif($showSources){'Install'}else{'Repair'}
    $script:play.Content=if($script:repairMode){'Install'}else{'Play'}
    $script:install.Width=if($script:hasInstallation){100}else{160}
    $script:install.Background=if($script:repairMode){'#F2C94C'}elseif($script:hasInstallation){'#253148'}else{'#66DBD0'}
    $script:install.Foreground=if($script:repairMode -or -not $script:hasInstallation){'#142032'}else{'#EEF2FB'}
    if($changed){
        $window.MinHeight=if($showSources){730}else{580}
        $window.Height=if($showSources){750}else{600}
        $script:sourceLayoutInitialized=$true
    }
    Set-BrandingPixelSizes
}
function Open-RepairSources {
    $script:repairConfiguration=Get-ModConfiguration $PSScriptRoot;$script:repairReady=$script:installedReady
    $script:repairMode=$true;Update-InstallLayout;Update-PsxChoice;Set-GuiState Idle
    $script:fields.Game.Focus()|Out-Null
}
function Cancel-RepairSources {
    $sourceTimer.Stop();$script:restoringConfiguration=$true
    try{
        $prior=$script:repairConfiguration
        if(-not $ValidateUi -and -not $PreviewPath){$script:savedConfiguration=Save-ModConfiguration $prior $PSScriptRoot}
        else{$script:savedConfiguration=$prior}
        $script:fields.Game.Text=$prior.GameSource;$script:fields.Psx.Text=$prior.PsxMusicSource
        $script:combo.SelectedIndex=[Array]::IndexOf([string[]]@('Pc','Psx','None'),$prior.DefaultSoundtrack)
        $script:audioOutput.SelectedIndex=[Array]::IndexOf([int[]]@(1,2,5),$prior.SoundMode)
        $script:fullscreenMode.SelectedIndex=[Array]::IndexOf([string[]]@('borderless','exclusive'),$prior.FullscreenMode)
        $window.FindName('HdTextures').IsChecked=$prior.HdTextures;$window.FindName('CleanAudio').IsChecked=$prior.CleanAudio
    }finally{$script:restoringConfiguration=$false}
    $script:repairMode=$false;$script:installedReady=$script:repairReady;Update-PsxChoice;Set-GuiState Idle
}
function Restart-SetupAfterUpdate {
    try{
        $helper=Start-ModSetupRestart $PSScriptRoot $PID;$helper.Dispose()
        $script:restartingAfterUpdate=$true
        $timer.Stop();$sourceTimer.Stop();$window.Close()
    }catch{
        Write-WoFailure $_ 'Restarting Setup after update'
        $script:sourceError=$true;$script:issueMessage='Update completed, but Setup could not reopen. Close this window and open Setup.exe.'
        Set-GuiState Idle
    }
}
function Refresh-LinkControls {
    $release=Get-ModConfiguredUrl (Join-Path $PSScriptRoot 'release.url') Release
    $source=Get-ModConfiguredUrl (Join-Path $PSScriptRoot 'source.url') Source
    $curl=[IO.File]::Exists((Join-Path ([Environment]::SystemDirectory) 'curl.exe'))
    $script:update.IsEnabled=($script:guiState -eq 'Idle' -and [bool]$release -and $curl)
    $script:update.Content=if($script:guiState -eq 'Updating'){'Updating…'}else{'Update'}
    $script:sourceLink.IsEnabled=[bool]$source
}
function Set-GuiState([string]$State) {
    Update-InstallLayout
    $script:guiState=$State;$locked=$State -in @('Installing','Updating')
    foreach($kind in @('Game','Psx')) {
        $script:magic[$kind].IsEnabled=$State -eq 'Idle'
        if($State -eq 'Searching'){$a=New-Object Windows.Media.Animation.DoubleAnimation(0,360,(New-Object Windows.Duration([TimeSpan]::FromSeconds(1.4))));$a.RepeatBehavior=[Windows.Media.Animation.RepeatBehavior]::Forever;$script:wands[$kind].RenderTransform.BeginAnimation([Windows.Media.RotateTransform]::AngleProperty,$a)}else{$script:wands[$kind].RenderTransform.BeginAnimation([Windows.Media.RotateTransform]::AngleProperty,$null)}
        $script:fields[$kind].IsEnabled=-not $locked;$script:browse[$kind].IsEnabled=-not $locked
        $script:fields[$kind].ToolTip=if($locked){'Setup is busy. This source field is locked until it finishes.'}elseif($kind -eq 'Game'){'PC ZIP/disc image or the small 11 MB PC version. Included PC music is extracted automatically. The small version works without music or with optional PlayStation music.'}else{'Optional European PlayStation disc for original artwork and any included CD audio, or an extracted music folder.'}
    }
    $script:install.IsEnabled=$State -eq 'Idle';$script:play.IsEnabled=($State -eq 'Idle' -and (Test-Path -LiteralPath (Join-Path $PSScriptRoot 'wobble/Wipeout2097.exe')))
    $script:combo.IsEnabled=-not $locked;$window.FindName('HdTextures').IsEnabled=-not $locked;$window.FindName('CleanAudio').IsEnabled=-not $locked
    $script:audioOutput.IsEnabled=-not $locked
    $script:fullscreenMode.IsEnabled=-not $locked
    $script:progress.IsIndeterminate=$State -in @('Searching','Updating')
    $script:status.ToolTip=$script:status.Text
    Refresh-LinkControls
    if($State -eq 'Idle'){
        if(-not $script:sourceError -and -not $script:issueMessage){$script:status.Text='Ready'}
        else{$script:status.Text=$script:issueMessage}
        $script:status.ToolTip=$script:status.Text;Update-SourceMessage
    }
}
function Update-SourceMessage {
    if($script:hasInstallation -and -not $script:repairMode -and -not $script:sourceError -and -not $script:issueMessage){
        $message=if($script:installedReady){'Ready to launch, all systems go!'}else{'Checking the installed game...'}
    }elseif(-not $script:sourceError -and -not $script:issueMessage -and $script:gameVerified -and (-not $script:fields.Psx.Text.Trim() -or $script:psxVerified)){
        $message=if($script:hasInstallation){'Sources are ready. Choose Install to repair the game.'}else{'Sources are ready. Choose Install to set up the game.'}
    }else{
        $message=Get-ModSourceMessage $script:fields.Game.Text.Trim() $script:fields.Psx.Text.Trim() ([bool]$script:gameVerified) ([bool]$script:psxVerified) ([bool]$script:sourceError) $script:issueMessage $script:installedReady
    }
    $window.FindName('SourceHint').Text=$message;$window.FindName('SourceHint').ToolTip=$message
}
function Update-PsxChoice {
    $pcInfo=Get-ModSourceInfo $script:fields.Game.Text.Trim()
    $script:gameVerified=[bool]$pcInfo.Game; $pcAvailable=[bool]$pcInfo.Pc -or (Test-Path -LiteralPath (Join-Path $PSScriptRoot 'wobble/music/pc/tracks.ini'));$window.FindName('PcChoice').IsEnabled=$pcAvailable
    $path=$script:fields.Psx.Text.Trim();$script:psxVerified=$false
    if($path){$info=Get-ModSourceInfo $path;$script:psxVerified=[bool]$info.Psx}
    $psxAudio=($script:psxVerified -and [bool]$info.PsxMusic) -or (Test-Path -LiteralPath (Join-Path $PSScriptRoot 'wobble/music/psx/tracks.ini'))
    $script:psxChoice.IsEnabled=$psxAudio
    if($script:restorePsxDefault -and $psxAudio){$script:combo.SelectedIndex=1;$script:restorePsxDefault=$false}
    elseif(-not $script:soundtrackTouched -or ($script:combo.SelectedIndex -eq 0 -and -not $pcAvailable) -or ($script:combo.SelectedIndex -eq 1 -and -not $psxAudio)){$script:combo.SelectedIndex=if($pcAvailable){0}elseif($psxAudio){1}else{2}}
    Update-SourceMessage
    Save-SetupPreferences
}
function Start-SourceDiscovery([string]$Kind='All') {
    if($script:scan -or $script:worker){return}
    $script:scanMode=$Kind;$script:scanResult=Join-Path $PSScriptRoot ('.work/discovery-'+[guid]::NewGuid().ToString('N')+'.json')
    [WoBeta.Files]::Parent($script:scanResult)
    $script:status.Text='Looking for your game files…';Set-GuiState Searching
    $script:scan=Start-ModUiProcess -FilePath $script:ModPowerShell -ArgumentList @('-NoProfile','-ExecutionPolicy','Bypass','-File',('"'+(Join-Path $PSScriptRoot 'scripts/Discover-Sources.ps1')+'"'),'-OutputFile',('"'+$script:scanResult+'"'))
}
$sourceTimer=New-Object Windows.Threading.DispatcherTimer;$sourceTimer.Interval=[TimeSpan]::FromMilliseconds(400);$sourceTimer.Add_Tick({$sourceTimer.Stop();Update-PsxChoice})
foreach($kind in @('Game','Psx')) {
    $script:magic[$kind].Tag=$kind;$script:magic[$kind].Add_Click({param($sender,$event)Start-SourceDiscovery ([string]$sender.Tag)})
    $script:browse[$kind].Tag=$kind
    $script:browse[$kind].Add_Click({param($sender,$event)
        $kind=[string]$sender.Tag;$menu=New-Object Windows.Controls.ContextMenu
        foreach($mode in @('File','Folder')){$item=New-Object Windows.Controls.MenuItem;$item.Header='Select '+$mode.ToLowerInvariant()+'…';$item.Tag=@($kind,$mode)
            $item.Add_Click({param($choice,$event)
                $kind=$choice.Tag[0];$mode=$choice.Tag[1];$path=''
                if($mode -eq 'File'){$dialog=New-Object Microsoft.Win32.OpenFileDialog;$dialog.Filter='Game sources|*.zip;*.iso;*.mds;*.mdf;*.cue;*.bin|All files|*.*';if($dialog.ShowDialog($window)){$path=$dialog.FileName}}
                else{$dialog=New-Object Windows.Forms.FolderBrowserDialog;$dialog.Description='Select your existing game or extracted music folder';if($dialog.ShowDialog() -eq [Windows.Forms.DialogResult]::OK){$path=$dialog.SelectedPath};$dialog.Dispose()}
                if($path){$script:fields[$kind].Text=$path;Update-PsxChoice}
            });$menu.Items.Add($item)|Out-Null
        }$menu.PlacementTarget=$sender;$menu.IsOpen=$true
    })
    $script:fields[$kind].Add_LostKeyboardFocus({Update-PsxChoice})
    $script:fields[$kind].Add_TextChanged({if($script:restoringConfiguration){return};$script:sourceError=$false;$script:issueMessage='';$script:installedReady=$false;$sourceTimer.Stop();$sourceTimer.Start()})
}
$script:combo.Add_DropDownClosed({$script:restorePsxDefault=$false;$script:soundtrackTouched=$true;Save-SetupPreferences})
$script:audioOutput.Add_SelectionChanged({
    if($ValidateUi -or $PreviewPath -or $script:audioOutput.SelectedIndex -lt 0){return}
    Save-SetupPreferences
    if(Test-Path -LiteralPath (Join-Path $PSScriptRoot 'wobble/Wipeout2097.exe')){
        Set-ModSoundMode -SettingsPath $priorSettings -SoundMode (@(1,2,5)[$script:audioOutput.SelectedIndex])
        $script:status.Text='Audio output saved for the next launch.';$script:status.ToolTip=$script:status.Text
    }
})
$script:sourceLink.Add_Click({$url=Get-ModConfiguredUrl (Join-Path $PSScriptRoot 'source.url') Source;if($url){Start-Process -FilePath $url|Out-Null}else{Refresh-LinkControls}})
$script:fullscreenMode.Add_SelectionChanged({
    if($ValidateUi -or $PreviewPath -or $script:fullscreenMode.SelectedIndex -lt 0){return}
    Save-SetupPreferences
    if(Test-Path -LiteralPath (Join-Path $PSScriptRoot 'wobble/Wipeout2097.exe')){
        Set-ModFullscreenMode -SettingsPath $priorGraphics -FullscreenMode (@('borderless','exclusive')[$script:fullscreenMode.SelectedIndex])
        $script:status.Text='Fullscreen mode saved for the next launch.';$script:status.ToolTip=$script:status.Text
    }
})
foreach($name in @('HdTextures','CleanAudio')){$window.FindName($name).Add_Click({Save-SetupPreferences})}
$script:update.Add_Click({
    if($script:worker -or $script:scan){return};Refresh-LinkControls;if(-not $script:update.IsEnabled){return};Save-SetupPreferences
    $script:workerKind='update';$script:workerStatus=Join-Path $PSScriptRoot ('.work/update-status-'+[guid]::NewGuid().ToString('N')+'.json');[WoBeta.Files]::Parent($script:workerStatus)
    $script:issueMessage='';$script:sourceError=$false;Update-SourceMessage;$script:status.Text='Starting update…';Set-GuiState Updating
    try{$script:worker=Start-ModUiProcess -FilePath $script:ModPowerShell -ArgumentList @('-NoProfile','-ExecutionPolicy','Bypass','-File',('"'+(Join-Path $PSScriptRoot 'scripts/Update-Mod.ps1')+'"'),'-StatusFile',('"'+$script:workerStatus+'"')) -ErrorLog ($script:WoLogPath+'.worker.log')}catch{Complete-WoFailure $_ 'Starting the updater failed' -StatusFile $script:workerStatus;$script:sourceError=$true;$script:issueMessage=Get-WoFailureMessage $_ 'Starting the updater';$script:status.Text=$script:issueMessage;Set-GuiState Idle}
})
function Start-SetupInstallation {
    if($script:worker -or $script:scan){return}
    Update-PsxChoice;Save-SetupPreferences
    if(-not $script:fields.Game.Text.Trim()){$script:status.Text='Choose your Windows game source to continue.';$script:status.ToolTip=$script:status.Text;return}
    $id=[guid]::NewGuid().ToString('N');$script:workerStatus=Join-Path $PSScriptRoot ('.work/status-'+$id+'.json');[WoBeta.Files]::Parent($script:workerStatus)
    $arguments=@('-NoProfile','-ExecutionPolicy','Bypass','-File')
    if($script:hasInstallation){
        # Reuse the shared repair path, including retained music/disc caches
        # when the original sources are temporarily unavailable.
        $script:workerKind='repair'
        $arguments+=@(('"'+(Join-Path $PSScriptRoot 'scripts/Reconcile-Installation.ps1')+'"'),'-ForceRepair')
    }else{
        $script:workerKind='installation';$request=Join-Path $PSScriptRoot ('.work/request-'+$id+'.json')
        Write-ModJson $request ([ordered]@{GameSource=$script:fields.Game.Text.Trim();PcMusicSource=$script:savedConfiguration.PcMusicSource;PsxMusicSource=$script:fields.Psx.Text.Trim();DefaultSoundtrack=$(if($script:combo.SelectedIndex -eq 1){'Psx'}elseif($script:combo.SelectedIndex -eq 2){'None'}else{'Pc'});HdTextures=[bool]$window.FindName('HdTextures').IsChecked;CleanAudio=[bool]$window.FindName('CleanAudio').IsChecked;SoundMode=@(1,2,5)[$script:audioOutput.SelectedIndex];FullscreenMode=@('borderless','exclusive')[$script:fullscreenMode.SelectedIndex]})
        $arguments+=@(('"'+(Join-Path $PSScriptRoot 'scripts/Install-Mod.ps1')+'"'),'-RequestFile',('"'+$request+'"'))
    }
    $arguments+=@('-StatusFile',('"'+$script:workerStatus+'"'))
    $script:sourceError=$false;$script:issueMessage='';Update-SourceMessage;$script:status.Text=if($script:hasInstallation){'Repairing the installation...'}else{'Starting installation...'};Set-GuiState Installing
    $script:worker=Start-ModUiProcess -FilePath $script:ModPowerShell -ArgumentList $arguments -ErrorLog ($script:WoLogPath+'.worker.log')
}
$script:play.Add_Click({
    if($script:repairMode){Start-SetupInstallation;return}
    Save-SetupPreferences;Start-Process -FilePath (Join-Path $PSScriptRoot 'Play.exe') -WorkingDirectory $PSScriptRoot|Out-Null
})
$script:install.Add_Click({
    if($script:worker -or $script:scan){return}
    if($script:repairMode){Cancel-RepairSources;return}
    if($script:hasInstallation){Open-RepairSources;return}
    Start-SetupInstallation
})
$timer=New-Object Windows.Threading.DispatcherTimer;$timer.Interval=[TimeSpan]::FromMilliseconds(300)
$timer.Add_Tick({
    if($script:scan -and $script:scan.HasExited){
        if(Test-Path -LiteralPath $script:scanResult){try{$found=Get-Content -Encoding UTF8 -LiteralPath $script:scanResult -Raw|ConvertFrom-Json;foreach($kind in @('Game','Psx')){if($script:scanMode -eq $kind -or ($script:scanMode -eq 'All' -and -not $script:fields[$kind].Text)){$script:fields[$kind].Text=$found.$kind}}}catch{}}
        $script:scan.Dispose();$script:scan=$null;Update-PsxChoice;Set-GuiState Idle
    }
    if($script:worker){
        if(Test-Path -LiteralPath $script:workerStatus){try{
            $state=Get-Content -Encoding UTF8 -LiteralPath $script:workerStatus -Raw|ConvertFrom-Json
            $script:status.Text=$state.message;$script:status.ToolTip=$state.message;$script:progress.Value=$state.percent
            if($state.state -eq 'error'){$script:sourceError=$true;$script:issueMessage=Get-WoFailureMessage ([string]$state.message) $script:workerKind;$script:status.Text=$script:issueMessage;Update-SourceMessage}
        }catch{}}
        if($script:worker.HasExited){
            $result=Get-ModUiWorkerResult $script:worker $script:workerStatus $script:workerKind
            Write-WoLog 'worker_exit' @{operation=$script:workerKind;code=$result.exit_code;state=$result.state;success=$result.success}
            $script:sourceError=-not $result.success;$script:issueMessage=if($result.success){''}else{$result.message}
            $script:installedReady=$result.success -and (Test-Path -LiteralPath (Join-Path $PSScriptRoot 'wobble/installation.json'))
            $script:status.Text=$result.message;$script:status.ToolTip=$result.message
            if(-not $result.success -and -not $result.error_shown){
                $failureLog=if($result.log_path){$result.log_path}else{$script:WoLogPath}
                $null=Show-WoFailure $result.message -LogPath $failureLog
            }
            $script:worker.Dispose();$script:worker=$null
            if($result.success -and $script:workerKind -eq 'update'){Restart-SetupAfterUpdate;return}
            if($result.success){
                Update-PsxChoice
                if($script:repairMode){$script:repairConfiguration=Get-ModConfiguration $PSScriptRoot;$script:repairReady=$script:installedReady}
            };Set-GuiState Idle
        }
    }
})
$window.Add_Activated({Refresh-LinkControls})
$window.Add_Closing({param($sender,$event)if(-not $script:restartingAfterUpdate){Save-SetupPreferences};if($script:worker){$event.Cancel=$true;$script:status.Text='Please wait for setup to finish before closing.'}else{$sourceTimer.Stop();$timer.Stop();if($script:scan -and -not $script:scan.HasExited){$script:scan.Kill()}}})
$script:restoringConfiguration=$false
Set-GuiState $PreviewState
if($ValidateUi -or $PreviewPath) {
    if($script:audioOutput.Items.Count -ne 3 -or $script:audioOutput.SelectedIndex -lt 0){throw 'Audio output must expose Mono, Stereo and Dolby Surround and restore a valid selection.'}
    if($script:audioOutput.IsEnabled -ne ($PreviewState -ne 'Updating')){throw 'Audio output must lock during updates.'}
    if($script:fullscreenMode.Items.Count -ne 2 -or $script:fullscreenMode.SelectedIndex -lt 0){throw 'Fullscreen mode must expose Borderless and Exclusive and restore the saved selection.'}
    if($script:fullscreenMode.IsEnabled -ne ($PreviewState -ne 'Updating')){throw 'Fullscreen mode must lock during updates.'}
    $footer=$script:progress.Parent
    if($script:progress.Height -gt 2 -or $footer.Children.IndexOf($script:status) -ge $footer.Children.IndexOf($script:progress)){throw 'Operation text must precede a thin progress line.'}
    foreach($kind in @('Game','Psx')) {
        if($PreviewState -eq 'Searching' -and ($script:magic[$kind].IsEnabled -or -not $script:wands[$kind].RenderTransform.HasAnimatedProperties)){throw 'Searching must animate and disable every magic button.'}
        if($PreviewState -eq 'Idle' -and -not $script:magic[$kind].IsEnabled){throw 'Idle magic buttons must be clickable.'}
        if($PreviewState -eq 'Updating' -and ($script:magic[$kind].IsEnabled -or $script:fields[$kind].IsEnabled -or $script:browse[$kind].IsEnabled)){throw 'Update must lock source selection and discovery.'}
    }
    if($script:psxChoice.IsEnabled){throw 'PlayStation default must be unavailable until a source is verified.'}
    $validRelease=[bool](Get-ModConfiguredUrl (Join-Path $PSScriptRoot 'release.url') Release)
    $hasCurl=[IO.File]::Exists((Join-Path ([Environment]::SystemDirectory) 'curl.exe'))
    if($script:update.IsEnabled -ne ($PreviewState -eq 'Idle' -and $validRelease -and $hasCurl)){throw 'Update availability does not match the local URL configuration and busy state.'}
    if($PreviewState -eq 'Updating' -and ($script:play.IsEnabled -or $script:install.IsEnabled -or -not $script:progress.IsIndeterminate)){throw 'Update must disable game/install actions and show progress.'}
    if($script:sourceLink.IsEnabled -ne [bool](Get-ModConfiguredUrl (Join-Path $PSScriptRoot 'source.url') Source)){throw 'Source link availability does not match source.url.'}
    $elements=New-Object Collections.Generic.List[object]
    function Inspect-Ui($element,[bool]$InsideControl=$false){
        $control=$element -is [Windows.Controls.Button] -or $element -is [Windows.Controls.ComboBox] -or $element -is [Windows.Controls.ComboBoxItem]
        $inspect=$control -or $element -is [Windows.Controls.Image] -or $element -is [Windows.Controls.TextBlock] -or $element -is [Windows.Controls.TextBox] -or $element -is [Windows.Controls.CheckBox] -or $element -is [Windows.Controls.ProgressBar]
        if($inspect){
            if($InsideControl -or $control){if($element.ToolTip){throw ('Redundant control tooltip: '+$element.Name)}}
            elseif(-not $element.ToolTip){throw ('Missing label tooltip: '+$element.Name)}
            $elements.Add($element)
        }
        foreach($child in [Windows.LogicalTreeHelper]::GetChildren($element)){if($child -is [Windows.DependencyObject]){Inspect-Ui $child ($InsideControl -or $control)}}
    }
    Inspect-Ui $window
    if($PreviewPath){$content=$window.Content;$content.Width=[double]::NaN;$content.Height=[double]::NaN;$content.Background=$window.Background;$window.Content=$null;$previewSurface=New-Object Windows.Controls.Border;$previewSurface.Background=$window.Background;$previewSurface.Resources=$window.Resources;[Windows.Documents.TextElement]::SetForeground($previewSurface,$window.Foreground);[Windows.Documents.TextElement]::SetFontFamily($previewSurface,$window.FontFamily);$previewSurface.Child=$content;$previewSurface.Measure((New-Object Windows.Size($window.Width,$window.Height)));$previewSurface.Arrange((New-Object Windows.Rect(0,0,$window.Width,$window.Height)));$previewSurface.UpdateLayout();$previewDpi=[Windows.Media.VisualTreeHelper]::GetDpi($window);$bitmap=New-Object Windows.Media.Imaging.RenderTargetBitmap([int][Math]::Ceiling($window.Width*$previewDpi.DpiScaleX),[int][Math]::Ceiling($window.Height*$previewDpi.DpiScaleY),(96*$previewDpi.DpiScaleX),(96*$previewDpi.DpiScaleY),[Windows.Media.PixelFormats]::Pbgra32);$bitmap.Render($previewSurface);$encoder=New-Object Windows.Media.Imaging.PngBitmapEncoder;$encoder.Frames.Add([Windows.Media.Imaging.BitmapFrame]::Create($bitmap));[WoBeta.Files]::Parent($PreviewPath);$f=[IO.File]::Create($PreviewPath);try{$encoder.Save($f)}finally{$f.Dispose()}}
    Write-Host ('PASS: '+$elements.Count+' UI elements validated; label tooltips retained; state '+$PreviewState);return
}
$dispatcher=[Windows.Threading.Dispatcher]::CurrentDispatcher
$dispatcher.add_UnhandledException({
    param($sender,$eventArgs)
    Write-WoLog 'ui_failure' @{message=$eventArgs.Exception.Message;exception=$eventArgs.Exception.GetType().FullName}
    $record=New-Object Management.Automation.ErrorRecord($eventArgs.Exception,'SetupUiFailure',[Management.Automation.ErrorCategory]::NotSpecified,$null)
    $script:sourceError=$true;$script:issueMessage=Get-WoFailureMessage $record 'Setup';$script:status.Text=$script:issueMessage;Update-SourceMessage
    $null=Show-WoFailure $script:issueMessage
    if(-not $script:worker){Set-GuiState Idle}
    $eventArgs.Handled=$true
})
$timer.Start();$window.Add_ContentRendered({if($script:startupStarted){return};$script:startupStarted=$true;if($env:WO4K_READY_MARKER){[IO.File]::WriteAllText($env:WO4K_READY_MARKER,'ready');$env:WO4K_READY_MARKER=$null;$env:WO4K_ERROR_MARKER=$null};if(Test-Path -LiteralPath (Join-Path $PSScriptRoot 'wobble/installation.json')){Start-InstallationCheck}else{Start-SourceDiscovery All}});$window.ShowDialog()|Out-Null

} catch {Complete-WoFailure $_ 'Setup failed' -NoDialog:$ValidateUi;throw}
