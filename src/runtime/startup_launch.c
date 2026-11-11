/* Initialize the legacy setup values without constructing its controls or
 * running its event loop. Native device enumeration and gameplay remain. */
static BYTE *wo_startup_image;
static WCHAR wo_startup_settings[MAX_PATH];
static void (__cdecl *wo_startup_native_graphics)(void);
static DWORD wo_startup_sound_mode=2;
static BOOL wo_startup_bypassed;
static BOOL wo_startup_display(void){
    BYTE *image=wo_startup_image;
    BOOL accelerated=*(DWORD *)(image+0x8f804)!=0;
    int driver=accelerated?*(SHORT *)(image+0x8f808):0;
    UINT device=accelerated?*(UINT *)(image+0x8f80c):0;
    if(driver<0 || driver>=4 || device>=4)return FALSE;
    /* These are the supported EXE's enumerated display tables, used by its
     * own mode validation. Keep a valid saved resolution; prefer640x480
     * when it is absent, with the first supported mode as a fallback. */
    const BYTE *supported=image+(accelerated?0xac064:0xac044)+driver*3268+device*8;
    UINT mode=*(UINT *)(image+0x8f82c);
    if(mode>=6 || !supported[mode]){
        mode=3;
        if(!supported[mode])for(mode=0;mode<6 && !supported[mode];++mode){}
        if(mode==6)return FALSE;
    }
    *(SHORT *)(image+0x8f8bc)=(SHORT)driver;
    *(UINT *)(image+0x8f8c8)=device;
    *(UINT *)(image+0x8f8cc)=accelerated?16:8;
    *(UINT *)(image+0x8f818)=accelerated;
    *(UINT *)(image+0x8ace0)=accelerated;
    *(UINT *)(image+0x8acdc)=accelerated;
    *(UINT *)(image+0x8f82c)=mode;
    return TRUE;
}
static int __cdecl wo_startup_controls(void){
    if(!wo_startup_display()){PostQuitMessage(1);return 0;}
    UINT mode=GetPrivateProfileIntW(L"Audio",L"SoundMode",2,wo_startup_settings);
    if(mode!=1 && mode!=2 && mode!=5)mode=2;
    wo_startup_sound_mode=mode;wo_startup_image[0x87070]=(BYTE)mode;
    *(DWORD *)(wo_startup_image+0x8f800)=1; /* native Play Game request */
    *(DWORD *)(wo_startup_image+0x8f7fc)=0; /* skip the setup event loop entirely */
    wo_startup_bypassed=TRUE;return 1;
}
static void __cdecl wo_startup_graphics(void){
    /* Do not reveal a500x360 window while DirectDraw is still starting. */
    wo_startup_native_graphics();
    ShowWindow(*(HWND *)(wo_startup_image+0xaf60c),SW_SHOWNORMAL);
}
/* WinMain makes the game's whole code section writable with
 * VirtualProtect(code, size, PAGE_READWRITE): no longer executable. Windows
 * forgives that only while DEP is off for the process, which an old EXE gets
 * by default; where DEP is enforced for every program the game died at the
 * very next instruction. Ask for PAGE_EXECUTE_READWRITE instead. */
static BOOL wo_startup_keep_code_executable(BYTE *image){
    static const BYTE protect_call[]={0x52,0x6a,PAGE_READWRITE,0x8d,0x4c,0x28,0x18,0x8b,0x44,0x28,0x1c,0x50,0x8b,0x41,0x14,0x03,0xc5,0x50,0xff,0x15};
    for(UINT i=0;i<sizeof(protect_call);++i)if(image[0x5ba22+i]!=protect_call[i])return FALSE;
    DWORD old,ignored;
    if(!VirtualProtect(image+0x5ba24,1,PAGE_EXECUTE_READWRITE,&old))return FALSE;
    image[0x5ba24]=PAGE_EXECUTE_READWRITE;
    FlushInstructionCache(GetCurrentProcess(),image+0x5ba24,1);
    VirtualProtect(image+0x5ba24,1,old,&ignored);return TRUE;
}
static BOOL wo_startup_install(BYTE *image,const WCHAR *settings){
    static const BYTE controls_call[]={0xe8,0xd9,0x52,0,0,0x6a,1};
    static const BYTE graphics_call[]={0xe8,0x57,0x11,0,0};
    static const BYTE window_style[]={0x68,0,0,0xc8,0x80};
    for(UINT i=0;i<sizeof(controls_call);++i)if(image[0x5bb32+i]!=controls_call[i])return FALSE;
    for(UINT i=0;i<sizeof(graphics_call);++i)if(image[0x5bef4+i]!=graphics_call[i])return FALSE;
    for(UINT i=0;i<sizeof(window_style);++i)if(image[0x5baa4+i]!=window_style[i])return FALSE;
    UINT n=0;while(settings[n] && n<MAX_PATH-1){wo_startup_settings[n]=settings[n];++n;}
    if(settings[n])return FALSE;wo_startup_settings[n]=0;
    DWORD old,ignored;BYTE *start=image+0x5baa4;UINT length=0x5bef9-0x5baa4;
    if(!VirtualProtect(start,length,PAGE_EXECUTE_READWRITE,&old))return FALSE;
    wo_startup_image=image;
    wo_startup_native_graphics=(void *)(image+0x5d050);
    *(DWORD *)(image+0x5baa5)=WS_POPUP; /* the real game window has no setup caption */
    *(DWORD *)(image+0x5bb33)=(DWORD)(UINT_PTR)wo_startup_controls-(DWORD)(UINT_PTR)(image+0x5bb37);
    image[0x5bb38]=SW_HIDE;
    *(DWORD *)(image+0x5bef5)=(DWORD)(UINT_PTR)wo_startup_graphics-(DWORD)(UINT_PTR)(image+0x5bef9);
    FlushInstructionCache(GetCurrentProcess(),start,length);
    VirtualProtect(start,length,old,&ignored);return TRUE;
}
