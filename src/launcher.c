/* MIT. Thin Windows launcher for the PowerShell GUI / game script. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wchar.h>
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE previous,PWSTR args,int show) {
    (void)instance;(void)previous;(void)args;(void)show;
    WCHAR path[32768],command[33000],shell[MAX_PATH];DWORD n=GetModuleFileNameW(0,path,32768);
    if(!n || n>=32768)return 2;while(n && path[n-1]!=L'\\')--n;
#ifdef WO_PLAY
    wcscpy(path+n,L"Play.ps1");
#else
    wcscpy(path+n,L"Setup.ps1");
#endif
    GetSystemDirectoryW(shell,MAX_PATH);wcscat(shell,L"\\WindowsPowerShell\\v1.0\\powershell.exe");
    _snwprintf(command,33000,L"\"%ls\" -NoLogo -NoProfile -STA -ExecutionPolicy Bypass -WindowStyle Hidden -File \"%ls\"",shell,path);
    STARTUPINFOW startup={0};PROCESS_INFORMATION process={0};startup.cb=sizeof(startup);startup.dwFlags=STARTF_USESHOWWINDOW;startup.wShowWindow=SW_HIDE;
    if(!CreateProcessW(shell,command,0,0,FALSE,CREATE_NO_WINDOW,0,0,&startup,&process)){MessageBoxW(0,L"Windows PowerShell could not start. Run Setup.ps1 using Windows PowerShell.",L"WipEout2097",MB_OK|MB_ICONERROR);return 3;}
    CloseHandle(process.hThread);CloseHandle(process.hProcess);return 0;
}
