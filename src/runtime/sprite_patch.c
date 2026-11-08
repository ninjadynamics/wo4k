typedef void (__cdecl *BillboardFn)(int,const BYTE *,const float *);
static BillboardFn old_billboards;
static QuadFn old_target_line;
static QuadFn old_effect_triangle;
static RefreshVoidFn old_update_flares;
static RefreshVoidFn old_update_explosions,old_draw_explosions;
static int reticle_depth;
static float reticle_cx,reticle_cy;
static BYTE sprite_copies[1024*1024];
static UINT sprite_copy_used;
static DWORD sprite_copy_frame=0xffffffffu;
static BOOL sprites_active;
static float sprite_opacity=1;
static UINT explosion_submission_depth;
static BYTE explosion_previous[32*24];
static BOOL explosion_history;
static DWORD explosion_history_frame;
static DWORD flare_raw[2],flare_drawn[2];
static BOOL flare_history;
static BOOL same_explosion(const BYTE *a,const BYTE *b) {
    return *(const SHORT *)(a+20)>0 && *(const SHORT *)(b+20)>0 &&
           *(const SHORT *)(a+20)>=*(const SHORT *)(b+20) &&
           equal(a,b,4) && equal(a+8,b+8,4) && equal(a+16,b+16,4) && equal(a+22,b+22,2);
}
static void interpolate_effect_positions(BYTE *live,const BYTE *current) {
    for(UINT i=0;i<32;++i) {
        const BYTE *a=explosion_previous+i*24,*b=current+i*24;
        if(same_explosion(a,b))*(LONG *)(live+i*24+4)=*(const LONG *)(a+4)+
            (LONG)((*(const LONG *)(b+4)-*(const LONG *)(a+4))*(float)ui_phase);
    }
}
static void adjust_reticle_vertex(TLVertex *v) {
    float cx=reticle_cx**(float *)(game+0xaf0b4)/320;
    float cy=reticle_cy**(float *)(game+0xc8630)/240;
    v->x=cx+(v->x-cx)*ui_x;v->y=cy+(v->y-cy)*ui_y;
}
static void __cdecl target_line(float z,const BYTE *p,int blend) {
    if(p>=game+0x5163c0 && p<game+0x5165c0) {
        const BYTE *bank=game+0x5163c0+((p-game-0x5163c0)/64)*64;
        float x=0,y=0;
        for(UINT i=0;i<4;++i)for(UINT j=0;j<2;++j) {
            x+=*(const SHORT *)(bank+i*16+8+j*4);y+=*(const SHORT *)(bank+i*16+10+j*4);
        }
        reticle_cx=x/8;reticle_cy=y/8;++reticle_depth;
        old_target_line(z,p,blend);--reticle_depth;
    } else old_target_line(z,p,blend);
}
static void __cdecl effect_triangle(float z,const BYTE *p,int blend) {
    if((p>=game+0x665860 && p<game+0x665be0) || (p>=game+0x665fa0 && p<game+0x666320)) {
        reticle_cx=*(const SHORT *)(p+16);reticle_cy=*(const SHORT *)(p+18);
        ++reticle_depth;old_effect_triangle(z,p,blend);--reticle_depth;
    } else old_effect_triangle(z,p,blend);
}
/* Verified native DrawExplode uses 24-byte records and omits their blend word
 * (offset18) from its 32-byte billboard. Reassociate only this draw's output,
 * including the exact camera/ship translation and per-emitter depth offset.
 * Mode0 is the supported EXE's normal submission, not an opaque-material flag. */
