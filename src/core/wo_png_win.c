#include "core/wo_png.h"
#include "core/wo_fs.h"
#include "core/wo_win.h"

#include <objidl.h>
#include <string.h>
#include <wincodec.h>

typedef struct Wic {
    IWICImagingFactory *factory;
    bool uninitialize;
} Wic;

static bool wic_fail(WoError *error, HRESULT hr, const char *what, const char *path) {
    return wo_fail_code(error, WO_ERR_FORMAT, (uint32_t)hr,
                        "Cannot %s the image \"%s\" (Windows Imaging Component error 0x%08lx).",
                        what, path, (unsigned long)hr);
}

static bool wic_begin(Wic *wic, WoError *error) {
    memset(wic, 0, sizeof(*wic));
    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (SUCCEEDED(hr)) {
        wic->uninitialize = true;
    } else if (hr != RPC_E_CHANGED_MODE) {
        return wo_fail_code(error, WO_ERR_INTERNAL, (uint32_t)hr,
                            "COM is unavailable (error 0x%08lx).", (unsigned long)hr);
    }
    hr = CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER,
                          &IID_IWICImagingFactory, (void **)&wic->factory);
    if (FAILED(hr)) {
        if (wic->uninitialize) {
            CoUninitialize();
        }
        memset(wic, 0, sizeof(*wic));
        return wo_fail_code(error, WO_ERR_INTERNAL, (uint32_t)hr,
                            "The Windows Imaging Component is unavailable (error 0x%08lx).",
                            (unsigned long)hr);
    }
    return true;
}

static void wic_end(Wic *wic) {
    if (wic->factory) {
        IWICImagingFactory_Release(wic->factory);
    }
    if (wic->uninitialize) {
        CoUninitialize();
    }
    memset(wic, 0, sizeof(*wic));
}

typedef struct Decoded {
    WoBuf file;
    IWICStream *stream;
    IWICBitmapDecoder *decoder;
    IWICBitmapFrameDecode *frame;
    uint32_t width;
    uint32_t height;
} Decoded;

static void decoded_free(Decoded *d) {
    if (d->frame) {
        IWICBitmapFrameDecode_Release(d->frame);
    }
    if (d->decoder) {
        IWICBitmapDecoder_Release(d->decoder);
    }
    if (d->stream) {
        IWICStream_Release(d->stream);
    }
    wo_buf_free(&d->file);
    memset(d, 0, sizeof(*d));
}

/* Read the file into memory (long paths and sharing are handled by wo_fs)
 * and open its first frame. */
static bool open_frame(Wic *wic, const char *path, Decoded *d, WoError *error) {
    memset(d, 0, sizeof(*d));
    if (!wo_read_file(path, WO_PNG_MAX_FILE_BYTES, &d->file, error)) {
        decoded_free(d);
        return false;
    }
    HRESULT hr = IWICImagingFactory_CreateStream(wic->factory, &d->stream);
    if (SUCCEEDED(hr)) {
        hr = IWICStream_InitializeFromMemory(d->stream, d->file.data, (DWORD)d->file.len);
    }
    if (SUCCEEDED(hr)) {
        hr = IWICImagingFactory_CreateDecoderFromStream(wic->factory, (IStream *)d->stream, NULL,
                                                        WICDecodeMetadataCacheOnDemand,
                                                        &d->decoder);
    }
    if (SUCCEEDED(hr)) {
        hr = IWICBitmapDecoder_GetFrame(d->decoder, 0, &d->frame);
    }
    UINT w = 0;
    UINT h = 0;
    if (SUCCEEDED(hr)) {
        hr = IWICBitmapFrameDecode_GetSize(d->frame, &w, &h);
    }
    if (FAILED(hr)) {
        decoded_free(d);
        return wic_fail(error, hr, "read", path);
    }
    if (w < 1 || h < 1 || w > WO_PNG_MAX_SIDE || h > WO_PNG_MAX_SIDE ||
        (uint64_t)w * h * 4u > WO_PNG_MAX_PIXEL_BYTES) {
        decoded_free(d);
        return wo_fail(error, WO_ERR_LIMIT, "Invalid image dimensions (%ux%u): %s", w, h, path);
    }
    d->width = w;
    d->height = h;
    return true;
}

static void swap_red_blue(WoImage *image) {
    size_t count = (size_t)image->width * image->height;
    for (size_t i = 0; i < count; i++) {
        uint8_t *p = image->pixels + i * 4;
        uint8_t t = p[0];
        p[0] = p[2];
        p[2] = t;
    }
}

bool wo_png_read(const char *path, WoPixelOrder order, WoImage *out, WoError *error) {
    memset(out, 0, sizeof(*out));
    Wic wic;
    if (!wic_begin(&wic, error)) {
        return false;
    }
    Decoded d;
    bool ok = open_frame(&wic, path, &d, error);
    IWICFormatConverter *converter = NULL;
    HRESULT hr = S_OK;
    if (ok) {
        hr = IWICImagingFactory_CreateFormatConverter(wic.factory, &converter);
        if (SUCCEEDED(hr)) {
            hr = IWICFormatConverter_Initialize(converter, (IWICBitmapSource *)d.frame,
                                                &GUID_WICPixelFormat32bppBGRA,
                                                WICBitmapDitherTypeNone, NULL, 0.0,
                                                WICBitmapPaletteTypeCustom);
        }
        ok = SUCCEEDED(hr) || wic_fail(error, hr, "convert", path);
    }
    size_t bytes = ok ? (size_t)d.width * d.height * 4u : 0;
    uint8_t *pixels = ok ? wo_alloc(bytes, error) : NULL;
    ok = pixels != NULL;
    if (ok) {
        hr = IWICFormatConverter_CopyPixels(converter, NULL, d.width * 4u, (UINT)bytes, pixels);
        ok = SUCCEEDED(hr) || wic_fail(error, hr, "decode", path);
    }
    if (converter) {
        IWICFormatConverter_Release(converter);
    }
    if (ok) {
        out->width = d.width;
        out->height = d.height;
        out->pixels = pixels;
        if (order == WO_PIXELS_RGBA) {
            swap_red_blue(out);
        }
    } else {
        wo_free(pixels);
    }
    decoded_free(&d);
    wic_end(&wic);
    return ok;
}

