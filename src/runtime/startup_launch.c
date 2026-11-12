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
/* The game keeps a race's models and effects in one fixed arena of 1.8 MB,
 * sized for the PC release's scenery. The PlayStation scenery is larger (Gare
 * d'Europa's model by 31 KB), the arena ran out while the race was set up, and
 * the game then wrote through the null pointer it got. Reserve 8 MB instead:
 * the block is allocated once and its size is passed again at each of the
 * four places that empty it. The allocator skips free blocks of 0x800000
 * bytes or more, so the arena stays just below that.
 *
 * A second pool of 0.6 MB lives for the whole session and is never emptied.
 * With the larger scenery it ran short after a few races (Gare d'Europa,
 * Talon's Reach, Gare d'Europa again drew the track with pieces missing).
 * It is sized from its allocation, so only that grows, to 4 MB. */
#define WO_ARENA_BYTES 0x7e0000u
#define WO_POOL_BYTES 0x400000u
static BOOL wo_startup_grow_arena(BYTE *image){
    static const DWORD reserve=0x2eb97,pool=0x2eb7a,resets[]={0x2ebb9,0x29ba0,0x2bf96,0x3324a};
    if(image[reserve-1]!=0x68 || *(DWORD *)(image+reserve)!=0x1ce800)return FALSE;
    if(image[pool-1]!=0x68 || *(DWORD *)(image+pool)!=0x95000)return FALSE;
    for(UINT i=0;i<sizeof(resets)/sizeof(resets[0]);++i)
        if(image[resets[i]-1]!=0x68 || *(DWORD *)(image+resets[i])!=0x1c2000)return FALSE;
    for(UINT i=0;i<=sizeof(resets)/sizeof(resets[0])+1;++i){
        UINT count=sizeof(resets)/sizeof(resets[0]);
        BYTE *at=image+(i==0?reserve:i<=count?resets[i-1]:pool);DWORD old,ignored;
        if(!VirtualProtect(at,4,PAGE_EXECUTE_READWRITE,&old))return FALSE;
        *(DWORD *)at=i==0?WO_ARENA_BYTES+0x10000:i<=count?WO_ARENA_BYTES:WO_POOL_BYTES;
        FlushInstructionCache(GetCurrentProcess(),at,4);
        VirtualProtect(at,4,old,&ignored);
    }
    return TRUE;
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
