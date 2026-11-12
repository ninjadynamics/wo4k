/* Read replacements directly; source PNG files are never rewritten or cached. */
#include "hd_catalog.h"
static WCHAR ui_hd_game_root[MAX_PATH];
static BOOL hd_append(WCHAR *out,UINT capacity,const WCHAR *text) {
    UINT n=0;while(n<capacity && out[n])++n;
    while(*text && n+1<capacity)out[n++]=*text++;
    if(n>=capacity || *text)return FALSE;out[n]=0;return TRUE;
}
static BOOL hd_exists(const WCHAR *path){DWORD attr=GetFileAttributesW(path);return attr!=INVALID_FILE_ATTRIBUTES && !(attr&FILE_ATTRIBUTE_DIRECTORY);}
/* The authored picture of that name, or the upscaled one where there is none. */
static BOOL hd_path(WCHAR *out,const WCHAR *relative) {
    WCHAR upscaled[MAX_PATH];upscaled[0]=0;out[0]=0;
    if(!hd_append(out,MAX_PATH,ui_hd_directory) || !hd_append(out,MAX_PATH,relative))return FALSE;
    if(!hd_exists(out) && ui_upscaled_directory[0] && hd_append(upscaled,MAX_PATH,ui_upscaled_directory) &&
       hd_append(upscaled,MAX_PATH,relative) && hd_exists(upscaled))copy((BYTE *)out,(const BYTE *)upscaled,sizeof(upscaled));
    return TRUE;
}
static BOOL hd_native_name(WCHAR *out,const char *filename,BOOL nested,BOOL keep_extension,int page) {
    const char *name=filename,*end;
    if(nested){while(*name=='.' || *name=='\\' || *name=='/')++name;}
    else for(const char *p=filename;*p;++p)if(*p=='\\' || *p=='/')name=p+1;
    end=name;while(*end)++end;
    if(!keep_extension && end-name>=4 && end[-4]=='.')end-=4;
    UINT n=0;for(const char *p=name;p<end;++p){if(n+1>=MAX_PATH || (BYTE)*p>=128)return FALSE;out[n++]=*p=='/'?L'\\':(WCHAR)*p;}out[n]=0;
    if(page>=0){
        if(!hd_append(out,MAX_PATH,L".page"))return FALSE;
        WCHAR digits[12];UINT k=0;do{digits[k++]=(WCHAR)(L'0'+page%10);page/=10;}while(page);
        while(k){WCHAR ch[2]={digits[--k],0};if(!hd_append(out,MAX_PATH,ch))return FALSE;}
    }
    return hd_append(out,MAX_PATH,L".png");
}
static BOOL hd_find(const char *filename,UINT page,WCHAR *path,BOOL *strip) {
    for(UINT i=0;i<sizeof(hd_aliases)/sizeof(hd_aliases[0]);++i)
        if(hd_aliases[i].page==page && startup_name(filename,hd_aliases[i].native) && hd_path(path,hd_aliases[i].png) && hd_exists(path)){*strip=FALSE;return TRUE;}
    WCHAR name[MAX_PATH];
    for(UINT part=0;part<2;++part)for(UINT nested=0;nested<2;++nested)for(UINT extension=0;extension<2;++extension){
        if(hd_native_name(name,filename,nested==0,extension!=0,part==0?(int)page:-1) && hd_path(path,name) && hd_exists(path)){*strip=part!=0;return TRUE;}
    }
    return FALSE;
}
static void hd_premultiply(BYTE *pixels,UINT count) {
    for(UINT i=0;i<count;++i){BYTE *p=pixels+i*4;UINT a=p[3];for(UINT c=0;c<3;++c)p[c]=(BYTE)((p[c]*a+127)/255);}
}
static DWORD hd_premultiply_color(DWORD color) {
    UINT a=color>>24;DWORD result=color&0xff000000;
    for(UINT shift=0;shift<24;shift+=8)result|=((((color>>shift)&255)*a+127)/255)<<shift;
    return result;
}
static UINT hd_page_size(UINT desired){UINT size=256;while(size<desired && size<8192)size*=2;return size;}
static BYTE *hd_square_png(const WCHAR *path,UINT page,UINT pages,BOOL strip,UINT *size) {
    UINT w=0,h=0;BYTE *pixels=read_art_png(path,&w,&h);
    if(!pixels)return 0;
    if(!pages || (strip && page>=pages)){VirtualFree(pixels,0,MEM_RELEASE);return 0;}
    UINT parts=strip?pages:1,desired=(w+parts-1)/parts;
    if(h>desired)desired=h;UINT target=hd_page_size(desired);
    if(w!=target || h!=target || parts>1){
        BYTE *crop=VirtualAlloc(0,target*target*4,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
        if(crop)for(UINT y=0;y<target;++y)for(UINT x=0;x<target;++x){
            UINT sx=((strip?page:0)*w+x*w/target)/parts,sy=y*h/target;
            copy(crop+(y*target+x)*4,pixels+(sy*w+sx)*4,4);
        }
        VirtualFree(pixels,0,MEM_RELEASE);pixels=crop;
    }
    *size=target;return pixels;
}
static BYTE *hd_sd_logo(UINT width) {
    WCHAR path[MAX_PATH];path[0]=0;
    if(!hd_append(path,MAX_PATH,ui_hd_game_root) || !hd_append(path,MAX_PATH,L"textures\\sd\\Pics\\Hi-res\\XLLOGO.tga"))return 0;
    HANDLE file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,0,OPEN_EXISTING,0,0);if(file==INVALID_HANDLE_VALUE)return 0;
    LARGE_INTEGER length;BYTE *raw=0,*pixels=0;DWORD read;
    if(!GetFileSizeEx(file,&length) || length.QuadPart<18 || length.QuadPart>2*1024*1024)goto done;
    raw=VirtualAlloc(0,(UINT)length.QuadPart,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    if(!raw || !ReadFile(file,raw,(DWORD)length.QuadPart,&read,0) || read!=(DWORD)length.QuadPart)goto done;
    UINT start=18+raw[0];
    if(raw[2]!=2 || raw[16]!=16 || *(WORD *)(raw+12)!=256 || *(WORD *)(raw+14)!=256 || start+256*256*2>(UINT)length.QuadPart)goto done;
    pixels=VirtualAlloc(0,width*width*4,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    if(pixels)for(UINT y=0;y<width;++y)for(UINT x=0;x<width;++x){
        WORD c=((WORD *)(raw+start))[(y*256/width)*256+x*256/width];BYTE *p=pixels+(y*width+x)*4;
        p[0]=(BYTE)((c&31)*255/31);p[1]=(BYTE)(((c>>5)&31)*255/31);p[2]=(BYTE)(((c>>10)&31)*255/31);p[3]=(c&32767)?255:0;
    }
done:
    if(raw)VirtualFree(raw,0,MEM_RELEASE);CloseHandle(file);return pixels;
}
static BYTE *hd_logo_overlay(BYTE *base,UINT *size,const BYTE *banner,UINT bw,UINT bh) {
    if(!bw || !bh)return base;
    UINT desired=bh*4,horizontal=(bw*256+461)/462;
    if(horizontal>desired)desired=horizontal;
    UINT width=hd_page_size(desired);if(base && *size>width)width=*size;
    if(!base)base=hd_sd_logo(width);
    else if(*size!=width){
        BYTE *grown=VirtualAlloc(0,width*width*4,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
        if(!grown)return base;
        for(UINT y=0;y<width;++y)for(UINT x=0;x<width;++x)copy(grown+(y*width+x)*4,base+((y**size/width)**size+x**size/width)*4,4);
        VirtualFree(base,0,MEM_RELEASE);base=grown;
    }
    if(!base)return 0;*size=width;
    for(UINT part=0;part<2;++part){
        UINT top=part?width/4:0,end=part?width/2:width/4,right=width*(part?210:254)/256;
        for(UINT y=top;y<end;++y)for(UINT x=0;x<right;++x){
            UINT sx=((part?254:0)*(width/256)+x)*bw/(464*(width/256)),sy=(y-top)*bh/(width/4);
            copy(base+(y*width+x)*4,banner+(sy*bw+sx)*4,4);
        }
    }
    return base;
}
#include "hd_regions.c"
static BYTE *hd_asset_pixels(const char *filename,UINT page,UINT pages,UINT *width) {
    WCHAR path[MAX_PATH];BOOL strip=FALSE;BYTE *pixels=0;
    if(hd_find(filename,page,path,&strip))pixels=hd_square_png(path,page,pages,strip,width);
    if(page==0 && startup_name(filename,"xllogo.tga") && hd_path(path,L"menu-banner-logo.png") && hd_exists(path)){
        UINT w=0,h=0;BYTE *banner=read_art_png(path,&w,&h);
        if(banner){pixels=hd_logo_overlay(pixels,width,banner,w,h);VirtualFree(banner,0,MEM_RELEASE);}
    }
    pixels=hd_compose_regions(filename,page,pages,pixels,width);
    if(pixels && !hd_world_name(filename))hd_premultiply(pixels,*width**width);
    return pixels;
}
