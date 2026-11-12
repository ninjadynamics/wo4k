/* WIC decodes the sole artwork PNGs directly to GPU upload memory. */
#include <wincodec.h>
static const GUID art_wic_class={0xcacaf262,0x9370,0x4615,{0xa1,0x3b,0x9f,0x55,0x39,0xda,0x4c,0x0a}};
static const GUID art_wic_iid={0xec5ec8a9,0xc395,0x4314,{0x9c,0x77,0x54,0xd7,0xa9,0x35,0xff,0x70}};
static const GUID art_bgra={0x6fddc324,0x4e03,0x4bfe,{0xb1,0x85,0x3d,0x77,0x76,0x8d,0xc9,0x0f}};
/* A page can take a hundred small PNGs. Each read opens COM and the codec
 * and closes them again; holding both open across the page keeps the codec
 * loaded in between. */
typedef struct ArtPngHold {HRESULT initialized;IWICImagingFactory *factory;} ArtPngHold;
static void art_png_hold(ArtPngHold *hold) {
    hold->factory=0;hold->initialized=CoInitializeEx(0,COINIT_APARTMENTTHREADED);
    CoCreateInstance(&art_wic_class,0,CLSCTX_INPROC_SERVER,&art_wic_iid,(void **)&hold->factory);
}
static void art_png_release(ArtPngHold *hold) {
    if(hold->factory)hold->factory->lpVtbl->Release(hold->factory);
    if(hold->initialized>=0)CoUninitialize();
}
static BYTE *read_art_png(const WCHAR *path,UINT *width,UINT *height) {
    IWICImagingFactory *factory=0;IWICBitmapDecoder *decoder=0;IWICBitmapFrameDecode *frame=0;IWICFormatConverter *converter=0;
    BYTE *pixels=0;UINT w=0,h=0;
    HRESULT initialized=CoInitializeEx(0,COINIT_APARTMENTTHREADED);
    HRESULT hr=CoCreateInstance(&art_wic_class,0,CLSCTX_INPROC_SERVER,&art_wic_iid,(void **)&factory);
    if(hr>=0)hr=factory->lpVtbl->CreateDecoderFromFilename(factory,path,0,GENERIC_READ,WICDecodeMetadataCacheOnLoad,&decoder);
    if(hr>=0)hr=decoder->lpVtbl->GetFrame(decoder,0,&frame);
    if(hr>=0)hr=frame->lpVtbl->GetSize(frame,&w,&h);
    if(hr>=0 && (!w || !h || w>16384 || h>16384 || (ULONGLONG)w*h*4>256*1024*1024))hr=E_FAIL;
    if(hr>=0)hr=factory->lpVtbl->CreateFormatConverter(factory,&converter);
    if(hr>=0)hr=converter->lpVtbl->Initialize(converter,(IWICBitmapSource *)frame,&art_bgra,WICBitmapDitherTypeNone,0,0,WICBitmapPaletteTypeCustom);
    if(hr>=0){pixels=VirtualAlloc(0,w*h*4,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);if(!pixels)hr=E_OUTOFMEMORY;}
    if(hr>=0)hr=converter->lpVtbl->CopyPixels(converter,0,w*4,w*h*4,pixels);
    if(converter)converter->lpVtbl->Release(converter);
    if(frame)frame->lpVtbl->Release(frame);
    if(decoder)decoder->lpVtbl->Release(decoder);
    if(factory)factory->lpVtbl->Release(factory);
    if(initialized>=0)CoUninitialize();
    if(hr<0){if(pixels)VirtualFree(pixels,0,MEM_RELEASE);wo_render_event("art_png_decode_error",(DWORD)hr);return 0;}
    *width=w;*height=h;return pixels;
}
