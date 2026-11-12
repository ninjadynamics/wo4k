/* Measured: QI for IDirect3DDevice2 on the game's V1 HAL device fails with
 * E_INVALIDARG, through system DirectDraw and DDrawCompat alike. */
static HRESULT WINAPI fixture_v1_query(IDirect3DDevice *device,REFIID iid,void **out){
    (void)device;(void)iid;*out=0;return E_INVALIDARG;
}
static void test_hd_world_packets(void){
    static BYTE stream[96],rewritten[2048];UINT n=0;
    ui_hd_pages[100].handle=901;ui_hd_pages[100].world=TRUE;ui_hd_opaque_pages[100].handle=902;
    for(UINT keyed=0;keyed<2;++keyed){
        /* Bind a world page; native blend/depth/texture modulation must survive. */
        n=0;stream[n++]=8;stream[n++]=8;*(WORD *)(stream+n)=6;n+=2;
        const UINT state[6]={1,41,27,19,20,21},value[6]={901,keyed,1,5,6,2};
        for(UINT i=0;i<6;++i){*(DWORD *)(stream+n)=state[i];*(DWORD *)(stream+n+4)=value[i];n+=8;}
        stream[n++]=3;stream[n++]=8;*(WORD *)(stream+n)=1;n+=2;
        *(WORD *)(stream+n)=0;*(WORD *)(stream+n+2)=1;*(WORD *)(stream+n+4)=2;*(WORD *)(stream+n+6)=0;n+=8;
        stream[n++]=11;stream[n++]=0;*(WORD *)(stream+n)=0;n+=2;
        TLVertex vertices[3]={{.color=0x80402010},{.color=0xff123456},{.color=0xff123456}};
        UINT count=3;if(!ui_hd_prepare(stream,n,vertices,&count,3) || count!=3 || vertices[0].color!=0x80402010)ExitProcess(210);
        UINT bytes=ui_hd_rewrite(stream,n,rewritten,sizeof(rewritten),vertices,3);if(!bytes)ExitProcess(211);
        UINT texture=0,blend=0,src=0,dst=0,mode=0,key=1,alpha=0,ref=0;BOOL drawn=FALSE;
        for(UINT at=0;at<bytes;){
            UINT op=rewritten[at],size=rewritten[at+1],items=*(WORD *)(rewritten+at+2);
            if(op==8)for(UINT i=0;i<items;++i){
                UINT s=*(DWORD *)(rewritten+at+4+i*8),v=*(DWORD *)(rewritten+at+8+i*8);
                if(s==1)texture=v;if(s==27)blend=v;if(s==19)src=v;if(s==20)dst=v;if(s==21)mode=v;if(s==41)key=v;if(s==15)alpha=v;if(s==24)ref=v;
            }
            if(op==3){
                if(texture!=(keyed?901u:902u) || !blend || src!=5 || dst!=6 || mode!=4 || key || (keyed && (!alpha || ref!=127)))ExitProcess(212);
                drawn=TRUE;
            }
            at+=4+size*items;
        }
        if(!drawn || texture!=901 || key!=keyed)ExitProcess(213);
    }
    /* The game's V1 device rejects Device2 QI and never emits COLORKEYENABLE:
     * its world buffers rely on V1 color keying being on. A keyed tree on an
     * HD page must take the alpha copy and test, never the opaque one. */
    static IDirect3DDeviceVtbl v1_vtable={.QueryInterface=fixture_v1_query};
    IDirect3DDevice v1={&v1_vtable};ui_hd_capture_states(&v1);
    if(ui_hd_extra_states || ui_hd_original_key!=TRUE || ui_hd_original_test || ui_hd_original_func!=8)ExitProcess(243);
    n=0;stream[n++]=8;stream[n++]=8;*(WORD *)(stream+n)=1;n+=2;
    *(DWORD *)(stream+n)=1;*(DWORD *)(stream+n+4)=901;n+=8;
    stream[n++]=3;stream[n++]=8;*(WORD *)(stream+n)=1;n+=2;
    *(WORD *)(stream+n)=0;*(WORD *)(stream+n+2)=1;*(WORD *)(stream+n+4)=2;*(WORD *)(stream+n+6)=0;n+=8;
    stream[n++]=11;stream[n++]=0;*(WORD *)(stream+n)=0;n+=2;
    TLVertex vertices[3]={{.color=0xff808080},{.color=0xff808080},{.color=0xff808080}};
    UINT bytes=ui_hd_rewrite(stream,n,rewritten,sizeof(rewritten),vertices,3);if(!bytes)ExitProcess(244);
    /* Not blended: the texture's alpha alone is tested (MODULATE, never
     * MODULATEALPHA), whatever alpha the game's vertices happen to carry. */
    UINT texture=0,key=1,alpha=0,ref=0,func=8,mode=2;BOOL drawn=FALSE;
    for(UINT at=0;at<bytes;){
        UINT op=rewritten[at],size=rewritten[at+1],items=*(WORD *)(rewritten+at+2);
        if(op==8)for(UINT i=0;i<items;++i){
            UINT s=*(DWORD *)(rewritten+at+4+i*8),v=*(DWORD *)(rewritten+at+8+i*8);
            if(s==1)texture=v;if(s==41)key=v;if(s==15)alpha=v;if(s==24)ref=v;if(s==25)func=v;if(s==21)mode=v;
        }
        if(op==3){if(texture!=901 || key || !alpha || ref!=127 || func!=5 || mode!=2)ExitProcess(245);drawn=TRUE;}
        at+=4+size*items;
    }
    /* Native keyed draws that follow get V1 color keying back, alpha test off. */
    if(!drawn || texture!=901 || key!=TRUE || alpha || ref || func!=8)ExitProcess(246);
    ui_hd_pages[100]=(UiHdPage){0};ui_hd_opaque_pages[100]=(UiHdPage){0};
}
static void test_hd_region_assets(void){
    /* Decode actual installed SD pages and authored PNGs through production
     * WIC/composition, without creating a window or changing game files. */
    WCHAR art[MAX_PATH],root[MAX_PATH];
    WCHAR enabled[2];if(!GetEnvironmentVariableW(L"WO_TEST_HD_REGION_ASSETS",enabled,2))return;
    if(!GetEnvironmentVariableW(L"WO_TEST_FRAME_ART_ROOT",art,MAX_PATH) || !GetEnvironmentVariableW(L"WO_TEST_GAME_ROOT",root,MAX_PATH))ExitProcess(214);
    ui_hd_directory[0]=ui_hd_game_root[0]=0;
    if(!hd_append(ui_hd_directory,MAX_PATH,art) || !hd_append(ui_hd_directory,MAX_PATH,L"\\") ||
       !hd_append(ui_hd_game_root,MAX_PATH,root) || !hd_append(ui_hd_game_root,MAX_PATH,L"\\"))ExitProcess(215);
    hd_load_region_index();if(!hd_region_count)ExitProcess(220);
    static const HdRegion arbitrary={.width=128,.height=128};
    if(hd_region_scale(&arbitrary,317,111)!=4 || hd_region_scale(&arbitrary,193,275)!=4 ||
       hd_region_scale(&arbitrary,1,1)!=1 || hd_page_size(317)!=512)ExitProcess(220);
    if(!hd_region_index_valid(hd_region_index_memory,12+hd_region_count*sizeof(HdRegion)))ExitProcess(220);
    /* Reject truncated tables, traversal paths and out-of-page rectangles. */
    UINT length=12+hd_region_count*sizeof(HdRegion);
    if(hd_region_index_valid(hd_region_index_memory,length-1))ExitProcess(220);
    WCHAR saved=hd_regions[0].png[0];hd_regions[0].png[0]=L'.';
    if(hd_region_index_valid(hd_region_index_memory,length))ExitProcess(220);hd_regions[0].png[0]=saved;
    UINT savedX=hd_regions[0].x;hd_regions[0].x=256;
    if(hd_region_index_valid(hd_region_index_memory,length))ExitProcess(220);hd_regions[0].x=savedX;
    UINT tested=0;
    for(UINT i=0;i<hd_region_count;++i){
        const HdRegion *r=hd_regions+i;WCHAR path[MAX_PATH];UINT w=0,h=0;
        if(!hd_path(path,r->png))ExitProcess(216);
        BYTE *image=read_art_png(path,&w,&h);if(!image)ExitProcess(217);
        /* The same PNG can back an arbitrary-resolution UI page or horizontal
         * strip. Storage normalization must not crop its normalized layout. */
        for(UINT strip=0;strip<2;++strip){
            UINT squareSize=0,parts=strip?3:1,part=strip?2:0;
            BYTE *square=hd_square_png(path,part,parts,strip!=0,&squareSize);
            if(!square || squareSize<256 || (squareSize&(squareSize-1)))ExitProcess(217);
            const UINT sample[2]={0,squareSize-1};
            for(UINT yy=0;yy<2;++yy)for(UINT xx=0;xx<2;++xx){
                UINT x=sample[xx],y=sample[yy],sx=(part*w+x*w/squareSize)/parts,sy=y*h/squareSize;
                if(!equal(square+(y*squareSize+x)*4,image+(sy*w+sx)*4,4))ExitProcess(217);
            }
            VirtualFree(square,0,MEM_RELEASE);
        }
        static char filename[80]="Pics\\Hi-res\\";UINT at=12;
        const char *p=r->native;while((*((char *)filename+at++)=*p++)){}
        WCHAR nativePath[MAX_PATH];nativePath[0]=0;
        if(!hd_append(nativePath,MAX_PATH,ui_hd_game_root) || !hd_append(nativePath,MAX_PATH,L"textures\\sd\\Pics\\Hi-res\\"))ExitProcess(218);
        WCHAR nativeName[32];for(UINT j=0;j<32;++j)nativeName[j]=(BYTE)r->native[j];
        if(!hd_append(nativePath,MAX_PATH,nativeName))ExitProcess(218);
        HANDLE file=CreateFileW(nativePath,GENERIC_READ,FILE_SHARE_READ,0,OPEN_EXISTING,0,0);
        BYTE header[18];DWORD read=0;
        if(file==INVALID_HANDLE_VALUE || !ReadFile(file,header,18,&read,0) || read!=18)ExitProcess(218);CloseHandle(file);
        UINT pages=*(WORD *)(header+12)/256,size=0;BYTE *page=hd_asset_pixels(filename,r->page,pages,&size);
        if(!page || size<256 || !hd_region_scale(r,w,h))ExitProcess(218);
        UINT scale=size/256,rw=r->width*scale,rh=r->height*scale;
        /* An opaque RGB region keeps the native page's colour key, not the
         * replacement's own exact black, except within one native texel of
         * the key's outline, which the replacement draws itself. */
        WCHAR wholePath[MAX_PATH];BOOL strip=FALSE;
        BYTE *native=hd_find(filename,r->page,wholePath,&strip)?0:hd_native_page(filename,r->page,pages,size);
        for(UINT y=0;y<rh;++y)for(UINT x=0;x<rw;++x){
            UINT at=((r->y*scale+y)*size+r->x*scale+x)*4;
            const BYTE *a=image+((y*h/rh)*w+x*w/rw)*4,*b=page+at;
            UINT alpha=r->alpha?a[3]:(native?native[at+3]:b[3]);
            if(!r->alpha && native){
                BOOL outline=FALSE;int nx=(int)(x/scale),ny=(int)(y/scale);
                for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx){
                    int ox=nx+dx,oy=ny+dy;
                    if(ox<0 || oy<0 || ox>=(int)r->width || oy>=(int)r->height)continue;
                    if(native[((r->y+(UINT)oy)*scale*size+(r->x+(UINT)ox)*scale)*4+3]!=native[((r->y+(UINT)ny)*scale*size+(r->x+(UINT)nx)*scale)*4+3])outline=TRUE;
                }
                BYTE paint=a[0]>a[1]?a[0]:a[1];if(a[2]>paint)paint=a[2];
                if(outline && alpha && paint<16)alpha=0;
                else if(outline && !alpha && paint>=64)alpha=255;
            }
            if(a[0]!=b[0] || a[1]!=b[1] || a[2]!=b[2] || alpha!=b[3])ExitProcess(219);
        }
        if(native)VirtualFree(native,0,MEM_RELEASE);
        VirtualFree(image,0,MEM_RELEASE);VirtualFree(page,0,MEM_RELEASE);++tested;
    }
    if(tested!=hd_region_count)ExitProcess(220);
    /* Black paint over an opaque native texel stays opaque; colour over a
     * keyed texel stays transparent. Where the two meet, the replacement
     * draws the outline: its black is see-through, its colour solid. A
     * region with its own alpha keeps it. */
    static BYTE keyed_page[256*256*4],paint[4*4];
    static HdRegion painted;painted.x=10;painted.y=20;painted.width=4;painted.height=1;
    BYTE *under=keyed_page+(20*256+10)*4;
    under[3]=255;under[7]=255;under[11]=0;under[15]=0;
    paint[3]=255;paint[7]=255;
    for(UINT i=2;i<4;++i){paint[i*4]=200;paint[i*4+1]=100;paint[i*4+2]=50;paint[i*4+3]=255;}
    hd_region_paste(keyed_page,256,&painted,paint,4,1);
    if(under[3]!=255 || under[0] || under[7] || under[11]!=255 || under[8]!=200 || under[15] || under[12]!=200)ExitProcess(250);
    painted.alpha=1;paint[3]=0;under[3]=255;
    hd_region_paste(keyed_page,256,&painted,paint,4,1);
    if(under[3] || under[7]!=255)ExitProcess(251);
}
/* The installer's debanded pictures change colours only: where the sheet
 * shows a texel the picture's colour is taken, over the whole block the
 * texel fills on a larger page; where the sheet is keyed, or the picture
 * see-through, the sheet's own stays. */
