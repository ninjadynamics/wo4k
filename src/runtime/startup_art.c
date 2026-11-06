/* Full-resolution startup artwork, kept outside the legacy 640x480 PPM path. */
#include <ddraw.h>
typedef IDirectDrawSurface *(__cdecl *ArtLoadFn)(const char *,int);
typedef void (__cdecl *ArtFlushFn)(int);
typedef struct StartupArt {
    IDirectDrawSurface *surface;
    UINT width,height;
} StartupArt;
typedef struct ArtBlit {
    DWORD operation;
    IDirectDrawSurface *surface;
    RECT source,destination;
} ArtBlit;
typedef struct ArtLayout {RECT source,destination;} ArtLayout;
_Static_assert(sizeof(ArtBlit)==40,"Native blit record ABI");
static StartupArt startup_art[2];
static WCHAR startup_directory[MAX_PATH];
static ArtLoadFn old_startup_load;
static ArtFlushFn old_startup_flush;
static int startup_visible=-1;
static UINT startup_display_width,startup_display_height;
#define STARTUP_NOTICE_MIN_FRAMES 30
#define STARTUP_NOTICE_FRAMES 300 /* the game shows the notice for 300 frames */
static UINT startup_notice_frames;
static BOOL startup_notice_drawn,startup_notice_finished;
static LARGE_INTEGER startup_notice_first;

