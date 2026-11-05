/* Optional real-GPU regression. Offscreen only: no window or presentation. */
static const GUID material_dd7={0x15e65ec0,0x3b9c,0x11d2,{0xb9,0x2f,0x00,0x60,0x97,0x97,0xea,0x5b}};
static const GUID material_d3d7={0xf5049e77,0x4861,0x11d2,{0xa4,0x07,0x00,0xa0,0xc9,0x06,0x29,0xa8}};
static const GUID material_hal={0x84e63de0,0x46aa,0x11cf,{0x81,0x6f,0x00,0x00,0xc0,0x20,0x15,0x6e}};
static HANDLE material_report=INVALID_HANDLE_VALUE;
static void material_check(HRESULT hr,UINT stage) {
    DWORD record[2]={stage,(DWORD)hr},written;
    if(material_report!=INVALID_HANDLE_VALUE)WriteFile(material_report,record,sizeof(record),&written,0);
    if(hr<0)ExitProcess(stage);
}
static DWORD material_pixel_at(IDirectDrawSurface7 *surface,UINT x) {
    DDSURFACEDESC2 lock={0};lock.dwSize=sizeof(lock);
    material_check(surface->lpVtbl->Lock(surface,0,&lock,DDLOCK_WAIT|DDLOCK_READONLY,0),180);
    DWORD pixel=*(DWORD *)((BYTE *)lock.lpSurface+32*lock.lPitch+x*4)&0xffffff;
    surface->lpVtbl->Unlock(surface,0);return pixel;
}
static DWORD material_pixel(IDirectDrawSurface7 *surface){return material_pixel_at(surface,32);}
#include "test_sprite_blend.c"
#include "test_ps1_sprites.c"
#include "test_hd_regions_gpu.c"
static void test_world_material_gpu(void) {
    WCHAR path[MAX_PATH];if(!GetEnvironmentVariableW(L"WO_TEST_DDRAW_WRAPPER",path,MAX_PATH))return;
    WCHAR report_path[MAX_PATH];
    if(GetEnvironmentVariableW(L"WO_TEST_MATERIAL_REPORT",report_path,MAX_PATH))
        material_report=CreateFileW(report_path,GENERIC_WRITE,FILE_SHARE_READ,0,CREATE_ALWAYS,0,0);
    HMODULE wrapper=LoadLibraryW(path);if(!wrapper)ExitProcess(170);
    typedef HRESULT (WINAPI *CreateFn)(GUID *,void **,const GUID *,IUnknown *);
    CreateFn create=(CreateFn)(void *)GetProcAddress(wrapper,"DirectDrawCreateEx");if(!create)ExitProcess(171);
    typedef HWND (WINAPI *DesktopFn)(void);
    DesktopFn desktop=(DesktopFn)(void *)GetProcAddress(GetModuleHandleW(L"user32.dll"),"GetDesktopWindow");if(!desktop)ExitProcess(172);
    IDirectDraw7 *dd=0;IDirect3D7 *d3d=0;IDirectDrawSurface7 *target=0,*texture=0;IDirect3DDevice7 *device=0;
    material_check(create(0,(void **)&dd,&material_dd7,0),173);
    material_check(dd->lpVtbl->SetCooperativeLevel(dd,desktop(),DDSCL_NORMAL),174);
    material_check(dd->lpVtbl->QueryInterface(dd,&material_d3d7,(void **)&d3d),175);
    DDSURFACEDESC2 desc={0};desc.dwSize=sizeof(desc);desc.dwFlags=DDSD_CAPS|DDSD_WIDTH|DDSD_HEIGHT|DDSD_PIXELFORMAT;
    desc.dwWidth=desc.dwHeight=64;desc.ddsCaps.dwCaps=DDSCAPS_3DDEVICE|DDSCAPS_VIDEOMEMORY|DDSCAPS_OFFSCREENPLAIN;
    desc.ddpfPixelFormat.dwSize=sizeof(DDPIXELFORMAT);desc.ddpfPixelFormat.dwFlags=DDPF_RGB;
    desc.ddpfPixelFormat.dwRGBBitCount=32;desc.ddpfPixelFormat.dwRBitMask=0xff0000;desc.ddpfPixelFormat.dwGBitMask=0xff00;desc.ddpfPixelFormat.dwBBitMask=0xff;
    material_check(dd->lpVtbl->CreateSurface(dd,&desc,&target,0),176);
    material_check(d3d->lpVtbl->CreateDevice(d3d,&material_hal,target,&device),177);
    desc.dwWidth=desc.dwHeight=2;desc.ddsCaps.dwCaps=DDSCAPS_TEXTURE|DDSCAPS_VIDEOMEMORY;
    desc.ddpfPixelFormat.dwFlags|=DDPF_ALPHAPIXELS;desc.ddpfPixelFormat.dwRGBAlphaBitMask=0xff000000;
    material_check(dd->lpVtbl->CreateSurface(dd,&desc,&texture,0),178);
    DDSURFACEDESC2 lock={0};lock.dwSize=sizeof(lock);material_check(texture->lpVtbl->Lock(texture,0,&lock,DDLOCK_WAIT,0),179);
    for(UINT y=0;y<2;++y)for(UINT x=0;x<2;++x)*(DWORD *)((BYTE *)lock.lpSurface+y*lock.lPitch+x*4)=0x00906030;
    texture->lpVtbl->Unlock(texture,0);
    D3DVIEWPORT7 viewport={0,0,64,64,0,1};material_check(device->lpVtbl->SetViewport(device,&viewport),181);
    device->lpVtbl->SetTexture(device,0,texture);
    device->lpVtbl->SetTextureStageState(device,0,D3DTSS_COLOROP,D3DTOP_MODULATE);
    device->lpVtbl->SetTextureStageState(device,0,D3DTSS_COLORARG1,D3DTA_TEXTURE);
    device->lpVtbl->SetTextureStageState(device,0,D3DTSS_COLORARG2,D3DTA_DIFFUSE);
    device->lpVtbl->SetTextureStageState(device,0,D3DTSS_ALPHAOP,D3DTOP_SELECTARG1);
    device->lpVtbl->SetTextureStageState(device,0,D3DTSS_ALPHAARG1,D3DTA_TEXTURE);
    device->lpVtbl->SetRenderState(device,D3DRENDERSTATE_ZENABLE,0);
    device->lpVtbl->SetRenderState(device,D3DRENDERSTATE_CULLMODE,1);
    device->lpVtbl->SetRenderState(device,D3DRENDERSTATE_ALPHABLENDENABLE,0);
    device->lpVtbl->SetRenderState(device,D3DRENDERSTATE_ALPHATESTENABLE,1);
    device->lpVtbl->SetRenderState(device,D3DRENDERSTATE_ALPHAREF,0);
    device->lpVtbl->SetRenderState(device,D3DRENDERSTATE_ALPHAFUNC,5);
    TLVertex vertices[4]={{0,0,.5f,1,0xffffffff,0,0,0},{64,0,.5f,1,0xffffffff,0,1,0},
        {0,64,.5f,1,0xffffffff,0,0,1},{64,64,.5f,1,0xffffffff,0,1,1}};
    material_check(device->lpVtbl->Clear(device,0,0,D3DCLEAR_TARGET,0x00205020,1,0),182);
    material_check(device->lpVtbl->BeginScene(device),183);
    material_check(device->lpVtbl->DrawPrimitive(device,D3DPT_TRIANGLESTRIP,D3DFVF_TLVERTEX,vertices,4,0),184);
    material_check(device->lpVtbl->EndScene(device),185);
    DWORD before=material_pixel(target);if(before!=0x205020)ExitProcess(186);
    /* Apply the actual production packet, not a duplicate set of reset states. */
    BYTE input[12]={3,8,1,0,0,0,1,0,2,0,0,0},output[128];
    UINT bytes=rewrite_effect_material(input,sizeof(input),output,sizeof(output),0,0,0,0,FALSE);if(!bytes)ExitProcess(187);
    for(UINT at=0;at<bytes;) {
        UINT opcode=output[at],size=output[at+1],count=*(WORD *)(output+at+2);
        if(opcode==8)for(UINT i=0;i<count;++i)
            material_check(device->lpVtbl->SetRenderState(device,(D3DRENDERSTATETYPE)*(DWORD *)(output+at+4+i*8),*(DWORD *)(output+at+8+i*8)),188);
        at+=4+size*count;
    }
    material_check(device->lpVtbl->BeginScene(device),189);
    material_check(device->lpVtbl->DrawPrimitive(device,D3DPT_TRIANGLESTRIP,D3DFVF_TLVERTEX,vertices,4,0),190);
    material_check(device->lpVtbl->EndScene(device),191);
    DWORD opaque=material_pixel(target);if(opaque!=0x906030)ExitProcess(192);
    /* Source-alpha foliage must remain transparent after the reset. */
    device->lpVtbl->Clear(device,0,0,D3DCLEAR_TARGET,0x00205020,1,0);
    device->lpVtbl->SetRenderState(device,D3DRENDERSTATE_ALPHABLENDENABLE,1);
    device->lpVtbl->SetRenderState(device,D3DRENDERSTATE_SRCBLEND,5);
    device->lpVtbl->SetRenderState(device,D3DRENDERSTATE_DESTBLEND,6);
    device->lpVtbl->BeginScene(device);device->lpVtbl->DrawPrimitive(device,D3DPT_TRIANGLESTRIP,D3DFVF_TLVERTEX,vertices,4,0);device->lpVtbl->EndScene(device);
    DWORD foliage=material_pixel(target);if(foliage!=0x205020)ExitProcess(193);
    /* Native vegetation also uses opaque RGB pages with keyed BLACK, rather
     * than source-alpha blending. The prior blanket key-off breaks these. */
    device->lpVtbl->SetTexture(device,0,0);texture->lpVtbl->Release(texture);texture=0;
    desc.ddpfPixelFormat.dwFlags=DDPF_RGB;desc.ddpfPixelFormat.dwRGBAlphaBitMask=0;
    material_check(dd->lpVtbl->CreateSurface(dd,&desc,&texture,0),194);
    lock=(DDSURFACEDESC2){0};lock.dwSize=sizeof(lock);
    material_check(texture->lpVtbl->Lock(texture,0,&lock,DDLOCK_WAIT,0),195);
    for(UINT y=0;y<2;++y)for(UINT x=0;x<2;++x)*(DWORD *)((BYTE *)lock.lpSurface+y*lock.lPitch+x*4)=0;
    texture->lpVtbl->Unlock(texture,0);
    DDCOLORKEY black={0,0};material_check(texture->lpVtbl->SetColorKey(texture,DDCKEY_SRCBLT,&black),196);
    device->lpVtbl->SetTexture(device,0,texture);device->lpVtbl->SetRenderState(device,D3DRENDERSTATE_ALPHABLENDENABLE,0);
    material_check(device->lpVtbl->SetRenderState(device,D3DRENDERSTATE_COLORKEYENABLE,1),197);
    device->lpVtbl->BeginScene(device);device->lpVtbl->DrawPrimitive(device,D3DPT_TRIANGLESTRIP,D3DFVF_TLVERTEX,vertices,4,0);device->lpVtbl->EndScene(device);
    DWORD native_keyed=material_pixel(target);if(native_keyed!=0x205020)ExitProcess(198);
    device->lpVtbl->SetRenderState(device,D3DRENDERSTATE_COLORKEYENABLE,0);
    device->lpVtbl->BeginScene(device);device->lpVtbl->DrawPrimitive(device,D3DPT_TRIANGLESTRIP,D3DFVF_TLVERTEX,vertices,4,0);device->lpVtbl->EndScene(device);
    DWORD broken_keyed=material_pixel(target);if(broken_keyed!=0)ExitProcess(199);
    device->lpVtbl->Clear(device,0,0,D3DCLEAR_TARGET,0x00205020,1,0);
    device->lpVtbl->SetRenderState(device,D3DRENDERSTATE_COLORKEYENABLE,1);
    for(UINT at=0;at<bytes;) {
        UINT opcode=output[at],size=output[at+1],count=*(WORD *)(output+at+2);
        if(opcode==8)for(UINT i=0;i<count;++i)
            material_check(device->lpVtbl->SetRenderState(device,(D3DRENDERSTATETYPE)*(DWORD *)(output+at+4+i*8),*(DWORD *)(output+at+8+i*8)),200);
        at+=4+size*count;
    }
    device->lpVtbl->BeginScene(device);device->lpVtbl->DrawPrimitive(device,D3DPT_TRIANGLESTRIP,D3DFVF_TLVERTEX,vertices,4,0);device->lpVtbl->EndScene(device);
    DWORD fixed_keyed=material_pixel(target);if(fixed_keyed!=0x205020)ExitProcess(201);
    /* Bilinear sampling at a keyed silhouette used to retain almost-black
     * samples (alpha cutoff1). A half-coverage cutout rejects that dark fringe. */
    device->lpVtbl->SetTexture(device,0,0);lock=(DDSURFACEDESC2){0};lock.dwSize=sizeof(lock);
    material_check(texture->lpVtbl->Lock(texture,0,&lock,DDLOCK_WAIT,0),202);
    for(UINT y=0;y<2;++y){*(DWORD *)((BYTE *)lock.lpSurface+y*lock.lPitch)=0x60c040;*(DWORD *)((BYTE *)lock.lpSurface+y*lock.lPitch+4)=0;}
    texture->lpVtbl->Unlock(texture,0);device->lpVtbl->SetTexture(device,0,texture);
    device->lpVtbl->SetTextureStageState(device,0,D3DTSS_MINFILTER,D3DTFN_LINEAR);
    device->lpVtbl->SetTextureStageState(device,0,D3DTSS_MAGFILTER,D3DTFG_LINEAR);
    device->lpVtbl->Clear(device,0,0,D3DCLEAR_TARGET,0x00205020,1,0);
    device->lpVtbl->BeginScene(device);device->lpVtbl->DrawPrimitive(device,D3DPT_TRIANGLESTRIP,D3DFVF_TLVERTEX,vertices,4,0);device->lpVtbl->EndScene(device);
    DWORD edge=material_pixel(target);WCHAR cutoff[8];
    if(GetEnvironmentVariableW(L"WO_TEST_EDGE_REF",cutoff,8)){
        if(cutoff[1] && edge!=0x205020)ExitProcess(203);
        if(!cutoff[1] && (edge==0x205020 || edge==0 || edge==0x60c040))ExitProcess(204);
    }
    test_sprite_blend_gpu(dd,device,target,&desc);
    test_ps1_blending_gpu(dd,device,target,&desc);
    test_hd_world_gpu(dd,device,target,&desc);
    DWORD pixels[8]={0x2097,before,opaque,foliage,native_keyed,broken_keyed,fixed_keyed,edge},written;
    if(material_report!=INVALID_HANDLE_VALUE){WriteFile(material_report,pixels,sizeof(pixels),&written,0);CloseHandle(material_report);}
    device->lpVtbl->SetTexture(device,0,0);texture->lpVtbl->Release(texture);device->lpVtbl->Release(device);
    target->lpVtbl->Release(target);d3d->lpVtbl->Release(d3d);dd->lpVtbl->Release(dd);
}
