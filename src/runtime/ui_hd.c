/* Independent GPU textures retain authored resolution and8-bit alpha. */
#include <d3d.h>
typedef void (__cdecl *UiNativeLoadFn)(int,const char *,SHORT,SHORT);
typedef struct UiHdPage {IDirectDrawSurface *surface;IDirect3DTexture *texture;DWORD handle;UINT width;} UiHdPage;
static UiHdPage ui_hd_pages[2];
static UiNativeLoadFn old_ui_native_load;
static WCHAR ui_hd_directory[MAX_PATH];
static DWORD ui_hd_original_key,ui_hd_original_test,ui_hd_original_ref,ui_hd_original_func=8;
/* V1 devices may reject Device2 QI. The native game never changes alpha-test
 * enable/ref/function, so its inherited defaults are FALSE/0/ALWAYS. Track
 * explicit execute-buffer state changes too, and restore those after HD UI. */
static BOOL ui_hd_extra_states;
static const GUID ui_hd_texture_iid={0x2cdcd9e0,0x25a0,0x11cf,{0xa3,0x1a,0x00,0xaa,0x00,0xb9,0x33,0x56}};
static const GUID ui_hd_device2_iid={0x93281501,0x8cf8,0x11d0,{0x89,0xab,0x00,0xa0,0xc9,0x05,0x41,0x29}};
static BYTE ui_hd_instructions[1024*1024];
static BYTE ui_hd_deferred[1024*1024];
typedef struct UiHdDeferred {DWORD handle;UINT states[12],count;} UiHdDeferred;
static BOOL ui_hd_handle(DWORD handle) {
    return handle && (handle==ui_hd_pages[0].handle || handle==ui_hd_pages[1].handle);
}
static void ui_hd_release(UiHdPage *page) {
    if(page->texture)page->texture->lpVtbl->Release(page->texture);
    if(page->surface)page->surface->lpVtbl->Release(page->surface);
    page->texture=0;page->surface=0;page->handle=0;page->width=0;
}
static void ui_hd_capture_states(IDirect3DDevice *device) {
    IDirect3DDevice2 *device2=0;ui_hd_extra_states=FALSE;
    if(device->lpVtbl->QueryInterface(device,&ui_hd_device2_iid,(void **)&device2)>=0 && device2) {
        HRESULT a=device2->lpVtbl->GetRenderState(device2,D3DRENDERSTATE_COLORKEYENABLE,&ui_hd_original_key);
        HRESULT b=device2->lpVtbl->GetRenderState(device2,D3DRENDERSTATE_ALPHATESTENABLE,&ui_hd_original_test);
        HRESULT c=device2->lpVtbl->GetRenderState(device2,D3DRENDERSTATE_ALPHAREF,&ui_hd_original_ref);
        HRESULT d=device2->lpVtbl->GetRenderState(device2,D3DRENDERSTATE_ALPHAFUNC,&ui_hd_original_func);
        ui_hd_extra_states=a>=0 && b>=0 && c>=0 && d>=0;device2->lpVtbl->Release(device2);
    }
}
static BOOL ui_hd_load(UINT index,UiHdPage *page) {
    WCHAR path[MAX_PATH];UINT at=0;
    while(ui_hd_directory[at]) {path[at]=ui_hd_directory[at];++at;}
    const WCHAR *name=index?L"hud-gauges.rgba":L"font-and-numbers.rgba";
    while((*((WCHAR *)path+at++)=*name++)) {}
    HANDLE file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,0,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,0);
    if(file==INVALID_HANDLE_VALUE)return FALSE;
    DWORD header[4],read=0;LARGE_INTEGER length;
    BOOL ok=ReadFile(file,header,16,&read,0) && read==16 && GetFileSizeEx(file,&length);
    if(!ok) {CloseHandle(file);return FALSE;}
    UINT width=header[1],height=header[2],size=header[3];
    ok=header[0]==0x31495557 && width>=256 && width<=8192 && width==height && !(width&(width-1)) &&
        size==width*height*4 && size<=64*1024*1024 && length.QuadPart==(LONGLONG)16+size;
    if(!ok) {CloseHandle(file);return FALSE;}
    BYTE *pixels=VirtualAlloc(0,size,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    if(!pixels) {CloseHandle(file);return FALSE;}
    ok=ReadFile(file,pixels,size,&read,0) && read==size;CloseHandle(file);
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
    VirtualFree(pixels,0,MEM_RELEASE);
    if(hr<0) {wo_render_event("ui_hd_load_error",(DWORD)hr);ui_hd_release(page);return FALSE;}
    return TRUE;
}
static void ui_hd_install(void) {
    UiHdPage fresh[2]={0};
    if(!ui_hd_load(0,fresh) || !ui_hd_load(1,fresh+1)) {
        ui_hd_release(fresh);ui_hd_release(fresh+1);wo_render_event("ui_hd_active_pages",0);return;
    }
    IDirect3DDevice *device=*(IDirect3DDevice **)(game+0xc8d10);
    ui_hd_capture_states(device);
    for(UINT i=0;i<2;++i) {
        ui_hd_release(ui_hd_pages+i);ui_hd_pages[i]=fresh[i];
        ((DWORD *)(game+0xc7910))[22+i]=fresh[i].handle;
    }
    wo_render_event("ui_hd_font_width",fresh[0].width);wo_render_event("ui_hd_gauge_width",fresh[1].width);
    wo_render_event("ui_hd_extended_states",ui_hd_extra_states);wo_render_event("ui_hd_active_pages",2);
}
static void __cdecl ui_native_load(int unused,const char *filename,SHORT first,SHORT count) {
    old_ui_native_load(unused,filename,first,count);
    if(first<=22 && (int)first+count>23 && startup_name(filename,"hud2.tga"))ui_hd_install();
}
static const UINT ui_hd_states[12]={27,19,20,21,14,7,41,15,17,18,24,25};
static BOOL ui_hd_emit(BYTE *out,UINT *written,UINT capacity,UINT *effective,const UINT *wanted) {
    UINT changes=0;for(UINT i=0;i<12;++i)if(effective[i]!=wanted[i])++changes;
    if(!changes)return TRUE;
    UINT bytes=4+changes*8;if(bytes>capacity-*written)return FALSE;
    BYTE *p=out+*written;p[0]=8;p[1]=8;*(WORD *)(p+2)=(WORD)changes;p+=4;
    for(UINT i=0;i<12;++i)if(effective[i]!=wanted[i]) {
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
    for(UINT i=0;i<12;++i)record->states[i]=states[i];
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
    UINT original[12]={0,2,2,2,1,1,0,0,1,1,0,8},effective[12];
    original[6]=ui_hd_original_key;original[7]=ui_hd_original_test;
    original[10]=ui_hd_original_ref;original[11]=ui_hd_original_func;
    for(UINT i=0;i<12;++i)effective[i]=original[i];
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
                for(UINT i=0;i<12;++i)if(state==ui_hd_states[i])original[i]=effective[i]=value;
            }
        }
        if(opcode==3 && ui_hd_handle(handle)) {
            if(size!=8)return 0;
            UINT wanted[12];for(UINT i=0;i<12;++i)wanted[i]=original[i];
            wanted[0]=1;wanted[1]=5;wanted[2]=6;wanted[3]=4;wanted[4]=original[4];
            if(ui_hd_extra_states)wanted[6]=0;
            wanted[7]=1;wanted[10]=0;wanted[11]=5; /* discard alpha0 before depth writes */
            wanted[8]=wanted[9]=2;
            UINT panels=0;
            if(handle==ui_hd_pages[0].handle && vertices) {
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
static void ui_hd_finish(void) {
    if(!ui_hd_pages[0].handle && !ui_hd_pages[1].handle)return;
    BYTE *buffer=*(BYTE **)(game+0x7c9690),*start=*(BYTE **)(game+0x53f1a0),*end=*(BYTE **)(game+0x631d54);
    UINT size=*(UINT *)(game+0xc8610);
    if(!buffer || !start || end<start || start<buffer || end>=buffer+size)return;
    UINT capacity=(UINT)(buffer+size-start);if(capacity>sizeof(ui_hd_instructions))capacity=sizeof(ui_hd_instructions);
    UINT count=*(UINT *)(game+0x631d50);
    if(count>(UINT)(start-buffer)/sizeof(TLVertex))return;
    UINT n=capacity>4?ui_hd_rewrite(start,(UINT)(end-start),ui_hd_instructions,capacity-4,(const TLVertex *)buffer,count):0;
    if(n) {copy(start,ui_hd_instructions,n);*(BYTE **)(game+0x631d54)=start+n;}
}
static void initialize_ui_hd(const WCHAR *root) {
    UINT n=0;while(root[n] && n<MAX_PATH-40) {ui_hd_directory[n]=root[n];++n;}
    if(root[n])return;
    const WCHAR *suffix=L"textures\\hd\\";while((*((WCHAR *)ui_hd_directory+n++)=*suffix++)) {}
    old_ui_native_load=(UiNativeLoadFn)hook(RVA_ui_native_load,entry_ui_native_load,sizeof(entry_ui_native_load),ui_native_load);
    wo_render_event("ui_hd_loader_hook",old_ui_native_load!=0);
}
