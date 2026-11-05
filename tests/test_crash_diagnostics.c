#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "../src/runtime/crash_diagnostics.c"
static WCHAR test_path[MAX_PATH],test_mode[16];
static LONG CALLBACK recover(EXCEPTION_POINTERS *p) {
    return p->ExceptionRecord->ExceptionCode==EXCEPTION_ACCESS_VIOLATION?EXCEPTION_CONTINUE_EXECUTION:EXCEPTION_CONTINUE_SEARCH;
}
static LONG WINAPI previous(EXCEPTION_POINTERS *p) {
    wo_crash_value("previous_filter_called",1,8);
    ExitProcess(p->ExceptionRecord->ExceptionCode);
    return EXCEPTION_CONTINUE_SEARCH;
}
void WINAPI test_entry(void) {
    ULONG_PTR details[2];PVOID recovery;UINT i;
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
    if(!GetEnvironmentVariableW(L"WO_CRASH_TEST_LOG",test_path,MAX_PATH))ExitProcess(11);
    GetEnvironmentVariableW(L"WO_CRASH_TEST_MODE",test_mode,16);
    SetUnhandledExceptionFilter(previous);wo_crash_install(test_path);
    if(test_mode[0]==L'h') {
        recovery=AddVectoredExceptionHandler(0,recover);
        details[0]=1;details[1]=0x123;
        for(i=0;i<20;++i)RaiseException(EXCEPTION_ACCESS_VIOLATION,0,2,details);
        RemoveVectoredExceptionHandler(recovery);
        wo_crash_value("handled_execution_continued",1,8);
        wo_crash_uninstall();ExitProcess(0);
    }
    /* Actual unhandled x86 write fault, not just an emulated exit code. */
    *(volatile DWORD *)(UINT_PTR)1=7;
    ExitProcess(12);
}