static ArtLayout startup_layout(UINT sw,UINT sh,LONG dw,LONG dh,UINT display_w,UINT display_h,BOOL notice) {
    ArtLayout r;
    r.source.left=r.source.top=0;r.source.right=(LONG)sw;r.source.bottom=(LONG)sh;
    r.destination.left=r.destination.top=0;r.destination.right=dw;r.destination.bottom=dh;
    if(!sw || !sh || dw<=0 || dh<=0 || !display_w || !display_h)return r;
    double aspect=(double)display_w/display_h,art_aspect=(double)sw/sh;
    if(notice && aspect<art_aspect) {
        LONG height=(LONG)(dh*aspect/art_aspect+.5);
        r.destination.top=(dh-height)/2;r.destination.bottom=r.destination.top+height;
    } else if(aspect>art_aspect) {
        LONG width=(LONG)(dw*art_aspect/aspect+.5);
        r.destination.left=(dw-width)/2;r.destination.right=r.destination.left+width;
    } else if(!notice) {
        LONG width=(LONG)(sh*aspect+.5);
        if(width>(LONG)sw)width=(LONG)sw;
        r.source.left=((LONG)sw-width)/2;r.source.right=r.source.left+width;
    }
    return r;
}
static BOOL startup_name(const char *path,const char *wanted) {
    const char *base=path;
    for(UINT i=0;path[i];++i)if(path[i]=='\\' || path[i]=='/')base=path+i+1;
    while(*base && *wanted) {
        char c=*base++;if(c>='A' && c<='Z')c+=32;
        if(c!=*wanted++)return FALSE;
    }
    return !*base && !*wanted;
}
static void startup_path(WCHAR *path,const WCHAR *name) {
    UINT n=0;while(startup_directory[n]) {path[n]=startup_directory[n];++n;}
    while((*((WCHAR *)path+n++)=*name++)) {}
}
static IDirectDrawSurface *startup_read_png(UINT index) {
    WCHAR path[MAX_PATH];startup_path(path,index?L"notice.png":L"title.png");
    UINT png_width=0,png_height=0;BYTE *pixels=read_art_png(path,&png_width,&png_height);
    if(!pixels)return 0;
    LONG width=(LONG)png_width,height=-(LONG)png_height;BOOL valid=TRUE;
    IDirectDrawSurface *surface=0;
    IDirectDraw *dd=*(IDirectDraw **)(game+0xc8b50);
    if(valid && dd) {
        DDSURFACEDESC desc={0};desc.dwSize=sizeof(desc);
        desc.dwFlags=DDSD_CAPS|DDSD_WIDTH|DDSD_HEIGHT|DDSD_PIXELFORMAT;
        desc.dwWidth=(DWORD)width;desc.dwHeight=(DWORD)(-height);
        desc.ddsCaps.dwCaps=DDSCAPS_OFFSCREENPLAIN|DDSCAPS_VIDEOMEMORY;
        desc.ddpfPixelFormat.dwSize=sizeof(desc.ddpfPixelFormat);desc.ddpfPixelFormat.dwFlags=DDPF_RGB;
        desc.ddpfPixelFormat.dwRGBBitCount=32;desc.ddpfPixelFormat.dwRBitMask=0x00ff0000;
        desc.ddpfPixelFormat.dwGBitMask=0x0000ff00;desc.ddpfPixelFormat.dwBBitMask=0x000000ff;
        HRESULT hr=dd->lpVtbl->CreateSurface(dd,&desc,&surface,0);
        if(hr<0) {
            desc.ddsCaps.dwCaps=DDSCAPS_OFFSCREENPLAIN|DDSCAPS_SYSTEMMEMORY;
            hr=dd->lpVtbl->CreateSurface(dd,&desc,&surface,0);
        }
        if(hr>=0 && surface) {
            DDSURFACEDESC locked={0};locked.dwSize=sizeof(locked);
            hr=surface->lpVtbl->Lock(surface,0,&locked,DDLOCK_WAIT|DDLOCK_WRITEONLY,0);
            if(hr>=0) {
                valid=locked.lpSurface && locked.lPitch>=width*4;
                if(valid)for(LONG y=0;y<-height;++y)
                    copy((BYTE *)locked.lpSurface+y*locked.lPitch,pixels+y*width*4,(UINT)width*4);
                surface->lpVtbl->Unlock(surface,0);
            } else valid=FALSE;
            if(!valid) {surface->lpVtbl->Release(surface);surface=0;}
        } else {wo_render_event("startup_art_create_failed",(DWORD)hr);surface=0;}
    }
    VirtualFree(pixels,0,MEM_RELEASE);
    if(surface) {
        startup_art[index].surface=surface;startup_art[index].width=(UINT)width;startup_art[index].height=(UINT)(-height);
        DWORD *info=(DWORD *)(game+0x4f57c0)+index*3;
        info[0]=(DWORD)width;info[1]=(DWORD)(-height);info[2]=1;
        wo_render_event(index?"startup_notice_width":"startup_title_width",(DWORD)width);
    }
    return surface;
}
static IDirectDrawSurface *__cdecl startup_load(const char *name,int index) {
    if(!old_startup_flush)return old_startup_load(name,index);
    if(index==0 || index==1) {
        startup_art[index].surface=0;
        if(startup_name(name,index?"woxlcopy.ppm":"rstartxl.ppm")) {
            wo_render_event("startup_art_requested",(DWORD)index);
            IDirectDrawSurface *surface=startup_read_png((UINT)index);
            if(surface)return surface;
            wo_render_event("startup_art_fallback",(DWORD)index);
        }
    }
    return old_startup_load(name,index);
}
static int startup_surface(const ArtBlit *entry) {
    if(entry->operation!=1 || !entry->surface)return -1;
    for(UINT i=0;i<2;++i) {
        DWORD *info=(DWORD *)(game+0x4f57c0)+i*3;
        if(entry->surface==startup_art[i].surface && info[2] && info[0]==startup_art[i].width &&
           info[1]==startup_art[i].height && entry->surface==((IDirectDrawSurface **)(game+0xc7b10))[i])return (int)i;
    }
    return -1;
}
static void startup_draw(IDirectDrawSurface *target,UINT index) {
    LONG width=(LONG)*(float *)(game+0xaf0b4),height=(LONG)*(float *)(game+0xc8630);
    /* DDrawCompat virtualizes these system metrics after SetDisplayMode.
     * Reuse the physical desktop size captured before the game's mode switch,
     * just as the widescreen camera does, rather than querying640x480 here. */
    UINT display_w=startup_display_width,display_h=startup_display_height;
    if(width<=0 || height<=0 || !display_w || !display_h)return;
    RECT full;full.left=full.top=0;full.right=width;full.bottom=height;
    /* Mark the enhanced render target current before the high-resolution GPU
     * blit. This clear applies ONLY to a queued full-screen startup image. */
    void *viewport=*(void **)(game+0xc8c88);
    if(viewport) {
        typedef HRESULT (__stdcall *ClearFn)(void *,DWORD,const RECT *,DWORD);
        ((ClearFn)(*(void ***)viewport)[12])(viewport,1,&full,1);
    }
    DDBLTFX fx={0};fx.dwSize=sizeof(fx);fx.dwFillColor=0;
    target->lpVtbl->Blt(target,&full,0,0,DDBLT_COLORFILL|DDBLT_WAIT,&fx);
    StartupArt *art=&startup_art[index];
    ArtLayout layout=startup_layout(art->width,art->height,width,height,display_w,display_h,index==1);
    HRESULT hr=target->lpVtbl->Blt(target,&layout.destination,art->surface,&layout.source,DDBLT_WAIT,0);
    if(hr<0)wo_render_event("startup_art_blt_failed",(DWORD)hr);
    else if(startup_visible!=(int)index) {startup_visible=(int)index;wo_render_event("startup_art_visible",index);}
    if(hr>=0 && index==1)startup_notice_drawn=TRUE;
}
/* Called for every presented frame. The splash has ended at the first
 * frame presented without the notice, once the notice has been on enough
 * frames to know the game redraws it every frame (it shows it for 300).
 * The game then loads its menus for about 100 ms without presenting, so
 * the prepared intro starts now and covers that instead of a black screen.
 * About 100 ms (at least 10 frames) before the last notice frame, the
 * player stages its window, which the compositor needs while frames are
 * still being presented. */
