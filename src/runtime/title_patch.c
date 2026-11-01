/* TitleScreen's EBX counter originally increments once per presentation.
 * Feed that register elapsed 30 Hz units, keeping its native 900/150 limits. */
#include "title_clock.h"
static void (__cdecl *old_title_screen)(void);
static void (__cdecl *old_title_input)(void) __attribute__((used));
static LARGE_INTEGER title_started,title_frequency;
static BOOL title_clock_started,title_active;
static BOOL title_input_active(void) {return title_active;}
static const char *title_prompt(const char *text) {
    if(title_active && controller_active && text==(const char *)(game+0x8c868))return "PRESS START";
    return text;
}
static DWORD __cdecl title_clock_frame(void) __attribute__((used,noinline));
static DWORD __cdecl title_clock_frame(void) {
    LARGE_INTEGER now;QueryPerformanceCounter(&now);
    if(!title_clock_started){title_started=now;title_clock_started=TRUE;return 0;}
    return title_frames((double)(now.QuadPart-title_started.QuadPart)/(double)title_frequency.QuadPart);
}
__attribute__((naked)) static void title_input(void) {
    __asm__ volatile("pushal\n\tcall _title_clock_frame\n\tmovl %eax,16(%esp)\n\tpopal\n\tjmp *_old_title_input");
}
static void __cdecl title_screen(void) {
    movie_before_title();
    title_active=TRUE;title_clock_started=FALSE;
    old_title_screen();
    title_active=FALSE;title_clock_started=FALSE;
}
static void initialize_title(void) {
    static const BYTE entry[]={0x81,0xec,0x00,0x01,0x00,0x00};
    static const BYTE input_call[]={0xe8,0xa7,0x96,0xff,0xff};
    if(!refresh_active || !equal(game+0x2f670,entry,sizeof(entry)) || !equal(game+0x2f864,input_call,sizeof(input_call)))return;
    QueryPerformanceFrequency(&title_frequency);if(!title_frequency.QuadPart)return;
    DWORD protection,ignored;
    if(!VirtualProtect(game+0x2f864,5,PAGE_EXECUTE_READWRITE,&protection))return;
    old_title_screen=(void *)hook(0x2f670,entry,sizeof(entry),title_screen);
    if(old_title_screen){
        old_title_input=(void *)(game+0x28f10);
        game[0x2f864]=0xe8;*(LONG *)(game+0x2f865)=(LONG)((BYTE *)title_input-(game+0x2f869));
        FlushInstructionCache(GetCurrentProcess(),game+0x2f864,5);
    }
    VirtualProtect(game+0x2f864,5,protection,&ignored);
    wo_render_event("render_title_clock",old_title_screen?1:0);
}
