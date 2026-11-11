/* Shift+PrintScreen saves what the game shows as
 * screenshots\wo4k-<date>-<time>.png in the mod folder, at display
 * resolution. The picture comes from DXGI desktop duplication, which sees
 * the game however Windows presents it; a plain copy of the screen can come
 * back black while the game's frames bypass the desktop compositor. Without
 * duplication the screen is copied, and if that is black the frame read back
 * from the game's own DirectDraw surface is saved: its internal 640x480
 * picture, before it is scaled to the display. The PNG's text chunks
 * say where it was taken: track, ship, game mode and the ship's and camera's
 * track coordinates. The frame is copied on the game's thread and encoded on
 * a worker, one screenshot at a time. Included after startup_art.c. */
#include "wo_version.h"
/* d3dtypes.h and dxgitype.h both define D3DCOLORVALUE; only the latter checks. */
#include <d3d.h>
#define D3DCOLORVALUE_DEFINED
#define COBJMACROS
#include <d3d11.h>
#include <dxgi1_2.h>
static const GUID shot_iid_dxgi_device={0x54ec77fa,0x1377,0x44e6,{0x8c,0x32,0x88,0xfd,0x5f,0x44,0xc8,0x4c}};
static const GUID shot_iid_output1={0x00cddea8,0x939b,0x4b83,{0xa3,0x40,0xa6,0x85,0x22,0x66,0x66,0xcc}};
static const GUID shot_iid_texture={0x6f15aaf2,0xd208,0x4e89,{0x9a,0xb4,0x48,0x95,0x35,0xd3,0x4f,0x9c}};
static const GUID shot_container_png={0x1b7cfaf4,0x713f,0x473c,{0xbb,0xcd,0x61,0x37,0x42,0x5f,0xae,0xaf}};
static const GUID shot_format_bgr24={0x6fddc324,0x4e03,0x4bfe,{0xb1,0x85,0x3d,0x77,0x76,0x8d,0xc9,0x0c}};
enum {SHOT_FIELDS=6,SHOT_TEXT=160};
static const WCHAR *const shot_field_paths[SHOT_FIELDS]={
    L"/[0]tEXt/{str=Description}",L"/[1]tEXt/{str=Track}",L"/[2]tEXt/{str=Ship}",
    L"/[3]tEXt/{str=Mode}",L"/[4]tEXt/{str=Position}",L"/[5]tEXt/{str=Software}"};
