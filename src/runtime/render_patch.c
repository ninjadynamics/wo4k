/* Rendering-only widescreen prototype for the hash-verified original PC EXE.
 * Enabled only by WO2097_RENDER_TEST=1 in the separate test launcher.
 * Original code is called through verified-instruction trampolines; simulation
 * state, AI, input and timing are never replaced here.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "build/render_hooks.h"
#include "simulation_api.h"
#include "audio_api.h"
#include "title_clock.h"

typedef struct TLVertex {
    float x,y,z,rhw;
    DWORD color,specular;
    float u,v;
} TLVertex;
typedef void (__cdecl *VertexFn)(TLVertex *,int,int,float,UINT,UINT,UINT);
typedef void (__cdecl *QuadFn)(float,const BYTE *,int);
typedef void (__cdecl *HudFn)(void *,int,void *,void *);
typedef void (__cdecl *ObjectFn)(BYTE *,int,BYTE *);
typedef void (__cdecl *SkeletonFn)(BYTE *,void *,int,void *,void *,int,int);
typedef void (__cdecl *SectionFn)(void *,BYTE *);
static BYTE *game;
static VertexFn old_vertex,old_scaled_vertex;
static QuadFn old_quad,old_scaled_quad,old_gouraud_quad,old_gradient_quad;
static HudFn old_hud;
static ObjectFn old_render_object;
static SkeletonFn old_render_skeleton;
static SectionFn old_section_objects;
static float ui_x=1,ui_y=1,anchor_x=0.5f,anchor_y=0.5f;
static float projection_x=1;
static float physical_aspect=1,geometry_width_scale=1;
static int hud_depth,ui_depth;
static float hud_message_motion_x;
static BOOL installed;
static BOOL distance_active;
static BOOL exhaust_active;
static BOOL refresh_active,refresh_race,simulation_active;
static DWORD refresh_frame;
static const BYTE *refresh_last_matrix,*refresh_last_skeleton;
static void refresh_ship_delta(int ship,float *delta);
#define TRAIL_VERTEX_TAG 0x574f5452u
#define FLARE_VERTEX_TAG 0x574f474cu
#define SKY_VERTEX_TAG 0x574f534bu
#define SPRITE_VERTEX_TAG 0x574f5350u
#define PS1_ADD_VERTEX_TAG 0x574f4131u
#define PS1_SUB_VERTEX_TAG 0x574f5332u
static void fix_queued_matrix(int before,const BYTE *skeleton);
static void initialize_sprites(void);
static float ui_panel_phase(void);
static SHORT interpolate_menu_angle(const BYTE *object,int x,int y,int z,SHORT angle,SHORT pitch);
static float particle_motion_x,particle_motion_y;
static void controller_input(void);
static void keyboard_focus(void);
static BOOL title_input_active(void);
static const char *title_prompt(const char *text);
static void initialize_title(void);
static void title_overlay_present(void);
static void hide_game_cursor(void);
static void prepare_intro_movie(void);
static void startup_present(void);
static void initialize_controller(void);
static void initialize_credits(void);
static void initialize_race_ui(const WCHAR *settings);
static void hud_layout_begin(BOOL fps);
static void hud_layout_finish(void);
static void apply_race_preferences(void);
static void finish_race_ui_effects(void);
static void ui_hd_finish(void);
static void ui_hd_presented(void);
static void hd_world_finish(void);
static BOOL ui_hd_frame_page(UINT slot);
static float credits_motion_y;
static BYTE *render_entries;
static BOOL initialize_distance(void);
static DWORD precise_matrices,scenery_candidates,scenery_visible;
void wo_render_event(const char *name,DWORD value);
static BOOL precise_transform(float *out,const BYTE *skeleton,UINT depth);
static void update_projection(void) {
    if(!installed)return;
    float *view=(float *)(game+(*(int *)(game+0x8c98c)?0x8b210:0x8b1d0));
    float x=view[0],y=view[5];if(x<0)x=-x;if(y<0)y=-y;
    if(x<=0.000001f || y<=0.000001f)return;
    projection_x=physical_aspect*(y/x)*geometry_width_scale;
    *(float *)(game+0x8b190)=projection_x;
}

static BOOL equal(const BYTE *a,const BYTE *b,UINT n) {while(n--)if(*a++!=*b++)return FALSE;return TRUE;}
static void copy(BYTE *a,const BYTE *b,UINT n) {while(n--)*a++=*b++;}
static void jump(BYTE *at,const void *to) {at[0]=0xe9;*(LONG *)(at+1)=(LONG)((const BYTE *)to-at-5);}
static void *hook(UINT rva,const BYTE *expected,UINT n,const void *replacement) {
    BYTE *entry=game+rva,*trampoline;DWORD protection,ignored;
    if(!equal(entry,expected,n))return 0;
    /* Never writable and executable at once: write, then seal as code. */
    trampoline=VirtualAlloc(0,n+5,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    if(!trampoline)return 0;
    copy(trampoline,entry,n);jump(trampoline+n,entry+n);
    if(!VirtualProtect(trampoline,n+5,PAGE_EXECUTE_READ,&ignored)){VirtualFree(trampoline,0,MEM_RELEASE);return 0;}
    FlushInstructionCache(GetCurrentProcess(),trampoline,n+5);
    if(!VirtualProtect(entry,n,PAGE_EXECUTE_READWRITE,&protection)){VirtualFree(trampoline,0,MEM_RELEASE);return 0;}
    jump(entry,replacement);for(UINT i=5;i<n;++i)entry[i]=0x90;
    VirtualProtect(entry,n,protection,&ignored);
    FlushInstructionCache(GetCurrentProcess(),entry,n);
    return trampoline;
}
#include "hud_text.c"
#include "menu_patch.c"
#include "scene_lighting.c"
#include "shadow_patch.c"
#include "backdrop_patch.c"
#include "rotation_patch.c"
#include "exhaust_patch.c"
#include "refresh_patch.c"
#include "race_ui_patch.c"
#include "menu_model_motion.c"
#include "controller_patch.c"
#include "keyboard_focus.c"
#include "movies_patch.c"
#include "title_patch.c"
#include "audio_menu_patch.c"
#include "png_art.c"
#include "startup_art.c"
#include "dx_error.c"
#include "ui_hd.c"
#include "credits_patch.c"
#include "sprite_patch.c"
#include "hud_layout.c"
static void adjust(TLVertex *v) {
    v->x+=*(float *)(game+0xaf0b4)*hud_message_motion_x/320;
    v->y+=*(float *)(game+0xc8630)*credits_motion_y/240;
    v->x+=*(float *)(game+0xaf0b4)*particle_motion_x/320;
    v->y+=*(float *)(game+0xc8630)*particle_motion_y/240;
    if(menu_motion_dx)v->x+=*(float *)(game+0xaf0b4)*menu_motion_dx;
    if(reticle_depth) {adjust_reticle_vertex(v);return;}
    if(installed && exhaust_active && flare_depth) {adjust_flare_vertex(v);return;}
    if(installed && menu_frame_depth) {menu_adjust_vertex(v);return;}
    if(!installed || !ui_depth)return;
    float width=*(float *)(game+0xaf0b4),height=*(float *)(game+0xc8630);
    v->x=v->x*ui_x+width*anchor_x*(1-ui_x);
    v->y=v->y*ui_y+height*anchor_y*(1-ui_y);
    hud_layout_vertex(v);
}
static void __cdecl vertex(TLVertex *v,int x,int y,float z,UINT r,UINT g,UINT b) {
    old_vertex(v,x,y,z,r,g,b);adjust(v);
}
static void __cdecl scaled_vertex(TLVertex *v,int x,int y,float z,UINT r,UINT g,UINT b) {
    old_scaled_vertex(v,x,y,z,r,g,b);adjust(v);
}
static void draw_quad(QuadFn original,float z,const BYTE *p,int blend,float basis,BOOL always_ui) {
    float previous_motion=menu_motion_dx;menu_motion_dx=panel_motion(p,basis);
    float previous_x=anchor_x,previous_y=anchor_y;
    int previous_depth=ui_depth;
    if(installed && (hud_depth || always_ui)) {
        float x=((SHORT *)(p+8))[0]+((SHORT *)(p+16))[0]+((SHORT *)(p+24))[0]+((SHORT *)(p+32))[0];
        float y=((SHORT *)(p+8))[1]+((SHORT *)(p+16))[1]+((SHORT *)(p+24))[1]+((SHORT *)(p+32))[1];
        x/=4*basis;y/=3*basis;
        /* Classify complete primitives, never individual corners: bars and
         * glyphs retain their shapes. Thresholds separate the original HUD's
         * left timer, centered indicator, and right race/speed/energy groups. */
        if(!anchor_depth && menu_active()) {
            /* Menu widgets keep their shape about their responsive location.
             * The header logo and its SD halves share their native anchor. */
            if(*(const WORD *)(p+22)==35) {menu_anchor(141,54);}
            else {menu_anchor((int)(x*320),(int)(y*240));}
        } else if(!anchor_depth) {
            anchor_x=hud_depth?(x<0.4f?0:(x>0.65f?1:0.5f)):0.5f;
            anchor_y=hud_depth?(y<0.375f?0:(y>0.625f?1:0.5f)):0.5f;
        }
        ++ui_depth;
    }
    UINT previous_group=hud_layout_group,first=hud_layout_count;
    hud_layout_group=(original==old_quad || original==old_scaled_quad)?hud_layout_classify(p):HUD_NONE;
    original(z,p,blend);
    hud_layout_quad(p,first);hud_layout_group=previous_group;
    anchor_x=previous_x;anchor_y=previous_y;ui_depth=previous_depth;
    menu_motion_dx=previous_motion;
}
BOOL wo_render_active(void) {return installed;}
#include "ui_hd_logo.c"
static void __cdecl quad(float z,const BYTE *p,int blend) {
    if(is_menu_frame(p))draw_menu_frame(z,p,blend);
    else draw_quad(old_quad,z,p,blend,320,FALSE);
}
static void __cdecl scaled_quad(float z,const BYTE *p,int blend) {
    if(ui_hd_draw_logo(z,p,blend))return;
    BYTE positioned[40];p=audio_layout_quad(p,positioned);
    if(menu_stat_gauge(p)) {
        /* Map shared FT4 endpoints identically instead of anchoring each
         * coloured segment at its own centre, which opens widescreen gaps. */
        BOOL thin_x=menu_thin_x,thin_y=menu_thin_y;
        menu_thin_x=menu_thin_y=FALSE;
        ++menu_frame_depth;old_scaled_quad(z,p,blend);--menu_frame_depth;
        menu_thin_x=thin_x;menu_thin_y=thin_y;
        return;
    }
    draw_quad(old_scaled_quad,z,p,blend,640,TRUE);
}
/* Native 402991 is DrawPolyG4, also used by projected world flares. It is
 * not a textured UI primitive: those projected centers must never be recentered. */
