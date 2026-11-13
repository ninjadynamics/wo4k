/* The PlayStation's large trackside boards advertise by turns. Each track
 * with such boards has a file of two pictures, a logo and a slogan, and at
 * every frame the game's board routine counts: every twentieth count it
 * moves on to the other picture and lays a quarter of it on each panel of
 * every board. The PC game kept the routine and the call, with the twenty
 * and the two still passed to it, but shows one picture and never counts.
 *
 * The installed scenery has the logo on the track's sheet, where a board
 * was chosen to show it. Where the track's archive holds that picture, its
 * place on the texture page is painted with the slogan and with the logo
 * again, twenty simulation steps each: the steps come thirty to the second,
 * as the PlayStation's frames did. The slogan is stretched over the logo's
 * place, as the PlayStation's panels stretch it over the board.
 *
 * The logo is whatever the page shows once its replacements are composed.
 * The slogan is the picture named after the disc's (as the debanded layer
 * names it: "textures-nured-cmp-1.png") in the authored folder or the
 * upscaled one; else the disc's own, with the debanded picture's colours
 * where there is one. */
typedef void (__cdecl *BoardAdvertFn)(void);
static BoardAdvertFn old_board_step;
enum {BOARD_ADVERT_STEPS=20};
/* live: on a page of ours. pictures: the logo and the slogan as the page's
 * pixels, `scale` to a texel. slogan: the disc's, a pixel to a texel. */
typedef struct BoardAdvert {
    BOOL live;UINT slot,x,y,width,height,scale,steps,shown,slogan_width,slogan_height;
    BYTE *slogan,*pictures[2];WCHAR png[48];
} BoardAdvert;
static BoardAdvert board_advert;
/* The advert files are named after the track's number as a Greek letter. */
static const WCHAR *board_advert_letter(UINT track) {
    static const struct {BYTE track;const WCHAR *letter;} letters[]={{1,L"alpha"},{2,L"beta"},{6,L"zeta"},{7,L"eta"},
        {8,L"theta"},{13,L"nu"},{17,L"rho"},{20,L"upsil"}};
    for(UINT i=0;i<sizeof(letters)/sizeof(letters[0]);++i)if(letters[i].track==track)return letters[i].letter;
    return 0;
}
static void board_advert_end(void) {
    if(board_advert.slogan)VirtualFree(board_advert.slogan,0,MEM_RELEASE);
    for(UINT i=0;i<2;++i)if(board_advert.pictures[i])VirtualFree(board_advert.pictures[i],0,MEM_RELEASE);
    for(UINT i=0;i<sizeof(board_advert);++i)((BYTE *)&board_advert)[i]=0;
}
/* A picture of sixteen colours with its palette: its place in video memory
 * and its size and, where there is room for them, its pixels. Like the
 * sheets, colour 0 is see-through. */
static BOOL board_advert_picture(const BYTE *picture,UINT bytes,UINT *x,UINT *y,UINT *width,UINT *height,BYTE *pixels) {
    if(bytes<8+12+32+12 || *(const DWORD *)picture!=0x10 || (*(const DWORD *)(picture+4)&15)!=8)return FALSE;
    UINT colours=*(const DWORD *)(picture+8);
    if(colours<12+32 || colours>bytes-8-12)return FALSE;
    const BYTE *image=picture+8+colours;
    UINT block=*(const DWORD *)image,words=*(const WORD *)(image+8),rows=*(const WORD *)(image+10);
    if(!words || !rows || words>64 || rows>256 || block<12 || block>bytes-8-colours || words*2*rows>block-12)return FALSE;
    *x=*(const WORD *)(image+4);*y=*(const WORD *)(image+6);*width=words*4;*height=rows;
    for(UINT i=0;pixels && i<words*4*rows;++i) {
        UINT c=*(const WORD *)(picture+20+(image[12+i/2]>>(i&1)*4&15)*2);
        ((DWORD *)pixels)[i]=((c&0x7fff)?0xff000000u:0)|((c&31)*255/31)<<16|((c>>5&31)*255/31)<<8|((c>>10&31)*255/31);
    }
    return TRUE;
}
/* The track's advert, if its scenery has the logo among its pictures: the
 * logo's place among the texture pages (as the waterfall's is reckoned) and
 * the slogan. */