static void test_sd_regions(void){
    static BYTE page[512*512*4],picture[2*4];
    static HdRegion region;region.x=10;region.y=20;region.width=2;region.height=1;
    for(UINT y=40;y<42;++y)for(UINT x=20;x<22;++x){BYTE *p=page+(y*512+x)*4;p[0]=1;p[1]=2;p[2]=3;p[3]=255;}
    picture[0]=50;picture[1]=60;picture[2]=70;picture[3]=255;
    picture[4]=80;picture[5]=90;picture[6]=100;picture[7]=255;
    sd_region_paste(page,512,&region,picture);
    for(UINT y=40;y<42;++y)for(UINT x=20;x<22;++x){
        const BYTE *p=page+(y*512+x)*4;if(p[0]!=50 || p[1]!=60 || p[2]!=70 || p[3]!=255)ExitProcess(310);
    }
    /* The second texel is keyed on the sheet; beside and below nothing is touched. */
    for(UINT y=40;y<43;++y)for(UINT x=22;x<25;++x)if(*(DWORD *)(page+(y*512+x)*4))ExitProcess(311);
    if(*(DWORD *)(page+(42*512+20)*4) || *(DWORD *)(page+(39*512+20)*4) || *(DWORD *)(page+(40*512+19)*4))ExitProcess(311);
    picture[3]=0;picture[0]=9;
    sd_region_paste(page,512,&region,picture);
    if(page[(40*512+20)*4]!=50 || page[(40*512+20)*4+3]!=255)ExitProcess(312);
}
/* The layer as installed here: every page the installer's index names is
 * composed, and each debanded picture on it shows exactly so, under any HD
 * replacement and over nothing else. */
