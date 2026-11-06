/* Independent GPU textures retain authored resolution and8-bit alpha. */
#include <d3d.h>
#include "hud_coverage.h"
typedef void (__cdecl *UiNativeLoadFn)(int,const char *,SHORT,SHORT);
typedef struct UiHdPage {IDirectDrawSurface *surface;IDirect3DTexture *texture;DWORD handle;UINT width;UiHdFrameArt frame_art;BYTE *hud_coverage;BOOL world;} UiHdPage;
enum {UI_HD_PAGE_COUNT=256};
enum {UI_HD_STATE_COUNT=13};
static UiHdPage ui_hd_pages[UI_HD_PAGE_COUNT];
static UiHdPage ui_hd_opaque_pages[UI_HD_PAGE_COUNT];
static UiHdPage ui_hd_logo;
#define UI_HD_LOGO_TAG 0x574f4c47u
static UiNativeLoadFn old_ui_native_load;
static WCHAR ui_hd_directory[MAX_PATH];
#include "ui_hd_assets.c"
static DWORD ui_hd_original_key,ui_hd_original_test,ui_hd_original_ref,ui_hd_original_func=8,ui_hd_original_address=1;
/* V1 devices may reject Device2 QI. The native game never changes alpha-test
 * enable/ref/function, so its inherited defaults are FALSE/0/ALWAYS. Track
 * explicit execute-buffer state changes too, and restore those after HD UI. */
static BOOL ui_hd_extra_states;
static const GUID ui_hd_texture_iid={0x2cdcd9e0,0x25a0,0x11cf,{0xa3,0x1a,0x00,0xaa,0x00,0xb9,0x33,0x56}};
static const GUID ui_hd_device2_iid={0x93281501,0x8cf8,0x11d0,{0x89,0xab,0x00,0xa0,0xc9,0x05,0x41,0x29}};
static BYTE ui_hd_instructions[1024*1024];
static BYTE ui_hd_deferred[1024*1024];
typedef struct UiHdDeferred {DWORD handle;UINT states[UI_HD_STATE_COUNT],count;} UiHdDeferred;
static BOOL ui_hd_handle(DWORD handle) {
    if(handle)for(UINT i=0;i<UI_HD_PAGE_COUNT;++i)if(handle==ui_hd_pages[i].handle)return TRUE;
    return FALSE;
}
static BOOL ui_hd_frame_white(const BYTE *pixels,UINT width,UINT x,UINT y){
    const BYTE *p=pixels+(y*width+x)*4;
    return p[0]>=240 && p[1]>=240 && p[2]>=240 && p[3]>=240;
}
static void ui_hd_measure_frame(const BYTE *pixels,UINT width,UINT page,UiHdFrameArt *art){
    *art=(UiHdFrameArt){0};
    if(!pixels || width<256 || (page!=0 && page!=2))return;
    UINT y=width*50/256;
    if(!page){
        UINT x=0;while(x<width/16 && ui_hd_frame_white(pixels,width,x,y))++x;
        if(!x || x==width/16)return;
        art->left_inner=(float)x*256/width;
        x=width/2;y=width*114/256;
        while(y<width*128/256 && ui_hd_frame_white(pixels,width,x,y))++y;
        if(y==width*128/256)return;art->start_top=(float)y*256/width;
        while(y<width*160/256 && !ui_hd_frame_white(pixels,width,x,y))++y;
        if(y==width*160/256)return;art->footer_top=(float)y*256/width;
        while(y<width*160/256 && ui_hd_frame_white(pixels,width,x,y))++y;
        if(y==width*160/256)return;art->footer_bottom=(float)y*256/width;
        while(y<width*186/256 && !ui_hd_frame_white(pixels,width,x,y))++y;
        if(y==width*186/256)return;art->footer_end=(float)y*256/width;
    }else{
        UINT x=width*48/256,end=width*64/256;
        while(x<end && !ui_hd_frame_white(pixels,width,x,y))++x;
        if(x==end)return;art->right_inner=(float)x*256/width;
        while(x<end && ui_hd_frame_white(pixels,width,x,y))++x;
        art->right_outer=(float)x*256/width;
        if(art->right_inner==art->right_outer)return;
    }
    art->valid=TRUE;
}
static BOOL ui_hd_frame_art(UiHdFrameArt *art){
    if(!ui_hd_pages[1].handle || !ui_hd_pages[2].handle || !ui_hd_pages[3].handle ||
       !ui_hd_pages[1].frame_art.valid || !ui_hd_pages[3].frame_art.valid)return FALSE;
    *art=ui_hd_pages[1].frame_art;
    art->right_inner=ui_hd_pages[3].frame_art.right_inner;
    art->right_outer=ui_hd_pages[3].frame_art.right_outer;return TRUE;
}
/* The game executes a frame's buffers during the NEXT present. When a race or
 * the attract demo ends, its last frame is still queued while the menu pages
 * load over the track's slots, and that queued frame names the replaced HD
 * pages' texture handles. Released at once, a handle that no new texture had
 * reused reached D3D dead: system DirectDraw faulted in D3dDrawPrimitives2
 * (Sagarmatha frees four, the menu frame art reuses three). Keep replaced
 * pages until a present has completed, then release them. */
