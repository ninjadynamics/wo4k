#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "../src/runtime/portable_paths.c"
static void bytes(BYTE *to,const BYTE *from,UINT n){while(n--)*to++=*from++;}
static BOOL same(const char *a,const char *b){while(*a && *a==*b){++a;++b;}return *a==*b;}
void __stdcall test_entry(void) {
    WCHAR root[MAX_PATH];if(!GetEnvironmentVariableW(L"WO_TEST_GAME_ROOT",root,MAX_PATH)||!SetCurrentDirectoryW(root))ExitProcess(1);
    const char *in[]={".\\Sound\\ENGINES.WAV","Pics\\Hi-res\\HUD2.tga",".\\Blitsurf\\Hi-res\\RSTARTXL.PPM",".\\Textures\\EXPLODE.CMP","Common\\CNTRL.CMP","Track01\\TRACKTEX.CMP","Track01\\TRACK.WAD","textures\\sd\\Pics\\Hi-res\\HUD2.tga",
        ".\\\\sound\\shp2shp.wav",".\\\\pics\\hi-res\\HUD2.tga",".//Sound/ENGINES.WAV",".\\.\\\\Sound\\ENGINES.WAV"};
    const char *out[]={"audio\\ENGINES.WAV","textures\\sd\\Pics\\Hi-res\\HUD2.tga","textures\\sd\\Blitsurf\\Hi-res\\RSTARTXL.PPM","textures\\sd\\Textures\\EXPLODE.CMP","textures\\sd\\Common\\CNTRL.CMP","textures\\sd\\Track01\\TRACKTEX.CMP","Track01\\TRACK.WAD","textures\\sd\\Pics\\Hi-res\\HUD2.tga",
        "audio\\shp2shp.wav","textures\\sd\\pics\\hi-res\\HUD2.tga","audio\\ENGINES.WAV","audio\\ENGINES.WAV"};
    const UINT count=sizeof(in)/sizeof(in[0]);
    for(UINT i=0;i<count;++i){char buffer[512];if(!same(wo_path(in[i],buffer,sizeof(buffer)),out[i]))ExitProcess(2);}
    const char *unchanged[]={"C:\\sound\\ENGINES.WAV","\\\\server\\sound\\ENGINES.WAV","\\sound\\ENGINES.WAV","..\\sound\\ENGINES.WAV","textures\\hd\\custom.png"};
    for(UINT i=0;i<sizeof(unchanged)/sizeof(unchanged[0]);++i){char buffer[512];if(wo_path(unchanged[i],buffer,sizeof(buffer))!=unchanged[i])ExitProcess(20+i);}
    HANDLE file=CreateFileW(L"Wipeout2097.exe",GENERIC_READ,FILE_SHARE_READ,0,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,0);if(file==INVALID_HANDLE_VALUE)ExitProcess(3);
    LARGE_INTEGER size;if(!GetFileSizeEx(file,&size)||size.QuadPart>2000000)ExitProcess(4);
    BYTE *raw=VirtualAlloc(0,(UINT)size.QuadPart,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);DWORD read=0;
    if(!raw||!ReadFile(file,raw,(DWORD)size.QuadPart,&read,0)||read!=size.QuadPart)ExitProcess(5);CloseHandle(file);
    IMAGE_NT_HEADERS32 *nt=(IMAGE_NT_HEADERS32 *)(raw+((IMAGE_DOS_HEADER *)raw)->e_lfanew);
    BYTE *image=VirtualAlloc(0,nt->OptionalHeader.SizeOfImage,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);if(!image)ExitProcess(6);
    bytes(image,raw,nt->OptionalHeader.SizeOfHeaders);IMAGE_SECTION_HEADER *sections=IMAGE_FIRST_SECTION(nt);
    for(UINT i=0;i<nt->FileHeader.NumberOfSections;++i)bytes(image+sections[i].VirtualAddress,raw+sections[i].PointerToRawData,sections[i].SizeOfRawData);
    IMAGE_IMPORT_DESCRIPTOR *desc=(IMAGE_IMPORT_DESCRIPTOR *)(image+nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);DWORD *slot=0;
    for(;desc->Name;++desc){IMAGE_THUNK_DATA32 *names=(IMAGE_THUNK_DATA32 *)(image+desc->OriginalFirstThunk),*slots=(IMAGE_THUNK_DATA32 *)(image+desc->FirstThunk);
        for(UINT i=0;names[i].u1.AddressOfData;++i)if(!(names[i].u1.Ordinal&IMAGE_ORDINAL_FLAG32)){
            IMAGE_IMPORT_BY_NAME *name=(IMAGE_IMPORT_BY_NAME *)(image+names[i].u1.AddressOfData);
            if(same((const char *)name->Name,"CreateFileA")){slot=&slots[i].u1.Function;*slot=(DWORD)(UINT_PTR)CreateFileA;}
        }
    }
    if(!slot)ExitProcess(7);wo_paths_initialize(image);if(*slot!=(DWORD)(UINT_PTR)wo_portable_open)ExitProcess(8);
    WoOpenFn open=(WoOpenFn)(UINT_PTR)*slot;
    for(UINT i=0;i<count;++i){HANDLE h=open(in[i],GENERIC_READ,FILE_SHARE_READ,0,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,0);if(h==INVALID_HANDLE_VALUE)ExitProcess(30+i);CloseHandle(h);}
    ExitProcess(0);
}