static BOOL sd_test_overlaid(const char *filename,const HdRegion *r){
    WCHAR path[MAX_PATH];
    for(UINT k=0;k<hd_region_count;++k){
        const HdRegion *h=hd_regions+k;
        if(h->page==r->page && startup_name(filename,h->native) && h->x<r->x+r->width && r->x<h->x+h->width &&
           h->y<r->y+r->height && r->y<h->y+h->height && hd_path(path,h->png) && hd_exists(path))return TRUE;
    }
    return FALSE;
}
static void test_sd_region_assets(void){
    WCHAR enabled[2],art[MAX_PATH],root[MAX_PATH],smooth[MAX_PATH];
    if(!GetEnvironmentVariableW(L"WO_TEST_HD_REGION_ASSETS",enabled,2))return;
    if(!GetEnvironmentVariableW(L"WO_TEST_FRAME_ART_ROOT",art,MAX_PATH) || !GetEnvironmentVariableW(L"WO_TEST_GAME_ROOT",root,MAX_PATH))ExitProcess(314);
    ui_hd_directory[0]=ui_hd_game_root[0]=ui_sd_directory[0]=0;
    if(!hd_append(ui_hd_directory,MAX_PATH,art) || !hd_append(ui_hd_directory,MAX_PATH,L"\\") ||
       !hd_append(ui_hd_game_root,MAX_PATH,root) || !hd_append(ui_hd_game_root,MAX_PATH,L"\\"))ExitProcess(314);
    /* Beside the HD artwork, unless the test is pointed elsewhere. */
    if(GetEnvironmentVariableW(L"WO_TEST_SD_ART_ROOT",smooth,MAX_PATH)){
        if(!hd_append(ui_sd_directory,MAX_PATH,smooth) || !hd_append(ui_sd_directory,MAX_PATH,L"\\"))ExitProcess(314);
    }else if(!hd_append(ui_sd_directory,MAX_PATH,art) || !hd_append(ui_sd_directory,MAX_PATH,L"\\..\\sd\\"))ExitProcess(314);
    hd_load_region_index();if(!sd_region_count)return;
    UINT pages_checked=0,pictures=0,changed=0;
    for(UINT i=0;i<sd_region_count;++i){
        const HdRegion *first=sd_regions+i;BOOL seen=FALSE;
        for(UINT j=0;j<i;++j)if(sd_regions[j].page==first->page && equal((const BYTE *)sd_regions[j].native,(const BYTE *)first->native,32))seen=TRUE;
        if(seen)continue;
        static char filename[80]="Pics\\Hi-res\\";UINT at=12;
        const char *p=first->native;while((*((char *)filename+at++)=*p++)){}
        WCHAR sheet[MAX_PATH],name[32];sheet[0]=0;
        for(UINT j=0;j<32;++j)name[j]=(BYTE)first->native[j];
        if(!hd_append(sheet,MAX_PATH,ui_hd_game_root) || !hd_append(sheet,MAX_PATH,L"textures\\sd\\Pics\\Hi-res\\") || !hd_append(sheet,MAX_PATH,name))ExitProcess(315);
        HANDLE file=CreateFileW(sheet,GENERIC_READ,FILE_SHARE_READ,0,OPEN_EXISTING,0,0);
        BYTE header[18];DWORD read=0;
        if(file==INVALID_HANDLE_VALUE || !ReadFile(file,header,18,&read,0) || read!=18)ExitProcess(315);CloseHandle(file);
        UINT pages=*(WORD *)(header+12)/256,size=0;
        WCHAR whole[MAX_PATH];BOOL strip=FALSE;
        if(hd_find(filename,first->page,whole,&strip))continue; /* a replacement for the whole page */
        BYTE *page=hd_asset_pixels(filename,first->page,pages,&size);
        BYTE *native=page?hd_native_page(filename,first->page,pages,size):0;
        if(!native){
            /* No page is made only where none of its pictures is there. */
            WCHAR path[MAX_PATH];
            for(UINT j=i;j<sd_region_count;++j)if(sd_region_file(sd_regions+j,filename,first->page,path))ExitProcess(316);
            continue;
        }
        UINT scale=size/256;
        for(UINT j=i;j<sd_region_count;++j){
            const HdRegion *r=sd_regions+j;WCHAR path[MAX_PATH];
            if(r->page!=first->page || !equal((const BYTE *)r->native,(const BYTE *)first->native,32) || sd_test_overlaid(filename,r))continue;
            UINT w=0,h=0;BYTE *image=sd_region_file(r,filename,first->page,path)?read_art_png(path,&w,&h):0;
            if(image && (w!=r->width || h!=r->height))ExitProcess(317);
            for(UINT y=0;y<r->height*scale;++y)for(UINT x=0;x<r->width*scale;++x){
                UINT where=((r->y*scale+y)*size+r->x*scale+x)*4;
                const BYTE *shown=page+where,*sheet_texel=native+where,*smooth_texel=image?image+((y/scale)*w+x/scale)*4:0;
                const BYTE *wanted=smooth_texel && sheet_texel[3] && smooth_texel[3]?smooth_texel:sheet_texel;
                if(shown[0]!=wanted[0] || shown[1]!=wanted[1] || shown[2]!=wanted[2] || shown[3]!=sheet_texel[3])ExitProcess(318);
                if(wanted!=sheet_texel && !equal(wanted,sheet_texel,3))++changed;
            }
            if(image){VirtualFree(image,0,MEM_RELEASE);++pictures;}
        }
        VirtualFree(native,0,MEM_RELEASE);VirtualFree(page,0,MEM_RELEASE);++pages_checked;
    }
    /* A layer that changes nothing would pass everything above. */
    if(pages_checked && pictures && !changed)ExitProcess(319);
}
