/* Compose small authored replacements over the installed native page at load
 * time. No original atlas data or generated page PNGs are shipped as artwork. */
/* Play compiles manifests/hd-textures.json + present PNGs into this fixed-width
 * little-endian index. It contains paths/rectangles only, never image pixels. */
typedef struct HdRegion {WCHAR png[128];char native[32];UINT page,x,y,width,height,alpha;} HdRegion;
static BYTE *hd_region_index_memory;
static HdRegion *hd_regions;
static UINT hd_region_count;
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
static void hd_load_region_index(void) {
    if(hd_region_index_memory)VirtualFree(hd_region_index_memory,0,MEM_RELEASE);
    hd_region_index_memory=0;hd_regions=0;hd_region_count=0;
    WCHAR path[MAX_PATH];path[0]=0;
    if(!hd_append(path,MAX_PATH,ui_hd_game_root) || !hd_append(path,MAX_PATH,L"textures\\hd-regions.bin"))return;
    HANDLE file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,0,OPEN_EXISTING,0,0);if(file==INVALID_HANDLE_VALUE)return;
    LARGE_INTEGER size;BYTE *data=0;DWORD read;
    if(!GetFileSizeEx(file,&size) || size.QuadPart<12 || size.QuadPart>12+16384*sizeof(HdRegion))goto done;
    data=VirtualAlloc(0,(UINT)size.QuadPart,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    if(!data || !ReadFile(file,data,(DWORD)size.QuadPart,&read,0) || read!=(DWORD)size.QuadPart || !hd_region_index_valid(data,read)){
        if(data)VirtualFree(data,0,MEM_RELEASE);data=0;wo_render_event("hd_region_index_invalid",0);goto done;
    }
    hd_region_index_memory=data;hd_region_count=*(UINT *)(data+8);hd_regions=(HdRegion *)(data+12);
    wo_render_event("hd_region_index_loaded",hd_region_count);
done:
    CloseHandle(file);
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
static void hd_region_paste(BYTE *base,UINT size,const HdRegion *region,const BYTE *image,UINT w,UINT h) {
    UINT scale=size/256,left=region->x*scale,top=region->y*scale,rw=region->width*scale,rh=region->height*scale;
    for(UINT y=0;y<rh;++y)for(UINT x=0;x<rw;++x){
        BYTE *dst=base+((top+y)*size+left+x)*4;
        copy(dst,image+((y*h/rh)*w+x*w/rw)*4,4);
        /* Exact black is the native key, including inside opaque RGB PNGs.
         * An independent opaque upload ignores this alpha when keying is off. */
        if(!region->alpha && !(dst[0]|dst[1]|dst[2]))dst[3]=0;
    }
}
static BYTE *hd_compose_regions(const char *filename,UINT page,UINT pages,BYTE *base,UINT *size) {
    UINT wanted=base?*size:256,matched=0;WCHAR path[MAX_PATH];
    for(UINT i=0;i<hd_region_count;++i){
        const HdRegion *region=hd_regions+i;
        if(region->page!=page || !startup_name(filename,region->native) || !hd_path(path,region->png) || !hd_exists(path))continue;
        UINT w=0,h=0;BYTE *image=read_art_png(path,&w,&h);if(!image)continue;
        UINT scale=hd_region_scale(region,w,h);VirtualFree(image,0,MEM_RELEASE);
        if(!scale){wo_render_event("hd_region_invalid_dimensions",i);continue;}
        if(scale*256>wanted)wanted=scale*256;++matched;
    }
    if(!matched)return base;
    if(!base)base=hd_native_page(filename,page,pages,wanted);
    else if(*size!=wanted){
        BYTE *grown=VirtualAlloc(0,wanted*wanted*4,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);if(!grown)return base;
        for(UINT y=0;y<wanted;++y)for(UINT x=0;x<wanted;++x)copy(grown+(y*wanted+x)*4,base+((y**size/wanted)**size+x**size/wanted)*4,4);
        VirtualFree(base,0,MEM_RELEASE);base=grown;
    }
    if(!base)return 0;*size=wanted;
    for(UINT i=0;i<hd_region_count;++i){
        const HdRegion *region=hd_regions+i;
        if(region->page!=page || !startup_name(filename,region->native) || !hd_path(path,region->png) || !hd_exists(path))continue;
        UINT w=0,h=0;BYTE *image=read_art_png(path,&w,&h);if(!image)continue;
        if(hd_region_scale(region,w,h))hd_region_paste(base,wanted,region,image,w,h);
        VirtualFree(image,0,MEM_RELEASE);
    }
    wo_render_event("hd_regions_composed",matched);return base;
}
