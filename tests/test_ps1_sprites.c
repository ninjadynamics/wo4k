static BYTE ps1_captured_quads[32*32];
static UINT ps1_capture_calls,ps1_capture_count;
static int ps1_capture_mode;
static void __cdecl ps1_capture_submission(BYTE *s,void *v,int n,void *t,void *quads,int count,int mode){
    (void)s;(void)v;(void)n;(void)t;
    ++ps1_capture_calls;ps1_capture_count=(UINT)count;ps1_capture_mode=mode;
    if(count<1 || count>32)ExitProcess(220);copy(ps1_captured_quads,quads,count*32);
}
static void __cdecl ps1_fixture_transform(void *s){(void)s;}
static void test_ps1_native_emitters(BYTE *image){
    BYTE *saved_camera=*(BYTE **)(image+0x665110),*saved_ships=*(BYTE **)(image+0x518030);
    static BYTE saved_live[32*24],saved_transform[5];copy(saved_live,image+0x665be0,sizeof(saved_live));copy(saved_transform,image+0x4d7f0,5);
    static BYTE *saved_textures[32];for(UINT i=0;i<32;++i)saved_textures[i]=((BYTE **)(image+0x66ad40))[i];
    SkeletonFn saved_submit=old_render_skeleton;float saved_opacity=sprite_opacity;
    UINT saved_queue=*(UINT *)(image+0x53f19c);WORD saved_sets=*(WORD *)(image+0xc8620);
    BYTE *saved_cursor=*(BYTE **)(image+0x65d020);
    static BYTE native_quads[32*32];*(BYTE **)(image+0x65d020)=native_quads;
    /* The test image is deliberately relocated. Rebase the exact original
     * DrawExplode absolute operands; leave its x86 instructions/culling intact. */
    const DWORD addresses[]={0xa65110,0xa66338,0xa65bf4,0xa65ef4,0x918030,0xa6ad40,0x4c8620,0xa5d020,0xa66330};
    UINT refs[32],ref_count=0;DWORD original_refs[32],delta=(DWORD)(UINT_PTR)image-0x400000;
    for(UINT at=0xe3b8;at+4<=0xe630;++at)for(UINT j=0;j<sizeof(addresses)/sizeof(addresses[0]);++j){
        if(*(DWORD *)(image+at)==addresses[j]){if(ref_count==32)ExitProcess(238);refs[ref_count]=at;original_refs[ref_count++]=addresses[j];*(DWORD *)(image+at)+=delta;at+=3;break;}
    }
    DWORD *trampoline_operand=(DWORD *)((BYTE *)old_draw_explosions+4),saved_operand=*trampoline_operand;
    if(saved_operand!=0xa65110)ExitProcess(239);*trampoline_operand+=delta;
    static BYTE position[96],skeleton[96],ships[12*240],texture[40];BYTE *camera=position;
    *(LONG *)(position+0x14)=100;*(LONG *)(position+0x18)=-200;*(LONG *)(position+0x1c)=400;*(BYTE **)(position+0x44)=skeleton;
    *(BYTE ***)(image+0x665110)=&camera;*(BYTE **)(image+0x518030)=ships;
    *(LONG *)(ships+240+0x40)=30;*(LONG *)(ships+240+0x44)=40;*(LONG *)(ships+240+0x48)=50;
    *(WORD *)(texture+0x0e)=768;*(WORD *)(texture+0x10)=453;
    texture[0x18]=texture[0x1c]=63;texture[0x1b]=texture[0x1d]=57;
    for(UINT i=0;i<32;++i)((BYTE **)(image+0x66ad40))[i]=texture;
    for(UINT i=0;i<32*24;++i)image[0x665be0+i]=0;
    const UINT ids[]={2,9,15,28};
    for(UINT j=0;j<4;++j){BYTE *e=image+0x665be0+ids[j]*24;
        *(LONG *)e=j==3?40000:500;*(LONG *)(e+4)=600;*(LONG *)(e+8)=j==1?707:700;
        /* First two native billboards have identical coordinates, colour and
         * UVs but different emitter modes. Original emission order resolves
         * this otherwise ambiguous match without changing their data. */
        e[13]=j<2?128:16;e[14]=j<2?64:16;e[15]=32;
        *(SHORT *)(e+16)=0;*(SHORT *)(e+18)=j==0?1:2;*(SHORT *)(e+20)=7;*(SHORT *)(e+22)=j==2?1:-1;
    }
    jump(image+0x4d7f0,ps1_fixture_transform);old_render_skeleton=ps1_capture_submission;
    sprite_opacity=.5f;*(UINT *)(image+0x53f19c)=0;*(WORD *)(image+0xc8620)=22;
    ps1_capture_calls=0;call_native_explosions();
    if(ps1_capture_calls!=1 || ps1_capture_count!=3 || ps1_capture_mode!=0)ExitProcess(221);
    for(UINT j=0;j<3;++j){BYTE *q=ps1_captured_quads+j*32;
        if(*(WORD *)(q+6)!=(j==0?0x5751:0x5752) || q[15]!=128 || *(SHORT *)(q+16)!=512 || *(SHORT *)(q+18)!=2048)ExitProcess(222);
    }
    static BYTE entry[96];static UINT triangles[32];UINT budget=0;
    *(UINT *)(entry+0x40)=3;*(BYTE **)(entry+0x44)=ps1_captured_quads;
    if(render_stage(entry)!=3 || !entry_budget(entry,&budget,triangles) || budget!=20)ExitProcess(242);
    static float identity[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};static LitVertex vertices[12];
    correct_billboards(vertices,3,ps1_captured_quads,identity);
    for(UINT j=0;j<12;++j)if(vertices[j].reserved!=(j<4?PS1_ADD_VERTEX_TAG:PS1_SUB_VERTEX_TAG) || vertices[j].color!=(j<8?0x80804020u:0x80101020u))ExitProcess(223);
    if(soft_sprite_submission(skeleton,0,0,0,ps1_captured_quads,3,1))ExitProcess(224);
    old_render_skeleton=saved_submit;sprite_opacity=saved_opacity;
    copy(image+0x665be0,saved_live,sizeof(saved_live));copy(image+0x4d7f0,saved_transform,5);
    *(BYTE **)(image+0x665110)=saved_camera;*(BYTE **)(image+0x518030)=saved_ships;
    *(UINT *)(image+0x53f19c)=saved_queue;*(WORD *)(image+0xc8620)=saved_sets;
    for(UINT i=0;i<32;++i)((BYTE **)(image+0x66ad40))[i]=saved_textures[i];
    *(BYTE **)(image+0x65d020)=saved_cursor;*trampoline_operand=saved_operand;
    for(UINT i=0;i<ref_count;++i)*(DWORD *)(image+refs[i])=original_refs[i];
}