enum {UI_HD_RETIRED_COUNT=1024};
static IUnknown *ui_hd_retired[UI_HD_RETIRED_COUNT];
static UINT ui_hd_retired_count;
static BOOL ui_hd_deferred_release; /* a present hook calls ui_hd_presented */
static void ui_hd_retire(IUnknown *object) {
    if(!object)return;
    if(ui_hd_deferred_release && ui_hd_retired_count<UI_HD_RETIRED_COUNT){ui_hd_retired[ui_hd_retired_count++]=object;return;}
    object->lpVtbl->Release(object);
}
static void ui_hd_presented(void) {
    for(UINT i=0;i<ui_hd_retired_count;++i)ui_hd_retired[i]->lpVtbl->Release(ui_hd_retired[i]);
    ui_hd_retired_count=0;
}
static void ui_hd_release(UiHdPage *page) {
    ui_hd_retire((IUnknown *)page->texture);
    ui_hd_retire((IUnknown *)page->surface);
    page->texture=0;page->surface=0;page->handle=0;page->width=0;
    page->frame_art.valid=FALSE;
    page->world=FALSE;
    if(page->hud_coverage)VirtualFree(page->hud_coverage,0,MEM_RELEASE);page->hud_coverage=0;
}
static void ui_hd_capture_states(IDirect3DDevice *device) {
    IDirect3DDevice2 *device2=0;ui_hd_extra_states=FALSE;
    if(device->lpVtbl->QueryInterface(device,&ui_hd_device2_iid,(void **)&device2)>=0 && device2) {
        HRESULT a=device2->lpVtbl->GetRenderState(device2,D3DRENDERSTATE_COLORKEYENABLE,&ui_hd_original_key);
        HRESULT b=device2->lpVtbl->GetRenderState(device2,D3DRENDERSTATE_ALPHATESTENABLE,&ui_hd_original_test);
        HRESULT c=device2->lpVtbl->GetRenderState(device2,D3DRENDERSTATE_ALPHAREF,&ui_hd_original_ref);
        HRESULT d=device2->lpVtbl->GetRenderState(device2,D3DRENDERSTATE_ALPHAFUNC,&ui_hd_original_func);
        device2->lpVtbl->GetRenderState(device2,D3DRENDERSTATE_TEXTUREADDRESS,&ui_hd_original_address);
        ui_hd_extra_states=a>=0 && b>=0 && c>=0 && d>=0;device2->lpVtbl->Release(device2);
    }
}
static BOOL ui_hd_upload(const BYTE *pixels,UINT width,UINT height,UiHdPage *page) {
    BOOL ok=width>=128 && width<=8192 && !(width&(width-1)) && height>=128 && height<=8192 && !(height&(height-1));
    IDirectDraw *dd=*(IDirectDraw **)(game+0xc8b50);
    IDirect3DDevice *device=*(IDirect3DDevice **)(game+0xc8d10);
    HRESULT hr=E_FAIL;
    if(ok && dd && device) {
        DDSURFACEDESC desc={0};desc.dwSize=sizeof(desc);
        desc.dwFlags=DDSD_CAPS|DDSD_WIDTH|DDSD_HEIGHT|DDSD_PIXELFORMAT;
        desc.dwWidth=width;desc.dwHeight=height;desc.ddsCaps.dwCaps=DDSCAPS_TEXTURE|DDSCAPS_VIDEOMEMORY;
        DDPIXELFORMAT *format=&desc.ddpfPixelFormat;format->dwSize=sizeof(*format);
        format->dwFlags=DDPF_RGB|DDPF_ALPHAPIXELS;format->dwRGBBitCount=32;
        format->dwRBitMask=0xff0000;format->dwGBitMask=0xff00;format->dwBBitMask=0xff;format->dwRGBAlphaBitMask=0xff000000;
        hr=dd->lpVtbl->CreateSurface(dd,&desc,&page->surface,0);
        if(hr>=0 && page->surface) {
            DDSURFACEDESC lock={0};lock.dwSize=sizeof(lock);
            hr=page->surface->lpVtbl->Lock(page->surface,0,&lock,DDLOCK_WAIT|DDLOCK_WRITEONLY,0);
            if(hr>=0) {
                ok=lock.lpSurface && lock.lPitch>=(LONG)width*4;
                if(ok)for(UINT y=0;y<height;++y)copy((BYTE *)lock.lpSurface+y*lock.lPitch,pixels+y*width*4,width*4);
                HRESULT unlock=page->surface->lpVtbl->Unlock(page->surface,0);
                if(!ok || unlock<0)hr=E_FAIL;
            }
            if(hr>=0)hr=page->surface->lpVtbl->QueryInterface(page->surface,&ui_hd_texture_iid,(void **)&page->texture);
            if(hr>=0 && page->texture)hr=page->texture->lpVtbl->GetHandle(page->texture,device,&page->handle);
            if(hr>=0 && page->handle)page->width=width;
            else hr=E_FAIL;
        }
    }
    if(hr<0) {wo_render_event("ui_hd_load_error",(DWORD)hr);ui_hd_release(page);return FALSE;}
    return TRUE;
}
/* The supported EXE places the second half at x304, abutting the first half's
 * right edge. Keep all254+210 source pixels and filter across the join in one
 * continuous texture; the reference decomp's x302 is a different layout. */
