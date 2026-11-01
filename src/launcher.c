/* MIT. Log bootstrap failures even when PowerShell cannot run the script. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wchar.h>
#include <stdio.h>
static void report(const WCHAR *message,const WCHAR *log) {
    WCHAR text[33000];
    _snwprintf(text,33000,L"%ls\r\n\r\nDiagnostic log:\r\n%ls\r\n\r\nShare this log with the WO4K developer.",message,log);
    MessageBoxW(0,text,L"WO4K - launcher failed",MB_OK|MB_ICONERROR);
}
static void line(HANDLE file,const char *event,DWORD value) {
    char text[160];DWORD written;int n=snprintf(text,sizeof(text),"%s %lu (0x%08lX)\r\n",event,(unsigned long)value,(unsigned long)value);
    if(file!=INVALID_HANDLE_VALUE)WriteFile(file,text,(DWORD)n,&written,0);
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE previous,PWSTR args,int show) {
    (void)instance;(void)previous;(void)args;(void)show;
    WCHAR root[32768],script[32768],command[33000],shell[MAX_PATH],log[32768],marker[32768],ready[32768];
    DWORD n=GetModuleFileNameW(0,root,32768),exitcode=1,error;
    if(!n || n>=32000)return 2;
    while(n && root[n-1]!=L'\\')--n;root[n]=0;
#ifdef WO_PLAY
    const WCHAR *name=L"play";const WCHAR *file=L"Play.ps1";
#else
    const WCHAR *name=L"setup";const WCHAR *file=L"Setup.ps1";
#endif
    SECURITY_ATTRIBUTES security={sizeof(security),0,TRUE};
    _snwprintf(log,32768,L"%lslogs",root);CreateDirectoryW(log,0);
    _snwprintf(log,32768,L"%lslogs\\bootstrap-%ls-%lu.log",root,name,(unsigned long)GetCurrentProcessId());
    HANDLE output=CreateFileW(log,GENERIC_WRITE,FILE_SHARE_READ,&security,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,0);
    if(output==INVALID_HANDLE_VALUE){
        WCHAR temp[MAX_PATH];GetTempPathW(MAX_PATH,temp);
        _snwprintf(log,32768,L"%lsWO4K",temp);CreateDirectoryW(log,0);
        _snwprintf(log,32768,L"%lsWO4K\\bootstrap-%ls-%lu.log",temp,name,(unsigned long)GetCurrentProcessId());
        output=CreateFileW(log,GENERIC_WRITE,FILE_SHARE_READ,&security,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,0);
    }
    if(output==INVALID_HANDLE_VALUE){report(L"WO4K could not create a diagnostic log. Check that the mod folder is writable.",L"Unavailable");return 3;}
    line(output,"launcher_start",1);
    typedef LONG (WINAPI *VersionFn)(OSVERSIONINFOW *);
    VersionFn version=(VersionFn)GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"RtlGetVersion");
    OSVERSIONINFOW os={0};os.dwOSVersionInfoSize=sizeof(os);
    if(version && version(&os)==0){line(output,"windows_major",os.dwMajorVersion);line(output,"windows_minor",os.dwMinorVersion);line(output,"windows_build",os.dwBuildNumber);}
    _snwprintf(marker,32768,L"%ls.reported",log);DeleteFileW(marker);
    SetEnvironmentVariableW(L"WO4K_ERROR_MARKER",marker);
    _snwprintf(ready,32768,L"%ls.ready",log);DeleteFileW(ready);
#ifdef WO_PLAY
    SetEnvironmentVariableW(L"WO4K_READY_MARKER",0);
#else
    SetEnvironmentVariableW(L"WO4K_READY_MARKER",ready);
#endif
    _snwprintf(script,32768,L"%ls%ls",root,file);
    if(GetFileAttributesW(script)==INVALID_FILE_ATTRIBUTES){line(output,"script_missing",GetLastError());CloseHandle(output);report(L"The launcher script is missing. Extract the complete WO4K release ZIP, then try again.",log);return 4;}
    GetSystemDirectoryW(shell,MAX_PATH);wcscat(shell,L"\\WindowsPowerShell\\v1.0\\powershell.exe");
    _snwprintf(command,33000,L"\"%ls\" -NoLogo -NoProfile -STA -ExecutionPolicy Bypass -WindowStyle Hidden -File \"%ls\"",shell,script);
    STARTUPINFOW startup={0};PROCESS_INFORMATION process={0};startup.cb=sizeof(startup);
    startup.dwFlags=STARTF_USESHOWWINDOW|STARTF_USESTDHANDLES;startup.wShowWindow=SW_HIDE;
    startup.hStdOutput=output;startup.hStdError=output;
    startup.hStdInput=CreateFileW(L"NUL",GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,&security,OPEN_EXISTING,0,0);
    BOOL started=CreateProcessW(shell,command,0,0,TRUE,CREATE_NO_WINDOW,0,root,&startup,&process);error=GetLastError();
    if(startup.hStdInput!=INVALID_HANDLE_VALUE)CloseHandle(startup.hStdInput);
    if(!started){line(output,"powershell_start_error",error);CloseHandle(output);report(L"Windows PowerShell could not start. Check Windows Security for a blocked file.",log);return 5;}
    CloseHandle(process.hThread);
#ifndef WO_PLAY
    /* Release Setup.exe once its GUI is ready, so Update can replace it. */
    while(WaitForSingleObject(process.hProcess,100)==WAIT_TIMEOUT){
        if(GetFileAttributesW(ready)!=INVALID_FILE_ATTRIBUTES){
            DeleteFileW(ready);CloseHandle(process.hProcess);line(output,"setup_ready",1);CloseHandle(output);return 0;
        }
    }
#else
    WaitForSingleObject(process.hProcess,INFINITE);
#endif
    DeleteFileW(ready);
    if(!GetExitCodeProcess(process.hProcess,&exitcode))exitcode=1;
    CloseHandle(process.hProcess);line(output,"powershell_exit",exitcode);CloseHandle(output);
    BOOL reported=GetFileAttributesW(marker)!=INVALID_FILE_ATTRIBUTES;DeleteFileW(marker);
    if(exitcode && !reported)report(L"WO4K could not finish starting. The script error or Windows exit code is recorded in the log. Check Windows Security if execution was blocked.",log);
    return (int)exitcode;
}