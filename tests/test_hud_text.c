#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "../src/runtime/build/render_hooks.h"
typedef struct TLVertex {float x,y,z,rhw;DWORD color,specular;float u,v;} TLVertex;
typedef void (__cdecl *QuadFn)(float,const BYTE *,int);
static BYTE *game;
static float ui_x=1,ui_y=1,anchor_x=.5f,anchor_y=.5f;
static float projection_x=1;
static float hud_message_motion_x;
static int hud_depth,ui_depth;
static BOOL installed=TRUE,refresh_active;
static QuadFn old_quad;
static SHORT interpolate_menu_angle(const BYTE *o,int x,int y,int z,SHORT a,SHORT p){(void)o;(void)x;(void)y;(void)z;(void)p;return a;}
static float ui_panel_phase(void){return 1;}
static void copy(BYTE *a,const BYTE *b,UINT n){while(n--)*a++=*b++;}
static BOOL equal(const BYTE *a,const BYTE *b,UINT n){while(n--)if(*a++!=*b++)return FALSE;return TRUE;}
static void *hook(UINT r,const BYTE *b,UINT n,const void *f){(void)r;(void)b;(void)n;(void)f;return 0;}
static void wo_render_event(const char *n,DWORD v){(void)n;(void)v;}
#include "../src/runtime/hud_text.c"
static BOOL title_input_active(void){return FALSE;}
static const char *title_prompt(const char *text){return text;}
#include "../src/runtime/menu_patch.c"
static BOOL ui_hd_frame_page(UINT slot){(void)slot;return FALSE;}
static BOOL ui_hd_frame_art(UiHdFrameArt *art){(void)art;return FALSE;}

static float screen_width,observed_left,observed_right,observed_anchor_y;
static BOOL observed;
/* Independent quad emission fixture: the original uses integer glyph pens,
 * half-width atlas quads, then the renderer's UI transform. Deliberately
 * uneven widths/advances expose advance-based and first-letter anchoring. */
static void __cdecl emit_text(const char *text,int x,int y,int size,int color) {
    (void)y;(void)color;
    const BYTE *metrics=game+(size==16?0x6e36c0:0x6e39f0);
    observed=FALSE;observed_anchor_y=anchor_y;
    int pen=x;
    for(UINT i=0;text[i];++i){
        BYTE c=(BYTE)text[i];UINT index=c>=32 && c<96?game[0x87640+c]:50;
        if(index==50){pen+=8;continue;}
        const WORD *glyph=(const WORD *)(metrics+index*8);
        float a=(pen+hud_message_motion_x)*screen_width/320;
        float b=(pen+glyph[2]*.5f+hud_message_motion_x)*screen_width/320;
        a=a*ui_x+screen_width*anchor_x*(1-ui_x);
        b=b*ui_x+screen_width*anchor_x*(1-ui_x);
        if(!observed || a<observed_left)observed_left=a;
        if(!observed || b>observed_right)observed_right=b;
        observed=TRUE;pen+=(SHORT)glyph[3]>>1;
    }
}
static float absf(float x){return x<0?-x:x;}
static int print_center_x;
static void __cdecl emit_center4(const char *t,int x,int y,int c){(void)t;(void)y;(void)c;print_center_x=x;}
static void check_message(const char *text,int x,int y,BOOL centered,int inside_hud,int size) {
    hud_depth=inside_hud;
    float ax=anchor_x,ay=anchor_y;int depth=ui_depth,ad=anchor_depth;
    text5(emit_text,text,x,y,size,1,centered);
    if(!observed || absf((observed_left+observed_right)*.5f-screen_width*.5f)>.001f)ExitProcess(10);
    if(anchor_x!=ax || anchor_y!=ay || ui_depth!=depth || anchor_depth!=ad || hud_message_motion_x!=0)ExitProcess(11);
    if(!inside_hud && observed_anchor_y!=.5f)ExitProcess(12);
}
void __stdcall test_entry(void) {
    game=VirtualAlloc(0,0x800000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);if(!game)ExitProcess(1);
    old_add_text=emit_text;
    for(UINT c=0;c<256;++c)game[0x87640+c]=50;
    for(UINT c='A';c<='Z';++c){UINT index=c-'A';game[0x87640+c]=(BYTE)index;
        for(UINT table=0;table<2;++table){WORD *metric=(WORD *)(game+(table?0x6e36c0:0x6e39f0)+index*8);metric[2]=(WORD)((11+index%9)*(table+1));metric[3]=(WORD)(metric[2]+3+index%4);}
    }
    for(UINT c='0';c<='9';++c){UINT index=26+c-'0';game[0x87640+c]=(BYTE)index;WORD *metric=(WORD *)(game+0x6e39f0+index*8);metric[2]=15;metric[3]=18;}
    old_print_center=emit_center4;
    const float displays[][2]={{640,480},{1920,1080},{2560,1080},{3840,1080},{1080,1920}};
    for(UINT i=0;i<sizeof(displays)/sizeof(displays[0]);++i){
        screen_width=displays[i][0];float ratio=(4.0f/3.0f)/(screen_width/displays[i][1]);ui_x=ratio<1?ratio:1;ui_y=ratio>1?1/ratio:1;
        check_message("AUTOPILOT",120,108,FALSE,1,8);
        check_message("DISENGAGING",110,120,FALSE,1,8);
        check_message("EXTENDED TIME",230,75,TRUE,0,8);
        check_message("DANGER",15,64,TRUE,0,8);
        check_message("CRITICAL SHIELD FAILURE",45,128,TRUE,0,8);
        check_message("PERFECT LAP",120,160,TRUE,1,8);
        check_message("WRONG WAY",50,110,TRUE,1,8);
        check_message("  EXTENDED TIME  ",230,75,TRUE,0,16);
        hud_depth=1;number5(0,3,160,128,8,0);
        if(!observed || absf((observed_left+observed_right)*.5f-screen_width*.5f)>.001f)ExitProcess(16);
        print_center("REAR LOCK",168,70,0);if(print_center_x!=160)ExitProcess(17);
        hud_depth=1;text5(emit_text,"CHECK",20,20,8,0,FALSE);
        if(absf(observed_left-20*screen_width*ui_x/320)>.001f)ExitProcess(13);
        /* A menu label at the same coordinates must retain its menu anchor. */
        menu_patching=TRUE;*(int *)(game+0x8c9d4)=1;hud_depth=0;
        text5(emit_text,"AUTOPILOT",120,108,8,0,FALSE);
        if(absf(observed_left-screen_width*menu_x(120))>.001f)ExitProcess(14);
        print_center("LOADING GAME",68,182,0);if(print_center_x!=68)ExitProcess(18);
        menu_patching=FALSE;*(int *)(game+0x8c9d4)=0;
    }
    float origin=99;
    if(hud_text_origin(game+0x87640,game+0x6e39f0,"   ",&origin) || origin!=99)ExitProcess(15);
    ExitProcess(0);
}
