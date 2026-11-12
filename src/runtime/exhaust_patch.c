/* Original trail simulation and flare artwork remain native. These wrappers
 * correct only render coordinates, depth and per-vertex alpha. Markers occupy
 * unused vertex fields until execute-stream preparation, then are cleared:
 * from the stream's copy there, and from the game's own vertices once the
 * frame has been drawn. */
typedef void (__cdecl *FlareFn)(int,int,int,int,float);
typedef void (__cdecl *TrailFn)(void *,void *,int);
static FlareFn old_draw_flare;
static TrailFn old_draw_trail;
static FinishFn old_finish_render;
static int flare_depth,trail_depth;
static int flare_integer_x,flare_integer_y;
static float flare_x,flare_y;
static UINT glow_aft_offset=12;
static UINT glow_up_offset=8;
static DWORD exhaust_flares,exhaust_trails,exhaust_material_buffers;
static DWORD glow_mesh_attachment_hits,glow_mesh_attachment_fallbacks;

static float glow_ship_matrix[15][16];
static DWORD glow_ship_frame[15];
static BOOL glow_ship_valid[15];
static void capture_ship_glow_matrix(const BYTE *object,int before) {
    if(!refresh_active || !refresh_race || !*(DWORD *)(game+0x8c98c) || before<0 ||
       *(int *)(game+0x53f19c)!=before+1)return;
    const BYTE *ships=*(const BYTE **)(game+0x518030);
    const BYTE *const *models=(const BYTE *const *)(game+0x517d00);
    if(!ships || !models)return;
    for(UINT ship=0;ship<15;++ship) {
        const SHORT *indices=(const SHORT *)(ships+ship*240);
        BOOL high=indices[0]>=0 && indices[0]<64 && models[indices[0]]==object;
        BOOL low=indices[1]>=0 && indices[1]<64 && models[indices[1]]==object;
        if(high || low) {
            copy((BYTE *)glow_ship_matrix[ship],render_entries+before*96,64);
            glow_ship_frame[ship]=refresh_frame;glow_ship_valid[ship]=TRUE;
        }
    }
}
static BOOL project_ship_glow(int ship,float *x,float *y,float *depth) {
    if(!refresh_active || !refresh_race || ship<0 || ship>=15 || !glow_ship_valid[ship] ||
       glow_ship_frame[ship]!=refresh_frame)return FALSE;
    const float *matrix=glow_ship_matrix[ship],*view=(const float *)(game+0x8b210);
    const float *projection=(const float *)(game+0x8b190);
    /* Native nozzle420 aft/15 up plus accepted emitter adjustment12 aft/8 up.
     * Use exactly the matrix submitted for this ship, including interpolated
     * heading/bank and camera, rather than projecting a separate trail sample. */
    float local_y=-(15.0f+glow_up_offset)/4096,local_z=-(420.0f+glow_aft_offset)/4096;
    float camera[3],v[3];
    for(UINT i=0;i<3;++i)camera[i]=local_y*matrix[4+i]+local_z*matrix[8+i]+matrix[12+i];
    for(UINT i=0;i<3;++i)v[i]=camera[0]*view[i]+camera[1]*view[4+i]+camera[2]*view[8+i]+view[12+i];
    if(v[2]<=.1f)return FALSE;
    *x=(v[0]*projection[0]/v[2]*2+1)*160;
    *y=(1-v[1]*projection[5]/v[2]*2)*120;
    *depth=projection[14]/v[2]+projection[10];
    return TRUE;
}
static BOOL shift_projected_glow(float *x,float *y,float *depth,const float *camera,const float *source) {
    const float *projection=(const float *)(game+0x8b190),*view=(const float *)(game+0x8b210);
    if(projection[0]==0 || projection[5]==0 || *depth>=projection[10])return FALSE;
    float z=projection[14]/(*depth-projection[10]);
    if(z<=0.1f)return FALSE;
    float rotated[3],delta[3];
    for(UINT i=0;i<3;++i)rotated[i]=source[0]*camera[i]+source[1]*camera[4+i]+source[2]*camera[8+i];
    for(UINT i=0;i<3;++i)delta[i]=rotated[0]*view[i]+rotated[1]*view[4+i]+rotated[2]*view[8+i];
    float new_z=z+delta[2];
    if(new_z<=0.1f)return FALSE;
    float vx=(*x/160-1)*0.5f*z/projection[0],vy=(1-*y/120)*0.5f*z/projection[5];
    *x=((vx+delta[0])*projection[0]/new_z*2+1)*160;
    *y=(1-(vy+delta[1])*projection[5]/new_z*2)*120;
    *depth=projection[14]/new_z+projection[10];
    return TRUE;
}
static BOOL offset_glow_emitter(float *x,float *y,float *depth,const float *camera,const LONG *nose,const LONG *fin) {
    if(!glow_aft_offset && !glow_up_offset)return FALSE;
    float source[3];
    /* Native fin = nose cross wing: it points down at zero attitude. */
    for(UINT i=0;i<3;++i)source[i]=(-nose[i]*(float)glow_aft_offset-fin[i]*(float)glow_up_offset)/(4096.0f*4096.0f);
    return shift_projected_glow(x,y,depth,camera,source);
}

