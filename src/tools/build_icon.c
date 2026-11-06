#include "tools/build_tool.h"

#include "core/wo_win.h"

#include <wincodec.h>

static const UINT sizes[] = {16, 24, 32, 48, 64, 128, 256};

static bool hr_ok(HRESULT hr, const char *what, WoError *error) {
    if (FAILED(hr)) {
        return wo_fail_code(error, WO_ERR_INTERNAL, (uint32_t)hr, "%s failed (0x%08lx).", what,
                            (unsigned long)hr);
    }
    return true;
}

static bool encode_png(IWICImagingFactory *factory, IWICBitmapSource *source, WoBuf *out,
                       WoError *error) {
    IStream *stream = NULL;
    IWICBitmapEncoder *encoder = NULL;
    IWICBitmapFrameEncode *frame = NULL;
    UINT width = 0;
    UINT height = 0;
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    bool ok = hr_ok(CreateStreamOnHGlobal(NULL, TRUE, &stream), "CreateStreamOnHGlobal", error) &&
              hr_ok(IWICImagingFactory_CreateEncoder(factory, &GUID_ContainerFormatPng, NULL, &encoder),
                    "CreateEncoder", error) &&
              hr_ok(IWICBitmapEncoder_Initialize(encoder, stream, WICBitmapEncoderNoCache),
                    "Encoder.Initialize", error) &&
              hr_ok(IWICBitmapEncoder_CreateNewFrame(encoder, &frame, NULL), "CreateNewFrame", error) &&
              hr_ok(IWICBitmapFrameEncode_Initialize(frame, NULL), "Frame.Initialize", error) &&
              hr_ok(IWICBitmapSource_GetSize(source, &width, &height), "GetSize", error) &&
              hr_ok(IWICBitmapFrameEncode_SetSize(frame, width, height), "SetSize", error) &&
              hr_ok(IWICBitmapFrameEncode_SetPixelFormat(frame, &format), "SetPixelFormat", error) &&
              hr_ok(IWICBitmapFrameEncode_WriteSource(frame, source, NULL), "WriteSource", error) &&
              hr_ok(IWICBitmapFrameEncode_Commit(frame), "Frame.Commit", error) &&
              hr_ok(IWICBitmapEncoder_Commit(encoder), "Encoder.Commit", error);
    if (ok) {
        HGLOBAL memory = NULL;
        ok = hr_ok(GetHGlobalFromStream(stream, &memory), "GetHGlobalFromStream", error);
        STATSTG stat;
        ok = ok && hr_ok(IStream_Stat(stream, &stat, STATFLAG_NONAME), "Stream.Stat", error);
        void *bytes = ok ? GlobalLock(memory) : NULL;
        ok = ok && bytes && wo_buf_append(out, bytes, (size_t)stat.cbSize.QuadPart, error);
        if (bytes) {
            GlobalUnlock(memory);
        }
    }
    if (frame) {
        IWICBitmapFrameEncode_Release(frame);
    }
    if (encoder) {
        IWICBitmapEncoder_Release(encoder);
    }
    if (stream) {
        IStream_Release(stream);
    }
    return ok;
}

bool build_icon(const char *png_path, WoBuf *ico, WoError *error) {
    HRESULT init = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    wchar_t *wide = wo_wide_path(png_path, error);
    IWICImagingFactory *factory = NULL;
    IWICBitmapDecoder *decoder = NULL;
    IWICBitmapFrameDecode *decoded = NULL;
    IWICBitmapSource *source = NULL;
    WoBuf frames[WO_COUNT(sizes)];
    memset(frames, 0, sizeof(frames));
    bool ok = wide &&
              hr_ok(CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER,
                                     &IID_IWICImagingFactory, (void **)&factory),
                    "Creating the imaging factory", error) &&
              hr_ok(IWICImagingFactory_CreateDecoderFromFilename(
                        factory, wide, NULL, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder),
                    "Reading the icon artwork", error) &&
              hr_ok(IWICBitmapDecoder_GetFrame(decoder, 0, &decoded), "GetFrame", error) &&
              hr_ok(WICConvertBitmapSource(&GUID_WICPixelFormat32bppBGRA,
                                           (IWICBitmapSource *)decoded, &source),
                    "Converting the icon artwork", error);
    for (size_t i = 0; ok && i < WO_COUNT(sizes); i++) {
        IWICBitmapScaler *scaler = NULL;
        ok = hr_ok(IWICImagingFactory_CreateBitmapScaler(factory, &scaler), "CreateBitmapScaler",
                   error) &&
             hr_ok(IWICBitmapScaler_Initialize(scaler, source, sizes[i], sizes[i],
                                               WICBitmapInterpolationModeFant),
                   "Scaling the icon", error) &&
             encode_png(factory, (IWICBitmapSource *)scaler, &frames[i], error);
        if (scaler) {
            IWICBitmapScaler_Release(scaler);
        }
    }
    if (ok) {
        uint8_t header[6] = {0, 0, 1, 0, (uint8_t)WO_COUNT(sizes), 0};
        ok = wo_buf_append(ico, header, sizeof(header), error);
        uint32_t offset = (uint32_t)(6 + 16 * WO_COUNT(sizes));
        for (size_t i = 0; ok && i < WO_COUNT(sizes); i++) {
            uint8_t entry[16] = {0};
            entry[0] = (uint8_t)(sizes[i] == 256 ? 0 : sizes[i]);
            entry[1] = entry[0];
            wo_put_le16(entry + 4, 1);
            wo_put_le16(entry + 6, 32);
            wo_put_le32(entry + 8, (uint32_t)frames[i].len);
            wo_put_le32(entry + 12, offset);
            offset += (uint32_t)frames[i].len;
            ok = wo_buf_append(ico, entry, sizeof(entry), error);
        }
        for (size_t i = 0; ok && i < WO_COUNT(sizes); i++) {
            ok = wo_buf_append(ico, frames[i].data, frames[i].len, error);
        }
    }
    for (size_t i = 0; i < WO_COUNT(sizes); i++) {
        wo_buf_free(&frames[i]);
    }
    if (source) {
        IWICBitmapSource_Release(source);
    }
    if (decoded) {
        IWICBitmapFrameDecode_Release(decoded);
    }
    if (decoder) {
        IWICBitmapDecoder_Release(decoder);
    }
    if (factory) {
        IWICImagingFactory_Release(factory);
    }
    wo_free(wide);
    if (SUCCEEDED(init)) {
        CoUninitialize();
    }
    return ok;
}
