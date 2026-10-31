#define WIN32_LEAN_AND_MEAN
#include <windows.h>
static BYTE *game;
static BOOL refresh_active=TRUE,refresh_race=TRUE,refresh_show_fps=TRUE;
static DWORD ui_ticks;
static void (__cdecl *old_ship_effects)(void *);
static int native_key_calls,native_last_key,preference_writes,effect_calls;
static WCHAR last_preference[32];
static BOOL equal(const BYTE *a,const BYTE *b,UINT n){while(n--)if(*a++!=*b++)return FALSE;return TRUE;}
static void *hook(UINT r,const BYTE *b,UINT n,const void *f){(void)r;(void)b;(void)n;(void)f;return 0;}
static void wo_render_event(const char *n,DWORD v){(void)n;(void)v;}
static BOOL test_write(LPCWSTR section,LPCWSTR key,LPCWSTR value,LPCWSTR path){
    (void)section;(void)key;(void)path;++preference_writes;UINT n=0;do{last_preference[n]=value[n];}while(value[n++]);return TRUE;
}
#define WritePrivateProfileStringW test_write
#include "../src/runtime/race_ui_patch.c"
static BYTE config[48];
static void __cdecl native_keys(void){
    ++native_key_calls;SHORT *key=(SHORT *)(game+0x66496a);native_last_key=*key;
    if(*key==0x3e)config[0x19]=(config[0x19]+1)%3;
    *key=-1;
}
static void __cdecl native_effects(void *camera){(void)camera;++effect_calls;}
void __stdcall test_entry(void){
    game=VirtualAlloc(0,0x800000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);if(!game)ExitProcess(1);
    *(BYTE **)(game+0x517958)=config;old_detail_keys=native_keys;old_ship_effects=native_effects;race_ui_active=TRUE;
    config[0x19]=1;apply_race_preferences();if(config[0x19]!=2)ExitProcess(2);
    SHORT *key=(SHORT *)(game+0x66496a);*key=0x29;detail_keys();
    if(refresh_show_fps || *key!=-1 || native_key_calls || preference_writes!=1 || last_preference[0]!=L'0')ExitProcess(3);
    detail_keys();if(refresh_show_fps || preference_writes!=1)ExitProcess(4);
    *key=0x29;detail_keys();if(!refresh_show_fps || preference_writes!=2)ExitProcess(5);
    *key=0x3b;detail_keys();if(native_last_key!=0x3b || !refresh_show_fps || preference_writes!=2)ExitProcess(6);
    *key=0x3e;detail_keys();if(config[0x19]!=0 || race_trail_detail!=0 || last_preference[0]!=L'0')ExitProcess(7);
    config[0x19]=1;apply_race_preferences();if(config[0x19]!=0)ExitProcess(8);
    *key=0x3e;detail_keys();if(config[0x19]!=1 || race_trail_detail!=1)ExitProcess(9);
    *(DWORD *)(game+0x8c98c)=1;*(int *)(game+0x87640)=60;
    const UINT rates[]={30,60,165,240};
    for(UINT r=0;r<4;++r){
        UINT transitions=0;BOOL previous=TRUE;
        for(UINT frame=0;frame<rates[r]*2;++frame){
            ui_ticks=frame*30/rates[r];
            *(DWORD *)(game+0x8c17c)=0; /* native per-render clear */
            old_ship_effects(0);finish_race_ui_effects();
            BOOL visible=*(DWORD *)(game+0x8c17c)!=0;
            /* Native cadence: on for 1/3 second, off for 1/6 second. */
            UINT phase=(frame*60/rates[r])%30;
            if(visible!=(phase<20))ExitProcess(10);
            if(visible!=previous)++transitions;previous=visible;
        }
        if(transitions!=7)ExitProcess(11);
    }
    *(int *)(game+0x87640)=0;finish_race_ui_effects();if(*(DWORD *)(game+0x8c17c))ExitProcess(12);
    if(!effect_calls)ExitProcess(13);
    ExitProcess(0);
}