typedef struct Screenshot {
    BYTE *pixels;UINT width,height,stride; /* 24-bit BGR rows, top first */
    HBITMAP bitmap; /* owns pixels when they are a screen copy */
    WCHAR path[MAX_PATH];
    char fields[SHOT_FIELDS][SHOT_TEXT];
} Screenshot;
static Screenshot shot_job;
static volatile LONG shot_busy;
static BOOL shot_key_down;
static void shot_text(char *out,const char *text) {
    UINT n=0;while(out[n])++n;
    while(*text && n+1<SHOT_TEXT)out[n++]=*text++;out[n]=0;
}
static void shot_number(char *out,LONG value) {
    char reverse[12],text[13];UINT n=0,i=0;DWORD v=value<0?(DWORD)-value:(DWORD)value;
    do{reverse[n++]=(char)('0'+v%10);v/=10;}while(v);
    if(value<0)text[i++]='-';
    while(n)text[i++]=reverse[--n];text[i]=0;shot_text(out,text);
}
static void shot_vector(char *out,const char *name,const LONG *v) {
    shot_text(out,name);shot_text(out," ");shot_number(out,v[0]);shot_text(out,", ");
    shot_number(out,v[1]);shot_text(out,", ");shot_number(out,v[2]);
}
/* Track, ship, mode and coordinates, from the game's own state. */
static void shot_describe(Screenshot *shot) {
    static const char *const teams[5]={"AG Systems","Auricom","Qirex","FEISAR","Piranha"};
    static const char *const modes[4]={"Arcade","Time Trial","Challenge I","Challenge II"};
    for(UINT i=0;i<SHOT_FIELDS;++i)shot->fields[i][0]=0;
    char *all=shot->fields[0],*track=shot->fields[1],*ship=shot->fields[2],*mode=shot->fields[3],*position=shot->fields[4];
    shot_text(shot->fields[5],"WO4K " WO_VERSION_STRING);
    BYTE *race=*(BYTE **)(game+0x517958),*ships=*(BYTE **)(game+0x518030),*camera=*(BYTE **)(game+0x517bf0);
    int own=*(SHORT *)(game+0x664820);DWORD kind=*(DWORD *)(game+0x65c364);
    if(!*(DWORD *)(game+0x8c98c) || !race || !ships || own<0 || own>14) {
        shot_text(all,"Menus");return;
    }
    int number=(signed char)race[1];
    /* The game's own track-name lookup; it returns the name when not asked to draw it. */
    const char *name=number>=1 && number<=20?((const char *(__cdecl *)(int,int,int,int))(game+0x3bb50))(number,0,0,0):0;
    shot_text(track,name && *name?name:"Unknown");shot_text(track," (track ");shot_number(track,number);shot_text(track,")");
    shot_text(ship,teams[own/3]);shot_text(ship," (ship ");shot_number(ship,own);shot_text(ship,")");
    shot_text(mode,modes[kind<4?kind:3]);
    shot_vector(position,"ship",(const LONG *)(ships+own*240+0x40));
    if(camera){shot_text(position,"; ");shot_vector(position,"camera",(const LONG *)(camera+0x14));}
    shot_text(all,track);shot_text(all,"; ");shot_text(all,ship);shot_text(all,"; ");shot_text(all,mode);
    shot_text(all,"; ");shot_text(all,position);
}
static HRESULT shot_encode(const Screenshot *shot) {
    IWICImagingFactory *factory=0;IWICStream *stream=0;IWICBitmapEncoder *encoder=0;
    IWICBitmapFrameEncode *frame=0;IWICMetadataQueryWriter *writer=0;
    HRESULT initialized=CoInitializeEx(0,COINIT_MULTITHREADED);
    HRESULT hr=CoCreateInstance(&art_wic_class,0,CLSCTX_INPROC_SERVER,&art_wic_iid,(void **)&factory);
    if(hr>=0)hr=factory->lpVtbl->CreateStream(factory,&stream);
    if(hr>=0)hr=stream->lpVtbl->InitializeFromFilename(stream,shot->path,GENERIC_WRITE);
    if(hr>=0)hr=factory->lpVtbl->CreateEncoder(factory,&shot_container_png,0,&encoder);
    if(hr>=0)hr=encoder->lpVtbl->Initialize(encoder,(IStream *)stream,WICBitmapEncoderNoCache);
    if(hr>=0)hr=encoder->lpVtbl->CreateNewFrame(encoder,&frame,0);
    if(hr>=0)hr=frame->lpVtbl->Initialize(frame,0);
    if(hr>=0)hr=frame->lpVtbl->SetSize(frame,shot->width,shot->height);
    GUID format=shot_format_bgr24;
    if(hr>=0)hr=frame->lpVtbl->SetPixelFormat(frame,&format);
    if(hr>=0 && !equal((const BYTE *)&format,(const BYTE *)&shot_format_bgr24,sizeof(format)))hr=E_FAIL;
    /* Metadata is a courtesy: a refused text chunk never loses the picture. */
    if(hr>=0 && frame->lpVtbl->GetMetadataQueryWriter(frame,&writer)>=0) {
        UINT block=0;
        for(UINT i=0;i<SHOT_FIELDS;++i) {
            if(!shot->fields[i][0])continue;
            WCHAR path[48];UINT n=0;const WCHAR *p=shot_field_paths[i];
            while((path[n]=p[n]))++n;path[2]=(WCHAR)(L'0'+block);
            PROPVARIANT value;for(UINT b=0;b<sizeof(value);++b)((BYTE *)&value)[b]=0;
            value.vt=VT_LPSTR;value.pszVal=(char *)shot->fields[i];
            if(writer->lpVtbl->SetMetadataByName(writer,path,&value)>=0)++block;
        }
        writer->lpVtbl->Release(writer);
    }
    if(hr>=0)hr=frame->lpVtbl->WritePixels(frame,shot->height,shot->stride,shot->stride*shot->height,shot->pixels);
    if(hr>=0)hr=frame->lpVtbl->Commit(frame);
    if(hr>=0)hr=encoder->lpVtbl->Commit(encoder);
    if(frame)frame->lpVtbl->Release(frame);
    if(encoder)encoder->lpVtbl->Release(encoder);
    if(stream)stream->lpVtbl->Release(stream);
    if(factory)factory->lpVtbl->Release(factory);
    if(initialized>=0)CoUninitialize();
    return hr;
}
/* TRUE when every sampled pixel is black: Windows gave no picture. */
static BOOL shot_black(const BYTE *pixels,UINT width,UINT height,UINT stride) {
    for(UINT y=0;y<height;y+=height/64+1)for(UINT x=0;x<width;x+=width/64+1) {
        const BYTE *p=pixels+y*stride+x*3;if(p[0]>8 || p[1]>8 || p[2]>8)return FALSE;
    }
    return TRUE;
}
static void shot_free(Screenshot *shot) {
    if(shot->bitmap)DeleteObject(shot->bitmap);else if(shot->pixels)VirtualFree(shot->pixels,0,MEM_RELEASE);
    shot->bitmap=0;shot->pixels=0;
}
static BOOL shot_duplicate(Screenshot *shot);
static BOOL shot_screen(Screenshot *shot);
static DWORD WINAPI shot_worker(void *unused) {
    /* The job arrives holding the game's own frame; a display-resolution
     * picture replaces it when Windows provides one. */
    static Screenshot display;display.bitmap=0;display.pixels=0;
    UINT source=shot_duplicate(&display)?2:shot_screen(&display)?1:0;
    if(source) {
        shot_free(&shot_job);
        shot_job.pixels=display.pixels;shot_job.bitmap=display.bitmap;
        shot_job.width=display.width;shot_job.height=display.height;shot_job.stride=display.stride;
    }
    wo_render_event("screenshot_source",source);
    HRESULT hr=shot_encode(&shot_job);
    shot_free(&shot_job);
    if(hr<0)wo_render_event("screenshot_failed",(DWORD)hr);else wo_render_event("screenshot_saved",shot_job.width);
    shot_busy=0;
    return 0;
}
static UINT shot_shift(DWORD mask){UINT n=0;if(!mask)return 0;while(!(mask&1)){mask>>=1;++n;}return n;}
/* Copies a locked surface to 24-bit BGR, whatever its RGB layout. */
static BYTE *shot_copy(const DDSURFACEDESC *desc) {
    UINT bytes=desc->ddpfPixelFormat.dwRGBBitCount/8,w=desc->dwWidth,h=desc->dwHeight;
    if(!(desc->ddpfPixelFormat.dwFlags&DDPF_RGB) || bytes<2 || bytes>4 || !w || !h || w>16384 || h>16384 ||
       !desc->lpSurface || desc->lPitch<(LONG)(w*bytes))return 0;
    const DWORD masks[3]={desc->ddpfPixelFormat.dwBBitMask,desc->ddpfPixelFormat.dwGBitMask,desc->ddpfPixelFormat.dwRBitMask};
    static BYTE scale[3][256];UINT shift[3];DWORD top[3];
    for(UINT c=0;c<3;++c) {
        shift[c]=shot_shift(masks[c]);top[c]=masks[c]>>shift[c];
        if(!top[c] || top[c]>255)return 0;
        for(DWORD v=0;v<=top[c];++v)scale[c][v]=(BYTE)(v*255/top[c]);
    }
    BYTE *pixels=VirtualAlloc(0,w*h*3,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);if(!pixels)return 0;
    for(UINT y=0;y<h;++y) {
        const BYTE *row=(const BYTE *)desc->lpSurface+(LONG_PTR)y*desc->lPitch;BYTE *out=pixels+y*w*3;
        for(UINT x=0;x<w;++x,row+=bytes,out+=3) {
            DWORD p=bytes==2?*(const WORD *)row:bytes==4?*(const DWORD *)row:(DWORD)(row[0]|row[1]<<8|row[2]<<16);
            for(UINT c=0;c<3;++c)out[c]=scale[c][(p&masks[c])>>shift[c]];
        }
    }
    return pixels;
}
static BOOL shot_path(WCHAR *path) {
    /* <game>\..\screenshots\wo4k-YYYYMMDD-HHMMSS-mmm.png, in UTC like the logs. */
    path[0]=0;
    if(!movie_root[0] || !movie_append(path,MAX_PATH,movie_root) || !movie_append(path,MAX_PATH,L"..\\screenshots"))return FALSE;
    CreateDirectoryW(path,0);
    FILETIME now;SYSTEMTIME t;GetSystemTimeAsFileTime(&now);if(!FileTimeToSystemTime(&now,&t))return FALSE;
    WCHAR name[40];UINT n=0;const WCHAR *prefix=L"\\wo4k-";while(*prefix)name[n++]=*prefix++;
    const UINT parts[7]={t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond,t.wMilliseconds};
    const UINT digits[7]={4,2,2,2,2,2,3};
    for(UINT i=0;i<7;++i) {
        if(i==3 || i==6)name[n++]=L'-';
        UINT v=parts[i],d=digits[i];for(UINT k=d;k>0;--k){name[n+k-1]=(WCHAR)(L'0'+v%10);v/=10;}n+=d;
    }
    const WCHAR *extension=L".png";while(*extension)name[n++]=*extension++;name[n]=0;
    return movie_append(path,MAX_PATH,name);
}
/* The game's own window keeps the size of the game's 640x480 mode while
 * DDrawCompat presents it over the whole display: its position says which
 * display, and that whole display is the picture. */
