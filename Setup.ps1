param([switch]$ValidateUi,[string]$PreviewPath,[ValidateSet('Idle','Searching','Updating')][string]$PreviewState='Idle')
$ErrorActionPreference='Stop'
if([Threading.Thread]::CurrentThread.ApartmentState -ne 'STA'){throw 'Start Setup.exe, or run powershell.exe -STA -File Setup.ps1.'}
Add-Type -AssemblyName PresentationFramework,PresentationCore,WindowsBase,System.Windows.Forms
. (Join-Path $PSScriptRoot 'scripts/Core.ps1')
. (Join-Path $PSScriptRoot 'scripts/Links.ps1')
# Import only function definitions; no filesystem search on the UI thread.
. (Join-Path $PSScriptRoot 'scripts/Discover-Sources.ps1') -OutputFile (Join-Path $PSScriptRoot '.work/unused.json')
[xml]$xaml=@'
<Window xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation" xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml" Title="WipEout 2097 / XL · Beta Mod" Width="820" Height="750" MinWidth="760" MinHeight="730" WindowStartupLocation="CenterScreen" Background="#0D111B" Foreground="#EEF2FB" FontFamily="Segoe UI" ToolTip="Portable WipEout 2097 beta mod setup. Your source files remain unchanged.">
 <Window.Resources>
  <Style TargetType="TextBlock"><Setter Property="Foreground" Value="#EEF2FB"/><Setter Property="TextWrapping" Value="Wrap"/><Setter Property="ToolTipService.ShowDuration" Value="60000"/></Style>
  <Style TargetType="Button">
   <Setter Property="Background" Value="#253148"/><Setter Property="Foreground" Value="#EEF2FB"/><Setter Property="Padding" Value="12,7"/><Setter Property="BorderThickness" Value="0"/><Setter Property="Cursor" Value="Hand"/><Setter Property="ToolTipService.ShowOnDisabled" Value="True"/>
   <Setter Property="Template"><Setter.Value><ControlTemplate TargetType="Button">
    <Border x:Name="Surface" Background="{TemplateBinding Background}" BorderBrush="#87909E" BorderThickness="0" CornerRadius="4" Padding="{TemplateBinding Padding}"><ContentPresenter x:Name="ButtonContent" HorizontalAlignment="Center" VerticalAlignment="Center" RecognizesAccessKey="True"/></Border>
    <ControlTemplate.Triggers>
     <Trigger Property="IsMouseOver" Value="True"><Setter TargetName="Surface" Property="Background" Value="#343A46"/><Setter Property="Foreground" Value="#FFFFFF"/></Trigger>
     <Trigger Property="IsPressed" Value="True"><Setter TargetName="Surface" Property="Background" Value="#454C59"/></Trigger>
     <Trigger Property="IsKeyboardFocused" Value="True"><Setter TargetName="Surface" Property="BorderThickness" Value="1"/></Trigger>
     <Trigger Property="IsEnabled" Value="False"><Setter TargetName="Surface" Property="Opacity" Value=".45"/></Trigger>
    </ControlTemplate.Triggers>
   </ControlTemplate></Setter.Value></Setter>
  </Style>
  <Style TargetType="TextBox"><Setter Property="Background" Value="#171F2D"/><Setter Property="Foreground" Value="#EDF3FF"/><Setter Property="BorderBrush" Value="#34425C"/><Setter Property="SelectionBrush" Value="#606773"/><Setter Property="Padding" Value="10,9"/><Setter Property="FontSize" Value="13"/>
   <Setter Property="Template"><Setter.Value><ControlTemplate TargetType="TextBox"><Border x:Name="Field" Background="{TemplateBinding Background}" BorderBrush="{TemplateBinding BorderBrush}" BorderThickness="1" CornerRadius="4"><ScrollViewer x:Name="PART_ContentHost" Margin="{TemplateBinding Padding}"/></Border><ControlTemplate.Triggers><Trigger Property="IsMouseOver" Value="True"><Setter TargetName="Field" Property="BorderBrush" Value="#687180"/></Trigger><Trigger Property="IsKeyboardFocused" Value="True"><Setter TargetName="Field" Property="BorderBrush" Value="#98A2B1"/></Trigger><Trigger Property="IsEnabled" Value="False"><Setter Property="Opacity" Value=".45"/></Trigger></ControlTemplate.Triggers></ControlTemplate></Setter.Value></Setter>
  </Style>
  <Style TargetType="CheckBox"><Setter Property="Foreground" Value="#D2DCEE"/><Setter Property="Margin" Value="0,9,0,0"/><Setter Property="ToolTipService.ShowOnDisabled" Value="True"/>
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
    <Button x:Name="SourceLink" Grid.Column="1" Background="Transparent" Padding="0" HorizontalAlignment="Right" VerticalAlignment="Center" IsEnabled="False" AutomationProperties.Name="GitHub source" ToolTip="GitHub: add a valid HTTPS GitHub URL to source.url to enable this link."><Image x:Name="GithubLogo" Width="105" Height="24" Stretch="Uniform" SnapsToDevicePixels="True" UseLayoutRounding="True" ToolTip="Open the project GitHub page when a valid source URL is configured."/></Button>
   </Grid>
  </StackPanel>
  <Border Grid.Row="1" Background="#172335" CornerRadius="8" Padding="14,11" Margin="0,19,0,17" ToolTip="Game installation uses your local source files without mounting a disc. Only the Update button downloads a mod release.">
   <TextBlock Text="Choose your Windows game source. PlayStation music is an optional extra." Foreground="#BCD5E8" ToolTip="Minimum: Windows PC game data and its PC soundtrack. Add a PS1 disc source to enable both soundtracks."/>
  </Border>
  <StackPanel Grid.Row="2">
   <TextBlock Text="WINDOWS GAME" FontSize="11" FontWeight="Bold" Foreground="#A5B5CD" ToolTip="Required. Original PC game ZIP, ISO, MDS/MDF, CUE/BIN or a supported Wobble folder."/>
   <Grid Margin="0,5,0,13"><Grid.ColumnDefinitions><ColumnDefinition Width="*"/><ColumnDefinition Width="43"/><ColumnDefinition Width="43"/></Grid.ColumnDefinitions>
    <TextBox x:Name="GamePath" ToolTip="Path to the PC game source. Auto-search fills verified candidates; you can type or browse instead."/>
    <Button x:Name="GameBrowse" Grid.Column="1" Margin="5,0,0,0" ToolTip="Select a PC source file or folder."><TextBlock Text="🔎" FontFamily="Segoe UI Emoji" ToolTip="Browse for a game archive, disc image or existing folder."/></Button>
    <Button x:Name="GameMagic" Grid.Column="2" Margin="5,0,0,0" ToolTip="Search common local folders again for supported Windows game files."><TextBlock x:Name="GameWand" Text="✨" FontFamily="Segoe UI Emoji" RenderTransformOrigin=".5,.5" ToolTip="Repeat automatic discovery for the Windows game source."><TextBlock.RenderTransform><RotateTransform/></TextBlock.RenderTransform></TextBlock></Button>
   </Grid>
   <TextBlock Text="PC SOUNDTRACK" FontSize="11" FontWeight="Bold" Foreground="#A5B5CD" ToolTip="Required PC music. Leave this field empty when the game source already includes its audio tracks."/>
   <Grid Margin="0,5,0,13"><Grid.ColumnDefinitions><ColumnDefinition Width="*"/><ColumnDefinition Width="43"/><ColumnDefinition Width="43"/></Grid.ColumnDefinitions>
    <TextBox x:Name="PcPath" ToolTip="PC MDS/MDF or CUE/BIN set, its ZIP, or existing PC music folder. A data-only ISO does not contain CD audio."/>
    <Button x:Name="PcBrowse" Grid.Column="1" Margin="5,0,0,0" ToolTip="Select the PC soundtrack source file or folder."><TextBlock Text="🔎" FontFamily="Segoe UI Emoji" ToolTip="Browse for original PC disc audio or previously extracted tracks."/></Button>
    <Button x:Name="PcMagic" Grid.Column="2" Margin="5,0,0,0" ToolTip="Search again for the original PC soundtrack."><TextBlock x:Name="PcWand" Text="✨" FontFamily="Segoe UI Emoji" RenderTransformOrigin=".5,.5" ToolTip="Repeat automatic discovery for PC music."><TextBlock.RenderTransform><RotateTransform/></TextBlock.RenderTransform></TextBlock></Button>
   </Grid>
   <TextBlock Text="PLAYSTATION SOUNDTRACK · OPTIONAL" FontSize="11" FontWeight="Bold" Foreground="#A5B5CD" ToolTip="Optional. Adds the11-song PlayStation soundtrack alongside the PC soundtrack."/>
   <Grid Margin="0,5,0,14"><Grid.ColumnDefinitions><ColumnDefinition Width="*"/><ColumnDefinition Width="43"/><ColumnDefinition Width="43"/></Grid.ColumnDefinitions>
    <TextBox x:Name="PsxPath" ToolTip="Optional PS1 game ZIP, CUE/BIN set or existing PlayStation music folder. The PS1 game cannot replace the Windows game data."/>
    <Button x:Name="PsxBrowse" Grid.Column="1" Margin="5,0,0,0" ToolTip="Select the optional PlayStation music source."><TextBlock Text="🔎" FontFamily="Segoe UI Emoji" ToolTip="Browse for PlayStation disc audio."/></Button>
    <Button x:Name="PsxMagic" Grid.Column="2" Margin="5,0,0,0" ToolTip="Search again for an optional PlayStation soundtrack source."><TextBlock x:Name="PsxWand" Text="✨" FontFamily="Segoe UI Emoji" RenderTransformOrigin=".5,.5" ToolTip="Repeat automatic discovery for PlayStation music."><TextBlock.RenderTransform><RotateTransform/></TextBlock.RenderTransform></TextBlock></Button>
   </Grid>
   <Grid><Grid.ColumnDefinitions><ColumnDefinition Width="*"/><ColumnDefinition Width="210"/></Grid.ColumnDefinitions>
    <TextBlock Text="Default soundtrack" VerticalAlignment="Center" Foreground="#C7D3E6" ToolTip="The music bank selected on first launch. You can switch soundtracks later in the game's audio options."/>
    <ComboBox x:Name="Soundtrack" Grid.Column="1" SelectedIndex="0" Padding="8,5" ToolTip="Choose PC or PlayStation as the default. PlayStation is enabled when its source is verified.">
     <ComboBoxItem Content="PC" ToolTip="Start with the original PC soundtrack. Always included in the minimum installation."/>
     <ComboBoxItem x:Name="PsxChoice" Content="PlayStation" IsEnabled="False" ToolTipService.ShowOnDisabled="True" ToolTip="Supply a valid PlayStation music source to enable this choice."/>
    </ComboBox>
   </Grid>
   <TextBlock x:Name="SoundtrackHint" Text="Switch soundtracks anytime in the game's Audio options." FontSize="12" Foreground="#9FB0CA" Margin="0,6,0,0" ToolTip="When both soundtracks are installed, change between PC and PlayStation music on the fly in Options / Audio. Your choice is remembered."/>
   <CheckBox x:Name="HdTextures" Content="Install HD artwork" IsChecked="True" ToolTip="Install the supplied HD fonts, HUD, notice and title. Original SD artwork is retained for fallback."/>
   <CheckBox x:Name="CleanAudio" Content="Improve audio quality" IsChecked="True" ToolTip="Reduce noise in your original 22 kHz sound effects while preserving their volume. Audio is processed locally from your game files."/>
  </StackPanel>
  <StackPanel Grid.Row="3" Margin="0,18,0,0">
   <ProgressBar x:Name="Progress" Height="4" Minimum="0" Maximum="100" Value="0" Foreground="#66DBD0" Background="#243149" ToolTip="Current discovery or installation progress. A moving bar means work is continuing in the background."/>
   <TextBlock x:Name="Status" Text="Ready when you are." Foreground="#9FB0CA" FontSize="12" Margin="0,8,0,13" ToolTip="Current setup state. Missing auto-search results are left empty without warnings."/>
   <Grid><Grid.ColumnDefinitions><ColumnDefinition Width="*"/><ColumnDefinition Width="100"/><ColumnDefinition Width="100"/><ColumnDefinition Width="160"/></Grid.ColumnDefinitions>
    <Grid>
     <TextBlock Text="Portable · No CD mounting" VerticalAlignment="Bottom" Margin="0,0,0,7" Foreground="#7385A1" FontSize="12" ToolTip="Installs into the wobble folder beside Setup. No registry installation, disc mounting, or fixed drive path."/>
     <Image x:Name="NinjaLogo" Width="256" Height="128" Stretch="Uniform" SnapsToDevicePixels="True" UseLayoutRounding="True" Margin="0,-32,24,0" HorizontalAlignment="Right" VerticalAlignment="Bottom" ToolTip="WO4K mod branding: Ninja Dynamics."/>
    </Grid>
    <Button x:Name="UpdateButton" VerticalAlignment="Bottom" Content="Update" Grid.Column="1" Margin="0,0,9,0" IsEnabled="False" ToolTip="Add a valid release URL to release.url to enable updates."/>
    <Button x:Name="PlayButton" VerticalAlignment="Bottom" Content="Play" Grid.Column="2" Margin="0,0,9,0" IsEnabled="False" ToolTip="Install the game first to enable Play."/>
    <Button x:Name="InstallButton" VerticalAlignment="Bottom" Content="Install / repair" Grid.Column="3" Background="#66DBD0" Foreground="#142032" FontWeight="SemiBold" ToolTip="Verify sources and build a complete installation. Existing saves/preferences are retained; the previous installation is backed up."/>
   </Grid>
  </StackPanel>
 </Grid>
