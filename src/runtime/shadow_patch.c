/* Shadow material correction. Original DrawShadow still projects its quad and
 * runs all original side effects. Only the submitted vertices/material change.
 * No texture asset is modified. Small concentric copies feather its silhouette.
 */
#define SHADOW_LAYERS 12
#define SHADOW_MAX_VERTICES 60
typedef struct LitVertex {float x,y,z;DWORD reserved,color,specular;float u,v;} LitVertex;
typedef struct ShadowTriangle {WORD a,b,c,flags;} ShadowTriangle;
typedef struct ShadowList {UINT count;ShadowTriangle *triangles;} ShadowList;
typedef void (__cdecl *ShadowFn)(void *,void *);
typedef void (__cdecl *FinishFn)(void);
static ShadowFn old_draw_shadow;
static FinishFn old_finish_transform;
static BOOL shadows_active;
static int shadow_depth;
static UINT shadow_alpha=20;
static float shadow_expansion=0.12f;
static LitVertex shadow_vertices[SHADOW_MAX_VERTICES*SHADOW_LAYERS];
static ShadowTriangle shadow_triangles[23][30*SHADOW_LAYERS];
static ShadowList shadow_lists[23];
static BYTE shadow_instructions[131072];
static DWORD shadow_draws,shadow_material_batches;
static BYTE shadow_render_ships[15*240];

static BOOL soften_shadow(void **vertices,int *count,void **triangles) {
    if(!shadows_active || !shadow_depth || *count<=0 || *count>60 || (*count%4))return FALSE;
    LitVertex *input=*vertices;ShadowList *lists=*triangles;
    if(!input || !lists)return FALSE;
    for(UINT material=0;material<23;++material) {
        if(lists[material].count>30)return FALSE;
        shadow_lists[material].count=lists[material].count*SHADOW_LAYERS;
        shadow_lists[material].triangles=shadow_triangles[material];
        for(UINT layer=0;layer<SHADOW_LAYERS;++layer)
            for(UINT n=0;n<lists[material].count;++n) {
                ShadowTriangle triangle=lists[material].triangles[n];
                if(triangle.a>=*count || triangle.b>=*count || triangle.c>=*count)return FALSE;
                triangle.a+=(WORD)(layer*(*count));triangle.b+=(WORD)(layer*(*count));triangle.c+=(WORD)(layer*(*count));
                shadow_triangles[material][layer*lists[material].count+n]=triangle;
            }
    }
    for(UINT layer=0;layer<SHADOW_LAYERS;++layer) {
        float scale=1+shadow_expansion*layer/(SHADOW_LAYERS-1);
        for(int quad=0;quad<*count;quad+=4) {
            float x=0,y=0,z=0;
            for(UINT c=0;c<4;++c) {x+=input[quad+c].x;y+=input[quad+c].y;z+=input[quad+c].z;}
            x*=0.25f;y*=0.25f;z*=0.25f;
            for(UINT c=0;c<4;++c) {
                LitVertex v=input[quad+c];
                v.x=x+(v.x-x)*scale;v.y=y+(v.y-y)*scale;v.z=z+(v.z-z)*scale;
                v.color=(v.color&0xffffff)|(shadow_alpha<<24);
                shadow_vertices[layer*(*count)+quad+c]=v;
            }
        }
    }
    *vertices=shadow_vertices;*triangles=shadow_lists;*count*=SHADOW_LAYERS;
    ++shadow_draws;return TRUE;
}
static void __cdecl draw_shadow(void *shapes,void *ships) {
    void *render_ships=ships;
    if(refresh_active && refresh_race && ships) {
        /* DrawShadow projects the current physics pose onto the track. The
         * mesh uses the previous/current render phase instead; using different
         * poses makes its shadow jump forward at each simulation tick. Project
         * a private render copy so the original track-plane math still runs,
         * with no mutation of ship physics, collision pointers or flags. */
        copy(shadow_render_ships,ships,sizeof(shadow_render_ships));
        for(UINT ship=0;ship<15;++ship) {
            float delta[3];refresh_ship_delta(ship,delta);
            LONG *position=(LONG *)(shadow_render_ships+ship*240+0x40);
            for(UINT axis=0;axis<3;++axis)position[axis]+=(LONG)delta[axis];
        }
        render_ships=shadow_render_ships;
    }
    ++shadow_depth;old_draw_shadow(shapes,render_ships);--shadow_depth;
}

/* Rewrite only STATERENDER commands around shadow triangles. Keep depth tests,
 * disable depth writes, combine texture alpha with per-layer vertex alpha and
 * use ordinary source-alpha blending. Restore original material state outside
 * these triangles and at buffer exit, including before the exhaust/HUD pass.
 */
