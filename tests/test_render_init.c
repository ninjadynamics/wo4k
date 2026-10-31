/* Run the production renderer's entire hook initialization against a mapped,
 * privately owned game image. No process/GPU/user setting is changed. */
#include "../src/runtime/render_patch.c"
static UINT race_ui_event=99;
static UINT native_effect_calls;
static SHORT observed_native_key;
static void __cdecl fixture_effects(void *camera){(void)camera;++native_effect_calls;}
static void __cdecl fixture_detail_keys(void){observed_native_key=*(SHORT *)(game+0x66496a);*(SHORT *)(game+0x66496a)=-1;}
static void __cdecl fixture_reset(void){}
void wo_render_event(const char *name,DWORD value){
    const char *wanted="render_race_ui_clock";while(*name && *name==*wanted){++name;++wanted;}if(!*name && !*wanted)race_ui_event=value;
}
UINT wo_audio_bank(void){return 0;}UINT wo_audio_tracks(void){return 9;}
BOOL wo_audio_switch(UINT bank){(void)bank;return FALSE;}
UINT wo_audio_pref(const WCHAR *key,UINT fallback){(void)key;return fallback;}
void wo_audio_save(UINT m,UINT e,UINT t){(void)m;(void)e;(void)t;}
void wo_audio_flush(BOOL force){(void)force;}
BOOL wo_sim_initialize(BYTE *image){(void)image;return FALSE;}
void wo_sim_reset(void){}BOOL wo_sim_half_tick(void){return FALSE;}BOOL wo_sim_wants_half_tick(void){return FALSE;}
void wo_sim_latch_input(void){}void wo_sim_restore_input(void){}DWORD wo_sim_extra_serial(void){return 0;}
void wo_sim_trail_shift(int ship,float *out){(void)ship;out[0]=out[1]=out[2]=0;}
void __stdcall test_entry(void){
    BYTE *image=VirtualAlloc(0,0x7ee000,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE);if(!image)ExitProcess(5);
    WCHAR root[MAX_PATH],path[MAX_PATH];
    UINT length=GetEnvironmentVariableW(L"WO_TEST_GAME_ROOT",root,MAX_PATH);
    if(!length || length>MAX_PATH-32)ExitProcess(1);
    for(UINT i=0;i<length;++i)path[i]=root[i];
    const WCHAR *tail=L"\\Wipeout2097.exe";UINT at=length;while((*((WCHAR *)path+at++)=*tail++)){}
    HANDLE file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,0,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,0);if(file==INVALID_HANDLE_VALUE)ExitProcess(2);
    LARGE_INTEGER size;if(!GetFileSizeEx(file,&size) || size.QuadPart>2000000)ExitProcess(3);
    BYTE *raw=VirtualAlloc(0,(UINT)size.QuadPart,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);DWORD read;
    if(!raw || !ReadFile(file,raw,(DWORD)size.QuadPart,&read,0) || read!=size.QuadPart)ExitProcess(4);CloseHandle(file);
    IMAGE_NT_HEADERS32 *nt=(IMAGE_NT_HEADERS32 *)(raw+((IMAGE_DOS_HEADER *)raw)->e_lfanew);
    if(nt->OptionalHeader.SizeOfImage!=0x7ee000)ExitProcess(6);
    copy(image,raw,nt->OptionalHeader.SizeOfHeaders);IMAGE_SECTION_HEADER *section=IMAGE_FIRST_SECTION(nt);
    for(UINT i=0;i<nt->FileHeader.NumberOfSections;++i)copy(image+section[i].VirtualAddress,raw+section[i].PointerToRawData,section[i].SizeOfRawData);
    root[length++]=L'\\';root[length]=0;
    wo_render_initialize(image,root);
    if(!installed || !refresh_active || !old_ship_effects || image[RVA_ship_effects]!=0xe9)ExitProcess(9);
    if(!race_ui_active || race_ui_event!=1 || !old_detail_keys || image[0x283f0]!=0xe9)ExitProcess(10);
    if(image+RVA_ship_effects+5+*(LONG *)(image+RVA_ship_effects+1)!=(BYTE *)ship_effects)ExitProcess(11);
    BYTE config[48];for(UINT i=0;i<sizeof(config);++i)config[i]=0;
    *(BYTE **)(image+0x517958)=config;config[0x19]=1;race_trail_detail=2;
    /* Enter the actual race-start CALL so refresh_reset sees the same native
     * return address as in gameplay. Its legacy clock body is isolated. */
    old_refresh_reset=fixture_reset;BYTE next=image[0x2ab07];image[0x2ab07]=0xc3;
    ((RefreshVoidFn)(image+0x2ab02))();image[0x2ab07]=next;
    if(config[0x19]!=2)ExitProcess(12);
    for(UINT i=0;i<sizeof(config);++i)if(i!=0x19 && config[i])ExitProcess(13);
    refresh_race=TRUE;*(DWORD *)(image+0x8c98c)=1;
    *(SHORT *)(image+0x664820)=0;*(BYTE **)(image+0x518030)=0;old_ship_effects=fixture_effects;
    const UINT rates[]={30,60,165,240};
    for(UINT r=0;r<4;++r)for(UINT frame=0;frame<rates[r];++frame){
        ui_ticks=frame*30/rates[r];*(int *)(image+0x87640)=60;
        *(DWORD *)(image+0x8c17c)=0;
        ((void (__cdecl *)(void *))(image+RVA_ship_effects))(0);
        if((*(DWORD *)(image+0x8c17c)!=0)!=((frame*60/rates[r])%30<20))ExitProcess(14);
    }
    *(int *)(image+0x87640)=0;
    ((void (__cdecl *)(void *))(image+RVA_ship_effects))(0);
    if(*(DWORD *)(image+0x8c17c) || !native_effect_calls)ExitProcess(15);
    if(!GetEnvironmentVariableW(L"WO_TEST_SETTINGS",race_ui_settings,MAX_PATH))ExitProcess(16);
    old_detail_keys=fixture_detail_keys;refresh_show_fps=TRUE;
    *(SHORT *)(image+0x66496a)=0x29;((RefreshVoidFn)(image+0x283f0))();
    if(refresh_show_fps || *(SHORT *)(image+0x66496a)!=-1 || GetPrivateProfileIntW(L"Render",L"ShowFPS",99,race_ui_settings)!=0)ExitProcess(17);
    *(SHORT *)(image+0x66496a)=0x3b;((RefreshVoidFn)(image+0x283f0))();
    if(observed_native_key!=0x3b || refresh_show_fps)ExitProcess(18);
    ExitProcess(0);
}