</Window>
'@
$window=[Windows.Markup.XamlReader]::Load((New-Object Xml.XmlNodeReader $xaml))
$setupIcon=Join-Path $PSScriptRoot 'assets/icons/setup.ico'
if([IO.File]::Exists($setupIcon)){$window.Icon=[Windows.Media.Imaging.BitmapFrame]::Create((New-Object Uri($setupIcon,[UriKind]::Absolute)))} 
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
    $window.FindName('GithubColumn').Width=New-Object Windows.GridLength($window.FindName('GithubLogo').Width+16)
    $window.MinWidth=[Math]::Max(760,64+360+180+24+$window.FindName('NinjaLogo').Width+16)
    if($window.Width -lt $window.MinWidth){$window.Width=$window.MinWidth}
}
Set-BrandingPixelSizes
$window.Add_SourceInitialized({Set-BrandingPixelSizes})
$window.Add_DpiChanged({Set-BrandingPixelSizes})
$script:fields=@{};$script:magic=@{};$script:wands=@{};$script:browse=@{}
foreach($kind in @('Game','Pc','Psx')){$script:fields[$kind]=$window.FindName($kind+'Path');$script:magic[$kind]=$window.FindName($kind+'Magic');$script:wands[$kind]=$window.FindName($kind+'Wand');$script:browse[$kind]=$window.FindName($kind+'Browse')}
$script:status=$window.FindName('Status');$script:progress=$window.FindName('Progress');$script:install=$window.FindName('InstallButton');$script:play=$window.FindName('PlayButton');$script:combo=$window.FindName('Soundtrack');$script:psxChoice=$window.FindName('PsxChoice')
$script:update=$window.FindName('UpdateButton');$script:sourceLink=$window.FindName('SourceLink');$script:guiState='Idle';$script:workerKind='installation'
$script:scan=$null;$script:worker=$null;$script:scanMode='All';$script:scanResult='';$script:workerStatus='';$script:psxVerified=$false
$script:restorePsxDefault=$false
$priorSettings=Join-Path $PSScriptRoot 'wobble/Wipeout2097.ini'
if(Test-Path -LiteralPath $priorSettings){$script:restorePsxDefault=[IO.File]::ReadAllText($priorSettings) -match '(?im)^Soundtrack\s*=\s*1\s*$'}
function Refresh-LinkControls {
    $release=Get-ModConfiguredUrl (Join-Path $PSScriptRoot 'release.url') Release
    $source=Get-ModConfiguredUrl (Join-Path $PSScriptRoot 'source.url') Source
    $curl=[IO.File]::Exists((Join-Path ([Environment]::SystemDirectory) 'curl.exe'))
    $script:update.IsEnabled=($script:guiState -eq 'Idle' -and [bool]$release -and $curl)
    $script:update.Content=if($script:guiState -eq 'Updating'){'Updating…'}else{'Update'}
    $script:update.ToolTip=if(-not $release){'Update unavailable: release.url is missing, empty or invalid. Add an HTTPS GitHub releases URL or a direct ZIP URL.'}elseif(-not $curl){'Update unavailable: Windows curl.exe could not be found.'}elseif($script:guiState -ne 'Idle'){'Setup is busy. Updates become available when the current operation finishes.'}else{'Use curl to download and verify the latest mod release, then update it locally. Audio, saves, settings and URL files are retained.'}
    $script:sourceLink.IsEnabled=[bool]$source
    $script:sourceLink.ToolTip=if($source){'Open the Ninja Dynamics GitHub page: '+$source}else{'GitHub link unavailable: source.url is missing, empty or invalid. Add an HTTPS GitHub organization or repository URL.'}
    $window.FindName('GithubLogo').ToolTip=$script:sourceLink.ToolTip
}
function Set-GuiState([string]$State) {
    $script:guiState=$State;$locked=$State -in @('Installing','Updating')
    foreach($kind in @('Game','Pc','Psx')) {
        $script:magic[$kind].IsEnabled=$State -eq 'Idle'
        $script:magic[$kind].ToolTip=if($State -eq 'Searching'){'Searching local folders; this button is disabled until discovery finishes.'}elseif($locked){'Setup is busy. Discovery is available again afterward.'}else{'Search local folders again for a verified '+$kind+' source.'}
        $script:wands[$kind].ToolTip=$script:magic[$kind].ToolTip
        if($State -eq 'Searching'){$a=New-Object Windows.Media.Animation.DoubleAnimation(0,360,(New-Object Windows.Duration([TimeSpan]::FromSeconds(1.4))));$a.RepeatBehavior=[Windows.Media.Animation.RepeatBehavior]::Forever;$script:wands[$kind].RenderTransform.BeginAnimation([Windows.Media.RotateTransform]::AngleProperty,$a)}else{$script:wands[$kind].RenderTransform.BeginAnimation([Windows.Media.RotateTransform]::AngleProperty,$null)}
        $script:fields[$kind].IsEnabled=-not $locked;$script:browse[$kind].IsEnabled=-not $locked
        $script:fields[$kind].ToolTip=if($locked){'Setup is busy. This source field is locked until it finishes.'}else{'Path to your '+$kind+' source. Type a path, browse, or use automatic discovery.'}
        $script:browse[$kind].ToolTip=if($locked){'Setup is busy. Source selection is temporarily unavailable.'}else{'Choose a '+$kind+' source file or folder.'}
    }
    $script:install.IsEnabled=$State -eq 'Idle';$script:play.IsEnabled=($State -eq 'Idle' -and (Test-Path -LiteralPath (Join-Path $PSScriptRoot 'wobble/Wipeout2097.exe')))
    $script:play.ToolTip=if($script:play.IsEnabled){'Refresh artwork and sound effects, then launch the mod with widescreen, controller and music support.'}elseif($State -ne 'Idle'){'Setup is busy. Play becomes available when work finishes.'}else{'Complete setup to enable Play.'}
    $script:install.ToolTip=if($State -eq 'Idle'){'Install or repair using these sources; preserve saves and back up the previous installation.'}else{'Setup is busy. Wait for the current operation to finish.'}
    $script:combo.IsEnabled=-not $locked;$window.FindName('HdTextures').IsEnabled=-not $locked;$window.FindName('CleanAudio').IsEnabled=-not $locked
    $script:combo.ToolTip=if($locked){'Setup is busy. The default soundtrack choice is temporarily locked.'}else{'Choose the initial soundtrack. PlayStation is enabled only with a verified source.'}
    $script:progress.IsIndeterminate=$State -in @('Searching','Updating')
    $script:status.ToolTip=$script:status.Text
    Refresh-LinkControls
}
function Update-PsxChoice {
    $path=$script:fields.Psx.Text.Trim();$script:psxVerified=$false
    if($path){$info=Get-ModSourceInfo $path;$script:psxVerified=[bool]$info.Psx}
    $script:psxChoice.IsEnabled=$script:psxVerified
    $script:psxChoice.ToolTip=if($script:psxVerified){'Verified PlayStation source available. Choose it as the initial soundtrack.'}else{'Add a valid PlayStation music source to enable this option.'}
    if(-not $script:psxVerified){$script:combo.SelectedIndex=0}elseif($script:restorePsxDefault){$script:combo.SelectedIndex=1;$script:restorePsxDefault=$false}
}
function Start-SourceDiscovery([string]$Kind='All') {
    if($script:scan -or $script:worker){return}
    $script:scanMode=$Kind;$script:scanResult=Join-Path $PSScriptRoot ('.work/discovery-'+[guid]::NewGuid().ToString('N')+'.json')
    [WoBeta.Files]::Parent($script:scanResult)
    $script:status.Text='Looking for your game files…';Set-GuiState Searching
    $script:scan=Start-Process -FilePath $script:ModPowerShell -WindowStyle Hidden -ArgumentList @('-NoProfile','-ExecutionPolicy','Bypass','-File',('"'+(Join-Path $PSScriptRoot 'scripts/Discover-Sources.ps1')+'"'),'-OutputFile',('"'+$script:scanResult+'"')) -PassThru
}
foreach($kind in @('Game','Pc','Psx')) {
    $script:magic[$kind].Tag=$kind;$script:magic[$kind].Add_Click({param($sender,$event)Start-SourceDiscovery ([string]$sender.Tag)})
    $script:browse[$kind].Tag=$kind
    $script:browse[$kind].Add_Click({param($sender,$event)
        $kind=[string]$sender.Tag;$menu=New-Object Windows.Controls.ContextMenu
        foreach($mode in @('File','Folder')){$item=New-Object Windows.Controls.MenuItem;$item.Header='Select '+$mode.ToLowerInvariant()+'…';$item.ToolTip=if($mode -eq 'File'){'Choose a ZIP or disc descriptor/image.'}else{'Choose an existing game or music folder.'};$item.Tag=@($kind,$mode)
            $item.Add_Click({param($choice,$event)
                $kind=$choice.Tag[0];$mode=$choice.Tag[1];$path=''
                if($mode -eq 'File'){$dialog=New-Object Microsoft.Win32.OpenFileDialog;$dialog.Filter='Game sources|*.zip;*.iso;*.mds;*.mdf;*.cue;*.bin|All files|*.*';if($dialog.ShowDialog($window)){$path=$dialog.FileName}}
                else{$dialog=New-Object Windows.Forms.FolderBrowserDialog;$dialog.Description='Select your existing game or extracted music folder';if($dialog.ShowDialog() -eq [Windows.Forms.DialogResult]::OK){$path=$dialog.SelectedPath};$dialog.Dispose()}
                if($path){$script:fields[$kind].Text=$path;Update-PsxChoice}
            });$menu.Items.Add($item)|Out-Null
        }$menu.PlacementTarget=$sender;$menu.IsOpen=$true
    })
    $script:fields[$kind].Add_LostKeyboardFocus({Update-PsxChoice})
}
$script:combo.Add_DropDownClosed({$script:restorePsxDefault=$false})
$script:sourceLink.Add_Click({$url=Get-ModConfiguredUrl (Join-Path $PSScriptRoot 'source.url') Source;if($url){Start-Process -FilePath $url|Out-Null}else{Refresh-LinkControls}})
$script:update.Add_Click({
    if($script:worker -or $script:scan){return};Refresh-LinkControls;if(-not $script:update.IsEnabled){return}
    $script:workerKind='update';$script:workerStatus=Join-Path $PSScriptRoot ('.work/update-status-'+[guid]::NewGuid().ToString('N')+'.json');[WoBeta.Files]::Parent($script:workerStatus)
    $script:status.Text='Starting update…';Set-GuiState Updating
    try{$script:worker=Start-Process -FilePath $script:ModPowerShell -WindowStyle Hidden -ArgumentList @('-NoProfile','-ExecutionPolicy','Bypass','-File',('"'+(Join-Path $PSScriptRoot 'scripts/Update-Mod.ps1')+'"'),'-StatusFile',('"'+$script:workerStatus+'"')) -PassThru}catch{$script:status.Text=$_.Exception.Message;Set-GuiState Idle}
})
$script:play.Add_Click({Start-Process -FilePath $script:ModPowerShell -WindowStyle Hidden -ArgumentList @('-NoProfile','-ExecutionPolicy','Bypass','-File',('"'+(Join-Path $PSScriptRoot 'Play.ps1')+'"'))|Out-Null})
$script:install.Add_Click({
    if($script:worker -or $script:scan){return};Update-PsxChoice
    if(-not $script:fields.Game.Text.Trim()){$script:status.Text='Choose your Windows game source to continue.';$script:status.ToolTip=$script:status.Text;return}
    $id=[guid]::NewGuid().ToString('N');$request=Join-Path $PSScriptRoot ('.work/request-'+$id+'.json');$script:workerStatus=Join-Path $PSScriptRoot ('.work/status-'+$id+'.json')
    Write-ModJson $request ([ordered]@{GameSource=$script:fields.Game.Text.Trim();PcMusicSource=$script:fields.Pc.Text.Trim();PsxMusicSource=$script:fields.Psx.Text.Trim();DefaultSoundtrack=$(if($script:combo.SelectedIndex -eq 1){'Psx'}else{'Pc'});HdTextures=[bool]$window.FindName('HdTextures').IsChecked;CleanAudio=[bool]$window.FindName('CleanAudio').IsChecked})
    $script:workerKind='installation';$script:status.Text='Starting installation…';Set-GuiState Installing
    $script:worker=Start-Process -FilePath $script:ModPowerShell -WindowStyle Hidden -ArgumentList @('-NoProfile','-ExecutionPolicy','Bypass','-File',('"'+(Join-Path $PSScriptRoot 'scripts/Install-Mod.ps1')+'"'),'-RequestFile',('"'+$request+'"'),'-StatusFile',('"'+$script:workerStatus+'"')) -PassThru
})
$timer=New-Object Windows.Threading.DispatcherTimer;$timer.Interval=[TimeSpan]::FromMilliseconds(300)
$timer.Add_Tick({
    if($script:scan -and $script:scan.HasExited){
        if(Test-Path -LiteralPath $script:scanResult){try{$found=Get-Content -Encoding UTF8 -LiteralPath $script:scanResult -Raw|ConvertFrom-Json;foreach($kind in @('Game','Pc','Psx')){if($script:scanMode -eq $kind -or ($script:scanMode -eq 'All' -and -not $script:fields[$kind].Text)){$script:fields[$kind].Text=$found.$kind}}}catch{}}
        $script:scan.Dispose();$script:scan=$null;Update-PsxChoice;$script:status.Text='Ready when you are.';Set-GuiState Idle
    }
    if($script:worker){
        if(Test-Path -LiteralPath $script:workerStatus){try{$s=Get-Content -Encoding UTF8 -LiteralPath $script:workerStatus -Raw|ConvertFrom-Json;$script:status.Text=$s.message;$script:status.ToolTip=$s.message;$script:progress.Value=$s.percent}catch{}}
        if($script:worker.HasExited){$script:worker.WaitForExit();if($script:worker.ExitCode -ne 0 -and $script:status.Text -like 'Starting*'){$script:status.Text='The '+$script:workerKind+' could not finish. Check the configuration and try again.'};$script:worker.Dispose();$script:worker=$null;Set-GuiState Idle}
    }
})
$window.Add_Activated({Refresh-LinkControls})
$window.Add_Closing({param($sender,$event)if($script:worker){$event.Cancel=$true;$script:status.Text='Please wait for setup to finish before closing.'}else{$timer.Stop();if($script:scan -and -not $script:scan.HasExited){$script:scan.Kill()}}})
Set-GuiState $PreviewState
if($ValidateUi -or $PreviewPath) {
    foreach($kind in @('Game','Pc','Psx')) {
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
    function Inspect-Ui($element){if($element -is [Windows.Controls.Image] -or $element -is [Windows.Controls.TextBlock] -or $element -is [Windows.Controls.Button] -or $element -is [Windows.Controls.TextBox] -or $element -is [Windows.Controls.ComboBox] -or $element -is [Windows.Controls.ComboBoxItem] -or $element -is [Windows.Controls.CheckBox] -or $element -is [Windows.Controls.ProgressBar]){if(-not $element.ToolTip){throw ('Missing tooltip: '+$element.GetType().Name+' '+$element.Name)};$elements.Add($element)};foreach($child in [Windows.LogicalTreeHelper]::GetChildren($element)){if($child -is [Windows.DependencyObject]){Inspect-Ui $child}}}
    Inspect-Ui $window
    if($PreviewPath){$content=$window.Content;$content.Width=[double]::NaN;$content.Height=[double]::NaN;$content.Background=$window.Background;$window.Content=$null;$previewSurface=New-Object Windows.Controls.Border;$previewSurface.Background=$window.Background;$previewSurface.Resources=$window.Resources;[Windows.Documents.TextElement]::SetForeground($previewSurface,$window.Foreground);[Windows.Documents.TextElement]::SetFontFamily($previewSurface,$window.FontFamily);$previewSurface.Child=$content;$previewSurface.Measure((New-Object Windows.Size($window.Width,$window.Height)));$previewSurface.Arrange((New-Object Windows.Rect(0,0,$window.Width,$window.Height)));$previewSurface.UpdateLayout();$previewDpi=[Windows.Media.VisualTreeHelper]::GetDpi($window);$bitmap=New-Object Windows.Media.Imaging.RenderTargetBitmap([int][Math]::Ceiling($window.Width*$previewDpi.DpiScaleX),[int][Math]::Ceiling($window.Height*$previewDpi.DpiScaleY),(96*$previewDpi.DpiScaleX),(96*$previewDpi.DpiScaleY),[Windows.Media.PixelFormats]::Pbgra32);$bitmap.Render($previewSurface);$encoder=New-Object Windows.Media.Imaging.PngBitmapEncoder;$encoder.Frames.Add([Windows.Media.Imaging.BitmapFrame]::Create($bitmap));[WoBeta.Files]::Parent($PreviewPath);$f=[IO.File]::Create($PreviewPath);try{$encoder.Save($f)}finally{$f.Dispose()}}
    Write-Host ('PASS: '+$elements.Count+' UI elements have tooltips; state '+$PreviewState);return
}
$timer.Start();$window.Add_ContentRendered({Start-SourceDiscovery All});$window.ShowDialog()|Out-Null
