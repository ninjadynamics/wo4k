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
static BYTE shadow_instructions[16384];
static DWORD shadow_draws,shadow_material_batches;

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
    ++shadow_depth;old_draw_shadow(shapes,ships);--shadow_depth;
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
    if(!screen && wanted!=TRAIL_VERTEX_TAG && wanted!=SKY_VERTEX_TAG && wanted!=SPRITE_VERTEX_TAG)return 0;
    for(UINT corner=0;corner<3;++corner) {
        UINT index=((const WORD *)triangle)[corner];
        if(index>=count || *(const DWORD *)(vertices+index*32+(screen?24:12))!=wanted)return 0;
    }
    return screen?3:(wanted==SKY_VERTEX_TAG?4:(wanted==SPRITE_VERTEX_TAG?5:2));
}
static void clear_effect_tags(BYTE *vertices,UINT count,BOOL screen) {
    for(UINT i=0;i<count;++i) {
        DWORD *tag=(DWORD *)(vertices+i*32+(screen?24:12));
        if(*tag==(screen?FLARE_VERTEX_TAG:TRAIL_VERTEX_TAG) || (!screen && (*tag==SKY_VERTEX_TAG || *tag==SPRITE_VERTEX_TAG)))*tag=0;
    }
}
static const UINT trail_values[6]={1,5,2,4,0,1};
static const UINT flare_values[6]={1,2,2,2,0,1};
static const UINT backdrop_values[6]={0,2,2,2,0,0};
static UINT rewrite_effect_material(const BYTE *input,UINT length,BYTE *output,UINT capacity,DWORD handle15,DWORD handle16,
                                   const BYTE *vertices,UINT vertex_count,BOOL screen) {
    UINT original[6]={0,2,2,2,1,1},effective[6]={0,2,2,2,1,1};
    DWORD texture=0;UINT offset=0,written=0;BOOL touched=FALSE;
    while(offset<length) {
        if(length-offset<4)return 0;
        UINT opcode=input[offset],size=input[offset+1],n=*(const WORD *)(input+offset+2);
        UINT bytes=4+size*n;
        if(bytes>length-offset)return 0;
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
                UINT sprite_values[6];for(UINT s=0;s<6;++s)sprite_values[s]=original[s];
                sprite_values[0]=1;sprite_values[1]=5;sprite_values[3]=4;sprite_values[4]=0;sprite_values[5]=1;
                const UINT *wanted=kind==1?shadow_values:(kind==2?trail_values:(kind==3?flare_values:(kind==4?backdrop_values:(kind==5?sprite_values:original))));
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
    if(!emit_material(output,&written,capacity,effective,original))return 0;
    return touched?written:0;
}
static void __cdecl finish_transform(void) {
    if(shadows_active || exhaust_active) {
        BYTE *start=*(BYTE **)(game+0x53f1a0),*end=*(BYTE **)(game+0x631d54);
        BYTE *buffer=*(BYTE **)(game+0x53f190);
        UINT size=*(UINT *)(game+0xc8610);
        UINT count=*(UINT *)(game+0x631d50);
        if(buffer && start && end>=start && start>=buffer && end<buffer+size && count<=(UINT)(start-buffer)/32) {
            UINT capacity=(UINT)(buffer+size-start);
            if(capacity>sizeof(shadow_instructions))capacity=sizeof(shadow_instructions);
            /* Leave the native epilogue four bytes for D3DOP_EXIT. */
            UINT rewritten=capacity>4?rewrite_effect_material(start,(UINT)(end-start),shadow_instructions,capacity-4,
                shadows_active?((DWORD *)(game+0xc7910))[15]:0,shadows_active?((DWORD *)(game+0xc7910))[16]:0,
                exhaust_active?buffer:0,count,FALSE):0;
            if(rewritten) {
                copy(start,shadow_instructions,rewritten);
                *(BYTE **)(game+0x631d54)=start+rewritten;
                if(!(shadow_material_batches++%300))wo_render_event("render_soft_shadow_batches",shadow_material_batches);
            }
            if(exhaust_active)clear_effect_tags(buffer,count,FALSE);
        }
    }
    old_finish_transform();
}
static void initialize_shadows(const WCHAR *settings) {
    if(!equal(game+RVA_finish_transform,entry_finish_transform,sizeof(entry_finish_transform)))return;
    old_finish_transform=(FinishFn)hook(RVA_finish_transform,entry_finish_transform,sizeof(entry_finish_transform),finish_transform);
    if(!old_finish_transform)return;
    if(!GetPrivateProfileIntW(L"Render",L"SoftShadows",1,settings))return;
    if(!equal(game+RVA_draw_shadow,entry_draw_shadow,sizeof(entry_draw_shadow)))return;
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
    old_draw_shadow=(ShadowFn)hook(RVA_draw_shadow,entry_draw_shadow,sizeof(entry_draw_shadow),draw_shadow);
    shadows_active=old_draw_shadow && old_finish_transform;
    wo_render_event("render_soft_shadows_test",shadows_active);
}