static UINT ps1_billboard_blend(const BYTE *quad,UINT *next_emitter) {
    BYTE *camera=*(BYTE **)(game+0x665110);
    BYTE *position=camera?*(BYTE **)camera:0;
    if(!position)return 0;
    const SHORT *q=(const SHORT *)quad;
    for(UINT i=*next_emitter;i<32;++i){
        const BYTE *effect=game+0x665be0+i*24;
        SHORT life=*(const SHORT *)(effect+20),flash=*(const SHORT *)(effect+16);
        UINT blend=*(const WORD *)(effect+18);
        if(life<=0 || !((flash==1 && life<15) || (flash==0 && life<17)) || (blend!=1 && blend!=2))continue;
        if(quad[12]!=effect[13] || quad[13]!=effect[14] || quad[14]!=effect[15])continue;
        LONG p[3];for(UINT axis=0;axis<3;++axis)p[axis]=*(const LONG *)(effect+axis*4)+*(const LONG *)(position+0x14+axis*4);
        SHORT ship=*(const SHORT *)(effect+22);
        if(ship>=0){
            BYTE *ships=*(BYTE **)(game+0x518030);if(!ships || ship>=12)continue;
            for(UINT axis=0;axis<3;++axis)p[axis]+=*(const LONG *)(ships+ship*240+0x40+axis*4);
        }
        if(q[0]==(SHORT)p[0] && q[1]==(SHORT)p[1] && q[2]==(SHORT)(p[2]-(LONG)i)){*next_emitter=i+1;return blend;}
    }
    return 0;
}
static BOOL soft_sprite_submission(BYTE *s,void *v,int n,void *t,void *quads,int count,int mode) {
    (void)v;
    if(!sprites_active || !explosion_submission_depth || n || !quads || count<1 || count>32 || *(UINT *)(game+0x53f19c)>=4096)return FALSE;
    if(sprite_copy_frame!=refresh_frame) {sprite_copy_frame=refresh_frame;sprite_copy_used=0;}
    UINT size=count*32;if(size>sizeof(sprite_copies)-sprite_copy_used)return FALSE;
    BYTE *out=sprite_copies+sprite_copy_used;sprite_copy_used+=size;copy(out,quads,size);
    UINT next_emitter=0;
    for(int i=0;i<count;++i){
        BYTE *q=out+i*32;UINT blend=ps1_billboard_blend(q,&next_emitter);
        if(blend){*(WORD *)(q+6)=(WORD)(blend==1?0x5751:0x5752);q[15]=(BYTE)(sprite_opacity*255+.5f);}
    }
    /* One original-size quad per animation sample. Concentric expansions
     * changed the silhouette and accumulated a dark matte around smoke. */
    int before=*(int *)(game+0x53f19c);
    old_render_skeleton(s,0,0,t,out,count,mode);fix_queued_matrix(before,s);
    return TRUE;
}
/* Native billboards rotate only around world Y. Construct the camera-facing
 * axes through the inverse queue matrix, keeping original texture dimensions
 * and bottom anchor. Pitch and banking can no longer flatten the sprite.
 * Camera-facing must not mean screen-upright, though: like the native ones,
 * and like the scenery around them, sprites keep standing along world Y, so
 * roll the camera's axes onto the view direction of world Y (row 1 of the
 * queue matrix). The first-person view rolls with the ship; the chase view
 * does not, so its sprites are unchanged. */