static BOOL shot_window(RECT *r) {
    HWND window=*(HWND *)(game+0xaf60c);
    return window && GetWindowRect(window,r);
}
/* The game's display, from DXGI desktop duplication. */
static BOOL shot_duplicate(Screenshot *shot) {
    typedef HRESULT (WINAPI *CreateDeviceFn)(IDXGIAdapter *,int,HMODULE,UINT,const void *,UINT,UINT,ID3D11Device **,void *,ID3D11DeviceContext **);
    RECT window;if(!shot_window(&window))return FALSE;
    HMODULE library=LoadLibraryW(L"d3d11.dll");
    CreateDeviceFn create=library?(CreateDeviceFn)(void *)GetProcAddress(library,"D3D11CreateDevice"):0;
    if(!create)return FALSE;
    ID3D11Device *device=0;ID3D11DeviceContext *context=0;IDXGIDevice *dxgi=0;IDXGIAdapter *adapter=0;
    IDXGIOutput1 *output1=0;IDXGIOutputDuplication *duplication=0;IDXGIResource *resource=0;
    ID3D11Texture2D *frame=0,*staging=0;BOOL acquired=FALSE,ok=FALSE;
    HRESULT hr=create(0,D3D_DRIVER_TYPE_HARDWARE,0,0,0,0,D3D11_SDK_VERSION,&device,0,&context);
    if(hr>=0)hr=ID3D11Device_QueryInterface(device,&shot_iid_dxgi_device,(void **)&dxgi);
    if(hr>=0)hr=IDXGIDevice_GetAdapter(dxgi,&adapter);
    LONG x=(window.left+window.right)/2,y=(window.top+window.bottom)/2;
    for(UINT i=0;hr>=0 && !output1;++i) {
        IDXGIOutput *output=0;DXGI_OUTPUT_DESC desc;
        hr=IDXGIAdapter_EnumOutputs(adapter,i,&output);if(hr<0)break;
        if(IDXGIOutput_GetDesc(output,&desc)>=0 && x>=desc.DesktopCoordinates.left && x<desc.DesktopCoordinates.right &&
           y>=desc.DesktopCoordinates.top && y<desc.DesktopCoordinates.bottom) {

            hr=IDXGIOutput_QueryInterface(output,&shot_iid_output1,(void **)&output1);
        }
        IDXGIOutput_Release(output);
    }
    if(hr>=0 && output1)hr=IDXGIOutput1_DuplicateOutput(output1,(IUnknown *)device,&duplication);else if(hr>=0)hr=E_FAIL;
    /* The first frame handed over is the whole desktop as it is now. */
    DXGI_OUTDUPL_FRAME_INFO info;
    if(hr>=0)hr=IDXGIOutputDuplication_AcquireNextFrame(duplication,500,&info,&resource);
    acquired=hr>=0;
    if(hr>=0)hr=IDXGIResource_QueryInterface(resource,&shot_iid_texture,(void **)&frame);
    D3D11_TEXTURE2D_DESC desc;
    if(hr>=0) {
        ID3D11Texture2D_GetDesc(frame,&desc);
        if(desc.Format!=DXGI_FORMAT_B8G8R8A8_UNORM)hr=E_FAIL; /* an HDR desktop is left to the screen copy */
    }
    if(hr>=0) {
        desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;desc.MiscFlags=0;
        desc.MipLevels=1;desc.ArraySize=1;desc.SampleDesc.Count=1;desc.SampleDesc.Quality=0;
        hr=ID3D11Device_CreateTexture2D(device,&desc,0,&staging);
    }
    D3D11_MAPPED_SUBRESOURCE mapped;
    if(hr>=0) {
        ID3D11DeviceContext_CopyResource(context,(ID3D11Resource *)staging,(ID3D11Resource *)frame);
        hr=ID3D11DeviceContext_Map(context,(ID3D11Resource *)staging,0,D3D11_MAP_READ,0,&mapped);
    }
    if(hr>=0) {
        LONG left=0,top=0;UINT width=desc.Width,height=desc.Height,stride=width*3;
        BYTE *pixels=width && height?VirtualAlloc(0,stride*height,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE):0;
        if(pixels) {
            for(UINT row=0;row<height;++row) {
                const BYTE *in=(const BYTE *)mapped.pData+(top+row)*mapped.RowPitch+left*4;BYTE *out=pixels+row*stride;
                for(UINT col=0;col<width;++col,in+=4,out+=3){out[0]=in[0];out[1]=in[1];out[2]=in[2];}
            }
            if(shot_black(pixels,width,height,stride))VirtualFree(pixels,0,MEM_RELEASE);
            else {shot->bitmap=0;shot->pixels=pixels;shot->width=width;shot->height=height;shot->stride=stride;ok=TRUE;}
        }
        ID3D11DeviceContext_Unmap(context,(ID3D11Resource *)staging,0);
    }
    if(!ok)wo_render_event("screenshot_duplication_failed",(DWORD)hr);
    if(staging)ID3D11Texture2D_Release(staging);
    if(frame)ID3D11Texture2D_Release(frame);
    if(resource)IDXGIResource_Release(resource);
    if(acquired)IDXGIOutputDuplication_ReleaseFrame(duplication);
    if(duplication)IDXGIOutputDuplication_Release(duplication);
    if(output1)IDXGIOutput1_Release(output1);
    if(adapter)IDXGIAdapter_Release(adapter);
    if(dxgi)IDXGIDevice_Release(dxgi);
    if(context)ID3D11DeviceContext_Release(context);
    if(device)ID3D11Device_Release(device);
    return ok;
}
/* The primary display, as a plain copy of the screen. */
static BOOL shot_screen(Screenshot *shot) {
    RECT r;r.left=r.top=0;r.right=GetSystemMetrics(SM_CXSCREEN);r.bottom=GetSystemMetrics(SM_CYSCREEN);
    if(r.right<320 || r.bottom<200 || r.right>16384 || r.bottom>16384)return FALSE;
    UINT width=(UINT)(r.right-r.left),height=(UINT)(r.bottom-r.top);
    BITMAPINFO info;for(UINT b=0;b<sizeof(info);++b)((BYTE *)&info)[b]=0;
    info.bmiHeader.biSize=sizeof(info.bmiHeader);info.bmiHeader.biWidth=(LONG)width;info.bmiHeader.biHeight=-(LONG)height;
    info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=24;
    HDC screen=GetDC(0),memory=screen?CreateCompatibleDC(screen):0;void *bits=0;
    HBITMAP bitmap=memory?CreateDIBSection(memory,&info,DIB_RGB_COLORS,&bits,0,0):0;BOOL ok=FALSE;
    if(bitmap && bits) {
        HGDIOBJ previous=SelectObject(memory,bitmap);
        ok=BitBlt(memory,0,0,(int)width,(int)height,screen,r.left,r.top,SRCCOPY);
        SelectObject(memory,previous);
    }
    if(memory)DeleteDC(memory);
    if(screen)ReleaseDC(0,screen);
    UINT stride=(width*3+3)&~3u;
    if(!ok || shot_black(bits,width,height,stride)){if(bitmap)DeleteObject(bitmap);return FALSE;}
    shot->bitmap=bitmap;shot->pixels=bits;shot->width=width;shot->height=height;shot->stride=stride;
    return TRUE;
}
/* The game's own frame: the primary holds the one just presented, the back
 * buffer the one before. */
