/* In-game movies: with WO2097_VIDEO_IN_GAME=1 the player decodes into shared
 * memory (--frames) and the movie loop presents each new frame through the
 * game's own surfaces, the way the HD startup art is drawn: exclusive
 * fullscreen shows no other window. Included after startup_art.c. */
#include "../common/wo_video_frames.h"
static HANDLE movie_frames_mapping;
static const WoVideoFrames *movie_frames_view;
static DWORD movie_frames_player;
static LONG movie_frames_shown;
static DWORD movie_frames_presented;
static IDirectDrawSurface *movie_frames_surface;
static UINT movie_frames_width,movie_frames_height;
/* The header as validated at open: sizes are never re-read from the view. */
static WoVideoFrames movie_frames_format;
static void movie_frames_close(void) {
    if(movie_frames_presented)wo_render_event("movie_frames_presented",movie_frames_presented);
    movie_frames_presented=0;
    if(movie_frames_surface){movie_frames_surface->lpVtbl->Release(movie_frames_surface);movie_frames_surface=0;}
    if(movie_frames_view){UnmapViewOfFile((void *)movie_frames_view);movie_frames_view=0;}
    if(movie_frames_mapping){CloseHandle(movie_frames_mapping);movie_frames_mapping=0;}
    movie_frames_player=0;movie_frames_shown=0;
}
/* The player creates the mapping once it knows the frame size. */
static BOOL movie_frames_open(DWORD player) {
    if(movie_frames_view && movie_frames_player==player)return TRUE;
    movie_frames_close();
    WCHAR name[64];name[0]=0;
    if(!movie_append(name,64,WO_VIDEO_FRAMES_NAME) || !movie_append_number(name,64,player))return FALSE;
    HANDLE mapping=OpenFileMappingW(FILE_MAP_READ,FALSE,name);if(!mapping)return FALSE;
    const WoVideoFrames *view=MapViewOfFile(mapping,FILE_MAP_READ,0,0,0);
    MEMORY_BASIC_INFORMATION region;WoVideoFrames *f=&movie_frames_format;
    if(view)copy((BYTE *)f,(const BYTE *)view,sizeof(*f));
    BOOL valid=view && VirtualQuery(view,&region,sizeof(region)) && f->magic==WO_VIDEO_FRAMES_MAGIC &&
        f->header_size==WO_VIDEO_FRAMES_HEADER && f->width && f->height &&
        f->width<=WO_VIDEO_FRAMES_MAX_WIDTH && f->height<=WO_VIDEO_FRAMES_MAX_HEIGHT &&
        f->stride>=f->width*4 && f->stride<=WO_VIDEO_FRAMES_MAX_WIDTH*4 && f->slot_size==f->stride*f->height &&
        region.RegionSize>=WO_VIDEO_FRAMES_HEADER+(SIZE_T)WO_VIDEO_FRAMES_SLOTS*f->slot_size;
    if(!valid){if(view)UnmapViewOfFile((void *)view);CloseHandle(mapping);return FALSE;}
    movie_frames_mapping=mapping;movie_frames_view=view;movie_frames_player=player;
    wo_render_event("movie_frames_width",f->width);wo_render_event("movie_frames_height",f->height);
    return TRUE;
}
static BOOL movie_frames_surface_ready(UINT width,UINT height) {
    if(movie_frames_surface && movie_frames_width==width && movie_frames_height==height)return TRUE;
    if(movie_frames_surface){movie_frames_surface->lpVtbl->Release(movie_frames_surface);movie_frames_surface=0;}
    IDirectDraw *dd=*(IDirectDraw **)(game+0xc8b50);if(!dd)return FALSE;
    DDSURFACEDESC desc={0};desc.dwSize=sizeof(desc);
    desc.dwFlags=DDSD_CAPS|DDSD_WIDTH|DDSD_HEIGHT|DDSD_PIXELFORMAT;desc.dwWidth=width;desc.dwHeight=height;
    desc.ddsCaps.dwCaps=DDSCAPS_OFFSCREENPLAIN|DDSCAPS_VIDEOMEMORY;
    desc.ddpfPixelFormat.dwSize=sizeof(desc.ddpfPixelFormat);desc.ddpfPixelFormat.dwFlags=DDPF_RGB;
    desc.ddpfPixelFormat.dwRGBBitCount=32;desc.ddpfPixelFormat.dwRBitMask=0x00ff0000;
    desc.ddpfPixelFormat.dwGBitMask=0x0000ff00;desc.ddpfPixelFormat.dwBBitMask=0x000000ff;
    HRESULT hr=dd->lpVtbl->CreateSurface(dd,&desc,&movie_frames_surface,0);
    if(hr<0){
        desc.ddsCaps.dwCaps=DDSCAPS_OFFSCREENPLAIN|DDSCAPS_SYSTEMMEMORY;
        hr=dd->lpVtbl->CreateSurface(dd,&desc,&movie_frames_surface,0);
    }
    if(hr<0){movie_frames_surface=0;wo_render_event("movie_frames_surface_failed",(DWORD)hr);return FALSE;}
    movie_frames_width=width;movie_frames_height=height;return TRUE;
}
/* Copies the newest frame, if it is new and was not rewritten meanwhile. */
static BOOL movie_frames_copy(const WoVideoFrames *view,LONG slot) {
    LONG before=view->sequence[slot];if(before&1)return FALSE;
    DDSURFACEDESC locked={0};locked.dwSize=sizeof(locked);
    HRESULT hr=movie_frames_surface->lpVtbl->Lock(movie_frames_surface,0,&locked,DDLOCK_WAIT|DDLOCK_WRITEONLY,0);
    if(hr==DDERR_SURFACELOST && movie_frames_surface->lpVtbl->Restore(movie_frames_surface)>=0)
        hr=movie_frames_surface->lpVtbl->Lock(movie_frames_surface,0,&locked,DDLOCK_WAIT|DDLOCK_WRITEONLY,0);
    if(hr<0)return FALSE;
    const WoVideoFrames *f=&movie_frames_format;
    const BYTE *source=(const BYTE *)view+WO_VIDEO_FRAMES_HEADER+(SIZE_T)slot*f->slot_size;
    BOOL fits=locked.lpSurface && locked.lPitch>=(LONG)(f->width*4);
    if(fits)for(UINT y=0;y<f->height;++y)
        copy((BYTE *)locked.lpSurface+y*locked.lPitch,source+(SIZE_T)y*f->stride,f->width*4);
    movie_frames_surface->lpVtbl->Unlock(movie_frames_surface,0);
    __asm__ __volatile__("":::"memory"); /* the copy reads finish before the recheck */
    return fits && view->sequence[slot]==before;
}
/* Called by the movie loop while the player runs; presents each new frame. */
static void movie_frames_present(DWORD player) {
    if(!player || !movie_frames_open(player))return;
    const WoVideoFrames *view=movie_frames_view;
    LONG count=view->frames,slot=view->latest;
    if(count==movie_frames_shown || slot<0 || slot>=WO_VIDEO_FRAMES_SLOTS)return;
    const WoVideoFrames *f=&movie_frames_format;
    if(!movie_frames_surface_ready(f->width,f->height) || !movie_frames_copy(view,slot))return;
    if(!movie_frames_shown)wo_render_event("movie_frames_first",(DWORD)count);
    movie_frames_shown=count;
    IDirectDrawSurface *target=*(IDirectDrawSurface **)(game+0xabd24);
    IDirectDrawSurface *primary=*(IDirectDrawSurface **)(game+0x8f8d8);
    LONG width=(LONG)*(float *)(game+0xaf0b4),height=(LONG)*(float *)(game+0xc8630);
    if(!target || !primary || width<=0 || height<=0)return;
    RECT full;full.left=full.top=0;full.right=width;full.bottom=height;
    /* As for the startup art: make the enhanced render target current, so
     * the frame is scaled up on the GPU at display resolution. */
    void *viewport=*(void **)(game+0xc8c88);
    if(viewport) {
        typedef HRESULT (__stdcall *ClearFn)(void *,DWORD,const RECT *,DWORD);
        ((ClearFn)(*(void ***)viewport)[12])(viewport,1,&full,1);
    }
    DDBLTFX fx={0};fx.dwSize=sizeof(fx);fx.dwFillColor=0;
    target->lpVtbl->Blt(target,&full,0,0,DDBLT_COLORFILL|DDBLT_WAIT,&fx);
    ArtLayout layout=startup_layout(f->width,f->height,width,height,movie_display_width,movie_display_height,TRUE);
    HRESULT hr=target->lpVtbl->Blt(target,&layout.destination,movie_frames_surface,&layout.source,DDBLT_WAIT,0);
    if(hr<0){wo_render_event("movie_frames_blt_failed",(DWORD)hr);return;}
    hr=primary->lpVtbl->Flip(primary,0,DDFLIP_WAIT);
    if(hr==DDERR_SURFACELOST)primary->lpVtbl->Restore(primary);
    else if(hr<0)wo_render_event("movie_frames_flip_failed",(DWORD)hr);
    else ++movie_frames_presented;
}