static BOOL board_advert_find(const BYTE *archive,UINT length,const BYTE *adverts,UINT adverts_length) {
    UINT size=0;const BYTE *packed=palette_cycle_file(archive,length,"scene.cmp",&size);
    BYTE *theirs=packed?palette_cycle_unpack(adverts,adverts_length):0;
    BYTE *scene=theirs?palette_cycle_unpack(packed,size):0;
    BOOL found=FALSE;
    if(scene && *(const DWORD *)adverts>=2) {
        UINT logo=*(const DWORD *)(adverts+4),slogan=*(const DWORD *)(adverts+8),at=0,x,y,w,h,sx,sy,sw,sh;
        for(UINT i=0;!found && i<*(const DWORD *)packed;++i) {
            UINT bytes=*(const DWORD *)(packed+4+i*4);
            found=bytes==logo && equal(scene+at,theirs,logo);at+=bytes;
        }
        found=found && board_advert_picture(theirs,logo,&x,&y,&w,&h,0) && x>=320 && x<1024 && (x-320)%64*4+w<=256 && y+h<=256 &&
            board_advert_picture(theirs+logo,slogan,&sx,&sy,&sw,&sh,0);
        if(found) {
            board_advert.slogan=VirtualAlloc(0,sw*sh*4,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
            found=board_advert.slogan && board_advert_picture(theirs+logo,slogan,&sx,&sy,&sw,&sh,board_advert.slogan);
            board_advert.slot=(x-320)/64;board_advert.x=(x-320)%64*4;board_advert.y=y;
            board_advert.width=w;board_advert.height=h;board_advert.slogan_width=sw;board_advert.slogan_height=sh;
        }
    }
    if(scene)VirtualFree(scene,0,MEM_RELEASE);
    if(theirs)VirtualFree(theirs,0,MEM_RELEASE);
    return found;
}
/* The track's scenery sheets, which the installer makes: "NRACE03.TGA". */
static BOOL board_advert_sheet(const char *filename) {
    const char *base=filename,*wanted="nrace";
    for(UINT i=0;filename[i];++i)if(filename[i]=='\\' || filename[i]=='/')base=filename+i+1;
    for(;*wanted;++wanted,++base)if((*base|32)!=*wanted)return FALSE;
    return TRUE;
}
/* A sheet loading over the advert's page ends what was painted there. The
 * scenery sheet of a track with an advert begins it: the page of that sheet
 * to paint, or none. */
static int board_advert_claim(const char *filename,UINT first,UINT count) {
    if(board_advert.live && board_advert.slot>=first && board_advert.slot-first<count)board_advert_end();
    if(!board_advert_sheet(filename))return -1;
    board_advert_end();
    const BYTE *config=*(const BYTE *const *)(game+0x517958);
    const WCHAR *letter=config?board_advert_letter(config[1]):0;if(!letter)return -1;
    WCHAR name[80];name[0]=0;
    if(!hd_append(name,80,L"textures\\ps1-source\\TEXTURES\\") || !hd_append(name,80,letter) || !hd_append(name,80,L"red.cmp"))return -1;
    UINT length=0,adverts_length=0;BYTE *adverts=hd_game_file(name,&adverts_length);
    BYTE *archive=adverts?palette_cycle_archive(config[1],&length):0;
    BOOL found=archive && board_advert_find(archive,length,adverts,adverts_length) &&
        hd_append(board_advert.png,48,L"textures-") && hd_append(board_advert.png,48,letter) && hd_append(board_advert.png,48,L"red-cmp-1.png");
    if(archive)VirtualFree(archive,0,MEM_RELEASE);
    if(adverts)VirtualFree(adverts,0,MEM_RELEASE);
    wo_render_event("board_advert_found",found);
    if(!found || board_advert.slot<first || board_advert.slot-first>=count){board_advert_end();return -1;}
    return (int)(board_advert.slot-first);
}
/* The slogan as the page shows it: a picture of its name from the authored
 * or the upscaled folder as it is; else the disc's, coloured by the debanded
 * picture where both show a texel. */
static void board_advert_slogan(BYTE *out,UINT width,UINT height) {
    WCHAR path[MAX_PATH];UINT w=0,h=0;BYTE *png=0;
    const BYTE *source=board_advert.slogan;UINT sw=board_advert.slogan_width,sh=board_advert.slogan_height;
    if(hd_path(path,board_advert.png) && hd_exists(path))png=read_art_png(path,&w,&h);
    if(png){source=png;sw=w;sh=h;}
    else {
        path[0]=0;
        if(hd_append(path,MAX_PATH,ui_sd_directory) && hd_append(path,MAX_PATH,board_advert.png) && hd_exists(path))png=read_art_png(path,&w,&h);
        if(png && w==sw && h==sh)for(UINT i=0;i<sw*sh;++i)if(board_advert.slogan[i*4+3] && png[i*4+3])copy(board_advert.slogan+i*4,png+i*4,3);
    }
    for(UINT y=0;y<height;++y)for(UINT x=0;x<width;++x)copy(out+(y*width+x)*4,source+((y*sh/height)*sw+x*sw/width)*4,4);
    if(png)VirtualFree(png,0,MEM_RELEASE);
}
/* The page of pixels the loader is about to upload: the logo is kept as it
 * shows there, and the slogan made to its size. */
static BYTE *board_advert_page(const char *filename,UINT page,UINT pages,BYTE *pixels,UINT *width) {
    if(!pixels){pixels=hd_native_page(filename,page,pages,256);*width=256;}
    UINT scale=*width/256,w=board_advert.width*scale,h=board_advert.height*scale;
    for(UINT i=0;pixels && i<2;++i)board_advert.pictures[i]=VirtualAlloc(0,w*h*4,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    if(!pixels || !board_advert.pictures[0] || !board_advert.pictures[1]){board_advert_end();return pixels;}
    for(UINT y=0;y<h;++y)copy(board_advert.pictures[0]+y*w*4,pixels+((board_advert.y*scale+y)**width+board_advert.x*scale)*4,w*4);
    board_advert_slogan(board_advert.pictures[1],w,h);
    board_advert.scale=scale;board_advert.steps=board_advert.shown=0;board_advert.live=TRUE;
    wo_render_event("board_advert_page",board_advert.slot);
    return pixels;
}
static BOOL board_advert_write(IDirectDrawSurface *surface,const BYTE *picture,DWORD solid) {
    UINT scale=board_advert.scale,w=board_advert.width*scale,h=board_advert.height*scale;
    RECT area={(LONG)(board_advert.x*scale),(LONG)(board_advert.y*scale),(LONG)(board_advert.x*scale+w),(LONG)(board_advert.y*scale+h)};
    DDSURFACEDESC lock={0};lock.dwSize=sizeof(lock);
    HRESULT hr=surface->lpVtbl->Lock(surface,&area,&lock,DDLOCK_WAIT|DDLOCK_WRITEONLY,0);
    if(hr>=0) {
        if(lock.lpSurface && lock.lPitch>=(LONG)(w*4)) {
            for(UINT y=0;y<h;++y) {
                DWORD *out=(DWORD *)((BYTE *)lock.lpSurface+y*lock.lPitch);const DWORD *in=(const DWORD *)(picture+y*w*4);
                for(UINT x=0;x<w;++x)out[x]=in[x]|solid;
            }
        }
        else hr=E_FAIL;
        HRESULT unlock=surface->lpVtbl->Unlock(surface,lock.lpSurface);
        if(hr>=0)hr=unlock;
    }
    if(hr<0)wo_render_event("board_advert_error",(DWORD)hr);
    return hr>=0;
}
static void __cdecl board_advert_step(void) {
    old_board_step();
    if(board_advert.live)++board_advert.steps;
}
/* Once a frame at most, after it is presented, and only when the picture
 * changes. */
static void board_advert_present(void) {
    if(!board_advert.live)return;
    UINT wanted=board_advert.steps/BOARD_ADVERT_STEPS&1;if(wanted==board_advert.shown)return;
    board_advert.shown=wanted;
    UiHdPage *page=ui_hd_pages+board_advert.slot,*opaque=ui_hd_opaque_pages+board_advert.slot;
    const BYTE *picture=board_advert.pictures[wanted];
    if(!page->surface || page->width!=board_advert.scale*256 || !board_advert_write(page->surface,picture,0) ||
       (opaque->surface && (opaque->width!=page->width || !board_advert_write(opaque->surface,picture,0xff000000u))))
        board_advert.live=FALSE;
}
/* The game's scenery routine for the track, once every simulation step. */
static void initialize_board_adverts(void) {
    static const BYTE entry[]={0xa1,0x58,0x79,0x91,0x00};
    old_board_step=(BoardAdvertFn)hook(0x2bfc0,entry,sizeof(entry),board_advert_step);
    wo_render_event("board_advert_hook",old_board_step!=0);
}