static BOOL shot_surface(Screenshot *shot,HRESULT *hr) {
    IDirectDrawSurface *surfaces[2]={*(IDirectDrawSurface **)(game+0x8f8d8),*(IDirectDrawSurface **)(game+0xabd24)};
    DDSURFACEDESC desc;*hr=E_FAIL;
    for(UINT i=0;i<2;++i) {
        if(!surfaces[i])continue;
        for(UINT b=0;b<sizeof(desc);++b)((BYTE *)&desc)[b]=0;desc.dwSize=sizeof(desc);
        *hr=surfaces[i]->lpVtbl->Lock(surfaces[i],0,&desc,DDLOCK_WAIT|DDLOCK_READONLY,0);
        if(*hr<0)continue;
        BYTE *pixels=shot_copy(&desc);
        surfaces[i]->lpVtbl->Unlock(surfaces[i],0);
        if(!pixels)continue;
        shot->bitmap=0;shot->pixels=pixels;shot->width=desc.dwWidth;shot->height=desc.dwHeight;shot->stride=desc.dwWidth*3;
        return TRUE;
    }
    return FALSE;
}
static void shot_take(void) {
    if(shot_busy)return;
    HRESULT hr=E_FAIL;
    if(!shot_surface(&shot_job,&hr)){wo_render_event("screenshot_failed",(DWORD)hr);return;}
    shot_describe(&shot_job);
    HANDLE thread=shot_path(shot_job.path)?(shot_busy=1,CreateThread(0,0,shot_worker,0,0,0)):0;
    if(thread){CloseHandle(thread);return;}
    shot_busy=0;shot_free(&shot_job);wo_render_event("screenshot_failed",0);
}
/* After every present. PrintScreen sends no key-down message; its state is
 * polled, and only acted on while the game is the foreground program. */
static void screenshot_poll(void) {
    BOOL down=(GetAsyncKeyState(VK_SNAPSHOT)&0x8000) && (GetAsyncKeyState(VK_SHIFT)&0x8000);
    if(down && !shot_key_down) {
        DWORD foreground=0;GetWindowThreadProcessId(GetForegroundWindow(),&foreground);
        if(foreground==GetCurrentProcessId())shot_take();
    }
    shot_key_down=down;
}
