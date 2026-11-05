/* Bounded, allocation-free x86 exception breadcrumbs. Never recover from or
 * suppress an exception: first-chance observations may be handled elsewhere.
 * No process memory dump, paths, or raw stack contents are recorded. */
#include "build/runtime_version.h"
static HANDLE wo_crash_file=INVALID_HANDLE_VALUE;
static LPTOP_LEVEL_EXCEPTION_FILTER wo_crash_previous;
static volatile LONG wo_crash_busy,wo_crash_count;
static DWORD wo_crash_started;
static PVOID wo_crash_observer;
static BOOL wo_crash_installed;

static UINT wo_crash_text(char *out,UINT n,const char *text) {
    while(*text && n<490)out[n++]=*text++;
    return n;
}
static UINT wo_crash_hex(char *out,UINT n,ULONGLONG value,UINT digits) {
    static const char hex[]="0123456789ABCDEF";
    out[n++]='0';out[n++]='x';
    while(digits) {--digits;out[n++]=hex[(UINT)(value>>(digits*4))&15];}
    return n;
}
static void wo_crash_value(const char *name,ULONGLONG value,UINT digits) {
    char line[512];DWORD written;UINT n=wo_crash_text(line,0,name);
    line[n++]=' ';n=wo_crash_hex(line,n,value,digits);line[n++]='\r';line[n++]='\n';
    if(wo_crash_file!=INVALID_HANDLE_VALUE)WriteFile(wo_crash_file,line,n,&written,0);
}
static void wo_crash_string(const char *name,const char *value) {
    char line[512];DWORD written;UINT n=wo_crash_text(line,0,name);
    line[n++]=' ';n=wo_crash_text(line,n,value);line[n++]='\r';line[n++]='\n';
    if(wo_crash_file!=INVALID_HANDLE_VALUE)WriteFile(wo_crash_file,line,n,&written,0);
}
static void wo_crash_address(const char *name,DWORD address) {
    MEMORY_BASIC_INFORMATION info;WCHAR path[MAX_PATH];char line[512];
    DWORD written;UINT n=wo_crash_text(line,0,name),i,start=0;
    line[n++]=' ';n=wo_crash_hex(line,n,address,8);
    if(VirtualQuery((void *)(UINT_PTR)address,&info,sizeof(info)) && info.Type==MEM_IMAGE &&
       GetModuleFileNameW((HMODULE)info.AllocationBase,path,MAX_PATH)) {
        path[MAX_PATH-1]=0;
        for(i=0;path[i];++i)if(path[i]==L'\\' || path[i]==L'/')start=i+1;
        line[n++]=' ';
        for(i=start;path[i] && n<470;++i)line[n++]=path[i]<128?(char)path[i]:'?';
        line[n++]='+';n=wo_crash_hex(line,n,address-(DWORD)(UINT_PTR)info.AllocationBase,8);
    }
    line[n++]='\r';line[n++]='\n';
    if(wo_crash_file!=INVALID_HANDLE_VALUE)WriteFile(wo_crash_file,line,n,&written,0);
}
static BOOL wo_crash_read(DWORD address,void *out,UINT bytes) {
    SIZE_T read=0;
    return ReadProcessMemory(GetCurrentProcess(),(void *)(UINT_PTR)address,out,bytes,&read) && read==bytes;
}
static BOOL wo_crash_executable(DWORD address) {
    MEMORY_BASIC_INFORMATION info;
    return VirtualQuery((void *)(UINT_PTR)address,&info,sizeof(info)) && info.State==MEM_COMMIT &&
        info.Type==MEM_IMAGE && !(info.Protect&(PAGE_GUARD|PAGE_NOACCESS)) &&
        (info.Protect&(PAGE_EXECUTE|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY));
}
static void wo_crash_record(EXCEPTION_POINTERS *exception,BOOL unhandled) {
    EXCEPTION_RECORD *record;CONTEXT *context;MEMORYSTATUSEX memory;
    MEMORY_BASIC_INFORMATION info;DWORD frame,pair[2],candidate;UINT i,found=0;
    if(!exception || !exception->ExceptionRecord || !exception->ContextRecord)return;
    record=exception->ExceptionRecord;context=exception->ContextRecord;
    /* Debugger, language and routine driver exceptions are not crash evidence. */
    if(!unhandled && record->ExceptionCode!=EXCEPTION_ACCESS_VIOLATION &&
       record->ExceptionCode!=EXCEPTION_IN_PAGE_ERROR && record->ExceptionCode!=EXCEPTION_ILLEGAL_INSTRUCTION &&
       record->ExceptionCode!=EXCEPTION_INT_DIVIDE_BY_ZERO && record->ExceptionCode!=EXCEPTION_STACK_OVERFLOW)return;
    if(InterlockedCompareExchange(&wo_crash_busy,1,0))return;
    if(!unhandled && InterlockedIncrement(&wo_crash_count)>16){InterlockedExchange(&wo_crash_busy,0);return;}
    wo_crash_value(unhandled?"exception_unhandled":"exception_first_chance",record->ExceptionCode,8);
    wo_crash_value("elapsed_ms",GetTickCount()-wo_crash_started,8);
    wo_crash_value("thread_id",GetCurrentThreadId(),8);
    wo_crash_address("fault_instruction",(DWORD)(UINT_PTR)record->ExceptionAddress);
    wo_crash_value("exception_flags",record->ExceptionFlags,8);
    if((record->ExceptionCode==EXCEPTION_ACCESS_VIOLATION || record->ExceptionCode==EXCEPTION_IN_PAGE_ERROR) && record->NumberParameters>=2) {
        /* 0=read, 1=write, 8=execute. */
        wo_crash_value("access_kind_read0_write1_execute8",record->ExceptionInformation[0],8);
        wo_crash_value("access_address",record->ExceptionInformation[1],8);
        if(VirtualQuery((void *)record->ExceptionInformation[1],&info,sizeof(info))) {
            wo_crash_value("access_page_state",info.State,8);wo_crash_value("access_page_protection",info.Protect,8);
        }
        if(record->ExceptionCode==EXCEPTION_IN_PAGE_ERROR && record->NumberParameters>=3)
            wo_crash_value("in_page_status",record->ExceptionInformation[2],8);
    }
    wo_crash_value("eax",context->Eax,8);wo_crash_value("ebx",context->Ebx,8);
    wo_crash_value("ecx",context->Ecx,8);wo_crash_value("edx",context->Edx,8);
    wo_crash_value("esi",context->Esi,8);wo_crash_value("edi",context->Edi,8);
    wo_crash_value("esp",context->Esp,8);wo_crash_value("ebp",context->Ebp,8);
    wo_crash_value("eflags",context->EFlags,8);
    frame=context->Ebp;
    for(i=0;i<16 && frame>=context->Esp && frame-context->Esp<0x100000;++i) {
        if(!wo_crash_read(frame,pair,sizeof(pair)))break;
        if(wo_crash_executable(pair[1]))wo_crash_address("frame_return",pair[1]);
        if(pair[0]<=frame)break;
        frame=pair[0];
    }
    /* Optimized code often omits EBP. Label these as candidates, not frames. */
    for(i=0;i<128 && found<16;++i) {
        if(!wo_crash_read(context->Esp+i*4,&candidate,4))break;
        if(wo_crash_executable(candidate)){wo_crash_address("stack_code_candidate",candidate);++found;}
    }
    memory.dwLength=sizeof(memory);
    if(GlobalMemoryStatusEx(&memory)) {
        wo_crash_value("physical_available_bytes",memory.ullAvailPhys,16);
        wo_crash_value("commit_available_bytes",memory.ullAvailPageFile,16);
        wo_crash_value("process_virtual_total_bytes",memory.ullTotalVirtual,16);
        wo_crash_value("process_virtual_available_bytes",memory.ullAvailVirtual,16);
    }
    if(wo_crash_file!=INVALID_HANDLE_VALUE)FlushFileBuffers(wo_crash_file);
    InterlockedExchange(&wo_crash_busy,0);
}
static LONG CALLBACK wo_crash_observe(EXCEPTION_POINTERS *exception) {
    wo_crash_record(exception,FALSE);
    return EXCEPTION_CONTINUE_SEARCH;
}
static LONG WINAPI wo_crash_unhandled(EXCEPTION_POINTERS *exception) {
    wo_crash_record(exception,TRUE);
    return wo_crash_previous?wo_crash_previous(exception):EXCEPTION_CONTINUE_SEARCH;
}
static void wo_crash_install(const WCHAR *path) {
    wo_crash_started=GetTickCount();
    wo_crash_file=CreateFileW(path,GENERIC_WRITE,FILE_SHARE_READ,0,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,0);
    wo_crash_string("mod_version",WO_MOD_VERSION);
    wo_crash_value("diagnostics_version",1,8);
    wo_crash_value("process_bits",32,8);
    wo_crash_observer=AddVectoredExceptionHandler(1,wo_crash_observe);
    wo_crash_value("first_chance_observer_installed",wo_crash_observer!=0,8);
    wo_crash_previous=SetUnhandledExceptionFilter(wo_crash_unhandled);
    wo_crash_installed=TRUE;
}
static void wo_crash_uninstall(void) {
    LPTOP_LEVEL_EXCEPTION_FILTER current;
    if(!wo_crash_installed)return;
    if(wo_crash_observer)RemoveVectoredExceptionHandler(wo_crash_observer);
    current=SetUnhandledExceptionFilter(wo_crash_previous);
    if(current!=wo_crash_unhandled)SetUnhandledExceptionFilter(current);
    if(wo_crash_file!=INVALID_HANDLE_VALUE)CloseHandle(wo_crash_file);
    wo_crash_file=INVALID_HANDLE_VALUE;
    wo_crash_installed=FALSE;
}
