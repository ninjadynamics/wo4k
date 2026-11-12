/* Exercise the production startup patch at the supported EXE's call sites;
 * substitute only Windows/UI work, keeping this check entirely off-screen. */
static UINT startup_show_calls,startup_graphics_calls,startup_quit_calls;
static BOOL WINAPI fixture_startup_show(HWND window,int mode){
    if((UINT_PTR)window!=123 || mode!=SW_SHOWNORMAL || startup_graphics_calls!=1)ExitProcess(110);
    ++startup_show_calls;return TRUE;
}
static void WINAPI fixture_startup_quit(int code){if(code!=1)ExitProcess(111);++startup_quit_calls;}
#define ShowWindow fixture_startup_show
#define PostQuitMessage fixture_startup_quit
#include "../src/runtime/startup_launch.c"
#undef ShowWindow
#undef PostQuitMessage
static void __cdecl fixture_startup_graphics(void){if(startup_show_calls)ExitProcess(112);++startup_graphics_calls;}
static void test_startup_launch(BYTE *image,const WCHAR *settings){
    /* WinMain's VirtualProtect of the code section keeps it executable; a
     * second pass finds nothing left to change. */
    if(image[0x5ba24]!=PAGE_READWRITE || !wo_startup_keep_code_executable(image) ||
       image[0x5ba24]!=PAGE_EXECUTE_READWRITE || wo_startup_keep_code_executable(image))ExitProcess(125);
    /* The arena is reserved larger and emptied at that size, once. */
    if(!wo_startup_grow_arena(image) || *(DWORD *)(image+0x2eb97)!=WO_ARENA_BYTES+0x10000 ||
       *(DWORD *)(image+0x2ebb9)!=WO_ARENA_BYTES || *(DWORD *)(image+0x29ba0)!=WO_ARENA_BYTES ||
       *(DWORD *)(image+0x2bf96)!=WO_ARENA_BYTES || *(DWORD *)(image+0x3324a)!=WO_ARENA_BYTES ||
       *(DWORD *)(image+0x2eb7a)!=WO_POOL_BYTES ||
       wo_startup_grow_arena(image))ExitProcess(126);
    BYTE original=image[0x5bef4];image[0x5bef4]=0;
    if(wo_startup_install(image,settings) || image[0x5bb38]!=1)ExitProcess(113);
    image[0x5bef4]=original;
    if(!wo_startup_install(image,settings) || image[0x5bb38]!=SW_HIDE || *(DWORD *)(image+0x5baa5)!=WS_POPUP)ExitProcess(114);
    if(wo_startup_native_graphics!=(void *)(image+0x5d050))ExitProcess(115);
    wo_startup_native_graphics=fixture_startup_graphics;
    int (__cdecl *controls)(void)=(void *)(image+0x5bb37+*(LONG *)(image+0x5bb33));
    void (__cdecl *graphics)(void)=(void *)(image+0x5bef9+*(LONG *)(image+0x5bef5));
    const WCHAR *values[]={L"1",L"2",L"5",L"9",0};const UINT expected[]={1,2,5,2,2};
    /* Trap any accidental call to the old setup-control constructor. */
    BYTE old_constructor=image[0x60e10];image[0x60e10]=0xcc;
    *(DWORD *)(image+0x8f804)=1;*(SHORT *)(image+0x8f808)=1;*(DWORD *)(image+0x8f80c)=2;
    BYTE *supported=image+0xac064+3268+16;
    for(UINT i=0;i<8;++i)supported[i]=0;supported[3]=1;*(DWORD *)(image+0x8f82c)=3;
    for(UINT i=0;i<5;++i){
        if(!WritePrivateProfileStringW(L"Audio",L"SoundMode",values[i],settings))ExitProcess(116);
        *(DWORD *)(image+0x8f800)=0;
        *(DWORD *)(image+0x8f7fc)=1;
        if(controls()!=1 || !wo_startup_bypassed || *(DWORD *)(image+0x8f800)!=1 || image[0x87070]!=expected[i])ExitProcess(117);
        if(startup_show_calls || *(DWORD *)(image+0x8f7fc) || *(DWORD *)(image+0x8f82c)!=3 ||
           *(SHORT *)(image+0x8f8bc)!=1 || *(DWORD *)(image+0x8f8c8)!=2 || *(DWORD *)(image+0x8f8cc)!=16 ||
           *(DWORD *)(image+0x8f818)!=1 || *(DWORD *)(image+0x8ace0)!=1 || *(DWORD *)(image+0x8acdc)!=1)ExitProcess(118);
    }
    /* Unsupported saved modes prefer640x480, then the first supported one. */
    *(DWORD *)(image+0x8f82c)=99;if(controls()!=1 || *(DWORD *)(image+0x8f82c)!=3)ExitProcess(121);
    supported[3]=0;supported[2]=1;if(controls()!=1 || *(DWORD *)(image+0x8f82c)!=2)ExitProcess(122);
    supported[5]=1;*(DWORD *)(image+0x8f82c)=5;if(controls()!=1 || *(DWORD *)(image+0x8f82c)!=5)ExitProcess(123);
    /* The original nonaccelerated8-bit fallback still initializes correctly. */
    *(DWORD *)(image+0x8f804)=0;image[0xac044+3]=1;*(DWORD *)(image+0x8f82c)=3;
    if(controls()!=1 || *(DWORD *)(image+0x8f8cc)!=8 || *(DWORD *)(image+0x8ace0) || *(DWORD *)(image+0x8acdc) ||
       *(SHORT *)(image+0x8f8bc) || *(DWORD *)(image+0x8f8c8))ExitProcess(124);
    HWND prior=*(HWND *)(image+0xaf60c);*(HWND *)(image+0xaf60c)=(HWND)123;
    graphics();if(startup_show_calls!=1 || startup_graphics_calls!=1)ExitProcess(119);
    *(HWND *)(image+0xaf60c)=prior;
    image[0xac044+3]=0;*(DWORD *)(image+0x8f800)=0;*(DWORD *)(image+0x8f7fc)=1;
    if(controls() || startup_quit_calls!=1 || *(DWORD *)(image+0x8f800))ExitProcess(120);
    image[0x60e10]=old_constructor;
    *(DWORD *)(image+0x8f804)=0;*(DWORD *)(image+0x8f7fc)=0;
}