static void __cdecl gouraud_quad(float z,const BYTE *p,int blend) {draw_quad(old_gouraud_quad,z,p,blend,320,menu_active());}
static void __cdecl gradient_quad(float z,const BYTE *p,int blend) {draw_quad(old_gradient_quad,z,p,blend,320,FALSE);}
static void __cdecl hud(void *ships,int choice,void *config,void *score) {
    DWORD saved_hud_frame=*(DWORD *)(game+0x875a8);
    BYTE saved_rear_cycle=game[0x8bb90];
    if(refresh_active) {*(DWORD *)(game+0x875a8)=ui_ticks;game[0x8bb90]=(BYTE)(ui_ticks%20);}
    hud_layout_begin(FALSE);
    ++hud_depth;old_hud(ships,choice,config,score);--hud_depth;
    hud_layout_finish();
    *(DWORD *)(game+0x875a8)=saved_hud_frame;
    game[0x8bb90]=saved_rear_cycle;
    static UINT frames;
    if(installed && !(frames++%300)) {
        wo_render_event("render_precision_matrices",precise_matrices);
        wo_render_event("render_float_rotation_reads",float_rotation_reads);
        wo_render_event("render_projection_x_million",(DWORD)(projection_x*1000000));
        wo_render_event("render_active_view",*(DWORD *)(game+0x8c98c));
        wo_render_event("render_scenery_candidates",scenery_candidates);
        wo_render_event("render_scenery_visible",scenery_visible);
    }
    draw_refresh_stats();
    draw_scene_light_status();
}

