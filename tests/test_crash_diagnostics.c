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
/* Direct reads are validated region by region: committed read/write and
 * read-only pages pass, a span reaching no-access, reserved or guard pages
 * fails without touching them, and so do NULL and wrapping ranges. */
static void test_reader(void) {
    BYTE *page=VirtualAlloc(0,0x5000,MEM_RESERVE,PAGE_NOACCESS),pair[8];
    DWORD old,value=0x12345678,out=0;MEMORY_BASIC_INFORMATION info;UINT i;
    if(!page)ExitProcess(20);
    if(!VirtualAlloc(page,0x2000,MEM_COMMIT,PAGE_READWRITE))ExitProcess(21);
    for(i=0;i<8;++i)page[0xffc+i]=(BYTE)(0xa0+i);
    if(!VirtualProtect(page+0x1000,0x1000,PAGE_READONLY,&old))ExitProcess(21);
    if(!VirtualAlloc(page+0x2000,0x1000,MEM_COMMIT,PAGE_NOACCESS))ExitProcess(22);
    if(!VirtualAlloc(page+0x3000,0x1000,MEM_COMMIT,PAGE_READWRITE|PAGE_GUARD))ExitProcess(22);
    /* page+0x4000 stays reserved. */
    if(!wo_crash_read((DWORD)(UINT_PTR)&value,&out,4) || out!=0x12345678)ExitProcess(23);
    if(!wo_crash_read((DWORD)(UINT_PTR)(page+0xffc),pair,8))ExitProcess(24);
    for(i=0;i<8;++i)if(pair[i]!=(BYTE)(0xa0+i))ExitProcess(24);
    if(wo_crash_read((DWORD)(UINT_PTR)(page+0x1ffc),pair,8))ExitProcess(25);
    if(wo_crash_read((DWORD)(UINT_PTR)(page+0x2000),pair,4))ExitProcess(26);
    if(wo_crash_read((DWORD)(UINT_PTR)(page+0x3000),pair,4))ExitProcess(27);
    if(!VirtualQuery(page+0x3000,&info,sizeof(info)) || !(info.Protect&PAGE_GUARD))ExitProcess(28);
    if(wo_crash_read((DWORD)(UINT_PTR)(page+0x4000),pair,4))ExitProcess(29);
    if(wo_crash_read(0,pair,4) || wo_crash_read(0xfffffffe,pair,4))ExitProcess(30);
    if(wo_crash_read((DWORD)(UINT_PTR)page,pair,0))ExitProcess(31);
    VirtualFree(page,0,MEM_RELEASE);
    wo_crash_value("reader_checks_passed",1,8);
    wo_crash_uninstall();ExitProcess(0);
}
void WINAPI test_entry(void) {
    ULONG_PTR details[2];PVOID recovery;UINT i;
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
    if(!GetEnvironmentVariableW(L"WO_CRASH_TEST_LOG",test_path,MAX_PATH))ExitProcess(11);
    GetEnvironmentVariableW(L"WO_CRASH_TEST_MODE",test_mode,16);
    SetUnhandledExceptionFilter(previous);wo_crash_install(test_path);
    if(test_mode[0]==L'r')test_reader();
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