static void adjust_flare_vertex(TLVertex *v) {
    float width=*(float *)(game+0xaf0b4),height=*(float *)(game+0xc8630);
    v->x=width*flare_x/320+(v->x-width*flare_integer_x/320)*ui_x;
    v->y=height*flare_y/240+(v->y-height*flare_integer_y/240)*ui_y;
    *(DWORD *)&v->u=FLARE_VERTEX_TAG;v->v=0;
}
/* A trail's vertices sit in the game's per-frame vertex store, and the next
 * frame hands the same slots out again from the start: to the track first,
 * then to whatever else it draws. The game's own writers never touch the
 * marker field, so a marker left behind turns the slot's next owner into a
 * trail. After a race on a short track, the far end of a longer one reached
 * the slots the short track's trails had used, and its road drew as a trail:
 * added to what lies under it, with nothing left to see of the road itself.
 * Remember each marked run and unmark it once the frame has been drawn. */
#define TRAIL_MARK_CAPACITY 64
static struct {LitVertex *vertices;int count;} trail_marks[TRAIL_MARK_CAPACITY];
static UINT trail_mark_count;
static void clear_trail_marks(void) {
    for(UINT i=0;i<trail_mark_count;++i)for(int j=0;j<trail_marks[i].count;++j) {
        LitVertex *v=trail_marks[i].vertices+j;
        if(v->reserved==TRAIL_VERTEX_TAG)v->reserved=0;
    }
    trail_mark_count=0;
}
static void prepare_trail_vertices(void *vertices,int count) {
    if(!exhaust_active || !trail_depth || !vertices || count<=0 || count>180 || count%6)return;
    /* A trail that cannot be remembered is left as the game draws it. */
    if(trail_mark_count==TRAIL_MARK_CAPACITY)return;
    LitVertex *v=vertices;
    trail_marks[trail_mark_count].vertices=v;trail_marks[trail_mark_count++].count=count;
    for(int i=0;i<count;++i) {
        BOOL center=(i%6==1 || i%6==4);
        v[i].color=(v[i].color&0xffffff)|(center?0xff000000u:0);
        v[i].reserved=TRAIL_VERTEX_TAG;
    }
    if(!(exhaust_trails++%600))wo_render_event("render_exhaust_trails",exhaust_trails);
}
/* The player's native trail is a rebuilt straight beam, not a history ring.
 * Move the complete beam with its interpolated ship. Moving only its head can
 * push that head past several128-unit segments at speed, folding triangles. */