/* Compose the original LOCAL transforms using floats. The game's original
 * GetSkeletonMatrix still runs and updates its integer cache for simulation.
 * Only the separate D3D queue matrix is replaced, so fractional contributions
 * survive every parent multiplication and translation instead of >>12 steps.
 */
static BOOL precise_transform(float *out,const BYTE *skeleton,UINT depth) {
    float local[16],parent[16];
    if(!skeleton || depth>=32)return FALSE;
    for(UINT i=0;i<16;++i)local[i]=0;
    const float *rotation=cached_rotation(skeleton);
    for(UINT row=0;row<3;++row)
        for(UINT col=0;col<3;++col)
            local[row*4+col]=rotation?rotation[col*3+row]:((const SHORT *)skeleton)[col*3+row]*(1.0f/4096.0f);
    for(UINT col=0;col<3;++col)local[12+col]=((const LONG *)(skeleton+0x14))[col]*(1.0f/4096.0f);
    local[15]=1;
    const BYTE *super=*(const BYTE *const *)(skeleton+0x44);
    if(!super) {
        copy((BYTE *)out,(const BYTE *)local,sizeof(local));
        if(!depth)interpolate_render_matrix(out,skeleton);
        return TRUE;
    }
    if(!precise_transform(parent,super,depth+1))return FALSE;
    for(UINT row=0;row<4;++row)
        for(UINT col=0;col<4;++col) {
            float value=0;
            for(UINT k=0;k<4;++k)value+=local[row*4+k]*parent[k*4+col];
            out[row*4+col]=value;
        }
    if(!depth)interpolate_render_matrix(out,skeleton);
    return TRUE;
}
static void fix_queued_matrix(int before,const BYTE *skeleton) {
    int after=*(int *)(game+0x53f19c);
    if(before>=0 && after==before+1) {
        float precise[16];
        if(precise_transform(precise,skeleton,0)) {
            menu_position_model(precise);
            copy(render_entries+before*96,(const BYTE *)precise,sizeof(precise));
            ++precise_matrices;
        }
    }
}
static void __cdecl render_object(BYTE *object,int mode,BYTE *rgb) {
    update_projection();
    int before=*(int *)(game+0x53f19c);
    old_render_object(object,mode,rgb);
    if(installed && __builtin_return_address(0)==game+0x2bb17)capture_backdrop(before);
    if(installed && object) {
        fix_queued_matrix(before,*(BYTE **)(object+0x30));
        capture_ship_glow_matrix(object,before);
    }
}
static void __cdecl render_skeleton(BYTE *skeleton,void *vertices,int count,void *triangles,void *quads,int quad_count,int mode) {
    update_projection();
    if(soft_sprite_submission(skeleton,vertices,count,triangles,quads,quad_count,mode))return;
    soften_shadow(&vertices,&count,&triangles);
    prepare_trail_vertices(vertices,count);
    int before=*(int *)(game+0x53f19c);
    old_render_skeleton(skeleton,vertices,count,triangles,quads,quad_count,mode);
    if(installed)fix_queued_matrix(before,skeleton);
}

