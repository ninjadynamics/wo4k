param([string]$GameRoot=(Join-Path $PSScriptRoot '../wobble'),
    [ValidateSet('intro','xtro1','xtro2','xtro3','xtro4')][string]$Movie='intro',
    [int]$ControllerOwner=0,[long]$OwnerWindow=0,[switch]$Probe,[switch]$ResolveOnly,[switch]$OriginalOnly)
. (Join-Path $PSScriptRoot 'Video-Paths.ps1')
$ErrorActionPreference='Stop'
trap {
    $errorLog=Join-Path $PSScriptRoot '../logs/video-error.log'
    [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($errorLog)))|Out-Null
    [IO.File]::WriteAllText($errorLog,($_|Out-String)+$_.ScriptStackTrace)
    throw
}
$moviePath=Find-ModVideo $GameRoot $Movie -OriginalOnly:$OriginalOnly
if($ResolveOnly){return $moviePath}
if(-not $moviePath){return}
if([Threading.Thread]::CurrentThread.ApartmentState -ne 'STA'){throw 'Video playback requires Windows PowerShell -STA.'}
Add-Type -AssemblyName PresentationFramework,PresentationCore,WindowsBase
if(-not ('Wo4kVideoInput' -as [type])){
    Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
public static class Wo4kVideoInput {
    [DllImport("user32.dll")] public static extern short GetAsyncKeyState(int key);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll", EntryPoint="GetWindowLongW")] static extern int GetWindowLong32(IntPtr window,int index);
    [DllImport("user32.dll", EntryPoint="SetWindowLongW")] static extern int SetWindowLong32(IntPtr window,int index,int value);
    [DllImport("user32.dll", EntryPoint="GetWindowLongPtrW")] static extern IntPtr GetWindowLong64(IntPtr window,int index);
    [DllImport("user32.dll", EntryPoint="SetWindowLongPtrW")] static extern IntPtr SetWindowLong64(IntPtr window,int index,IntPtr value);
    public static void NoActivate(IntPtr window) {
        if(IntPtr.Size==8)SetWindowLong64(window,-20,new IntPtr(GetWindowLong64(window,-20).ToInt64()|0x08000000L));
        else SetWindowLong32(window,-20,GetWindowLong32(window,-20)|0x08000000);
    }
    public static int Keys() {
        int mask=0;if(GetAsyncKeyState(27)<0)mask|=1;if(GetAsyncKeyState(13)<0)mask|=2;if(GetAsyncKeyState(32)<0)mask|=4;return mask;
    }
}
"@
}
$window=New-Object Windows.Window
$window.Title='WO4K video';$window.Background=[Windows.Media.Brushes]::Black
$window.WindowStyle='None';$window.ResizeMode='NoResize';$window.WindowState='Maximized'
$window.Cursor=[Windows.Input.Cursors]::None;$window.ForceCursor=$true;$window.Topmost=$true
# A modal owned window disables/deactivates the DirectDraw window. That loses
# its surfaces. Keep the game focused and enabled while a non-activating video
# overlays it; the dispatcher below services our media without ShowDialog.
if($OwnerWindow){
    $window.ShowActivated=$false
    $window.WindowState='Normal';$window.Left=0;$window.Top=0
    $window.Width=[Windows.SystemParameters]::PrimaryScreenWidth;$window.Height=[Windows.SystemParameters]::PrimaryScreenHeight
    $interop=New-Object Windows.Interop.WindowInteropHelper($window);$interop.Owner=[IntPtr]$OwnerWindow
    $window.Add_SourceInitialized({[Wo4kVideoInput]::NoActivate($interop.Handle)})
}
$video=New-Object Windows.Controls.MediaElement
$video.LoadedBehavior='Manual';$video.UnloadedBehavior='Close';$video.Stretch='Uniform'
$video.Cursor=[Windows.Input.Cursors]::None
$video.Source=New-Object Uri([IO.Path]::GetFullPath($moviePath));$window.Content=$video
if($Probe -and ($window.Cursor -ne [Windows.Input.Cursors]::None -or -not $window.ForceCursor -or $window.ToolTip)){throw 'Movie cursor/tooltip policy failed.'}
if($Probe){$window.WindowState='Normal';$window.Width=640;$window.Height=480;$video.Volume=0}
$state=@{opened=$false;failed=$false;reason='skipped';duration=0;buttons=0;first=$true;keys=[Wo4kVideoInput]::Keys();closed=$false;focusLost=$false}
$clock=[Diagnostics.Stopwatch]::StartNew()
$video.Add_MediaOpened({
    $state.opened=$true;$state.width=$video.NaturalVideoWidth;$state.height=$video.NaturalVideoHeight
    if($video.NaturalDuration.HasTimeSpan){$state.duration=$video.NaturalDuration.TimeSpan.TotalSeconds}
    if($video.NaturalVideoWidth -le 0 -or $video.NaturalVideoHeight -le 0){$state.failed=$true;$state.reason='no video stream';$window.Close()}
})
$video.Add_MediaEnded({$state.reason='finished';$window.Close()})
$video.Add_MediaFailed({param($sender,$event)$state.failed=$true;$state.reason=$event.ErrorException.Message;$window.Close()})
$window.Add_PreviewKeyDown({param($sender,$event)if($event.Key -in 'Escape','Return','Space'){$event.Handled=$true;$window.Close()}})
$window.Add_MouseDown({$window.Close()})
$window.Add_ContentRendered({$video.Play()})
$mapping=$null;$view=$null;$controller=$null;$timer=$null
try{
    # Reuse the running game's SDL3 mapping; standalone probes own their helper.
    $hostPath=Join-Path $GameRoot 'wo2097-controller.exe'
    if(-not $Probe -and ($ControllerOwner -or [IO.File]::Exists($hostPath))){
        try{
            if($ControllerOwner){
                $mapping=[IO.MemoryMappedFiles.MemoryMappedFile]::OpenExisting(('Local\WO2097Input-'+$ControllerOwner),[IO.MemoryMappedFiles.MemoryMappedFileRights]::Read)
                $view=$mapping.CreateViewAccessor(0,36,[IO.MemoryMappedFiles.MemoryMappedFileAccess]::Read)
            }else{
                $mapping=[IO.MemoryMappedFiles.MemoryMappedFile]::CreateNew(('Local\WO2097Input-'+$PID),64)
                $view=$mapping.CreateViewAccessor()
                $controller=Start-Process -FilePath $hostPath -ArgumentList ([string]$PID) -WorkingDirectory $GameRoot -WindowStyle Hidden -PassThru
            }
        }catch{Write-Verbose 'Controller skip unavailable; keyboard remains available.'}
    }
    $timer=New-Object Windows.Threading.DispatcherTimer;$timer.Interval=[TimeSpan]::FromMilliseconds(20)
    $timer.Add_Tick({
        if($OwnerWindow){
            $foreground=[Wo4kVideoInput]::GetForegroundWindow().ToInt64()
            if($foreground -ne $OwnerWindow){$state.focusLost=$true}
            $keys=[Wo4kVideoInput]::Keys()
            if($foreground -eq $OwnerWindow -and ($keys -band (-bnot $state.keys))){$window.Close()}
            $state.keys=$keys
        }
        if($view){
            $sequence=$view.ReadInt32(0)
            if(($sequence -band 1) -eq 0 -and $view.ReadUInt32(4) -eq 0x574f5044 -and $view.ReadInt32(12)){
                $buttons=$view.ReadUInt32(16)
                if($sequence -eq $view.ReadInt32(0)){
                    if(-not $state.first -and ($buttons -band (-bnot $state.buttons))){$window.Close()}
                    $state.first=$false;$state.buttons=$buttons
                }
            }
        }
        if($Probe -and $state.opened -and $video.Position.TotalMilliseconds -ge 250){$state.reason='probe passed';$window.Close()}
        if((-not $state.opened -and $clock.Elapsed.TotalSeconds -gt 15) -or ($state.opened -and $clock.Elapsed.TotalSeconds -gt [Math]::Max(30,$state.duration+15))){$state.failed=$true;$state.reason='playback timed out';$window.Close()}
    })
    $timer.Start()
    if($OwnerWindow){
        $frame=New-Object Windows.Threading.DispatcherFrame
        $window.Add_Closed({$state.closed=$true;$frame.Continue=$false})
        $window.Show()
        if(-not $state.closed){[Windows.Threading.Dispatcher]::PushFrame($frame)}
    }else{$window.ShowDialog()|Out-Null}
}finally{
    if($timer){$timer.Stop()};$video.Close()
    if($controller){if(-not $controller.HasExited){$controller.Kill();$controller.WaitForExit()};$controller.Dispose()}
    if($view){$view.Dispose()};if($mapping){$mapping.Dispose()}
}
# A broken/unsupported MP4 replacement can still fall back to the supplied MPG.
if($state.failed -and -not $OriginalOnly -and [IO.Path]::GetExtension($moviePath) -ieq '.mp4' -and (Find-ModVideo $GameRoot $Movie -OriginalOnly)){
    & $PSCommandPath -GameRoot $GameRoot -Movie $Movie -ControllerOwner $ControllerOwner -OwnerWindow $OwnerWindow -Probe:$Probe -OriginalOnly
    return
}
if($Probe){if($state.failed -or $state.reason -ne 'probe passed'){throw ('Video probe failed ('+$Movie+'): '+$state.reason)};Write-Host ('PASS: '+$Movie+' decoded '+$state.width+'x'+$state.height+'; playback advanced.');return}
$logs=Join-Path $PSScriptRoot '../logs';[IO.Directory]::CreateDirectory($logs)|Out-Null
[IO.File]::WriteAllText((Join-Path $logs ('video-'+$Movie+'.log')),('File='+$moviePath+"`r`nOpened="+$state.opened+"`r`nResult="+$state.reason+"`r`nGameFocusLost="+$state.focusLost))