static BYTE *ui_hd_join_logo(const BYTE *atlas,UINT size,UINT *width,UINT *height) {
    if(!atlas || size<256 || size>4096 || (size&(size-1)))return 0;
    *width=size*2;*height=size/2;
    BYTE *joined=VirtualAlloc(0,*width**height*4,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    if(!joined)return 0;
    UINT scale=size/256;
    for(UINT y=0;y<64*scale;++y){
        copy(joined+y**width*4,atlas+y*size*4,254*scale*4);
        copy(joined+(y**width+254*scale)*4,atlas+((y+64*scale)*size)*4,210*scale*4);
    }
    return joined;
}
static BOOL ui_hd_load(const char *filename,UINT index,UINT pages,UiHdPage *page,UiHdPage *opaque) {
    BOOL logo=index==0 && startup_name(filename,"xllogo.tga");
    if(logo)ui_hd_release(&ui_hd_logo);
    UINT width=0;BYTE *pixels=hd_asset_pixels(filename,index,pages,&width);
    if(!pixels)return FALSE;
    if(startup_name(filename,"testmenu.tga"))ui_hd_measure_frame(pixels,width,index,&page->frame_art);
    BOOL ok=ui_hd_upload(pixels,width,width,page);
    if(ok && hd_world_name(filename)){
        page->world=TRUE;
        for(UINT i=0;i<width*width;++i)pixels[i*4+3]=255;
        ok=ui_hd_upload(pixels,width,width,opaque);opaque->world=TRUE;
        if(!ok){ui_hd_release(page);ui_hd_release(opaque);}
    }
    if(ok && startup_name(filename,"hud2.tga"))page->hud_coverage=hud_coverage_create(pixels,width);
    if(ok && logo){
        UINT w=0,h=0;BYTE *joined=ui_hd_join_logo(pixels,width,&w,&h);
        if(joined){
            ui_hd_upload(joined,w,h,&ui_hd_logo);VirtualFree(joined,0,MEM_RELEASE);
            wo_render_event("ui_hd_joined_logo",ui_hd_logo.handle!=0);
        }
    }
    VirtualFree(pixels,0,MEM_RELEASE);return ok;
}
static void ui_hd_commit_page(UINT slot,const UiHdPage *fresh) {
    if(slot>=UI_HD_PAGE_COUNT)return;
    ui_hd_release(ui_hd_pages+slot);ui_hd_pages[slot]=*fresh;
    if(fresh->handle)((DWORD *)(game+0xc7910))[slot]=fresh->handle;
}
static void __cdecl ui_native_load(int unused,const char *filename,SHORT first,SHORT count) {
    old_ui_native_load(unused,filename,first,count);
    if(first<0 || count<1 || (UINT)first+(UINT)count>UI_HD_PAGE_COUNT)return;
    UINT loaded=0;
    for(UINT i=0;i<(UINT)count;++i){
        UiHdPage fresh={0},opaque={0};
        if(ui_hd_load(filename,i,(UINT)count,&fresh,&opaque)){
            ++loaded;wo_render_event("ui_hd_loaded_slot",(UINT)first+i);wo_render_event("ui_hd_loaded_width",fresh.width);
        }
        ui_hd_commit_page((UINT)first+i,&fresh);
        ui_hd_release(ui_hd_opaque_pages+(UINT)first+i);ui_hd_opaque_pages[(UINT)first+i]=opaque;
    }
    IDirect3DDevice *device=*(IDirect3DDevice **)(game+0xc8d10);
    if(loaded && device)ui_hd_capture_states(device);
    UINT total=0;for(UINT i=0;i<UI_HD_PAGE_COUNT;++i)if(ui_hd_pages[i].handle)++total;
    if(loaded)wo_render_event("ui_hd_active_pages",total);
}
static int ui_hd_slot(DWORD handle) {
    if(handle)for(UINT i=0;i<UI_HD_PAGE_COUNT;++i)if(ui_hd_pages[i].handle==handle)return (int)i;
    return -1;
}
static BOOL ui_hd_logo_triangle(const BYTE *triangle,const TLVertex *vertices,UINT count) {
    if(!ui_hd_logo.handle || !vertices)return FALSE;
    for(UINT i=0;i<3;++i){
        UINT index=((const WORD *)triangle)[i];
        if(index>=count || vertices[index].specular!=UI_HD_LOGO_TAG)return FALSE;
    }
    return TRUE;
}
static BOOL ui_hd_texture(BYTE *out,UINT *written,UINT capacity,DWORD handle) {
    if(12>capacity-*written)return FALSE;
    BYTE *p=out+*written;p[0]=8;p[1]=8;*(WORD *)(p+2)=1;
    *(DWORD *)(p+4)=1;*(DWORD *)(p+8)=handle;*written+=12;return TRUE;
}
static BOOL ui_hd_frame_page(UINT slot){return slot>=1 && slot<=3 && ui_hd_pages[slot].handle!=0;}
static const UINT ui_hd_states[UI_HD_STATE_COUNT]={27,19,20,21,14,7,41,15,17,18,24,25,3};
static BOOL ui_hd_emit(BYTE *out,UINT *written,UINT capacity,UINT *effective,const UINT *wanted) {
    UINT changes=0;for(UINT i=0;i<UI_HD_STATE_COUNT;++i)if(effective[i]!=wanted[i])++changes;
    if(!changes)return TRUE;
    UINT bytes=4+changes*8;if(bytes>capacity-*written)return FALSE;
    BYTE *p=out+*written;p[0]=8;p[1]=8;*(WORD *)(p+2)=(WORD)changes;p+=4;
    for(UINT i=0;i<UI_HD_STATE_COUNT;++i)if(effective[i]!=wanted[i]) {
        *(UINT *)p=ui_hd_states[i];*(UINT *)(p+4)=wanted[i];p+=8;effective[i]=wanted[i];
    }
    *written+=bytes;return TRUE;
}
/* DrawMenuBox41b200 samples font page22 at(109,53)/(109,59).
 * Those opaque1x1 patches are panels, not transparent lettering. Native
 * callers can enqueue text BEFORE the box and rely on depth writes to retain
 * it. Draw these panels first, back to front, retaining their native depth
 * writes; then blend lettering over them, discarding alpha0 before depth. */
static BOOL ui_hd_panel(const BYTE *triangle,const TLVertex *vertices,UINT count,float *depth) {
    if(!vertices)return FALSE;
    UINT first=((const WORD *)triangle)[0];if(first>=count)return FALSE;
    float v=vertices[first].v,top;
    if(v>=53.0f/256 && v<=54.0f/256)top=53.0f/256;
    else if(v>=59.0f/256 && v<=60.0f/256)top=59.0f/256;
    else return FALSE;
    float z=0;
    for(UINT i=0;i<3;++i) {
        UINT index=((const WORD *)triangle)[i];if(index>=count)return FALSE;
        const TLVertex *p=vertices+index;
        if(!(p->u>=109.0f/256 && p->u<=110.0f/256 && p->v>=top && p->v<=top+1.0f/256))return FALSE;
        z+=p->z;
    }
    *depth=z/3;return TRUE;
}
static BOOL ui_hd_defer(DWORD handle,const UINT *states,const BYTE *triangles,UINT count,
                        const TLVertex *vertices,UINT vertex_count,BOOL omit_panels,UINT *length) {
    if(sizeof(UiHdDeferred)>sizeof(ui_hd_deferred)-*length)return FALSE;
    UiHdDeferred *record=(UiHdDeferred *)(ui_hd_deferred+*length);
    record->handle=handle;record->count=0;
    for(UINT i=0;i<UI_HD_STATE_COUNT;++i)record->states[i]=states[i];
    UINT bytes=sizeof(*record);
    for(UINT i=0;i<count;++i) {
        const BYTE *triangle=triangles+i*8;float z;
        if(omit_panels && ui_hd_panel(triangle,vertices,vertex_count,&z))continue;
        if(bytes+8>sizeof(ui_hd_deferred)-*length)return FALSE;
        copy((BYTE *)record+bytes,triangle,8);bytes+=8;++record->count;
    }
    if(record->count)*length+=bytes;
    return TRUE;
}
static BOOL ui_hd_flush(BYTE *out,UINT *written,UINT capacity,UINT *effective,const UINT *original,DWORD handle,UINT length) {
    DWORD current=handle;
    for(UINT offset=0;offset<length;) {
        const UiHdDeferred *record=(const UiHdDeferred *)(ui_hd_deferred+offset);
        if(!ui_hd_emit(out,written,capacity,effective,record->states))return FALSE;
        if(current!=record->handle) {
            if(12>capacity-*written)return FALSE;
            BYTE *p=out+*written;p[0]=8;p[1]=8;*(WORD *)(p+2)=1;
            *(DWORD *)(p+4)=1;*(DWORD *)(p+8)=record->handle;*written+=12;current=record->handle;
        }
        UINT bytes=4+record->count*8;if(bytes>capacity-*written)return FALSE;
        BYTE *p=out+*written;p[0]=3;p[1]=8;*(WORD *)(p+2)=(WORD)record->count;
        copy(p+4,(const BYTE *)(record+1),bytes-4);*written+=bytes;
        offset+=sizeof(*record)+bytes-4;
    }
    if(!ui_hd_emit(out,written,capacity,effective,original))return FALSE;
    if(current!=handle) {
        if(12>capacity-*written)return FALSE;
        BYTE *p=out+*written;p[0]=8;p[1]=8;*(WORD *)(p+2)=1;
        *(DWORD *)(p+4)=1;*(DWORD *)(p+8)=handle;*written+=12;
    }
    return TRUE;
}
static UINT ui_hd_rewrite(const BYTE *input,UINT length,BYTE *out,UINT capacity,const TLVertex *vertices,UINT vertex_count) {
    UINT original[UI_HD_STATE_COUNT]={0,2,2,2,1,1,0,0,1,1,0,8,1},effective[UI_HD_STATE_COUNT];
    original[6]=ui_hd_original_key;original[7]=ui_hd_original_test;
    original[10]=ui_hd_original_ref;original[11]=ui_hd_original_func;
    original[12]=ui_hd_original_address;
    for(UINT i=0;i<UI_HD_STATE_COUNT;++i)effective[i]=original[i];
    UINT offset=0,written=0,deferred=0;DWORD handle=0;BOOL touched=FALSE;
    while(offset<length) {
        if(length-offset<4)return 0;
        UINT opcode=input[offset],size=input[offset+1],count=*(const WORD *)(input+offset+2),bytes=4+size*count;
        if(bytes>length-offset)return 0;
        /* Restore every injected state before the next original instruction;
         * original packets can update a subset of states. */
        if(!ui_hd_emit(out,&written,capacity,effective,original))return 0;
        if(opcode==11) {
            if(size || count || bytes!=length-offset)return 0;
            if(!ui_hd_flush(out,&written,capacity,effective,original,handle,deferred))return 0;
            deferred=0;
        }
        if(opcode==8) {
            if(size!=8)return 0;
            for(UINT j=0;j<count;++j) {
                UINT state=*(const UINT *)(input+offset+4+j*8),value=*(const UINT *)(input+offset+8+j*8);
                if(state==1)handle=value;
                for(UINT i=0;i<UI_HD_STATE_COUNT;++i)if(state==ui_hd_states[i])original[i]=effective[i]=value;
            }
        }
        if(opcode==3 && ui_hd_handle(handle)) {
            if(size!=8)return 0;
            UINT wanted[UI_HD_STATE_COUNT];for(UINT i=0;i<UI_HD_STATE_COUNT;++i)wanted[i]=original[i];
            int slot=ui_hd_slot(handle);
            if(slot>=0 && ui_hd_pages[slot].world){
                /* World textures keep native blend/depth/modulation. A second
                 * upload makes keyed BLACK opaque again when keying is off. */
                DWORD texture=original[6]?handle:ui_hd_opaque_pages[slot].handle;
                if(!texture)return 0;
                /* Legacy MODULATE selects texture alpha when one exists.
                 * MODULATEALPHA preserves the diffuse opacity that RGB pages
                 * supplied before this RGBA replacement was introduced. */
                if(wanted[3]==2)wanted[3]=4;
                wanted[6]=0;
                if(original[6]){wanted[7]=1;wanted[10]=127;wanted[11]=5;}
                if(!ui_hd_emit(out,&written,capacity,effective,wanted))return 0;
                if(texture!=handle && !ui_hd_texture(out,&written,capacity,texture))return 0;
                if(bytes>capacity-written)return 0;copy(out+written,input+offset,bytes);written+=bytes;
                if(texture!=handle && !ui_hd_texture(out,&written,capacity,handle))return 0;
                offset+=bytes;touched=TRUE;continue;
            }
            wanted[0]=1;wanted[1]=2;wanted[2]=6;wanted[3]=4;wanted[4]=original[4];
            if(ui_hd_extra_states)wanted[6]=0;
            wanted[7]=1;wanted[10]=0;wanted[11]=5; /* discard alpha0 before depth writes */
            wanted[8]=wanted[9]=2;
            if(title_input_active()){wanted[4]=0;wanted[5]=0;} /* title lettering overlays the artwork, no scene depth */
            if(slot>=1 && slot<=3)wanted[12]=D3DTADDRESS_CLAMP;
            if(slot<22 || slot>24){
                if(!ui_hd_emit(out,&written,capacity,effective,wanted))return 0;
                DWORD current=handle;
                for(UINT first=0;first<count;){
                    BOOL logo=slot==25 && ui_hd_logo_triangle(input+offset+4+first*8,vertices,vertex_count);
                    UINT last=first+1;
                    while(last<count && (slot==25 && ui_hd_logo_triangle(input+offset+4+last*8,vertices,vertex_count))==logo)++last;
                    DWORD texture=logo?ui_hd_logo.handle:handle;
                    if(current!=texture && !ui_hd_texture(out,&written,capacity,texture))return 0;
                    current=texture;UINT run=4+(last-first)*8;if(run>capacity-written)return 0;
                    BYTE *p=out+written;p[0]=3;p[1]=8;*(WORD *)(p+2)=(WORD)(last-first);
                    copy(p+4,input+offset+4+first*8,run-4);written+=run;first=last;
                }
                if(current!=handle && !ui_hd_texture(out,&written,capacity,handle))return 0;
                offset+=bytes;touched=TRUE;continue;
            }
            UINT panels=0;
            if(handle==ui_hd_pages[22].handle && vertices) {
                for(UINT j=0;j<count;++j) {float z;if(ui_hd_panel(input+offset+4+j*8,vertices,vertex_count,&z))++panels;}
            }
            if(panels) {
                wanted[4]=original[4];
                if(!ui_hd_emit(out,&written,capacity,effective,wanted) || 4+panels*8>capacity-written)return 0;
                BYTE *ordered=out+written;ordered[0]=3;ordered[1]=8;*(WORD *)(ordered+2)=(WORD)panels;
                UINT placed=0;
                for(UINT j=0;j<count;++j) {
                    const BYTE *triangle=input+offset+4+j*8;float z;
                    if(!ui_hd_panel(triangle,vertices,vertex_count,&z))continue;
                    UINT at=placed++;
                    while(at) {
                        float before;if(!ui_hd_panel(ordered+4+(at-1)*8,vertices,vertex_count,&before) || before>=z)break;
                        copy(ordered+4+at*8,ordered+4+(at-1)*8,8);--at;
                    }
                    copy(ordered+4+at*8,triangle,8);
                }
                written+=4+panels*8;
                if(count>panels) {
                    if(!ui_hd_defer(handle,wanted,input+offset+4,count,vertices,vertex_count,TRUE,&deferred))return 0;
                }
                touched=TRUE;offset+=bytes;continue;
            }
            /* Native flat button fills are in the last texture bucket. Keep
             * transparent UI after them, with depth tests against all solid
             * backgrounds. Alpha testing rejects invisible texels before depth writes.
             * Retain native depth for visible glyphs across execute buffers. */
            if(!ui_hd_defer(handle,wanted,input+offset+4,count,vertices,vertex_count,FALSE,&deferred))return 0;
            touched=TRUE;offset+=bytes;continue;
        }
        if(bytes>capacity-written)return 0;
        copy(out+written,input+offset,bytes);written+=bytes;offset+=bytes;
    }
    if(!ui_hd_flush(out,&written,capacity,effective,original,handle,deferred))return 0;
    return touched?written:0;
}
static BYTE ui_hd_source[1024*1024];
static WORD ui_hd_vertex_map[65536];
/* Clone only translucent UI vertices, preserving SD/shared vertices and fades. */
static BOOL ui_hd_prepare(BYTE *instructions,UINT length,TLVertex *vertices,UINT *count,UINT capacity) {
    if(*count>65535 || *count>capacity)return FALSE;
    if(capacity>65535)capacity=65535;
    UINT initial=*count,used=initial,offset=0;DWORD handle=0;
    for(UINT i=0;i<initial;++i)ui_hd_vertex_map[i]=65535;
    while(offset<length){
        if(length-offset<4)return FALSE;
        UINT opcode=instructions[offset],size=instructions[offset+1],n=*(WORD *)(instructions+offset+2),bytes=4+size*n;
        if(bytes>length-offset)return FALSE;
        if(opcode==8){if(size!=8)return FALSE;for(UINT i=0;i<n;++i)if(*(DWORD *)(instructions+offset+4+i*8)==1)handle=*(DWORD *)(instructions+offset+8+i*8);}
        int slot=ui_hd_slot(handle);
        if(opcode==3 && slot>=0 && !ui_hd_pages[slot].world){
            if(size!=8)return FALSE;
            for(UINT i=0;i<n;++i)for(UINT corner=0;corner<3;++corner){
                WORD *index=(WORD *)(instructions+offset+4+i*8+corner*2);
                if(*index>=initial)return FALSE;
                if(vertices[*index].color>>24==255)continue;
                if(ui_hd_vertex_map[*index]==65535){
                    if(used>=capacity)return FALSE;
                    vertices[used]=vertices[*index];vertices[used].color=hd_premultiply_color(vertices[used].color);
                    ui_hd_vertex_map[*index]=(WORD)used++;
                }
                *index=ui_hd_vertex_map[*index];
            }
        }
        offset+=bytes;
    }
    *count=used;return TRUE;
}
static void ui_hd_finish_buffer(BYTE *buffer) {
    BOOL any=FALSE;for(UINT i=0;i<UI_HD_PAGE_COUNT;++i)if(ui_hd_pages[i].handle){any=TRUE;break;}if(!any)return;
    BYTE *start=*(BYTE **)(game+0x53f1a0),*end=*(BYTE **)(game+0x631d54);
    UINT size=*(UINT *)(game+0xc8610);
    if(!buffer || !start || end<start || start<buffer || end>=buffer+size)return;
    UINT capacity=(UINT)(buffer+size-start);if(capacity>sizeof(ui_hd_instructions))capacity=sizeof(ui_hd_instructions);
    UINT count=*(UINT *)(game+0x631d50),length=(UINT)(end-start),vertex_capacity=(UINT)(start-buffer)/sizeof(TLVertex);
    if(count>vertex_capacity || length>sizeof(ui_hd_source))return;
    copy(ui_hd_source,start,length);
    if(!ui_hd_prepare(ui_hd_source,length,(TLVertex *)buffer,&count,vertex_capacity)){wo_render_event("ui_hd_vertex_capacity",vertex_capacity);return;}
    UINT n=capacity>4?ui_hd_rewrite(ui_hd_source,length,ui_hd_instructions,capacity-4,(const TLVertex *)buffer,count):0;
    if(n){copy(start,ui_hd_instructions,n);*(BYTE **)(game+0x631d54)=start+n;*(UINT *)(game+0x631d50)=count;}
    for(UINT i=0;i<count;++i)if(((TLVertex *)buffer)[i].specular==UI_HD_LOGO_TAG)((TLVertex *)buffer)[i].specular=0;
}
static void ui_hd_finish(void){ui_hd_finish_buffer(*(BYTE **)(game+0x7c9690));}
static void hd_world_finish(void){ui_hd_finish_buffer(*(BYTE **)(game+0x53f190));}
static void initialize_ui_hd(const WCHAR *root) {
    WCHAR hd[4];if(GetEnvironmentVariableW(L"WO2097_HD_ART",hd,4) && hd[0]==L'0')return;
    UINT n=0;while(root[n] && n<MAX_PATH-40) {ui_hd_directory[n]=root[n];++n;}
    if(root[n])return;
    for(UINT i=0;i<=n;++i)ui_hd_game_root[i]=root[i];
    const WCHAR *suffix=L"..\\assets\\hd\\";while((*((WCHAR *)ui_hd_directory+n++)=*suffix++)) {}
    hd_load_region_index();
    ui_hd_deferred_release=refresh_active;
    old_ui_native_load=(UiNativeLoadFn)hook(RVA_ui_native_load,entry_ui_native_load,sizeof(entry_ui_native_load),ui_native_load);
    wo_render_event("ui_hd_loader_hook",old_ui_native_load!=0);
}