/* The full-scene path submits every scenery object and lets D3D clip it. The
 * native origin/radius tests can discard visible geometry around corners.
 * Outside full-scene mode, retain section candidates and float frustum tests.
 */
static void __cdecl section_objects(void *unused,BYTE *camera) {
    if(!installed) {old_section_objects(unused,camera);return;}
    BYTE *section=*(BYTE **)(camera+4);
    SHORT *indices=*(SHORT **)(section+0x1c);
    int count=*(SHORT *)(section+0x20);
    BYTE **objects=*(BYTE ***)(game+0x6650fc);
    BYTE *config=*(BYTE **)(game+0x517958);
    UINT limit=((WORD *)(game+0x899d8))[(unsigned char)config[0x1b]&7];
    SHORT origin[4]={0,0,0,0};LONG position[4],flag;
    typedef void (__cdecl *SetFn)(BYTE *);
    typedef void (__cdecl *RotFn)(const SHORT *,LONG *,LONG *);
    SetFn set=(SetFn)(game+0x4d7f0);
    RotFn rot=(RotFn)(game+0x56e10);
    BYTE *next=distance_active?objects[0]:0;
    for(int i=count-1;distance_active?next!=0:i>=0;--i) {
        BYTE *object=distance_active?next:objects[indices[i]],*skeleton=*(BYTE **)(object+0x30);
        if(distance_active)next=*(BYTE **)(object+0x3c);
        *(SHORT *)(skeleton+0x40)=1;set(skeleton);rot(origin,position,&flag);
        float radius=(float)*(LONG *)(object+0x34),z=(float)position[2]+radius;
        ++scenery_candidates;
        if(distance_active) {
            render_object(object,0,0);++scenery_visible;continue;
        }
        if(z<=2048 || (float)position[2]-radius>=(float)limit)continue;
        float x=position[0]*160.0f/z,y=position[1]*160.0f/z,r=radius*160.0f/z;
        float half_width=160.0f/projection_x;
        if(x < -half_width-r || x > half_width+r || y < -120-r || y > 120+r)continue;
        render_object(object,0,0);++scenery_visible;
    }
    ++*(int *)(game+0x899d0);
}

