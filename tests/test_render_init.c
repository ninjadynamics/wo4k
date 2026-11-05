/* Run the production renderer's entire hook initialization against a mapped,
 * privately owned game image. No process/GPU/user setting is changed. */
#include "../src/runtime/render_patch.c"
static UINT race_ui_event=99;
static UINT native_effect_calls;
static SHORT observed_native_key;
static void __cdecl fixture_effects(void *camera){(void)camera;++native_effect_calls;}
static void __cdecl fixture_detail_keys(void){observed_native_key=*(SHORT *)(game+0x66496a);*(SHORT *)(game+0x66496a)=-1;}
static UINT movie_observed=99,movie_calls,movie_pending_flag;
static BYTE *movie_observed_config;
static void fixture_video(UINT movie){movie_observed=movie;++movie_calls;}
static void __cdecl fixture_movie_race(BYTE *config){movie_observed_config=config;if(movie_pending_flag)*(WORD *)(game+movie_pending_flag)=1;}
static void __cdecl fixture_title_screen(void){if(!movie_intro_seen || movie_observed!=0)ExitProcess(37);}
static UINT art_blits,art_flushes;
static HRESULT WINAPI fixture_art_blt(IDirectDrawSurface *target,RECT *dest,IDirectDrawSurface *source,RECT *src,DWORD flags,DDBLTFX *fx){
    (void)target;(void)dest;(void)source;(void)src;(void)flags;(void)fx;++art_blits;return 0;
}
static void __cdecl fixture_art_flush(int layer){if(layer==1 && art_blits!=2)ExitProcess(38);++art_flushes;}
static UINT title_order,queued_executions,native_presents;
static BOOL expect_title_execute;
static HRESULT WINAPI fixture_title_begin(IDirect3DDevice *device){
    (void)device;if(title_order!=0)ExitProcess(50);title_order=1;return 0;
}
static HRESULT WINAPI fixture_title_execute(IDirect3DDevice *device,IDirect3DExecuteBuffer *buffer,IDirect3DViewport *viewport,DWORD flags){
    (void)device;if(title_order!=1 || (UINT_PTR)buffer!=0x1234+queued_executions || (UINT_PTR)viewport!=77 || flags!=queued_executions+1)ExitProcess(51);
    ++queued_executions;return 0;
}
static HRESULT WINAPI fixture_title_end(IDirect3DDevice *device){
    (void)device;if(title_order!=1 || queued_executions!=2)ExitProcess(52);title_order=2;return 0;
}
static int __cdecl fixture_present(void){
    if(title_order!=(expect_title_execute?2u:0u))ExitProcess(53);
    ++native_presents;return 123;
}
static UINT title_input_calls;
static void __cdecl fixture_title_input(void){++title_input_calls;}
static void __cdecl fixture_reset(void){}
static void fixture_logo_poly(BYTE *p,BOOL second){
    for(UINT i=0;i<40;++i)p[i]=0;
    p[4]=p[5]=p[6]=255;*(WORD *)(p+22)=35;
    int left=second?304:50,width=second?210:254,top=second?64:0;
    *(SHORT *)(p+8)=*(SHORT *)(p+24)=(SHORT)left;
    *(SHORT *)(p+16)=*(SHORT *)(p+32)=(SHORT)(left+width);
    *(SHORT *)(p+10)=*(SHORT *)(p+18)=76;*(SHORT *)(p+26)=*(SHORT *)(p+34)=140;
    p[20]=p[36]=(BYTE)width;p[13]=p[21]=(BYTE)top;p[29]=p[37]=(BYTE)(top+64);
}
#include "test_menu_frame.c"
#include "test_menu_art_alignment.c"
#include "test_hud_layout.c"
#include "test_startup_launch.c"
#include "test_scene_lighting.c"
#include "test_native_transform_finish.c"
#include "test_world_material_gpu.c"
#include "test_hd_regions.c"
#include "test_shadow_menu_motion.c"
void wo_render_event(const char *name,DWORD value){
    const char *wanted="render_race_ui_clock";while(*name && *name==*wanted){++name;++wanted;}if(!*name && !*wanted)race_ui_event=value;
}
BOOL wo_audio_movie_pause(void){return FALSE;}
void wo_audio_movie_resume(BOOL playing){(void)playing;}
UINT wo_audio_bank(void){return 0;}UINT wo_audio_tracks(void){return 9;}
BOOL wo_audio_switch(UINT bank){(void)bank;return FALSE;}
UINT wo_audio_pref(const WCHAR *key,UINT fallback){(void)key;return fallback;}
void wo_audio_save(UINT m,UINT e,UINT t){(void)m;(void)e;(void)t;}
void wo_audio_flush(BOOL force){(void)force;}
BOOL wo_sim_initialize(BYTE *image){(void)image;return FALSE;}
void wo_sim_reset(void){}BOOL wo_sim_half_tick(void){return FALSE;}BOOL wo_sim_wants_half_tick(void){return FALSE;}
void wo_sim_latch_input(void){}void wo_sim_restore_input(void){}DWORD wo_sim_extra_serial(void){return 0;}
void wo_sim_trail_shift(int ship,float *out){(void)ship;out[0]=out[1]=out[2]=0;}
static LONG WINAPI fixture_exception(EXCEPTION_POINTERS *fault){
    if(fault->ExceptionRecord->ExceptionCode!=EXCEPTION_ACCESS_VIOLATION)return EXCEPTION_CONTINUE_SEARCH;
    WCHAR path[MAX_PATH];UINT n=GetEnvironmentVariableW(L"WO_TEST_SETTINGS",path,MAX_PATH);
    if(n && n<MAX_PATH-8){const WCHAR *suffix=L".fault";while((*((WCHAR *)path+n++)=*suffix++)){}
        HANDLE out=CreateFileW(path,GENERIC_WRITE,FILE_SHARE_READ,0,CREATE_ALWAYS,0,0);
        DWORD values[10]={fault->ExceptionRecord->ExceptionCode,(DWORD)(UINT_PTR)fault->ExceptionRecord->ExceptionAddress,
            fault->ContextRecord->Eip,fault->ContextRecord->Esp,fault->ContextRecord->Eax,fault->ContextRecord->Ecx,
            fault->ContextRecord->Edx,fault->ContextRecord->Esi,fault->ContextRecord->Edi,fault->ContextRecord->Ebp},written;
        if(out!=INVALID_HANDLE_VALUE){WriteFile(out,values,sizeof(values),&written,0);CloseHandle(out);}
    }
    ExitProcess(249);return EXCEPTION_CONTINUE_SEARCH;
}
void __stdcall test_entry(void){
    typedef UINT (WINAPI *ErrorModeFn)(UINT);
    ErrorModeFn errors=(ErrorModeFn)GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"SetErrorMode");
    if(errors)errors(0x8003);
    typedef PVOID (WINAPI *ExceptionFn)(ULONG,PVECTORED_EXCEPTION_HANDLER);
    ExceptionFn exceptions=(ExceptionFn)GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"AddVectoredExceptionHandler");
    if(exceptions)exceptions(1,fixture_exception);
    test_world_material_gpu();
    test_hd_world_packets();
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
    if(!old_title_screen || image[0x2f670]!=0xe9 || image[0x2f864]!=0xe8)ExitProcess(19);
    if(image+0x2f869+*(LONG *)(image+0x2f865)!=(BYTE *)title_input)ExitProcess(20);
    for(UINT rate=30;rate<=240;rate+=(rate==30?30:(rate==60?105:75))){
        for(UINT f=0;f<rate*30;++f)if(title_frames((double)f/rate)>=900)ExitProcess(21);
        if(title_frames(30.0)!=900 || title_frames(5.0)!=150)ExitProcess(22);
    }
    old_title_input=fixture_title_input;title_clock_started=TRUE;
    LARGE_INTEGER title_now;QueryPerformanceCounter(&title_now);
    title_started.QuadPart=title_now.QuadPart-title_frequency.QuadPart*30;
    UINT counter;
    __asm__ volatile("pushl %%ebx\n\tmovl $77,%%ebx\n\tcall _title_input\n\tmovl %%ebx,%%eax\n\tpopl %%ebx":"=a"(counter)::"ecx","edx","memory");
    if(counter<900 || counter>901 || title_input_calls!=1)ExitProcess(23);
    title_active=TRUE;controller_active=TRUE;
    if(title_prompt((const char *)(image+0x8c868))[6]!='S')ExitProcess(24);
    controller_active=FALSE;
    if(title_prompt((const char *)(image+0x8c868))!=(const char *)(image+0x8c868))ExitProcess(25);
    ControllerWire title_pad={0};title_pad.buttons=1u<<6;
    *(WORD *)(image+0x65f7cc)=0;apply_controller_state(&title_pad,TRUE);
    if(!(*(WORD *)(image+0x65f7cc)&0x4000))ExitProcess(26);
    apply_controller_state(0,FALSE);title_active=FALSE;
    *(WORD *)(image+0x65f7cc)=0;apply_controller_state(&title_pad,TRUE);
    if(!(*(WORD *)(image+0x65f7cc)&0x1000) || (*(WORD *)(image+0x65f7cc)&0x4000))ExitProcess(27);
    apply_controller_state(0,FALSE);
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
    test_startup_launch(image,race_ui_settings);
    old_detail_keys=fixture_detail_keys;refresh_show_fps=TRUE;
    *(SHORT *)(image+0x66496a)=0x29;((RefreshVoidFn)(image+0x283f0))();
    if(refresh_show_fps || *(SHORT *)(image+0x66496a)!=-1 || GetPrivateProfileIntW(L"Render",L"ShowFPS",99,race_ui_settings)!=0)ExitProcess(17);
    *(SHORT *)(image+0x66496a)=0x3b;((RefreshVoidFn)(image+0x283f0))();
    if(observed_native_key!=0x3b || refresh_show_fps)ExitProcess(18);
    test_scene_lighting(image,race_ui_settings);
    test_native_transform_finish(image);
    test_sprite_fades();
    test_ps1_native_emitters(image);
    /* Follow the actual patched race CALL target; emulate each native win.
     * Native flags and played markers must stay untouched by our wrapper. */
    if(!old_movie_race || image[0x2fb3b]!=0xe8 || image+0x2fb40+*(LONG *)(image+0x2fb3c)!=(BYTE *)movie_race)ExitProcess(28);
    old_movie_race=fixture_movie_race;movie_player=fixture_video;
    const UINT flags[]={0x8c764,0x8c774,0x8c76c,0x8c778};
    void (__cdecl *patched_race)(BYTE *)=(void *)(image+0x2fb40+*(LONG *)(image+0x2fb3c));
    for(UINT i=0;i<4;++i){
        for(UINT j=0;j<6;++j)*(WORD *)(image+0x8c764+j*4)=0;
        movie_pending_flag=flags[i];movie_observed=99;UINT before=movie_calls;
        patched_race(config);
        if(movie_observed!=i+1 || movie_calls!=before+1 || movie_observed_config!=config || *(WORD *)(image+flags[i])!=1)ExitProcess(29+i);
    }
    movie_pending_flag=0;
    for(UINT j=0;j<6;++j)*(WORD *)(image+0x8c764+j*4)=0;
    movie_calls=0;patched_race(config);if(movie_calls)ExitProcess(33);
    *(WORD *)(image+0x8c764)=*(WORD *)(image+0x8c768)=1;
    *(WORD *)(image+0x8c76c)=*(WORD *)(image+0x8c770)=1;
    patched_race(config);if(movie_calls)ExitProcess(34);
    old_title_screen=fixture_title_screen;movie_intro_seen=FALSE;
    title_screen();title_screen();if(movie_calls!=1 || title_active)ExitProcess(35);
    /* Native TitleScreen queues its artwork in layer1 (the background pass).
     * Keep that ordering; its text overlay must explicitly ignore scene depth. */
    static IDirectDrawSurfaceVtbl vtable={.Blt=fixture_art_blt};
    IDirectDrawSurface target={&vtable},art={&vtable};
    *(IDirectDrawSurface **)(image+0xabd24)=&target;*(void **)(image+0xc8c88)=0;
    *(float *)(image+0xaf0b4)=640;*(float *)(image+0xc8630)=480;
    startup_art[0].surface=&art;startup_art[0].width=7680;startup_art[0].height=2160;
    ((IDirectDrawSurface **)(image+0xc7b10))[0]=&art;
    DWORD *info=(DWORD *)(image+0x4f57c0);info[0]=7680;info[1]=2160;info[2]=1;
    ArtBlit *blits=(ArtBlit *)(image+0x4e1f30);
    blits[1000].operation=1;blits[1000].surface=&art;
    blits[0].operation=2;blits[0].surface=(void *)123;
    *(DWORD *)(image+0x4f57b0)=1;*(DWORD *)(image+0x4f57b4)=1;
    title_active=TRUE;old_startup_flush=fixture_art_flush;
    startup_flush(1);
    if(art_blits!=2 || art_flushes || *(DWORD *)(image+0x4f57b4) || *(DWORD *)(image+0x4f57b0)!=1)ExitProcess(36);
    startup_flush(0);if(art_blits!=2 || art_flushes!=1)ExitProcess(39);
    BYTE instructions[28]={8,8,1,0},rewritten[512];
    *(DWORD *)(instructions+4)=1;*(DWORD *)(instructions+8)=123;
    instructions[12]=3;instructions[13]=8;*(WORD *)(instructions+14)=1;
    instructions[24]=11;ui_hd_pages[22].handle=123;
    UINT rewritten_size=ui_hd_rewrite(instructions,28,rewritten,sizeof(rewritten),0,0);
    BOOL depth_off=FALSE,write_off=FALSE,draw_seen=FALSE;
    if(!rewritten_size)ExitProcess(45);
    for(UINT at=0;at<rewritten_size;){
        UINT opcode=rewritten[at],size=rewritten[at+1],count=*(WORD *)(rewritten+at+2);
        if(opcode==8)for(UINT n=0;n<count;++n){
            DWORD type=*(DWORD *)(rewritten+at+4+n*8),value=*(DWORD *)(rewritten+at+8+n*8);
            if(type==7)depth_off=!value;if(type==14)write_off=!value;
        }
        if(opcode==3){if(!depth_off || !write_off)ExitProcess(46);draw_seen=TRUE;}
        at+=4+size*count;
    }
    if(!draw_seen)ExitProcess(47);
    title_active=FALSE;
    /* The optional third page binds to native HUD2 slot24 and gets alpha blending. */
    DWORD *hud_handles=(DWORD *)(image+0xc7910);
    static UiHdPage pages[3]={{.handle=201,.width=1024},{.handle=202,.width=1024},{.handle=203,.width=1024}};
    for(UINT i=0;i<3;++i)ui_hd_commit_page(22+i,pages+i);
    if(hud_handles[22]!=201 || hud_handles[23]!=202 || hud_handles[24]!=203 || !ui_hd_handle(203))ExitProcess(65);
    *(DWORD *)(instructions+8)=203;
    rewritten_size=ui_hd_rewrite(instructions,28,rewritten,sizeof(rewritten),0,0);
    if(!rewritten_size)ExitProcess(66);
    BOOL blend=FALSE;draw_seen=FALSE;
    for(UINT at=0;at<rewritten_size;){
        UINT opcode=rewritten[at],size=rewritten[at+1],count=*(WORD *)(rewritten+at+2);
        if(opcode==8)for(UINT n=0;n<count;++n)if(*(DWORD *)(rewritten+at+4+n*8)==27)blend=*(DWORD *)(rewritten+at+8+n*8)!=0;
        if(opcode==3){if(!blend)ExitProcess(67);draw_seen=TRUE;}
        at+=4+size*count;
    }
    if(!draw_seen)ExitProcess(68);
    BYTE alpha_pixels[8]={255,255,255,0,0,128,255,128};
    hd_premultiply(alpha_pixels,2);
    if(*(DWORD *)alpha_pixels || *(DWORD *)(alpha_pixels+4)!=0x80804000)ExitProcess(70);
    static TLVertex fade_vertices[8];for(UINT i=0;i<3;++i)fade_vertices[i].color=0xffffffff;
    fade_vertices[0].color=0x80804020;
    static BYTE mixed[52]={8,8,1,0};*(DWORD *)(mixed+4)=1;*(DWORD *)(mixed+8)=203;
    mixed[12]=mixed[36]=3;mixed[13]=mixed[37]=8;*(WORD *)(mixed+14)=*(WORD *)(mixed+38)=1;
    *(WORD *)(mixed+18)=*(WORD *)(mixed+42)=1;*(WORD *)(mixed+20)=*(WORD *)(mixed+44)=2;
    mixed[24]=8;mixed[25]=8;*(WORD *)(mixed+26)=1;*(DWORD *)(mixed+28)=1;*(DWORD *)(mixed+32)=999;mixed[48]=11;
    UINT fade_count=3;
    if(!ui_hd_prepare(mixed,sizeof(mixed),fade_vertices,&fade_count,8) || fade_count!=4 || *(WORD *)(mixed+16)!=3 || *(WORD *)(mixed+40)!=0 || fade_vertices[0].color!=0x80804020 || fade_vertices[3].color!=0x80402010)ExitProcess(71);
    WCHAR native_png[MAX_PATH];static const WCHAR expected_png[]=L"Pics\\Hi-res\\FOO.page2.png";
    if(!hd_native_name(native_png,".\\Pics\\Hi-res\\FOO.tga",TRUE,FALSE,2) || !equal((BYTE *)native_png,(BYTE *)expected_png,sizeof(expected_png)))ExitProcess(72);
    BYTE *logo_base=VirtualAlloc(0,256*256*4,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    static BYTE logo_banner[462*64*4];
    if(!logo_base)ExitProcess(73);
    for(UINT i=0;i<256*256;++i)((DWORD *)logo_base)[i]=0xff00aa00;
    for(UINT y=0;y<64;++y)for(UINT x=0;x<462;++x)((DWORD *)logo_banner)[y*462+x]=0xff000000|x;
    UINT logo_size=256;logo_base=hd_logo_overlay(logo_base,&logo_size,logo_banner,462,64);
    if(!logo_base || logo_size!=256 || ((DWORD *)logo_base)[0]!=0xff000000 || ((DWORD *)logo_base)[64*256]!=(0xff000000|252) || ((DWORD *)logo_base)[200*256]!=0xff00aa00)ExitProcess(74);
    UINT joined_width=0,joined_height=0;BYTE *joined_logo=ui_hd_join_logo(logo_base,256,&joined_width,&joined_height);
    if(!joined_logo || joined_width!=512 || joined_height!=128)ExitProcess(75);
    /* Every texel crosses the old split continuously; padding is transparent. */
    for(UINT y=0;y<64;++y)for(UINT x=0;x<512;++x)
        if(((DWORD *)joined_logo)[y*512+x]!=(x<464?(0xff000000|(x*462/464)):0))ExitProcess(76);
    for(UINT i=64*512;i<128*512;++i)if(((DWORD *)joined_logo)[i])ExitProcess(77);
    VirtualFree(joined_logo,0,MEM_RELEASE);
    VirtualFree(logo_base,0,MEM_RELEASE);
    /* One native texture bucket can contain both the header and atlas widgets.
     * Bind the joined texture only for marked header triangles, restoring it
     * before every ordinary widget and after the final header run. */
    static TLVertex logo_vertices[8];for(UINT i=0;i<4;++i)logo_vertices[i].specular=UI_HD_LOGO_TAG;
    ui_hd_logo.handle=501;ui_hd_pages[25].handle=250;
    static BYTE logo_instructions[44]={8,8,1,0};
    *(DWORD *)(logo_instructions+4)=1;*(DWORD *)(logo_instructions+8)=250;
    logo_instructions[12]=3;logo_instructions[13]=8;*(WORD *)(logo_instructions+14)=3;
    const WORD logo_indices[12]={0,1,3,31,4,5,6,31,0,3,2,31};copy(logo_instructions+16,(const BYTE *)logo_indices,sizeof(logo_indices));
    logo_instructions[40]=11;
    rewritten_size=ui_hd_rewrite(logo_instructions,sizeof(logo_instructions),rewritten,sizeof(rewritten),logo_vertices,8);
    if(!rewritten_size)ExitProcess(78);
    DWORD active_texture=0;UINT logo_draws=0;
    for(UINT at=0;at<rewritten_size;){
        UINT opcode=rewritten[at],size=rewritten[at+1],count=*(WORD *)(rewritten+at+2);
        if(opcode==8)for(UINT i=0;i<count;++i)if(*(DWORD *)(rewritten+at+4+i*8)==1)active_texture=*(DWORD *)(rewritten+at+8+i*8);
        if(opcode==3){if(count!=1 || active_texture!=(logo_draws==1?250u:501u))ExitProcess(79);++logo_draws;}
        at+=4+size*count;
    }
    if(logo_draws!=3 || active_texture!=250)ExitProcess(80);
    pages[2].handle=0;pages[2].width=0;hud_handles[24]=777;
    for(UINT i=0;i<3;++i)ui_hd_commit_page(22+i,pages+i);
    if(hud_handles[24]!=777 || ui_hd_pages[24].handle || ui_hd_handle(203))ExitProcess(69);
    /* Run the ORIGINAL InitText/AddText/quad/vertex machine code. Relocate
     * only verified native DATA operands into this privately mapped image.
     * No GPU or input APIs are called while building these actual vertices. */
    #include "native_font_refs.h"
    for(UINT i=0;i<sizeof(native_font_refs)/sizeof(native_font_refs[0]);++i){
        DWORD *operand=(DWORD *)(image+native_font_refs[i]);
        if(*operand>=0x400000 && *operand<0xbee000)*operand+=(DWORD)(UINT_PTR)image-0x400000;
    }
    *(DWORD *)((BYTE *)old_scaled_vertex+7)+=(DWORD)(UINT_PTR)image-0x400000;
    ((void (__cdecl *)(void))(image+0xb610))();
    BYTE *native_vertices=VirtualAlloc(0,0x40000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    if(!native_vertices)ExitProcess(54);
    *(BYTE **)(image+0x87038)=native_vertices;
    *(SHORT *)(image+0x65add8)=*(SHORT *)(image+0x65adda)=0;
    *(int *)(image+0x8c9d4)=0;*(DWORD *)(image+0x8c98c)=0;
    *(float *)(image+0xaf0b4)=640;*(float *)(image+0xc8630)=480;
    test_hud_layout(image,native_vertices);
    *(float *)(image+0xaf0b4)=640;*(float *)(image+0xc8630)=480;
    const float aspect_x[]={.75f,1,4.0f/7,.375f,1},aspect_y[]={1,1,1,1,.5625f};
    title_active=TRUE;
    for(UINT aspect=0;aspect<5;++aspect)for(UINT pad=0;pad<2;++pad){
        *(UINT *)(native_vertices+0x39210)=*(UINT *)(native_vertices+0x39214)=0;
        *(SHORT *)(image+0x87040)=0;ui_x=aspect_x[aspect];ui_y=aspect_y[aspect];
        controller_active=pad!=0;
        add_text((const char *)(image+0x8c868),160,220,8,0);
        UINT vertex_count=*(UINT *)(native_vertices+0x39214);
        if(vertex_count!=40)ExitProcess(55);
        const TLVertex *actual=(const TLVertex *)(native_vertices+0xa410);
        float bounds[4]={10000,10000,-10000,-10000};
        for(UINT i=0;i<vertex_count;++i){
            if(!(actual[i].x>=0 && actual[i].x<=640 && actual[i].y>=0 && actual[i].y<=480))ExitProcess(56);
            if(actual[i].x<bounds[0])bounds[0]=actual[i].x;if(actual[i].x>bounds[2])bounds[2]=actual[i].x;
            if(actual[i].y<bounds[1])bounds[1]=actual[i].y;if(actual[i].y>bounds[3])bounds[3]=actual[i].y;
        }
        if(bounds[0]+bounds[2]<639 || bounds[0]+bounds[2]>641)ExitProcess(57);
        WCHAR bounds_file[MAX_PATH];
        if(!aspect && pad && GetEnvironmentVariableW(L"WO_TEST_BOUNDS",bounds_file,MAX_PATH)){
            HANDLE report=CreateFileW(bounds_file,GENERIC_WRITE,FILE_SHARE_READ,0,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,0);DWORD written;
            if(report!=INVALID_HANDLE_VALUE){WriteFile(report,bounds,sizeof(bounds),&written,0);CloseHandle(report);}
        }
    }
    title_active=FALSE;*(int *)(image+0x8c9d4)=1;ui_x=.75f;ui_y=1;
    /* Execute the supported EXE's complete header routine, including its own
     * SetTextQuad arguments. Its second half starts at x152, unlike the x151
     * in the reference decompilation. Fixtures alone missed that difference. */
    const UINT native_logo_refs[]={0x397b8,0x397be,0x397f6,0x397fb};
    for(UINT i=0;i<sizeof(native_logo_refs)/sizeof(native_logo_refs[0]);++i){
        DWORD *operand=(DWORD *)(image+native_logo_refs[i]);
        if(*operand<0x400000 || *operand>=0xbee000)ExitProcess(89);
        *operand+=(DWORD)(UINT_PTR)image-0x400000;
    }
    *(float *)(image+0x8ace4)=.1f;*(UINT *)(image+0x89cb8)=0;
    *(UINT *)(native_vertices+0x39210)=*(UINT *)(native_vertices+0x39214)=0;*(SHORT *)(image+0x87040)=0;
    ((void (__cdecl *)(void))(image+0x39790))();
    if(*(UINT *)(native_vertices+0x39214)!=4 || *(UINT *)(native_vertices+0x39210)!=2)ExitProcess(90);
    BYTE logo_first[40],logo_second[40];fixture_logo_poly(logo_first,FALSE);fixture_logo_poly(logo_second,TRUE);
    for(UINT aspect=0;aspect<5;++aspect){
        ui_x=aspect_x[aspect];ui_y=aspect_y[aspect];
        *(UINT *)(native_vertices+0x39210)=*(UINT *)(native_vertices+0x39214)=0;*(SHORT *)(image+0x87040)=0;
        ((void (__cdecl *)(void))(image+0x39790))();
        if(*(UINT *)(native_vertices+0x39214)!=4 || *(UINT *)(native_vertices+0x39210)!=2)ExitProcess(81);
        const TLVertex *v=(const TLVertex *)(native_vertices+0xa410);
        for(UINT i=0;i<4;++i)if(v[i].specular!=UI_HD_LOGO_TAG || v[i].u!=((i&1)?232.0f/256:0) || v[i].v!=((i&2)?.5f:0))ExitProcess(82);
        if(v[1].x-v[0].x<464*ui_x-.01f || v[1].x-v[0].x>464*ui_x+.01f || v[2].y-v[0].y<64*ui_y-.01f || v[2].y-v[0].y>64*ui_y+.01f)ExitProcess(83);
    }
    /* Actual finish pipeline, including faded-vertex cloning and tag cleanup. */
    static BYTE logo_execute[4096];
    for(UINT i=0;i<4;++i)((TLVertex *)logo_execute)[i]=((TLVertex *)(native_vertices+0xa410))[i];
    ((TLVertex *)logo_execute)[0].color=0x80804020;
    *(BYTE **)(image+0x7c9690)=logo_execute;*(BYTE **)(image+0x53f1a0)=logo_execute+1024;
    *(BYTE **)(image+0x631d54)=logo_execute+1024+36;*(UINT *)(image+0xc8610)=sizeof(logo_execute);*(UINT *)(image+0x631d50)=4;
    copy(logo_execute+1024,logo_instructions,36);*(WORD *)(logo_execute+1024+14)=2;
    const WORD pair_indices[8]={0,1,3,31,0,3,2,31};copy(logo_execute+1024+16,(const BYTE *)pair_indices,sizeof(pair_indices));
    ui_hd_finish();
    if(*(UINT *)(image+0x631d50)!=5 || ((TLVertex *)logo_execute)[4].color!=0x80402010)ExitProcess(84);
    for(UINT i=0;i<5;++i)if(((TLVertex *)logo_execute)[i].specular)ExitProcess(85);
    *(UINT *)(native_vertices+0x39214)=0;*(SHORT *)(image+0x87040)=0;
    scaled_quad(.1f,logo_second,0);if(*(UINT *)(native_vertices+0x39214)!=4)ExitProcess(86); /* unpaired second is retained */
    fixture_logo_poly(logo_first,FALSE);logo_first[12]=1;
    scaled_quad(.1f,logo_first,0);if(*(UINT *)(native_vertices+0x39214)!=8)ExitProcess(87); /* different widget is retained */
    ui_hd_logo.handle=0;fixture_logo_poly(logo_first,FALSE);
    *(UINT *)(native_vertices+0x39214)=0;*(SHORT *)(image+0x87040)=0;
    ((void (__cdecl *)(void))(image+0x39790))();
    if(*(UINT *)(native_vertices+0x39214)!=8)ExitProcess(88); /* SD/upload-failure fallback */
    test_menu_frame(image,native_vertices);
    test_menu_art_alignment(image,native_vertices);
    test_team_stat_gauges(image,native_vertices);
    *(int *)(image+0x8c9d4)=0;title_active=TRUE;
    /* Verify the production present path executes the untouched original
     * title buffers BEFORE the native flip, and leaves all queues owned by it. */
    static IDirect3DDeviceVtbl title_vtable={.BeginScene=fixture_title_begin,.Execute=fixture_title_execute,.EndScene=fixture_title_end};
    IDirect3DDevice title_device={&title_vtable};
    *(IDirect3DDevice **)(image+0xc8d10)=&title_device;*(void **)(image+0xc8c88)=(void *)77;
    distance_execute[0]=0x1234;distance_execute[1]=0x1235;distance_states[0]=1;distance_states[1]=2;
    *(UINT *)(image+0x8f8dc)=2;startup_visible=0;refresh_race=FALSE;ui_last_time.QuadPart=0;
    old_refresh_present=fixture_present;expect_title_execute=TRUE;title_prompt_pending=TRUE;
    if(refresh_present()!=123 || queued_executions!=2 || native_presents!=1 || *(UINT *)(image+0x8f8dc)!=2)ExitProcess(58);
    title_active=FALSE;title_order=0;expect_title_execute=FALSE;
    if(refresh_present()!=123 || queued_executions!=2 || native_presents!=2)ExitProcess(59);
    WCHAR art_root[MAX_PATH];
    if(GetEnvironmentVariableW(L"WO_TEST_ART_ROOT",art_root,MAX_PATH)){
        const WCHAR *names[]={L"font-and-numbers.png",L"hud-gauges.png",L"notice.png",L"title.png"};
        const UINT widths[]={1024,1024,2880,7680},heights[]={1024,1024,2160,2160};
        for(UINT i=0;i<4;++i){
            WCHAR art_path[MAX_PATH];art_path[0]=0;movie_append(art_path,MAX_PATH,art_root);movie_append(art_path,MAX_PATH,names[i]);
            UINT w=0,h=0;BYTE *pixels=read_art_png(art_path,&w,&h);
            if(!pixels || w!=widths[i] || h!=heights[i])ExitProcess(41+i);
            VirtualFree(pixels,0,MEM_RELEASE);
        }
    }
    /* Optional end-to-end native -> PowerShell -> media -> return smoke test.
     * Only reads a short, user-created local video fixture. */
    if(GetEnvironmentVariableW(L"WO_TEST_BRIDGE_ROOT",movie_root,MAX_PATH)){
        LARGE_INTEGER before,after;QueryPerformanceCounter(&before);
        play_game_movie(0);QueryPerformanceCounter(&after);
        double seconds=(double)(after.QuadPart-before.QuadPart)/refresh_frequency.QuadPart;
        if(seconds<1 || seconds>25 || !ui_last_time.QuadPart)ExitProcess(40);
    }
    test_hd_region_assets();
    test_projected_shadow_motion(image);
    ExitProcess(0);
}
