static void test_sprite_fades(void) {
    float identity[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    SHORT quad[16]={0};LitVertex vertices[4];
    quad[8]=quad[9]=quad[10]=quad[11]=64;
    for(UINT i=0;i<4;++i)vertices[i]=(LitVertex){.color=0xff80c040};
    /* Regular foliage keeps its own native color and mask. */
    correct_billboards(vertices,1,(const BYTE *)quad,identity);
    for(UINT i=0;i<4;++i)if(vertices[i].color!=0xff80c040 || vertices[i].reserved)ExitProcess(205);
    quad[3]=0x5753;((BYTE *)quad)[15]=128;
    correct_billboards(vertices,1,(const BYTE *)quad,identity);
    for(UINT i=0;i<4;++i)if(vertices[i].color!=0x80406020 || vertices[i].reserved!=SPRITE_VERTEX_TAG)ExitProcess(206);
    for(UINT i=0;i<4;++i)vertices[i].color=0xff80c040;((BYTE *)quad)[15]=0;
    correct_billboards(vertices,1,(const BYTE *)quad,identity);
    for(UINT i=0;i<4;++i)if(vertices[i].color)ExitProcess(207);
    /* The game reuses the slot for a plain billboard and does not write the
     * marker field: the sprite's marker must not stay behind. */
    quad[3]=0;for(UINT i=0;i<4;++i)vertices[i].color=0xff80c040;
    correct_billboards(vertices,1,(const BYTE *)quad,identity);
    for(UINT i=0;i<4;++i)if(vertices[i].color!=0xff80c040 || vertices[i].reserved)ExitProcess(60);
}
static void sprite_gpu_packet(IDirect3DDevice7 *device,const BYTE *input,UINT length,TLVertex *vertices,BOOL corrected) {
    BYTE rewritten[1024];LitVertex tags[3]={{.reserved=SPRITE_VERTEX_TAG},{.reserved=SPRITE_VERTEX_TAG},{.reserved=SPRITE_VERTEX_TAG}};
    const BYTE *packet=input;UINT bytes=length;
    if(corrected){bytes=rewrite_effect_material(input,length,rewritten,sizeof(rewritten),0,0,(const BYTE *)tags,3,FALSE);packet=rewritten;if(!bytes)ExitProcess(208);}
    material_check(device->lpVtbl->BeginScene(device),209);
    for(UINT at=0;at<bytes;){
        UINT opcode=packet[at],size=packet[at+1],count=*(WORD *)(packet+at+2);
        if(opcode==8)for(UINT i=0;i<count;++i){
            UINT type=*(DWORD *)(packet+at+4+i*8),value=*(DWORD *)(packet+at+8+i*8);
            /* This color-compositing probe has no depth surface. */
            if(type==7 || type==14)continue;
            if(type==21){
                /* Device7 expresses legacy MODULATEALPHA with texture stages. */
                material_check(device->lpVtbl->SetTextureStageState(device,0,D3DTSS_ALPHAOP,value==4?D3DTOP_MODULATE:D3DTOP_SELECTARG1),210);
                material_check(device->lpVtbl->SetTextureStageState(device,0,D3DTSS_ALPHAARG1,D3DTA_TEXTURE),210);
                material_check(device->lpVtbl->SetTextureStageState(device,0,D3DTSS_ALPHAARG2,D3DTA_DIFFUSE),210);
            }else material_check(device->lpVtbl->SetRenderState(device,(D3DRENDERSTATETYPE)type,value),211);
        }
        if(opcode==3)material_check(device->lpVtbl->DrawPrimitive(device,D3DPT_TRIANGLESTRIP,D3DFVF_TLVERTEX,vertices,4,0),212);
        at+=4+size*count;
    }
    material_check(device->lpVtbl->EndScene(device),213);
}
static void test_sprite_blend_gpu(IDirectDraw7 *dd,IDirect3DDevice7 *device,IDirectDrawSurface7 *target,const DDSURFACEDESC2 *format) {
    DDSURFACEDESC2 desc=*format;IDirectDrawSurface7 *texture=0;
    desc.ddpfPixelFormat.dwFlags=DDPF_RGB|DDPF_ALPHAPIXELS;desc.ddpfPixelFormat.dwRGBAlphaBitMask=0xff000000;
    material_check(dd->lpVtbl->CreateSurface(dd,&desc,&texture,0),214);
    DDSURFACEDESC2 lock={0};lock.dwSize=sizeof(lock);material_check(texture->lpVtbl->Lock(texture,0,&lock,DDLOCK_WAIT,0),215);
    for(UINT y=0;y<2;++y){*(DWORD *)((BYTE *)lock.lpSurface+y*lock.lPitch)=0xff808080;*(DWORD *)((BYTE *)lock.lpSurface+y*lock.lPitch+4)=0;}
    texture->lpVtbl->Unlock(texture,0);device->lpVtbl->SetTexture(device,0,texture);
    device->lpVtbl->SetTextureStageState(device,0,D3DTSS_ADDRESSU,D3DTADDRESS_CLAMP);
    device->lpVtbl->SetTextureStageState(device,0,D3DTSS_ADDRESSV,D3DTADDRESS_CLAMP);
    device->lpVtbl->SetTextureStageState(device,0,D3DTSS_MINFILTER,D3DTFN_LINEAR);
    device->lpVtbl->SetTextureStageState(device,0,D3DTSS_MAGFILTER,D3DTFG_LINEAR);
    device->lpVtbl->SetRenderState(device,D3DRENDERSTATE_COLORKEYENABLE,0);
    device->lpVtbl->SetRenderState(device,D3DRENDERSTATE_ALPHATESTENABLE,0);
    BYTE packet[96];UINT length=0;
    length+=scene_test_state(packet+length,27,1);length+=scene_test_state(packet+length,19,5);
    length+=scene_test_state(packet+length,20,6);length+=scene_test_state(packet+length,21,4);
    length+=scene_test_triangle(packet+length,0,1,2);
    TLVertex vertices[4]={{0,0,.5f,1,0xffffffff,0,0,0},{64,0,.5f,1,0xffffffff,0,1,0},
        {0,64,.5f,1,0xffffffff,0,0,1},{64,64,.5f,1,0xffffffff,0,1,1}};
    const UINT fades[]={0,21,128,255};DWORD original_edge=0,corrected_edge=0;
    for(UINT i=0;i<4;++i){
        UINT alpha=fades[i];
        for(UINT v=0;v<4;++v)vertices[v].color=(alpha<<24)|0xffffff;
        device->lpVtbl->Clear(device,0,0,D3DCLEAR_TARGET,0x808080,1,0);sprite_gpu_packet(device,packet,length,vertices,FALSE);
        DWORD before=material_pixel(target);if(alpha==255){original_edge=before;if((before&255)>=120)ExitProcess(216);}
        for(UINT v=0;v<4;++v)vertices[v].color=hd_premultiply_color((alpha<<24)|0xffffff);
        device->lpVtbl->Clear(device,0,0,D3DCLEAR_TARGET,0x808080,1,0);sprite_gpu_packet(device,packet,length,vertices,TRUE);
        DWORD after=material_pixel(target);if(alpha==255)corrected_edge=after;
        for(UINT shift=0;shift<24;shift+=8)if(((after>>shift)&255)<126 || ((after>>shift)&255)>130)ExitProcess(217);
        /* The opaque interior keeps the same brightness across every fade. */
        DWORD core=material_pixel_at(target,8);
        for(UINT shift=0;shift<24;shift+=8)if(((core>>shift)&255)<126 || ((core>>shift)&255)>130)ExitProcess(218);
    }
    /* Additive explosions retain their original destination factor (ONE). */
    *(DWORD *)(packet+32)=2;
    device->lpVtbl->Clear(device,0,0,D3DCLEAR_TARGET,0x204020,1,0);sprite_gpu_packet(device,packet,length,vertices,TRUE);
    DWORD additive=material_pixel(target);if((additive&255)<=32 || ((additive>>8)&255)<=64)ExitProcess(219);
    DWORD record[4]={0x53505254,original_edge,corrected_edge,additive},written;
    if(material_report!=INVALID_HANDLE_VALUE)WriteFile(material_report,record,sizeof(record),&written,0);
    device->lpVtbl->SetTexture(device,0,0);texture->lpVtbl->Release(texture);
}
