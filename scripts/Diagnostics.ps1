# Local logs: only the explicitly selected, non-identifying hardware fields.
$script:WoLogPath=$null
$script:WoLogRoot=$null
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
    $script:WoLogRoot=[IO.Path]::GetFullPath($Root).TrimEnd('\')
    $name=$Operation+'-'+(Get-Date -Format 'yyyyMMdd-HHmmss-fff')+'-'+$PID+'.log'
    $directory=Join-Path $script:WoLogRoot 'logs'
    try{[IO.Directory]::CreateDirectory($directory)|Out-Null;$script:WoLogPath=Join-Path $directory $name;[IO.File]::WriteAllText($script:WoLogPath,'')}
    catch{$directory=Join-Path ([IO.Path]::GetTempPath()) 'WO4K/logs';[IO.Directory]::CreateDirectory($directory)|Out-Null;$script:WoLogPath=Join-Path $directory $name;[IO.File]::WriteAllText($script:WoLogPath,'')}
    $version='unknown';try{$version=[IO.File]::ReadAllText((Join-Path $Root 'VERSION')).Trim()}catch{}
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
function Complete-WoFailure($Record,[string]$Stage,[string]$StatusFile,[switch]$NoDialog) {
    Write-WoFailure $Record $Stage
    $shown=Show-WoFailure ($Stage+': '+$Record.Exception.Message) -NoDialog:$NoDialog
    if($StatusFile){try{
        $status=[ordered]@{percent=0;state='error';message=(Protect-WoLogText $Record.Exception.Message);log_path=$script:WoLogPath;error_shown=$shown}
        [IO.File]::WriteAllText($StatusFile,(ConvertTo-Json $status),(New-Object Text.UTF8Encoding($false)))
    }catch{}}
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
function Save-WoGameLogs([string]$GameRoot) {
    foreach($name in @('Debug.txt','DDrawCompat-Wipeout2097.log')){
        $path=Join-Path $GameRoot $name
        if([IO.File]::Exists($path)){
            try{
                $text=[IO.File]::ReadAllText($path)
                $text=[regex]::Replace($text,'(?im)^.*(?:DeviceIdentifier|PNPDevice|AdapterLuid|SerialNumber).*(?:\r?\n|$)','')
                $text=[regex]::Replace($text,'(?i)\b[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}\b','[identifier omitted]')
                $target=[IO.Path]::ChangeExtension($script:WoLogPath,$name)
                [IO.File]::WriteAllText($target,(Protect-WoLogText $text),(New-Object Text.UTF8Encoding($false)))
                [IO.File]::Delete($path)
                Write-WoLog 'game_log_saved' @{file=[IO.Path]::GetFileName($target)}
            }catch{Write-WoLog 'game_log_unavailable' @{file=$name;message=$_.Exception.Message}}
        }
    }
}