bool wo_png_read_size(const char *path, uint32_t *width, uint32_t *height, WoError *error) {
    *width = 0;
    *height = 0;
    Wic wic;
    if (!wic_begin(&wic, error)) {
        return false;
    }
    Decoded d;
    bool ok = open_frame(&wic, path, &d, error);
    if (ok) {
        *width = d.width;
        *height = d.height;
        decoded_free(&d);
    }
    wic_end(&wic);
    return ok;
}

bool wo_png_has_transparency(const char *path, bool *transparent, WoError *error) {
    *transparent = false;
    WoImage image;
    if (!wo_png_read(path, WO_PIXELS_BGRA, &image, error)) {
        return false;
    }
    *transparent = wo_image_has_transparency(&image);
    wo_image_free(&image);
    return true;
}

static bool encode(Wic *wic, const WoImage *bgra, IStream *stream, const char *path,
                   WoError *error) {
    IWICBitmapEncoder *encoder = NULL;
    IWICBitmapFrameEncode *frame = NULL;
    IPropertyBag2 *props = NULL;
    HRESULT hr = IWICImagingFactory_CreateEncoder(wic->factory, &GUID_ContainerFormatPng, NULL,
                                                  &encoder);
    if (SUCCEEDED(hr)) {
        hr = IWICBitmapEncoder_Initialize(encoder, stream, WICBitmapEncoderNoCache);
    }
    if (SUCCEEDED(hr)) {
        hr = IWICBitmapEncoder_CreateNewFrame(encoder, &frame, &props);
    }
    if (SUCCEEDED(hr)) {
        hr = IWICBitmapFrameEncode_Initialize(frame, props);
    }
    if (SUCCEEDED(hr)) {
        hr = IWICBitmapFrameEncode_SetSize(frame, bgra->width, bgra->height);
    }
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    if (SUCCEEDED(hr)) {
        hr = IWICBitmapFrameEncode_SetPixelFormat(frame, &format);
    }
    if (SUCCEEDED(hr) && !IsEqualGUID(&format, &GUID_WICPixelFormat32bppBGRA)) {
        hr = WINCODEC_ERR_UNSUPPORTEDPIXELFORMAT;
    }
    UINT stride = bgra->width * 4u;
    if (SUCCEEDED(hr)) {
        hr = IWICBitmapFrameEncode_WritePixels(frame, bgra->height, stride, stride * bgra->height,
                                               bgra->pixels);
    }
    if (SUCCEEDED(hr)) {
        hr = IWICBitmapFrameEncode_Commit(frame);
    }
    if (SUCCEEDED(hr)) {
        hr = IWICBitmapEncoder_Commit(encoder);
    }
    if (props) {
        IPropertyBag2_Release(props);
    }
    if (frame) {
        IWICBitmapFrameEncode_Release(frame);
    }
    if (encoder) {
        IWICBitmapEncoder_Release(encoder);
    }
    return SUCCEEDED(hr) || wic_fail(error, hr, "write", path);
}

bool wo_png_write(const char *path, const WoImage *image, WoPixelOrder order, WoError *error) {
    if (image->width < 1 || image->height < 1 || image->width > WO_PNG_MAX_SIDE ||
        image->height > WO_PNG_MAX_SIDE ||
        (uint64_t)image->width * image->height * 4u > WO_PNG_MAX_PIXEL_BYTES) {
        return wo_fail(error, WO_ERR_LIMIT, "Invalid image dimensions (%ux%u) for \"%s\".",
                       image->width, image->height, path);
    }
    WoImage bgra = *image;
    if (order == WO_PIXELS_RGBA) {
        size_t bytes = (size_t)image->width * image->height * 4u;
        bgra.pixels = wo_alloc(bytes, error);
        if (!bgra.pixels) {
            return false;
        }
        memcpy(bgra.pixels, image->pixels, bytes);
        swap_red_blue(&bgra);
    }
    Wic wic;
    bool ok = wic_begin(&wic, error);
    IStream *stream = NULL;
    if (ok) {
        HRESULT hr = CreateStreamOnHGlobal(NULL, TRUE, &stream);
        ok = SUCCEEDED(hr) || wic_fail(error, hr, "write", path);
    }
    ok = ok && encode(&wic, &bgra, stream, path, error);
    if (ok) {
        HGLOBAL memory = NULL;
        STATSTG stat;
        HRESULT hr = GetHGlobalFromStream(stream, &memory);
        if (SUCCEEDED(hr)) {
            hr = IStream_Stat(stream, &stat, STATFLAG_NONAME);
        }
        void *bytes = SUCCEEDED(hr) ? GlobalLock(memory) : NULL;
        ok = bytes != NULL || wic_fail(error, FAILED(hr) ? hr : E_OUTOFMEMORY, "write", path);
        if (bytes) {
            ok = wo_write_file_atomic(path, bytes, (size_t)stat.cbSize.QuadPart, error);
            GlobalUnlock(memory);
        }
    }
    if (stream) {
        IStream_Release(stream);
    }
    wic_end(&wic);
    if (bgra.pixels != image->pixels) {
        wo_free(bgra.pixels);
    }
    return ok;
}