static const UINT shadow_states[6]={27,19,20,21,14,7};
static const UINT shadow_values[6]={1,5,6,4,0,1};
static BOOL emit_material(BYTE *output,UINT *length,UINT capacity,UINT *effective,const UINT *wanted) {
    UINT changed=0;
    for(UINT i=0;i<6;++i)if(effective[i]!=wanted[i])++changed;
    if(!changed)return TRUE;
    if(*length+4+changed*8>capacity)return FALSE;
    BYTE *p=output+*length;p[0]=8;p[1]=8;*(WORD *)(p+2)=(WORD)changed;p+=4;
    for(UINT i=0;i<6;++i)if(effective[i]!=wanted[i]) {
        *(UINT *)p=shadow_states[i];*(UINT *)(p+4)=wanted[i];p+=8;effective[i]=wanted[i];
    }
    *length+=4+changed*8;return TRUE;
}
static UINT effect_triangle_kind(const BYTE *triangle,const BYTE *vertices,UINT count,BOOL screen) {
    if(!vertices)return 0;
    UINT first=((const WORD *)triangle)[0];
    if(first>=count)return 0;
    DWORD wanted=screen?FLARE_VERTEX_TAG:*(const DWORD *)(vertices+first*32+12);
    if(!screen && wanted!=TRAIL_VERTEX_TAG && wanted!=SKY_VERTEX_TAG && wanted!=SPRITE_VERTEX_TAG && wanted!=PS1_ADD_VERTEX_TAG && wanted!=PS1_SUB_VERTEX_TAG)return 0;
    for(UINT corner=0;corner<3;++corner) {
        UINT index=((const WORD *)triangle)[corner];
        if(index>=count || *(const DWORD *)(vertices+index*32+(screen?24:12))!=wanted)return 0;
    }
    return screen?3:(wanted==SKY_VERTEX_TAG?4:(wanted==SPRITE_VERTEX_TAG?5:(wanted==PS1_ADD_VERTEX_TAG?7:(wanted==PS1_SUB_VERTEX_TAG?8:2))));
}
static void clear_effect_tags(BYTE *vertices,UINT count,BOOL screen) {
    for(UINT i=0;i<count;++i) {
        DWORD *tag=(DWORD *)(vertices+i*32+(screen?24:12));
        if(*tag==(screen?FLARE_VERTEX_TAG:TRAIL_VERTEX_TAG) || (!screen && (*tag==SKY_VERTEX_TAG || *tag==SPRITE_VERTEX_TAG || *tag==PS1_ADD_VERTEX_TAG || *tag==PS1_SUB_VERTEX_TAG)))*tag=0;
    }
}
static const UINT trail_values[6]={1,5,2,4,0,1};
static const UINT flare_values[6]={1,2,2,2,0,1};
static const UINT backdrop_values[6]={0,2,2,2,0,0};
static const UINT ps1_add_values[6]={1,2,2,2,0,1};
static const UINT ps1_invert_values[6]={1,10,1,2,0,1};
static WORD ps1_white_indices[16384];
static UINT ps1_white_vertices(BYTE *buffer,UINT count,UINT capacity,WORD *indices){
    if(count>16384 || capacity>65535 || count>capacity)return 0;
    UINT needed=0;
    for(UINT i=0;i<count;++i)if(((LitVertex *)buffer)[i].reserved==PS1_SUB_VERTEX_TAG)++needed;
    if(needed>capacity-count)return 0;
    UINT next=count;
    for(UINT i=0;i<count;++i){
        indices[i]=0xffff;
        if(((LitVertex *)buffer)[i].reserved==PS1_SUB_VERTEX_TAG){
            LitVertex white=((LitVertex *)buffer)[i];white.reserved=0;white.color=0xffffffff;white.specular=0;
            ((LitVertex *)buffer)[next]=white;indices[i]=(WORD)next++;
        }
    }
    return next;
}
static BOOL ps1_texture_state(BYTE *out,UINT *written,UINT capacity,DWORD handle){
    if(*written>capacity || capacity-*written<12)return FALSE;
    BYTE *p=out+*written;p[0]=8;p[1]=8;*(WORD *)(p+2)=1;*(DWORD *)(p+4)=1;*(DWORD *)(p+8)=handle;*written+=12;return TRUE;
}
static BOOL ps1_triangle(BYTE *out,UINT *written,UINT capacity,const BYTE *triangle,const WORD *white,UINT count){
    if(*written>capacity || capacity-*written<12)return FALSE;
    BYTE *p=out+*written;p[0]=3;p[1]=8;*(WORD *)(p+2)=1;copy(p+4,triangle,8);
    if(white)for(UINT c=0;c<3;++c){UINT index=((const WORD *)triangle)[c];if(index>=count || white[index]==0xffff)return FALSE;((WORD *)(p+4))[c]=white[index];}
    *written+=12;return TRUE;
}
static BOOL emit_world_reset(BYTE *output,UINT *written,UINT capacity) {
    if(capacity-*written<12)return FALSE;
    BYTE *p=output+*written;p[0]=8;p[1]=8;*(WORD *)(p+2)=1;
    *(UINT *)(p+4)=15;*(UINT *)(p+8)=0;
    *written+=12;return TRUE;
}
static UINT rewrite_effect_material_ps1(const BYTE *input,UINT length,BYTE *output,UINT capacity,DWORD handle15,DWORD handle16,
                                   const BYTE *vertices,UINT vertex_count,BOOL screen,const WORD *white_indices) {
    UINT original[6]={0,2,2,2,1,1},effective[6]={0,2,2,2,1,1};
    DWORD texture=0;UINT offset=0,written=0,reset_at=0;BOOL touched=FALSE,reset_done=screen;
    if(!screen) {
        /* Reset the HD UI's alpha-test rejection, independently of lighting.
         * Preserve native COLORKEYENABLE: world foliage/billboards use keyed
         * black pixels even when alpha blending is disabled. Color keying and
         * explicit alpha testing are separate states in the legacy renderer. */
        /* FinishTransformBuffer still holds a pointer to its PROCESSVERTICES
         * placeholder and fills it AFTER this rewrite. Keep that packet at
         * its native address; insert states immediately AFTER it. */
        for(UINT at=0;at<length;) {
            if(length-at<4)return 0;
            UINT size=input[at+1],n=*(const WORD *)(input+at+2),bytes=4+size*n;
            if(bytes>length-at)return 0;
            if(input[at]==9){reset_at=at+bytes;break;}
            at+=bytes;
        }
    }
    while(offset<length) {
        if(length-offset<4)return 0;
        UINT opcode=input[offset],size=input[offset+1],n=*(const WORD *)(input+offset+2);
        UINT bytes=4+size*n;
        if(bytes>length-offset)return 0;
        if(!reset_done && offset==reset_at) {
            if(!emit_world_reset(output,&written,capacity))return 0;
            reset_done=TRUE;touched=TRUE;
        }
        if(opcode==8) {
            if(size!=8)return 0;
            for(UINT j=0;j<n;++j) {
                UINT state=*(const UINT *)(input+offset+4+j*8),value=*(const UINT *)(input+offset+8+j*8);
                if(state==1)texture=value;
                for(UINT i=0;i<6;++i)if(state==shadow_states[i])original[i]=effective[i]=value;
            }
        }
        if(opcode==3) {
            if(size!=8)return 0;
            UINT first=0;
            while(first<n) {
                const BYTE *triangle=input+offset+4+first*8;
                UINT kind=effect_triangle_kind(triangle,vertices,vertex_count,screen);
                if(!kind && texture && (texture==handle15 || texture==handle16))kind=1;
                UINT last=first+1;
                while(last<n) {
                    UINT next=effect_triangle_kind(input+offset+4+last*8,vertices,vertex_count,screen);
                    if(!next && texture && (texture==handle15 || texture==handle16))next=1;
                    if(next!=kind)break;
                    ++last;
                }
                if(kind==8){
                    /* D3D1 has no subtract blend equation. Invert B with a
                     * white, untextured triangle; add F; invert once more:
                     * 1 - clamp(1-B+F) = clamp(B-F). Run per triangle so
                     * intersecting smoke quads cannot cancel the inversions.
                     * Depth writes stay off, including the two white passes. */
                    if(!white_indices || !texture)return 0;
                    for(UINT tri=first;tri<last;++tri){
                        const BYTE *source=input+offset+4+tri*8;
                        if(!ps1_texture_state(output,&written,capacity,0) ||
                           !emit_material(output,&written,capacity,effective,ps1_invert_values) ||
                           !ps1_triangle(output,&written,capacity,source,white_indices,vertex_count) ||
                           !ps1_texture_state(output,&written,capacity,texture) ||
                           !emit_material(output,&written,capacity,effective,ps1_add_values) ||
                           !ps1_triangle(output,&written,capacity,source,0,vertex_count) ||
                           !ps1_texture_state(output,&written,capacity,0) ||
                           !emit_material(output,&written,capacity,effective,ps1_invert_values) ||
                           !ps1_triangle(output,&written,capacity,source,white_indices,vertex_count) ||
                           !ps1_texture_state(output,&written,capacity,texture))return 0;
                    }
                    touched=TRUE;first=last;continue;
                }
                UINT sprite_values[6];for(UINT s=0;s<6;++s)sprite_values[s]=original[s];
                /* Effect RGB already includes black-matte texture coverage.
                 * Fade RGB with vertex alpha once, then use ONE here instead
                 * of multiplying filtered coverage a second time. */
                sprite_values[0]=1;sprite_values[1]=2;sprite_values[3]=4;sprite_values[4]=0;sprite_values[5]=1;
                const UINT *wanted=kind==7?ps1_add_values:(kind==1?shadow_values:(kind==2?trail_values:(kind==3?flare_values:(kind==4?backdrop_values:(kind==5?sprite_values:original)))));
                if(!emit_material(output,&written,capacity,effective,wanted))return 0;
                UINT run_bytes=(last-first)*8;
                if(written+4+run_bytes>capacity)return 0;
                output[written]=3;output[written+1]=8;*(WORD *)(output+written+2)=(WORD)(last-first);
                copy(output+written+4,triangle,run_bytes);written+=4+run_bytes;
                if(kind)touched=TRUE;
                first=last;
            }
            offset+=bytes;continue;
        }
        if(written+bytes>capacity)return 0;
        copy(output+written,input+offset,bytes);written+=bytes;offset+=bytes;
    }
    if(!reset_done){if(!emit_world_reset(output,&written,capacity))return 0;touched=TRUE;}
    if(!emit_material(output,&written,capacity,effective,original))return 0;
    return touched?written:0;
}
static UINT rewrite_effect_material(const BYTE *input,UINT length,BYTE *output,UINT capacity,DWORD handle15,DWORD handle16,
                                   const BYTE *vertices,UINT vertex_count,BOOL screen){
    return rewrite_effect_material_ps1(input,length,output,capacity,handle15,handle16,vertices,vertex_count,screen,0);
}
static void __cdecl finish_transform(void) {
    {
        BYTE *start=*(BYTE **)(game+0x53f1a0),*end=*(BYTE **)(game+0x631d54);
        BYTE *buffer=*(BYTE **)(game+0x53f190);
        UINT size=*(UINT *)(game+0xc8610);
        UINT count=*(UINT *)(game+0x631d50);
        if(buffer && start && end>=start && start>=buffer && end<buffer+size && count<=(UINT)(start-buffer)/32) {
            UINT capacity=(UINT)(buffer+size-start);
            if(capacity>sizeof(shadow_instructions))capacity=sizeof(shadow_instructions);
            /* Leave the native epilogue four bytes for D3DOP_EXIT. */
            UINT augmented=ps1_white_vertices(buffer,count,(UINT)(start-buffer)/32,ps1_white_indices);
            UINT rewritten=capacity>4?rewrite_effect_material_ps1(start,(UINT)(end-start),shadow_instructions,capacity-4,
                shadows_active?((DWORD *)(game+0xc7910))[15]:0,shadows_active?((DWORD *)(game+0xc7910))[16]:0,
                buffer,count,FALSE,augmented>count?ps1_white_indices:0):0;
            if(rewritten) {
                copy(start,shadow_instructions,rewritten);
                *(BYTE **)(game+0x631d54)=start+rewritten;
                if(augmented>count){count=augmented;*(UINT *)(game+0x631d50)=count;}
                if(!(shadow_material_batches++%300))wo_render_event("render_soft_shadow_batches",shadow_material_batches);
            }
            /* Classify the final material states, before effect tags are cleared. */
            scene_lighting_finish();
            clear_effect_tags(buffer,count,FALSE);
            hd_world_finish();
        }
    }
    old_finish_transform();
}
static void initialize_shadows(const WCHAR *settings) {
    if(!equal(game+RVA_finish_transform,entry_finish_transform,sizeof(entry_finish_transform)))return;
    old_finish_transform=(FinishFn)hook(RVA_finish_transform,entry_finish_transform,sizeof(entry_finish_transform),finish_transform);
    if(!old_finish_transform)return;
    if(!equal(game+RVA_draw_shadow,entry_draw_shadow,sizeof(entry_draw_shadow)))return;
    old_draw_shadow=(ShadowFn)hook(RVA_draw_shadow,entry_draw_shadow,sizeof(entry_draw_shadow),draw_shadow);
    wo_render_event("render_shadow_motion_test",old_draw_shadow!=0);
    if(!GetPrivateProfileIntW(L"Render",L"SoftShadows",1,settings))return;
    UINT opacity=GetPrivateProfileIntW(L"Render",L"ShadowOpacityPercent",60,settings);
    UINT softness=GetPrivateProfileIntW(L"Render",L"ShadowSoftnessPercent",12,settings);
    if(opacity>95)opacity=95;if(softness>40)softness=40;
    float low=0,high=1,target=1-opacity/100.0f;
    for(UINT step=0;step<24;++step) {
        float middle=(low+high)*0.5f,power=1;
        for(UINT layer=0;layer<SHADOW_LAYERS;++layer)power*=middle;
        if(power>target)high=middle;else low=middle;
    }
    shadow_alpha=(UINT)((1-(low+high)*0.5f)*255+0.5f);
    shadow_expansion=softness/100.0f;
    shadows_active=old_draw_shadow && old_finish_transform;
    wo_render_event("render_soft_shadows_test",shadows_active);
}