static void ps1_gpu_execute(IDirect3DDevice7 *device,IDirectDrawSurface7 *texture,const BYTE *packet,UINT bytes,TLVertex *vertices,UINT count){
    material_check(device->lpVtbl->BeginScene(device),225);
    for(UINT at=0;at<bytes;){
        UINT op=packet[at],size=packet[at+1],n=*(const WORD *)(packet+at+2);
        if(op==8)for(UINT i=0;i<n;++i){
            UINT state=*(const DWORD *)(packet+at+4+i*8),value=*(const DWORD *)(packet+at+8+i*8);
            if(state==1)material_check(device->lpVtbl->SetTexture(device,0,value?texture:0),226);
            else if(state==21){
                material_check(device->lpVtbl->SetTextureStageState(device,0,D3DTSS_COLOROP,D3DTOP_MODULATE),226);
                device->lpVtbl->SetTextureStageState(device,0,D3DTSS_COLORARG1,D3DTA_TEXTURE);
                device->lpVtbl->SetTextureStageState(device,0,D3DTSS_COLORARG2,D3DTA_DIFFUSE);
                device->lpVtbl->SetTextureStageState(device,0,D3DTSS_ALPHAOP,D3DTOP_SELECTARG1);
                device->lpVtbl->SetTextureStageState(device,0,D3DTSS_ALPHAARG1,D3DTA_TEXTURE);
            }else if(state!=7 && state!=14)material_check(device->lpVtbl->SetRenderState(device,(D3DRENDERSTATETYPE)state,value),226);
        }
        if(op==3)for(UINT i=0;i<n;++i){
            const WORD *tri=(const WORD *)(packet+at+4+i*8);
            if(tri[0]>=count || tri[1]>=count || tri[2]>=count)ExitProcess(227);
            material_check(device->lpVtbl->DrawIndexedPrimitive(device,D3DPT_TRIANGLELIST,D3DFVF_TLVERTEX,vertices,count,(WORD *)tri,3,0),228);
        }
        at+=4+size*n;
    }
    material_check(device->lpVtbl->EndScene(device),229);
}
static void test_ps1_blending_gpu(IDirectDraw7 *dd,IDirect3DDevice7 *device,IDirectDrawSurface7 *target,const DDSURFACEDESC2 *format){
    DDSURFACEDESC2 desc=*format;IDirectDrawSurface7 *texture=0;
    desc.ddpfPixelFormat.dwFlags=DDPF_RGB|DDPF_ALPHAPIXELS;desc.ddpfPixelFormat.dwRGBAlphaBitMask=0xff000000;
    material_check(dd->lpVtbl->CreateSurface(dd,&desc,&texture,0),230);
    DDSURFACEDESC2 lock={0};lock.dwSize=sizeof(lock);material_check(texture->lpVtbl->Lock(texture,0,&lock,DDLOCK_WAIT,0),231);
    for(UINT y=0;y<2;++y){*(DWORD *)((BYTE *)lock.lpSurface+y*lock.lPitch)=0xff204020;*(DWORD *)((BYTE *)lock.lpSurface+y*lock.lPitch+4)=0;}
    texture->lpVtbl->Unlock(texture,0);
    device->lpVtbl->SetRenderState(device,D3DRENDERSTATE_COLORKEYENABLE,0);device->lpVtbl->SetRenderState(device,D3DRENDERSTATE_ALPHATESTENABLE,0);
    static BYTE input[128],output[2048];UINT length=0;
    length+=scene_test_state(input+length,1,42);length+=scene_test_state(input+length,27,1);
    length+=scene_test_state(input+length,19,5);length+=scene_test_state(input+length,20,6);length+=scene_test_state(input+length,21,2);
    length+=scene_test_triangle(input+length,0,1,2);length+=scene_test_triangle(input+length,1,3,2);
    static TLVertex vertices[8]={{0,0,.5f,1,0xffffffff,0,0,0},{64,0,.5f,1,0xffffffff,0,1,0},
        {0,64,.5f,1,0xffffffff,0,0,1},{64,64,.5f,1,0xffffffff,0,1,1}};
    const DWORD backgrounds[]={0x809060,0x102010};const UINT fades[]={0,128,255};
    for(UINT subtract=0;subtract<2;++subtract)for(UINT b=0;b<2;++b)for(UINT f=0;f<3;++f)for(UINT overlaps=1;overlaps<=2;++overlaps){
        static LitVertex tags[8];WORD whites[4];for(UINT i=0;i<4;++i)tags[i].reserved=subtract?PS1_SUB_VERTEX_TAG:PS1_ADD_VERTEX_TAG;
        UINT augmented=ps1_white_vertices((BYTE *)tags,4,8,whites);if(augmented!=(subtract?8u:4u))ExitProcess(232);
        UINT written=rewrite_effect_material_ps1(input,length,output,sizeof(output),0,0,(const BYTE *)tags,4,FALSE,subtract?whites:0);if(!written)ExitProcess(233);
        if(rewrite_effect_material_ps1(input,length,output,8,0,0,(const BYTE *)tags,4,FALSE,subtract?whites:0))ExitProcess(234);
        /* The failed small-buffer probe is transactional with respect to input;
         * rewrite the complete packet before rendering. */
        written=rewrite_effect_material_ps1(input,length,output,sizeof(output),0,0,(const BYTE *)tags,4,FALSE,subtract?whites:0);
        for(UINT i=0;i<4;++i){vertices[i].color=hd_premultiply_color((fades[f]<<24)|0xffffff);vertices[i+4]=vertices[i];vertices[i+4].color=0xffffffff;}
        device->lpVtbl->Clear(device,0,0,D3DCLEAR_TARGET,backgrounds[b],1,0);
        for(UINT n=0;n<overlaps;++n)ps1_gpu_execute(device,texture,output,written,vertices,augmented);
        DWORD core=material_pixel_at(target,8),outside=material_pixel_at(target,56);
        if(outside!=backgrounds[b])ExitProcess(235);
        for(UINT shift=0;shift<24;shift+=8){
            int back=(backgrounds[b]>>shift)&255,front=((0x204020>>shift)&255)*fades[f]/255;
            int expected=subtract?back-(int)overlaps*front:back+(int)overlaps*front;
            if(expected<0)expected=0;if(expected>255)expected=255;
            int actual=(core>>shift)&255;if(actual<expected-2 || actual>expected+2)ExitProcess(236);
        }
        DWORD src,dst;device->lpVtbl->GetRenderState(device,D3DRENDERSTATE_SRCBLEND,&src);device->lpVtbl->GetRenderState(device,D3DRENDERSTATE_DESTBLEND,&dst);
        if(src!=5 || dst!=6)ExitProcess(237);
    }
    device->lpVtbl->SetTexture(device,0,0);texture->lpVtbl->Release(texture);
}