static BOOL is_player_beam(const BYTE *trail,int ship) {
    if(ship!=*(SHORT *)(game+0x664820) || *(const int *)(trail+12)!=0)return FALSE;
    const LONG *left0=(const LONG *)(trail+16+8),*right0=(const LONG *)(trail+16+24);
    const LONG *left1=(const LONG *)(trail+16+40+8);LONG step[3];
    for(UINT axis=0;axis<3;++axis)step[axis]=left1[axis]-left0[axis];
    if(!step[0] && !step[1] && !step[2])return FALSE;
    for(UINT segment=0;segment<30;++segment) {
        const BYTE *item=trail+16+segment*40;
        if(!item[0])return FALSE;
        const LONG *left=(const LONG *)(item+8),*right=(const LONG *)(item+24);
        for(UINT axis=0;axis<3;++axis)
            if(left[axis]!=left0[axis]+step[axis]*(LONG)segment ||
               right[axis]!=right0[axis]+step[axis]*(LONG)segment)return FALSE;
    }
    return TRUE;
}
static BOOL copy_interpolated_trail(BYTE *out,const BYTE *trail,int ship) {
    if(!refresh_active || !refresh_race || !trail || ship<0 || ship>=15)return FALSE;
    int index=*(const int *)(trail+12);if(index<0 || index>=30)return FALSE;
    float delta[3];refresh_ship_delta(ship,delta);BOOL beam=is_player_beam(trail,ship);
    copy(out,trail,0x4c0);
    for(UINT segment=0;segment<30;++segment)if(beam || segment==(UINT)index) {
        LONG *left=(LONG *)(out+16+segment*40+8),*right=(LONG *)(out+16+segment*40+24);
        for(UINT axis=0;axis<3;++axis) {left[axis]+=(LONG)delta[axis];right[axis]+=(LONG)delta[axis];}
    }
    return TRUE;
}
static void __cdecl draw_trail(void *camera,void *trail,int ship) {
    BYTE render_trail[0x4c0];
    if(copy_interpolated_trail(render_trail,trail,ship))trail=render_trail;
    ++trail_depth;old_draw_trail(camera,trail,ship);--trail_depth;
}
static void __cdecl __attribute__((noinline)) draw_flare(int x,int y,int distance,int ship,float z) {
    if(!exhaust_active) {old_draw_flare(x,y,distance,ship,z);return;}
    flare_integer_x=x;flare_integer_y=y;flare_x=(float)x;flare_y=(float)y;
    /* The verified native caller retains its pre-_ftol float coordinates in
     * its own stack frame. Read them only for that exact return address. */
    const BYTE *entry=(const BYTE *)__builtin_frame_address(0)+4;
    if(*(const BYTE *const *)entry==game+0x3183b) {
        float fx=*(const float *)(entry+0x34),fy=*(const float *)(entry+0x30);
        float dx=fx-x,dy=fy-y;
        if(dx>-1 && dx<1 && dy>-1 && dy<1) {flare_x=fx;flare_y=fy;}
    }
    /* Native caller applies a .99 normalized-depth multiplier, pulling distant
     * glows in front of unrelated surfaces. Restore its actual projected depth. */
    float bias=*(float *)(game+0x862b8);
    if(bias>0.98f && bias<1)z/=bias;
    if(ship>=0 && ship<15 && *(const BYTE *const *)entry==game+0x3183b) {
        const BYTE *ships=*(const BYTE *const *)(game+0x518030);
        BOOL attached=project_ship_glow(ship,&flare_x,&flare_y,&z);
        if(ship==*(SHORT *)(game+0x664820)) {
            if(attached) {if(!(glow_mesh_attachment_hits++%300))wo_render_event("glow_mesh_attachment_hits",glow_mesh_attachment_hits);}
            else if(!(glow_mesh_attachment_fallbacks++%300))wo_render_event("glow_mesh_attachment_fallbacks",glow_mesh_attachment_fallbacks);
        }
        if(!attached && ships)offset_glow_emitter(&flare_x,&flare_y,&z,(const float *)(entry+0x78),
                                    (const LONG *)(ships+ship*240+0x10),(const LONG *)(ships+ship*240+0x30));
    }
    /* Native flare corners are signed16-bit coordinates. A projected centre
     * beyond that range wraps different corners across the viewport. Build
     * the original artwork about zero, then place it at the float centre in
     * adjust_flare_vertex, before clipping. Native size is capped at40 pixels. */
    if(distance<=0)return;
    flare_integer_x=flare_integer_y=0;
    ++flare_depth;old_draw_flare(0,0,distance,ship,z);--flare_depth;
    if(!(exhaust_flares++%600))wo_render_event("render_exhaust_flares",exhaust_flares);
}
static void __cdecl finish_render(void) {
    /* The screen pass follows the world's queue: its trails are drawn. */
    clear_trail_marks();
    if(exhaust_active) {
        BYTE *start=*(BYTE **)(game+0x53f1a0),*end=*(BYTE **)(game+0x631d54);
        BYTE *buffer=*(BYTE **)(game+0x7c9690);
        UINT size=*(UINT *)(game+0xc8610),count=*(UINT *)(game+0x631d50);
        if(buffer && start>=buffer && end>=start && end<buffer+size && count<=(UINT)(start-buffer)/32) {
            UINT capacity=(UINT)(buffer+size-start);
            if(capacity>sizeof(shadow_instructions))capacity=sizeof(shadow_instructions);
            UINT n=capacity>4?rewrite_effect_material(start,(UINT)(end-start),shadow_instructions,capacity-4,
                0,0,buffer,count,TRUE):0;
            if(n) {
                copy(start,shadow_instructions,n);*(BYTE **)(game+0x631d54)=start+n;
                if(!(exhaust_material_buffers++%600))wo_render_event("render_exhaust_screen_buffers",exhaust_material_buffers);
            }
            clear_effect_tags(buffer,count,TRUE);
        }
    }
    ui_hd_finish();
    old_finish_render();
}
static void initialize_exhaust(const WCHAR *settings) {
    if(!old_finish_transform)return;
#define EXHAUST_VERIFY(name) if(!equal(game+RVA_##name,entry_##name,sizeof(entry_##name)))return
    EXHAUST_VERIFY(draw_flare);EXHAUST_VERIFY(draw_trail);EXHAUST_VERIFY(finish_render);
    old_draw_flare=(FlareFn)hook(RVA_draw_flare,entry_draw_flare,sizeof(entry_draw_flare),draw_flare);
    old_draw_trail=(TrailFn)hook(RVA_draw_trail,entry_draw_trail,sizeof(entry_draw_trail),draw_trail);
    old_finish_render=(FinishFn)hook(RVA_finish_render,entry_finish_render,sizeof(entry_finish_render),finish_render);
    exhaust_active=old_draw_flare && old_draw_trail && old_finish_render;
    glow_aft_offset=GetPrivateProfileIntW(L"Render",L"GlowAftOffset",12,settings);
    if(glow_aft_offset>64)glow_aft_offset=12;
    glow_up_offset=GetPrivateProfileIntW(L"Render",L"GlowUpOffset",8,settings);
    if(glow_up_offset>64)glow_up_offset=8;
    wo_render_event("render_exhaust_test",exhaust_active);
    wo_render_event("render_glow_aft_offset",glow_aft_offset);
    wo_render_event("render_glow_up_offset",glow_up_offset);
}
