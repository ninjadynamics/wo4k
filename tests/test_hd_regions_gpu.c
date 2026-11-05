/* Apply the actual HD-world packet to an offscreen GPU: opaque black must
 * draw, keyed black must disappear, vertex fades and additive RGB must survive. */
static void test_hd_world_gpu(IDirectDraw7 *dd,IDirect3DDevice7 *device,IDirectDrawSurface7 *target,DDSURFACEDESC2 *format){
    IDirectDrawSurface7 *texture=0;DDSURFACEDESC2 desc=*format;
    desc.dwWidth=desc.dwHeight=2;desc.ddsCaps.dwCaps=DDSCAPS_TEXTURE|DDSCAPS_VIDEOMEMORY;
    desc.ddpfPixelFormat.dwFlags=DDPF_RGB|DDPF_ALPHAPIXELS;desc.ddpfPixelFormat.dwRGBAlphaBitMask=0xff000000;
    material_check(dd->lpVtbl->CreateSurface(dd,&desc,&texture,0),221);
    ui_hd_pages[100].handle=901;ui_hd_pages[100].world=TRUE;ui_hd_opaque_pages[100].handle=902;
    TLVertex v[4]={{0,0,.5f,1,0xffffffff,0,0,0},{64,0,.5f,1,0xffffffff,0,1,0},
                   {0,64,.5f,1,0xffffffff,0,0,1},{64,64,.5f,1,0xffffffff,0,1,1}};
    for(UINT probe=0;probe<5;++probe){
        UINT keyed=probe==1 || probe==3,blend=probe==2 || probe==4;
        DWORD color=probe<2?0:(probe==4?0x202020:0x906030),alpha=keyed && !color?0:255;
        DDSURFACEDESC2 lock={0};lock.dwSize=sizeof(lock);
        material_check(texture->lpVtbl->Lock(texture,0,&lock,DDLOCK_WAIT,0),222);
        for(UINT y=0;y<2;++y)for(UINT x=0;x<2;++x)*(DWORD *)((BYTE *)lock.lpSurface+y*lock.lPitch+x*4)=alpha<<24|color;
        texture->lpVtbl->Unlock(texture,0);
        for(UINT i=0;i<4;++i)v[i].color=probe==2?0x80ffffff:0xffffffff;
        static BYTE input[128],output[2048];UINT n=0;
        input[n++]=8;input[n++]=8;*(WORD *)(input+n)=9;n+=2;
        static const UINT states[9]={1,41,27,19,20,21,15,24,25};
        UINT values[9]={901,keyed,blend,probe==4?2u:5u,probe==4?2u:6u,2,0,0,8};
        for(UINT i=0;i<9;++i){*(DWORD *)(input+n)=states[i];*(DWORD *)(input+n+4)=values[i];n+=8;}
        input[n++]=3;input[n++]=8;*(WORD *)(input+n)=1;n+=2;
        *(WORD *)(input+n)=0;*(WORD *)(input+n+2)=1;*(WORD *)(input+n+4)=2;*(WORD *)(input+n+6)=0;n+=8;
        UINT bytes=ui_hd_rewrite(input,n,output,sizeof(output),v,4);if(!bytes)ExitProcess(223);
        device->lpVtbl->SetRenderState(device,D3DRENDERSTATE_ZENABLE,0);
        device->lpVtbl->SetRenderState(device,D3DRENDERSTATE_CULLMODE,1);
        device->lpVtbl->SetTextureStageState(device,0,D3DTSS_COLOROP,D3DTOP_MODULATE);
        device->lpVtbl->SetTextureStageState(device,0,D3DTSS_COLORARG1,D3DTA_TEXTURE);
        device->lpVtbl->SetTextureStageState(device,0,D3DTSS_COLORARG2,D3DTA_DIFFUSE);
        device->lpVtbl->SetTextureStageState(device,0,D3DTSS_ALPHAARG1,D3DTA_TEXTURE);
        device->lpVtbl->SetTextureStageState(device,0,D3DTSS_ALPHAARG2,D3DTA_DIFFUSE);
        material_check(device->lpVtbl->Clear(device,0,0,D3DCLEAR_TARGET,0x205020,1,0),224);
        for(UINT at=0;at<bytes;){
            UINT op=output[at],size=output[at+1],count=*(WORD *)(output+at+2);
            if(op==8)for(UINT i=0;i<count;++i){
                UINT state=*(DWORD *)(output+at+4+i*8),value=*(DWORD *)(output+at+8+i*8);
                if(state==1)material_check(device->lpVtbl->SetTexture(device,0,texture),225);
                else if(state==21)material_check(device->lpVtbl->SetTextureStageState(device,0,D3DTSS_ALPHAOP,value==4?D3DTOP_MODULATE:D3DTOP_SELECTARG1),226);
                else material_check(device->lpVtbl->SetRenderState(device,(D3DRENDERSTATETYPE)state,value),227);
            }
            if(op==3){
                material_check(device->lpVtbl->BeginScene(device),228);
                material_check(device->lpVtbl->DrawPrimitive(device,D3DPT_TRIANGLESTRIP,D3DFVF_TLVERTEX,v,4,0),229);
                material_check(device->lpVtbl->EndScene(device),230);
                DWORD pixel=material_pixel(target);
                if(probe==0 && pixel!=0)ExitProcess(231);
                if(probe==1 && pixel!=0x205020)ExitProcess(232);
                if(probe==2){UINT r=pixel>>16,g=pixel>>8&255,b=pixel&255;if(r<87 || r>89 || g<87 || g>89 || b<39 || b>41)ExitProcess(233);}
                if(probe==3 && pixel!=0x906030)ExitProcess(234);
                if(probe==4 && pixel!=0x407040)ExitProcess(235);
            }
            at+=4+size*count;
        }
    }
    ui_hd_pages[100]=(UiHdPage){0};ui_hd_opaque_pages[100]=(UiHdPage){0};
    device->lpVtbl->SetTexture(device,0,0);texture->lpVtbl->Release(texture);
}