static void startup_present(void) {
    if(startup_notice_finished)return;
    if(startup_notice_drawn) {
        LARGE_INTEGER now;QueryPerformanceCounter(&now);
        if(!startup_notice_frames)startup_notice_first=now;
        ++startup_notice_frames;startup_notice_drawn=FALSE;
        if(startup_notice_frames>=STARTUP_NOTICE_MIN_FRAMES && refresh_frequency.QuadPart>0) {
            double interval=(double)(now.QuadPart-startup_notice_first.QuadPart)/
                (double)refresh_frequency.QuadPart/(double)(startup_notice_frames-1);
            UINT lead=interval>0?(UINT)(0.1/interval)+1:10;
            if(lead<10)lead=10;
            if(startup_notice_frames+lead>=STARTUP_NOTICE_FRAMES)stage_prepared_intro();
        }
        return;
    }
    if(startup_notice_frames<STARTUP_NOTICE_MIN_FRAMES)return;
    startup_notice_finished=TRUE;
    wo_render_event("startup_notice_finished",startup_notice_frames);
    start_prepared_intro();
}
static void __cdecl startup_flush(int layer) {
    if(layer<0 || layer>1) {old_startup_flush(layer);return;}
    DWORD *count=(DWORD *)(game+0x4f57b0)+layer;
    ArtBlit *entries=(ArtBlit *)(game+0x4e1f30)+(UINT)layer*1000;
    BOOL found=FALSE;
    if(*count<=1000)for(UINT i=0;i<*count;++i)if(startup_surface(entries+i)>=0)found=TRUE;
    if(!found) {old_startup_flush(layer);return;}
    IDirectDrawSurface *target=*(IDirectDrawSurface **)(game+0xabd24);
    if(!target) {old_startup_flush(layer);return;}
    DDBLTFX fx={0};fx.dwSize=sizeof(fx);
    for(UINT i=*count;i>0;--i) {
        ArtBlit *entry=entries+i-1;int art=startup_surface(entry);
        if(art>=0)startup_draw(target,(UINT)art);
        else if(entry->operation==1)
            target->lpVtbl->Blt(target,&entry->destination,entry->surface,&entry->source,DDBLT_KEYSRC|DDBLT_ASYNC,&fx);
        else if(entry->operation==2) {
            fx.dwFillColor=(DWORD)entry->surface;
            target->lpVtbl->Blt(target,&entry->source,0,0,DDBLT_COLORFILL|DDBLT_ASYNC,&fx);
        }
    }
    *count=0;
}
static void initialize_startup_art(const WCHAR *root,UINT display_width,UINT display_height) {
    WCHAR hd[4];if(GetEnvironmentVariableW(L"WO2097_HD_ART",hd,4) && hd[0]==L'0')return;
    if(!display_width || !display_height)return;
    startup_display_width=display_width;startup_display_height=display_height;
    if(!equal(game+RVA_startup_load,entry_startup_load,sizeof(entry_startup_load)) ||
       !equal(game+RVA_startup_flush,entry_startup_flush,sizeof(entry_startup_flush)))return;
    UINT n=0;while(root[n] && n<MAX_PATH-40) {startup_directory[n]=root[n];++n;}
    if(root[n])return;
    const WCHAR *suffix=L"..\\assets\\hd\\";
    while((*((WCHAR *)startup_directory+n++)=*suffix++)) {}
    old_startup_load=(ArtLoadFn)hook(RVA_startup_load,entry_startup_load,sizeof(entry_startup_load),startup_load);
    old_startup_flush=(ArtFlushFn)hook(RVA_startup_flush,entry_startup_flush,sizeof(entry_startup_flush),startup_flush);
    wo_render_event("startup_physical_width",display_width);
    wo_render_event("startup_physical_height",display_height);
    wo_render_event("startup_art_patch",old_startup_load && old_startup_flush);
}
