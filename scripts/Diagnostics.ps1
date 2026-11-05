# Local logs: only the explicitly selected, non-identifying hardware fields.
$script:WoLogPath=$null
$script:WoLogRoot=$null
$script:WoLogStartedUtc=$null
$script:WoLogVersion='unknown'
function Protect-WoLogText([string]$Text) {
    foreach($pair in @(@($script:WoLogRoot,'%MODROOT%'),@($env:USERPROFILE,'%USERPROFILE%'),@($env:TEMP,'%TEMP%'))){
        if($pair[0]){$Text=[regex]::Replace($Text,[regex]::Escape($pair[0].Replace('\','\\')),[string]$pair[1],[Text.RegularExpressions.RegexOptions]::IgnoreCase);$Text=[regex]::Replace($Text,[regex]::Escape($pair[0]),[string]$pair[1],[Text.RegularExpressions.RegexOptions]::IgnoreCase)}
    }
    return $Text
}
function Write-WoLog([string]$Event,$Data) {
    if(-not $script:WoLogPath){return}
    try{
        $entry=[ordered]@{time=[DateTime]::UtcNow.ToString('o');event=$Event;data=$Data}
        $text=Protect-WoLogText (ConvertTo-Json -InputObject $entry -Depth 6 -Compress)
        [IO.File]::AppendAllText($script:WoLogPath,$text+[Environment]::NewLine,(New-Object Text.UTF8Encoding($false)))
    }catch{Write-Verbose 'Could not append to the local support log.'}
}
function Get-WoSystemInfo {
    $result=[ordered]@{windows=[Environment]::OSVersion.VersionString;os_64bit=[Environment]::Is64BitOperatingSystem;process_64bit=[Environment]::Is64BitProcess;powershell=$PSVersionTable.PSVersion.ToString()}
    foreach($query in @(
        @('Win32_OperatingSystem',@('Caption','Version','BuildNumber','OSArchitecture'),'os'),
        @('Win32_Processor',@('Name','NumberOfCores','NumberOfLogicalProcessors'),'cpu'),
        @('Win32_ComputerSystem',@('TotalPhysicalMemory'),'memory'),
        @('Win32_VideoController',@('Name','DriverVersion'),'gpu')
    )){
        try{
            $items=@(Get-CimInstance -ClassName $query[0] -Property $query[1] -OperationTimeoutSec 3 -ErrorAction Stop)
            switch($query[2]){
                os {if($items.Count){$result.windows=$items[0].Caption;$result.version=$items[0].Version;$result.build=$items[0].BuildNumber;$result.architecture=$items[0].OSArchitecture}}
                cpu {$result.cpu=@($items|ForEach-Object {[ordered]@{model=$_.Name;cores=$_.NumberOfCores;logical_processors=$_.NumberOfLogicalProcessors}})}
                memory {if($items.Count){$result.ram_gib=[Math]::Round([double]$items[0].TotalPhysicalMemory/1GB,2)}}
                gpu {$result.gpu=@($items|ForEach-Object {[ordered]@{model=$_.Name;driver=$_.DriverVersion}})}
            }
        }catch{$result[$query[2]+'_query']='unavailable'}
    }
    return $result
}
function Start-WoLog([string]$Operation,[string]$Root,[switch]$SkipHardware) {
    $script:WoLogStartedUtc=[DateTime]::UtcNow
    $script:WoLogRoot=[IO.Path]::GetFullPath($Root).TrimEnd('\')
    $name=$Operation+'-'+(Get-Date -Format 'yyyyMMdd-HHmmss-fff')+'-'+$PID+'.log'
    $directory=Join-Path $script:WoLogRoot 'logs'
    try{[IO.Directory]::CreateDirectory($directory)|Out-Null;$script:WoLogPath=Join-Path $directory $name;[IO.File]::WriteAllText($script:WoLogPath,'')}
    catch{$directory=Join-Path ([IO.Path]::GetTempPath()) 'WO4K/logs';[IO.Directory]::CreateDirectory($directory)|Out-Null;$script:WoLogPath=Join-Path $directory $name;[IO.File]::WriteAllText($script:WoLogPath,'')}
    $version='unknown';try{$version=[IO.File]::ReadAllText((Join-Path $Root 'VERSION')).Trim()}catch{}
    $script:WoLogVersion=$version
    Write-WoLog 'session' @{operation=$Operation;mod_version=$version;privacy='Local only; no identifiers, network addresses, account names or automatic uploads.'}
    if(-not $SkipHardware){Write-WoLog 'system' (Get-WoSystemInfo)}
    return $script:WoLogPath
}
function Write-WoFailure($Record,[string]$Stage) {
    $exception=$Record.Exception
    $data=[ordered]@{stage=$Stage;message=$exception.Message;exception=$exception.GetType().FullName;hresult=('0x{0:X8}' -f $exception.HResult);stack=$Record.ScriptStackTrace}
    $causes=@();$cause=$exception
    for($i=0;$cause -and $i -lt 4;$i++){
        $detail=[ordered]@{type=$cause.GetType().FullName;message=$cause.Message;hresult=('0x{0:X8}' -f $cause.HResult)}
        if($cause -is [ComponentModel.Win32Exception]){$detail.native_error=$cause.NativeErrorCode}
        $causes+=$detail;$cause=$cause.InnerException
    }
    $data.causes=$causes
    Write-WoLog 'failure' $data
}
function Show-WoErrorDialog([string]$Text) {
    Add-Type -AssemblyName PresentationFramework
    [Windows.MessageBox]::Show($Text,'WO4K - operation failed',[Windows.MessageBoxButton]::OK,[Windows.MessageBoxImage]::Error)|Out-Null
}
function Show-WoFailure([string]$Message,[string]$LogPath=$script:WoLogPath,[switch]$NoDialog) {
    if($NoDialog){return $false}
    $text=(Protect-WoLogText $Message)+"`r`n`r`nDiagnostic log:`r`n"+$LogPath+"`r`n`r`nShare this log with the WO4K developer. It stays on your PC until you choose to share it."
    try{
        Show-WoErrorDialog $text
        if($env:WO4K_ERROR_MARKER){try{[IO.File]::WriteAllText($env:WO4K_ERROR_MARKER,'reported')}catch{}}
        return $true
    }catch{return $false}
}
# User-facing explanations stay separate from the complete exception chain
# written by Write-WoFailure. Never label every worker error as corrupt data.
function Get-WoFailureMessage($Record,[string]$Stage) {
    $message=if($Record -is [string]){$Record}else{[string]$Record.Exception.Message}
    if($Stage -match '(?i)Restarting Setup'){return 'Update completed, but Setup could not reopen. Start Setup.exe from the mod folder.'}
    $sharing=$false;$denied=$false
    if($Record -isnot [string]){
        for($cause=$Record.Exception;$cause;$cause=$cause.InnerException){
            if(($cause.HResult -band 65535) -eq 32){$sharing=$true}
            if(($cause.HResult -band 65535) -eq 5){$denied=$true}
        }
    }
    if($message -match '^(One or more .*missing or damaged\.|Windows blocked changes|Update could not replace|The original Windows game source could not|The selected soundtrack is missing|The downloaded update is incomplete|The update could not be downloaded|An HD artwork file could not|A required game component is missing|Setup could not finish)'){return $message}
    if($sharing -or $message -match 'being used by another process|sharing violation|still open'){
        return 'Update could not replace a file that is still open. Close other Setup and game windows, reopen Setup, then try Update again.'
    }
    if($denied -or $message -match 'access.*denied|UnauthorizedAccess'){
        return 'Windows blocked changes to the mod folder. Check its permissions and Windows Security, then try again.'
    }
    if($message -match '(?i)track.*(texture|atlas)|texture.*(corrupt|damaged|missing)|PlayStation texture cache|native texture|packed.*sheet|\.TGA|\.CMP|\.TIM'){
        return 'One or more track or sprite textures are missing or damaged. Restore the configured game sources and run Install / repair.'
    }
    if($message -match '(?i)TRACK\d+[/\\]TRACK\.WAD|track files'){
        return 'One or more track files are missing or damaged. Select your original Windows game source and run Install / repair.'
    }
    if($message -match '(?i)sound effect|sound-effect|audio/source|audio bank'){
        return 'One or more sound effects are missing or damaged. Select your original Windows game source and run Install / repair.'
    }
    if($message -match '(?i)soundtrack|CD audio|Missing music|music source'){
        return 'The selected soundtrack is missing or incomplete. Restore its disc or music folder, or choose No music in Setup.'
    }
    if($message -match '(?i)Windows game needs repair|source is unavailable|supported PC executable|Windows game data|Windows game source|Select the Windows game source'){
        return 'The original Windows game source could not be found or verified. Choose the PC game ZIP, disc or folder and run Install / repair.'
    }
    if($message -match '(?i)PlayStation artwork|PlayStation texture data|European.*disc'){
        return 'The PlayStation artwork source could not be read. Choose the European WipEout 2097 disc, or clear the optional PlayStation source.'
    }
    if($message -match '(?i)PNG|artwork|Cannot map|page number'){
        return 'An HD artwork file could not be loaded. Check the PNG files in assets/hd, then try again.'
    }
    if($message -match '(?i)checksum|ZIP|inventory|release package|package.*(invalid|incomplete)|Updated file verification'){
        return 'The downloaded update is incomplete or damaged. Try Update again.'
    }
    if($message -match '(?i)curl|download|network|connect|TLS|certificate'){
        return 'The update could not be downloaded. Check your internet connection and try Update again.'
    }
    if($message -match '(?i)Missing .*\.(dll|exe)|required.*DLL|runtime component'){
        return 'A required game component is missing or damaged. Run Update or Install / repair; also check Windows Security for a quarantined file.'
    }
    if($message -match '(?i)The game exited with code|game crashed'){
        return 'The game stopped unexpectedly. Restart it; if it happens again, share the diagnostic log.'
    }
    if($Stage -match '(?i)Starting the game'){
        return 'The game could not be started. Run Install / repair and check Windows Security for blocked game files.'
    }
    if($Stage -match '(?i)update'){return 'The update could not be completed. Reopen Setup and try Update again; details are saved in the log.'}
    return 'Setup could not finish checking or repairing the installation. Run Install / repair; details are saved in the log.'
}
function Complete-WoFailure($Record,[string]$Stage,[string]$StatusFile,[switch]$NoDialog) {
    Write-WoFailure $Record $Stage
    $explanation=Get-WoFailureMessage $Record $Stage
    $status=[ordered]@{percent=0;state='error';message=$explanation;log_path=$script:WoLogPath;error_shown=$false}
    # Show the same explanation in Setup immediately, even while its worker's
    # dialog is open. Update popup ownership before the worker exits.
    if($StatusFile){try{[IO.File]::WriteAllText($StatusFile,(ConvertTo-Json $status),(New-Object Text.UTF8Encoding($false)))}catch{}}
    $status.error_shown=Show-WoFailure $explanation -NoDialog:$NoDialog
    if($StatusFile){try{[IO.File]::WriteAllText($StatusFile,(ConvertTo-Json $status),(New-Object Text.UTF8Encoding($false)))}catch{}}
}
function Get-WoExitDescription([int]$Code) {
    $hex='0x'+[BitConverter]::ToUInt32([BitConverter]::GetBytes($Code),0).ToString('X8')
    $hint=switch($hex){
        '0xC0000135' {'A required DLL could not be loaded.'}
        '0xC000007B' {'A required executable or DLL has an invalid format.'}
        '0xC0000428' {'Windows could not verify a file signature; check Windows Security and Code Integrity events.'}
        '0xC0000022' {'Access was denied; check Windows Security for a blocked file.'}
        '0xC0000005' {'The game crashed with an access violation.'}
        default {'Check this session log and Windows Security / Event Viewer for details.'}
    }
    return "The game exited with code $hex ($Code). $hint"
}
function Write-WoProcessSnapshot([Diagnostics.Process]$Process,[switch]$Modules) {
    try{
        $Process.Refresh()
        Write-WoLog 'game_memory' @{private_bytes=$Process.PrivateMemorySize64;virtual_bytes=$Process.VirtualMemorySize64;working_set_bytes=$Process.WorkingSet64;peak_working_set_bytes=$Process.PeakWorkingSet64;peak_paged_bytes=$Process.PeakPagedMemorySize64;threads=$Process.Threads.Count;handles=$Process.HandleCount}
    }catch{Write-WoLog 'game_memory_unavailable' $_.Exception.Message}
    if($Modules){
        try{
            $items=@($Process.Modules|Select-Object -First 256|ForEach-Object {
                @{name=$_.ModuleName;base=('0x{0:X8}' -f $_.BaseAddress.ToInt64());bytes=$_.ModuleMemorySize;version=$_.FileVersionInfo.FileVersion}
            })
            Write-WoLog 'game_modules' @{modules=$items}
        }catch{Write-WoLog 'game_modules_unavailable' $_.Exception.Message}
    }
}
function Write-WoGameConfiguration([string]$GameRoot) {
    # Allowlist settings instead of dumping arbitrary user-edited INI content.
    $allowed=@('EngineTrails','GeometryWidthPercent','FloatRotations','SoftShadows','ShadowOpacityPercent','ShadowSoftnessPercent','GlowAftOffset','GlowUpOffset','ShowFPS','ShadowLiftPercent','HighlightsPercent','Soundtrack','MusicVolume','EffectsVolume','Track','SoundMode','EnableDDrawCompat','LogLevel','DisplayResolution','DisplayAspectRatio','FullscreenMode','ResolutionScale','RenderColorDepth','Antialiasing','TextureFilter','ColorKeyMethod','FpsLimiter','VertexFixup','VertexBufferMemoryType','SpriteDetection','SpriteFilter','VSync','PresentDelay','CpuAffinity','CpuAffinityRotation','BltFilter')
    foreach($name in @('Wipeout2097.ini','DDrawCompat.ini','DDrawCompat-Wipeout2097.ini')){
        $path=Join-Path $GameRoot $name
        if(-not [IO.File]::Exists($path)){continue}
        try{
            $values=[ordered]@{};$section=''
            foreach($line in [IO.File]::ReadAllLines($path)){
                if($line -match '^\s*\[([\w -]+)\]'){$section=$matches[1]}
                elseif($line -match '^\s*(\w+)\s*=\s*([^;#]*)'){
                    $key=$matches[1];$value=$matches[2].Trim()
                    if($key -in $allowed -and $value -match '^[a-zA-Z0-9_().,% +:-]{0,100}$'){$values[$section+'/'+$key]=$value}
                }
            }
            Write-WoLog 'game_configuration' @{file=$name;settings=$values}
        }catch{Write-WoLog 'game_configuration_unavailable' @{file=$name;message=$_.Exception.Message}}
    }
}
function Save-WoWindowsCrashEvent([int]$ProcessId,[datetime]$StartedUtc) {
    # Application Error 1000 has named fields. Never copy its rendered Message,
    # report/account identifiers, executable paths, or unrelated crash records.
    for($attempt=0;$attempt -lt 3;$attempt++){
        try{
            $events=@(Get-WinEvent -FilterHashtable @{LogName='Application';ProviderName='Application Error';Id=1000;StartTime=$StartedUtc.ToLocalTime().AddSeconds(-1)} -MaxEvents 24 -ErrorAction Stop)
            foreach($event in $events){
                $xml=[xml]$event.ToXml();$fields=@{}
                foreach($item in $xml.Event.EventData.Data){$fields[[string]$item.Name]=[string]$item.'#text'}
                $eventPid=0L;$pidText=[string]$fields.ProcessId
                if($pidText -match '^0x[0-9a-f]+$'){$eventPid=[Convert]::ToInt64($pidText.Substring(2),16)}
                elseif($pidText -match '^\d+$'){$eventPid=[long]$pidText}
                if($eventPid -ne $ProcessId -or [IO.Path]::GetFileName([string]$fields.AppName) -ine 'Wipeout2097.exe'){continue}
                $data=[ordered]@{time=$event.TimeCreated.ToUniversalTime().ToString('o')}
                foreach($key in @('AppName','AppVersion','ModuleName','ModuleVersion','ExceptionCode','FaultingOffset')){
                    $value=[string]$fields[$key]
                    if($key -in 'AppName','ModuleName'){$value=[IO.Path]::GetFileName($value)}
                    $data[$key]=$value
                }
                Write-WoLog 'windows_application_error' $data
                return
            }
        }catch{
            if($_.FullyQualifiedErrorId -notmatch 'NoMatchingEventsFound'){
                Write-WoLog 'windows_application_error_unavailable' $_.Exception.Message
                return
            }
        }
        if($attempt -lt 2){Start-Sleep -Milliseconds 1000}
    }
    Write-WoLog 'windows_application_error_unavailable' 'No matching Application Error event was available yet; native exception breadcrumbs may still identify the fault.'
}
function Save-WoGameLogs([string]$GameRoot) {
    $sources=@()
    foreach($name in @('Debug.txt','DDrawCompat-Wipeout2097.log')){
        $sources+=@{name=$name;path=(Join-Path $GameRoot $name);current_session=$true}
    }
    # The native runtime writes beside the mod, not inside wobble. These
    # breadcrumbs include video_started/video_exit_code and renderer progress.
    $parent=[IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($GameRoot).TrimEnd('\'))
    foreach($name in @('game.log','game-crash.log','game-debug.txt')){$sources+=@{name=$name;path=(Join-Path $parent ('logs/'+$name));current_session=$true}}
    foreach($name in @('video-intro.log','video-xtro1.log','video-xtro2.log','video-xtro3.log','video-xtro4.log','video-error.log')){
        $sources+=@{name=$name;path=(Join-Path $script:WoLogRoot ('logs/'+$name));current_session=$true}
    }
    foreach($source in $sources){
        $name=$source.name;$path=$source.path
        if([IO.File]::Exists($path)){
            try{
                if($source.current_session -and $script:WoLogStartedUtc -and [IO.File]::GetLastWriteTimeUtc($path) -lt $script:WoLogStartedUtc){continue}
                $text=[IO.File]::ReadAllText($path)
                $text=[regex]::Replace($text,'(?im)^.*(?:DeviceIdentifier|PNPDevice|AdapterLuid|SerialNumber).*(?:\r?\n|$)','')
                $text=[regex]::Replace($text,'(?i)\b[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}\b','[identifier omitted]')
                $target=[IO.Path]::ChangeExtension($script:WoLogPath,$name)
                [IO.File]::WriteAllText($target,('# WO4K mod_version='+$script:WoLogVersion+[Environment]::NewLine+(Protect-WoLogText $text)),(New-Object Text.UTF8Encoding($false)))
                [IO.File]::Delete($path)
                Write-WoLog 'game_log_saved' @{file=[IO.Path]::GetFileName($target)}
                # A pasted launcher log should retain useful evidence even
                # when the reporter omits its separately saved attachments.
                $tail=@($text -split '\r?\n'|Where-Object {$_}|Select-Object -Last 80)
                Write-WoLog 'game_log_tail' @{file=[IO.Path]::GetFileName($target);lines=$tail}
            }catch{Write-WoLog 'game_log_unavailable' @{file=$name;message=$_.Exception.Message}}
        }
    }
}
