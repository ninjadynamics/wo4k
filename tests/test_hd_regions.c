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
    UINT texture=0,key=1,alpha=0,ref=0,func=8;BOOL drawn=FALSE;
    for(UINT at=0;at<bytes;){
        UINT op=rewritten[at],size=rewritten[at+1],items=*(WORD *)(rewritten+at+2);
        if(op==8)for(UINT i=0;i<items;++i){
            UINT s=*(DWORD *)(rewritten+at+4+i*8),v=*(DWORD *)(rewritten+at+8+i*8);
            if(s==1)texture=v;if(s==41)key=v;if(s==15)alpha=v;if(s==24)ref=v;if(s==25)func=v;
        }
        if(op==3){if(texture!=901 || key || !alpha || ref!=127 || func!=5)ExitProcess(245);drawn=TRUE;}
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
        for(UINT y=0;y<rh;++y)for(UINT x=0;x<rw;++x){
            const BYTE *a=image+((y*h/rh)*w+x*w/rw)*4,*b=page+(((r->y*scale+y)*size+r->x*scale+x)*4);
            UINT alpha=r->alpha || (a[0]|a[1]|a[2])?a[3]:0;
            if(a[0]!=b[0] || a[1]!=b[1] || a[2]!=b[2] || alpha!=b[3])ExitProcess(219);
        }
        VirtualFree(image,0,MEM_RELEASE);VirtualFree(page,0,MEM_RELEASE);++tested;
    }
    if(tested!=hd_region_count)ExitProcess(220);
}