void wo_render_initialize(BYTE *image,const WCHAR *root) {
    WCHAR enabled[8],settings[MAX_PATH];int width,height;float ratio;
    IMAGE_NT_HEADERS32 *nt=(IMAGE_NT_HEADERS32 *)(image+((IMAGE_DOS_HEADER *)image)->e_lfanew);
    if(installed || nt->OptionalHeader.Magic!=0x10b || nt->OptionalHeader.SizeOfImage!=0x7ee000)return;
    if(!GetEnvironmentVariableW(L"WO2097_RENDER_TEST",enabled,8) || enabled[0]!=L'1')return;
    game=image;
    render_entries=game+0x533c50;
    /* Validate every entry before changing anything. */
#define VERIFY(name) if(!equal(game+RVA_##name,entry_##name,sizeof(entry_##name)))return
    VERIFY(hud);VERIFY(vertex);VERIFY(scaled_vertex);VERIFY(quad);
    VERIFY(scaled_quad);VERIFY(gouraud_quad);VERIFY(gradient_quad);
    VERIFY(render_object);VERIFY(render_skeleton);VERIFY(section_objects);
    if(*(DWORD *)(game+0x8b190)!=0x3f800000)return;
    width=GetSystemMetrics(SM_CXSCREEN);height=GetSystemMetrics(SM_CYSCREEN);
    if(width<=0 || height<=0)return;
    ratio=(4.0f/3.0f)/((float)width/(float)height);
    /* The selected view matrix ALSO scales X/Y differently during races
     * (27.2 vs32); menus use38/38. Compensate its active ratio as well as the
     * physical display, before both software effects and native D3D draw. */
    physical_aspect=(float)height/(float)width;
    projection_x=physical_aspect;
    UINT path_length=0;
    while(root[path_length] && path_length<MAX_PATH-32) {settings[path_length]=root[path_length];++path_length;}
    const WCHAR *settings_name=L"Wipeout2097.ini";
    while((*((WCHAR *)settings+path_length++)=*settings_name++)) {}
    UINT geometry_width=GetPrivateProfileIntW(L"Render",L"GeometryWidthPercent",100,settings);
    if(geometry_width<75 || geometry_width>150)geometry_width=100;
    geometry_width_scale=geometry_width/100.0f;
    ui_x=ratio<1?ratio:1;ui_y=ratio>1?1/ratio:1;
#define INSTALL(name,kind) old_##name=(kind)hook(RVA_##name,entry_##name,sizeof(entry_##name),name)
    INSTALL(vertex,VertexFn);INSTALL(scaled_vertex,VertexFn);
    INSTALL(quad,QuadFn);INSTALL(scaled_quad,QuadFn);INSTALL(gouraud_quad,QuadFn);INSTALL(gradient_quad,QuadFn);
    INSTALL(hud,HudFn);
    INSTALL(render_object,ObjectFn);INSTALL(render_skeleton,SkeletonFn);INSTALL(section_objects,SectionFn);
    if(!old_vertex || !old_scaled_vertex || !old_quad || !old_scaled_quad || !old_gouraud_quad || !old_gradient_quad || !old_hud)return;
    if(!old_render_object || !old_render_skeleton || !old_section_objects)return;
    /* Horizontal FOV expansion. Both native D3D world geometry and the
     * software-projected effects read this same matrix coefficient. */
    *(float *)(game+0x8b190)=projection_x;
    installed=TRUE;
    update_projection();
    wo_render_event("render_patch_version",44);
    wo_render_event("render_display_width",width);wo_render_event("render_display_height",height);
    wo_render_event("render_geometry_width_percent",geometry_width);
    initialize_menu();
    initialize_scene_lighting(settings);
    initialize_shadows(settings);
    initialize_exhaust(settings);
    initialize_rotations(settings);
    initialize_distance();
    initialize_refresh(settings);
    initialize_movies(root);
    initialize_title();
    initialize_audio_menu();
    initialize_startup_art(root,(UINT)width,(UINT)height);
    initialize_dx_error();
    initialize_ui_hd(root);
    initialize_hud_layout();
    if(refresh_active)simulation_active=wo_sim_initialize(image);
}

#include "distance_patch.c"
#include "title_overlay.c"