static float sprite_sqrt(float x) {double y;__asm__("fsqrt":"=t"(y):"0"((double)x));return (float)y;}
static void correct_billboards(LitVertex *v,int count,const BYTE *quads,const float *m) {
    float a=m[0],b=m[4],c=m[8],d=m[1],e=m[5],f=m[9],g=m[2],h=m[6],i=m[10];
    float det=a*(e*i-f*h)-b*(d*i-f*g)+c*(d*h-e*g);if(det>-.000001f && det<.000001f)return;
    float right[3]={(e*i-f*h)/det,(f*g-d*i)/det,(d*h-e*g)/det};
    float up[3]={(c*h-b*i)/det,(a*i-c*g)/det,(b*g-a*h)/det};
    /* World Y seen from the camera: (b,e) across and down the screen. Looking
     * straight along it there is no roll to follow; keep the camera's axes. */
    float roll=b*b+e*e;
    if(roll>.000001f) {
        float length=1/sprite_sqrt(roll),across=b*length,down=e*length;
        for(UINT axis=0;axis<3;++axis) {
            float r=right[axis],u=up[axis];
            right[axis]=down*r-across*u;up[axis]=across*r+down*u;
        }
    }
    for(int q=0;q<count;++q) {
        const SHORT *s=(const SHORT *)(quads+q*32);
        float width=s[8]*s[10]*(4.0f/256/4096),height=s[9]*s[11]*(1.0f/256/4096);
        for(UINT corner=0;corner<4;++corner) {
            float x=(corner&1?1:-1)*width*.5f,y=(corner&2)?0:-height;
            float *p=&v[q*4+corner].x;
            for(UINT axis=0;axis<3;++axis)p[axis]=s[axis]/4096.0f+x*right[axis]+y*up[axis];
            if(s[3]==0x5753) {
                v[q*4+corner].color=hd_premultiply_color((v[q*4+corner].color&0xffffff)|((DWORD)quads[q*32+15]<<24));
                v[q*4+corner].reserved=SPRITE_VERTEX_TAG;
            }
            if(s[3]==0x5751 || s[3]==0x5752){
                const BYTE *source=quads+q*32;DWORD color=(DWORD)source[15]<<24;
                /* PS1 modulation has neutral128; native PC vertex RGB uses
                 * neutral255. These emitters use channel values <=128. */
                for(UINT c=0;c<3;++c){UINT value=source[12+c]*2;if(value>255)value=255;color|=value<<(16-c*8);}
                v[q*4+corner].color=hd_premultiply_color(color);
                v[q*4+corner].reserved=s[3]==0x5751?PS1_ADD_VERTEX_TAG:PS1_SUB_VERTEX_TAG;
            }
        }
    }
}
static void __cdecl billboards(int count,const BYTE *q,const float *m) {
    UINT before=*(UINT *)(game+0x631d4c);old_billboards(count,q,m);
    if(count>0 && before+(UINT)count*4<=1000)
        correct_billboards((LitVertex *)(game+0x537490)+before,count,q,m);
}
static void __cdecl update_explosions(void) {if(!sprites_active)old_update_explosions();} /* else draw_explosions steps it */
static void __cdecl update_flares(void) {if(!sprites_active)old_update_flares();}
static void call_native_explosions(void){++explosion_submission_depth;old_draw_explosions();--explosion_submission_depth;}
static void __cdecl draw_explosions(void) {
    BYTE *live=game+0x665be0;BYTE saved[32*24];
    if(explosion_history && refresh_frame<explosion_history_frame) {explosion_history=FALSE;flare_history=FALSE;}
    explosion_history_frame=refresh_frame;
    if(*(DWORD *)(game+0x89cb0)) {call_native_explosions();return;}
    const UINT angles[2]={0x899ec,0x899f8};
    for(UINT i=0;i<2;++i) {
        DWORD *angle=(DWORD *)(game+angles[i]);
        if(flare_history && *angle==flare_drawn[i])*angle=flare_raw[i];
    }
    if(sprites_active && !*(DWORD *)(game+0x89cb0))for(UINT n=0;n<ui_steps;++n) {
        copy(explosion_previous,live,sizeof(explosion_previous));explosion_history=TRUE;
        old_update_explosions();old_update_flares();
    }
    for(UINT i=0;i<2;++i) {
        DWORD *angle=(DWORD *)(game+angles[i]);flare_raw[i]=*angle;
        *angle=(DWORD)((LONG)*angle-(LONG)((i?-96:160)*(1-(float)ui_phase)))&4095;
        flare_drawn[i]=*angle;
    }
    flare_history=TRUE;
    if(!explosion_history || *(DWORD *)(game+0x89cb0)) {call_native_explosions();return;}
    copy(saved,live,sizeof(saved));
    interpolate_effect_positions(live,saved);
    /* Both texture samples share the current emitter/camera placement. Only
     * the animation frame and its opacity differ; never blend whole scenes. */
    for(UINT i=0;i<32;++i) {
        BYTE *a=explosion_previous+i*24,*b=live+i*24;
        if(same_explosion(a,b))*(SHORT *)(b+20)=*(SHORT *)(a+20);
        else *(SHORT *)(b+20)=0;
    }
    sprite_opacity=1-(float)ui_phase;call_native_explosions();
    copy(live,saved,sizeof(saved));
    interpolate_effect_positions(live,saved);
    /* New effects have no previous frame, so retain their full opacity. */
    for(UINT i=0;i<32;++i)if(!same_explosion(explosion_previous+i*24,live+i*24))*(SHORT *)(live+i*24+20)=0;
    sprite_opacity=(float)ui_phase;call_native_explosions();copy(live,saved,sizeof(saved));
    for(UINT i=0;i<32;++i)if(same_explosion(explosion_previous+i*24,live+i*24))*(SHORT *)(live+i*24+20)=0;
    sprite_opacity=1;call_native_explosions();copy(live,saved,sizeof(saved));
}

static void initialize_sprites(void) {
    if(!refresh_active)return;
#define SPRITE_VERIFY(name) if(!equal(game+RVA_##name,entry_##name,sizeof(entry_##name)))return
    SPRITE_VERIFY(billboards);SPRITE_VERIFY(target_line);SPRITE_VERIFY(effect_triangle);
    SPRITE_VERIFY(update_flares);SPRITE_VERIFY(update_explosions);SPRITE_VERIFY(draw_explosions);
    old_billboards=(BillboardFn)hook(RVA_billboards,entry_billboards,sizeof(entry_billboards),billboards);
    old_target_line=(QuadFn)hook(RVA_target_line,entry_target_line,sizeof(entry_target_line),target_line);
    old_effect_triangle=(QuadFn)hook(RVA_effect_triangle,entry_effect_triangle,sizeof(entry_effect_triangle),effect_triangle);
    old_update_flares=(RefreshVoidFn)hook(RVA_update_flares,entry_update_flares,sizeof(entry_update_flares),update_flares);
    old_update_explosions=(RefreshVoidFn)hook(RVA_update_explosions,entry_update_explosions,sizeof(entry_update_explosions),update_explosions);
    old_draw_explosions=(RefreshVoidFn)hook(RVA_draw_explosions,entry_draw_explosions,sizeof(entry_draw_explosions),draw_explosions);
    sprites_active=old_billboards && old_target_line && old_effect_triangle && old_update_flares && old_update_explosions && old_draw_explosions;
    wo_render_event("render_sprite_correction",sprites_active);
}
