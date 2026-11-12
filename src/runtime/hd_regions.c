/* Compose small authored replacements over the installed native page at load
 * time. No original atlas data or generated page PNGs are shipped as artwork. */
/* Play compiles manifests/hd-textures.json + present PNGs into this fixed-width
 * little-endian index. It contains paths/rectangles only, never image pixels. */
typedef struct HdRegion {WCHAR png[128];char native[32];UINT page,x,y,width,height,alpha;} HdRegion;
static BYTE *hd_region_index_memory;
static HdRegion *hd_regions;
static UINT hd_region_count;
/* A second index of the same form: the debanded pictures the installer made
 * of the sheets' own (assets\sd, textures\sd-regions.bin). Each has its
 * sheet picture's size and lies under any replacement. Where one is missing
 * the sheet's texels show. */
static BYTE *sd_region_index_memory;
static HdRegion *sd_regions;
static UINT sd_region_count;
static BOOL hd_region_name_valid(const WCHAR *name,UINT capacity,BOOL png) {
    UINT n=0;while(n<capacity && name[n]){
        WCHAR c=name[n];if(!((c>=L'a' && c<=L'z') || (c>=L'A' && c<=L'Z') || (c>=L'0' && c<=L'9') || c==L'-' || c==L'.'))return FALSE;
        ++n;
    }
    if(n<5 || n>=capacity)return FALSE;
    const WCHAR *ext=png?L".png":L".tga";
    for(UINT i=0;i<4;++i){WCHAR c=name[n-4+i];if(c>=L'A' && c<=L'Z')c+=32;if(c!=ext[i])return FALSE;}
    for(UINT i=0;i<n-4;++i)if(name[i]==L'.')return FALSE;
    return TRUE;
}
static BOOL hd_region_index_valid(const BYTE *data,UINT length) {
    if(length<12 || !equal(data,(const BYTE *)"WOHDREG1",8))return FALSE;
    UINT count=*(const UINT *)(data+8);
    if(count>16384 || length!=12+count*sizeof(HdRegion))return FALSE;
    const HdRegion *regions=(const HdRegion *)(data+12);
    for(UINT i=0;i<count;++i){
        const HdRegion *r=regions+i;WCHAR native[32];
        for(UINT j=0;j<32;++j)native[j]=(BYTE)r->native[j];
        if(!hd_region_name_valid(r->png,128,TRUE) || !hd_region_name_valid(native,32,FALSE) ||
            r->page>255 || !r->width || !r->height || r->width>256 || r->height>256 ||
            r->x>256-r->width || r->y>256-r->height || r->alpha>1)return FALSE;
    }
    return TRUE;
}
static void hd_load_index(const WCHAR *name,BYTE **memory,HdRegion **regions,UINT *count,const char *loaded,const char *invalid) {
    if(*memory)VirtualFree(*memory,0,MEM_RELEASE);
    *memory=0;*regions=0;*count=0;
    WCHAR path[MAX_PATH];path[0]=0;
    if(!hd_append(path,MAX_PATH,ui_hd_game_root) || !hd_append(path,MAX_PATH,name))return;
    HANDLE file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,0,OPEN_EXISTING,0,0);if(file==INVALID_HANDLE_VALUE)return;
    LARGE_INTEGER size;BYTE *data=0;DWORD read;
    if(!GetFileSizeEx(file,&size) || size.QuadPart<12 || size.QuadPart>12+16384*sizeof(HdRegion))goto done;
    data=VirtualAlloc(0,(UINT)size.QuadPart,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    if(!data || !ReadFile(file,data,(DWORD)size.QuadPart,&read,0) || read!=(DWORD)size.QuadPart || !hd_region_index_valid(data,read)){
        if(data)VirtualFree(data,0,MEM_RELEASE);data=0;wo_render_event(invalid,0);goto done;
    }
    *memory=data;*count=*(UINT *)(data+8);*regions=(HdRegion *)(data+12);
    wo_render_event(loaded,*count);
done:
    CloseHandle(file);
}
static void hd_load_region_index(void) {
    hd_load_index(L"textures\\hd-regions.bin",&hd_region_index_memory,&hd_regions,&hd_region_count,"hd_region_index_loaded","hd_region_index_invalid");
    hd_load_index(L"textures\\sd-regions.bin",&sd_region_index_memory,&sd_regions,&sd_region_count,"sd_region_index_loaded","sd_region_index_invalid");
}
static BOOL hd_world_name(const char *filename) {
    return !startup_name(filename,"hud2.tga") && !startup_name(filename,"hud.tga") &&
        !startup_name(filename,"testmenu.tga") && !startup_name(filename,"menukey.tga") &&
        !startup_name(filename,"xllogo.tga") && !startup_name(filename,"backgrnd.tga");
}
static BYTE *hd_native_page(const char *filename,UINT page,UINT pages,UINT size) {
    WCHAR path[MAX_PATH];UINT n=0;const char *p=filename;
    while(*p=='.' || *p=='\\' || *p=='/')++p;
    while(*p && n+1<MAX_PATH){char ch=*p++;path[n++]=ch=='/'?L'\\':(WCHAR)(BYTE)ch;}
    if(*p)return 0;path[n]=0;
    WCHAR full[MAX_PATH];full[0]=0;
    if(!hd_append(full,MAX_PATH,ui_hd_game_root) || !hd_append(full,MAX_PATH,L"textures\\sd\\"))return 0;
    BOOL nested=FALSE;for(UINT i=0;i<n;++i)if(path[i]==L'\\')nested=TRUE;
    if(!nested && !hd_append(full,MAX_PATH,L"Pics\\Hi-res\\"))return 0;
    if(!hd_append(full,MAX_PATH,path))return 0;
    HANDLE file=CreateFileW(full,GENERIC_READ,FILE_SHARE_READ,0,OPEN_EXISTING,0,0);
    if(file==INVALID_HANDLE_VALUE)return 0;
    LARGE_INTEGER length;BYTE *raw=0,*pixels=0;DWORD read;
    if(!GetFileSizeEx(file,&length) || length.QuadPart<18 || length.QuadPart>32*1024*1024)goto done;
    raw=VirtualAlloc(0,(UINT)length.QuadPart,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    if(!raw || !ReadFile(file,raw,(DWORD)length.QuadPart,&read,0) || read!=(DWORD)length.QuadPart)goto done;
    UINT w=*(WORD *)(raw+12),h=*(WORD *)(raw+14),start=18+raw[0];
    if(raw[2]!=2 || raw[16]!=16 || w!=pages*256 || h!=256 || page>=pages || start+w*h*2>(UINT)length.QuadPart)goto done;
    pixels=VirtualAlloc(0,size*size*4,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    if(pixels)for(UINT y=0;y<size;++y)for(UINT x=0;x<size;++x){
        WORD c=((WORD *)(raw+start))[(y*256/size)*w+page*256+x*256/size];BYTE *dst=pixels+(y*size+x)*4;
        dst[0]=(BYTE)((c&31)*255/31);dst[1]=(BYTE)(((c>>5)&31)*255/31);dst[2]=(BYTE)(((c>>10)&31)*255/31);dst[3]=(c&32767)?255:0;
    }
done:
    if(raw)VirtualFree(raw,0,MEM_RELEASE);CloseHandle(file);return pixels;
}
static UINT hd_region_scale(const HdRegion *region,UINT w,UINT h) {
    if(!w || !h || !region->width || !region->height)return 0;
    UINT desired=(w+region->width-1)/region->width,vertical=(h+region->height-1)/region->height;
    if(vertical>desired)desired=vertical;
    UINT scale=1;while(scale<desired && scale<32)scale*=2;
    return scale;
}
/* An opaque RGB replacement has no cut-out of its own. Away from the native
 * cut-out's outline it keeps the native colour key: keying by the
 * replacement's own exact black punched holes wherever upscaling had crushed
 * dark paint to black (Phenitia Park's tunnel panels: 28% keyed natively,
 * 50% exact black in HD). Within one native texel of that outline the
 * replacement draws the outline itself: its black is see-through and its
 * paint is solid, so the edge has the replacement's shape and detail rather
 * than native-sized blocks of it (the BRITVA sign showed black slivers of
 * its HD background between the two shapes). An independent opaque upload
 * ignores this alpha when keying is off. */
static BYTE *hd_region_outline(const BYTE *base,UINT size,const HdRegion *region) {
    UINT scale=size/256,left=region->x*scale,top=region->y*scale,rw=region->width,rh=region->height;
    BYTE *outline=VirtualAlloc(0,rw*rh,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    if(outline)for(UINT y=0;y<rh;++y)for(UINT x=0;x<rw;++x){
        BYTE own=base[((top+y*scale)*size+left+x*scale)*4+3];
        for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx){
            int ox=(int)x+dx,oy=(int)y+dy;
            if(ox<0 || oy<0 || ox>=(int)rw || oy>=(int)rh)continue;
            if(base[((top+(UINT)oy*scale)*size+left+(UINT)ox*scale)*4+3]!=own)outline[y*rw+x]=1;
        }
    }
    return outline;
}
static void hd_region_paste(BYTE *base,UINT size,const HdRegion *region,const BYTE *image,UINT w,UINT h) {
    UINT scale=size/256,left=region->x*scale,top=region->y*scale,rw=region->width*scale,rh=region->height*scale;
    BYTE *outline=region->alpha?0:hd_region_outline(base,size,region);
    for(UINT y=0;y<rh;++y)for(UINT x=0;x<rw;++x){
        BYTE *dst=base+((top+y)*size+left+x)*4,key=dst[3];
        copy(dst,image+((y*h/rh)*w+x*w/rw)*4,4);
        if(region->alpha)continue;
        if(outline && outline[(y/scale)*region->width+x/scale]){
            BYTE paint=dst[0]>dst[1]?dst[0]:dst[1];if(dst[2]>paint)paint=dst[2];
            if(key && paint<16)key=0;
            else if(!key && paint>=64)key=255;
        }
        dst[3]=key;
    }
    if(outline)VirtualFree(outline,0,MEM_RELEASE);
}
/* A debanded picture changes colours only: the sheet decides which texels
 * show, and on a page larger than the sheet each texel fills its block. */
static void sd_region_paste(BYTE *base,UINT size,const HdRegion *region,const BYTE *image) {
    UINT scale=size/256,left=region->x*scale,top=region->y*scale;
    for(UINT y=0;y<region->height*scale;++y)for(UINT x=0;x<region->width*scale;++x){
        BYTE *dst=base+((top+y)*size+left+x)*4;const BYTE *src=image+((y/scale)*region->width+x/scale)*4;
        if(dst[3] && src[3])copy(dst,src,3);
    }
}
static BOOL sd_region_file(const HdRegion *region,const char *filename,UINT page,WCHAR *path) {
    path[0]=0;
    return region->page==page && startup_name(filename,region->native) && hd_append(path,MAX_PATH,ui_sd_directory) &&
        hd_append(path,MAX_PATH,region->png) && hd_exists(path);
}
static BYTE *hd_compose_listed(const char *filename,UINT page,UINT pages,BYTE *base,UINT *size) {
    /* A replacement for the whole page leaves nothing of the sheet to deband. */
    BOOL whole=base!=0;
    UINT wanted=base?*size:256,matched=0,smoothed=0;WCHAR path[MAX_PATH];
    for(UINT i=0;i<hd_region_count;++i){
        const HdRegion *region=hd_regions+i;
        if(region->page!=page || !startup_name(filename,region->native) || !hd_path(path,region->png) || !hd_exists(path))continue;
        UINT w=0,h=0;BYTE *image=read_art_png(path,&w,&h);if(!image)continue;
        UINT scale=hd_region_scale(region,w,h);VirtualFree(image,0,MEM_RELEASE);
        if(!scale){wo_render_event("hd_region_invalid_dimensions",i);continue;}
        if(scale*256>wanted)wanted=scale*256;++matched;
    }
    for(UINT i=0;!whole && i<sd_region_count;++i)if(sd_region_file(sd_regions+i,filename,page,path))++smoothed;
    if(!matched && !smoothed)return base;
    if(!base)base=hd_native_page(filename,page,pages,wanted);
    else if(*size!=wanted){
        BYTE *grown=VirtualAlloc(0,wanted*wanted*4,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);if(!grown)return base;
        for(UINT y=0;y<wanted;++y)for(UINT x=0;x<wanted;++x)copy(grown+(y*wanted+x)*4,base+((y**size/wanted)**size+x**size/wanted)*4,4);
        VirtualFree(base,0,MEM_RELEASE);base=grown;
    }
    if(!base)return 0;*size=wanted;
    for(UINT i=0;smoothed && i<sd_region_count;++i){
        const HdRegion *region=sd_regions+i;
        if(!sd_region_file(region,filename,page,path))continue;
        UINT w=0,h=0;BYTE *image=read_art_png(path,&w,&h);if(!image)continue;
        if(w==region->width && h==region->height)sd_region_paste(base,wanted,region,image);
        VirtualFree(image,0,MEM_RELEASE);
    }
    for(UINT i=0;i<hd_region_count;++i){
        const HdRegion *region=hd_regions+i;
        if(region->page!=page || !startup_name(filename,region->native) || !hd_path(path,region->png) || !hd_exists(path))continue;
        UINT w=0,h=0;BYTE *image=read_art_png(path,&w,&h);if(!image)continue;
        if(hd_region_scale(region,w,h))hd_region_paste(base,wanted,region,image,w,h);
        VirtualFree(image,0,MEM_RELEASE);
    }
    if(matched)wo_render_event("hd_regions_composed",matched);
    if(smoothed)wo_render_event("sd_regions_composed",smoothed);
    return base;
}
static BYTE *hd_compose_regions(const char *filename,UINT page,UINT pages,BYTE *base,UINT *size) {
    BOOL listed=FALSE;
    for(UINT i=0;!listed && i<hd_region_count;++i)listed=hd_regions[i].page==page && startup_name(filename,hd_regions[i].native);
    for(UINT i=0;!listed && i<sd_region_count;++i)listed=sd_regions[i].page==page && startup_name(filename,sd_regions[i].native);
    if(!listed)return base;
    ArtPngHold hold;art_png_hold(&hold);
    base=hd_compose_listed(filename,page,pages,base,size);
    art_png_release(&hold);return base;
}
