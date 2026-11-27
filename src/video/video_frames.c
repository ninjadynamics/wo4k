#include "video/video_frames.h"

#include <stdio.h>
#include <string.h>

#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>

/* A frame this far behind the clock is dropped to catch up... */
#define FRAMES_LATE_HNS 1000000
/* ...unless none has been shown for this long: a decoder slower than the
 * movie shows fewer frames, never a frozen one. */
#define FRAMES_STALE_MS 100

typedef HRESULT (WINAPI *CreateAttributesFn)(IMFAttributes **attributes, UINT32 size);
typedef HRESULT (WINAPI *CreateMediaTypeFn)(IMFMediaType **type);
typedef HRESULT (WINAPI *CreateReaderFn)(LPCWSTR url, IMFAttributes *attributes,
    IMFSourceReader **reader);

struct VideoFrames {
    /* The worker creates, uses and releases the reader: a synchronous
     * reader made on the player's single-threaded apartment delivered no
     * samples to another thread. */
    IMFSourceReader *reader;
    wchar_t *path;
    bool local_mode;
    HWND notify_window;
    VideoPixelFormat format;
    bool bt709;
    bool full_range;
    VideoInfo info; /* the size shown, fixed at open */
    HRESULT open_error;
    const char *open_what;
    HANDLE opened; /* set once the reader is open, or has failed */
    HANDLE run;    /* set by video_frames_run, or to stop */
    VideoFrameSlots slots;
    VideoFrameBuffer local;
    /* The decoder's current frames, which hold the size shown. */
    uint32_t source_width;
    uint32_t source_height;
    LONG source_stride; /* negative: bottom-up rows (RGB32) */
    HANDLE thread;
    volatile LONG stop;
    volatile LONG cued;
    volatile LONG ended;
    volatile LONG dropped;
    VideoFramesClock clock;
    void *context;
};

bool video_frame_slots_create(VideoFrameSlots *slots, uint32_t width,
    uint32_t height)
{
    if (width == 0 || height == 0 || width > WO_VIDEO_FRAMES_MAX_WIDTH ||
        height > WO_VIDEO_FRAMES_MAX_HEIGHT) {
        return false;
    }
    DWORD stride = width * 4;
    DWORD slot_size = stride * height;
    SIZE_T total = WO_VIDEO_FRAMES_HEADER + (SIZE_T)WO_VIDEO_FRAMES_SLOTS * slot_size;
    wchar_t name[64];
    swprintf(name, sizeof(name) / sizeof(name[0]), L"%ls%lu", WO_VIDEO_FRAMES_NAME,
        (unsigned long)GetCurrentProcessId());
    slots->mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE,
        (DWORD)((ULONGLONG)total >> 32), (DWORD)total, name);
    if (slots->mapping != NULL && GetLastError() == ERROR_ALREADY_EXISTS) {
        /* Only ever one per process; never take over a stale one. */
        CloseHandle(slots->mapping);
        slots->mapping = NULL;
    }
    if (slots->mapping != NULL) {
        slots->view = MapViewOfFile(slots->mapping, FILE_MAP_WRITE, 0, 0, total);
    }
    if (slots->view == NULL) {
        video_frame_slots_close(slots);
        return false;
    }
    slots->slots = (BYTE *)slots->view + WO_VIDEO_FRAMES_HEADER;
    WoVideoFrames *header = slots->view;
    header->header_size = WO_VIDEO_FRAMES_HEADER;
    header->width = width;
    header->height = height;
    header->stride = stride;
    header->slot_size = slot_size;
    header->latest = -1;
    /* Last, so a reader never sees a header that is not filled in. */
    InterlockedExchange((volatile LONG *)&header->magic, (LONG)WO_VIDEO_FRAMES_MAGIC);
    return true;
}

/* Under the slot's sequence number, odd while it is written. */
LONG video_frame_slots_write(VideoFrameSlots *slots, const BYTE *top,
    LONG pitch, LONGLONG time_hns)
{
    WoVideoFrames *header = slots->view;
    LONG slot = header->latest == 0 ? 1 : 0;
    BYTE *target = slots->slots + (SIZE_T)slot * header->slot_size;
    DWORD row = header->width * 4;
    InterlockedIncrement(&header->sequence[slot]);
    for (DWORD y = 0; y < header->height; ++y) {
        memcpy(target + (SIZE_T)y * header->stride, top + (LONG_PTR)y * pitch, row);
    }
    header->time_hns[slot] = time_hns;
    InterlockedIncrement(&header->sequence[slot]);
    return slot;
}

void video_frame_slots_publish(VideoFrameSlots *slots, LONG slot)
{
    InterlockedExchange(&slots->view->latest, slot);
    InterlockedIncrement(&slots->view->frames);
    InterlockedIncrement(&slots->published);
}

void video_frame_slots_close(VideoFrameSlots *slots)
{
    if (slots->view != NULL) {
        UnmapViewOfFile(slots->view);
        slots->view = NULL;
    }
    if (slots->mapping != NULL) {
        CloseHandle(slots->mapping);
        slots->mapping = NULL;
    }
    slots->slots = NULL;
}

bool video_frame_buffer_create(VideoFrameBuffer *buffer, uint32_t width,
    uint32_t height, VideoPixelFormat format, bool bt709, bool full_range,
    HWND notify_window)
{
    if (width == 0 || height == 0 || width > WO_VIDEO_FRAMES_MAX_WIDTH ||
        height > WO_VIDEO_FRAMES_MAX_HEIGHT) {
        return false;
    }
    uint32_t y_stride = 0;
    uint32_t uv_stride = 0;
    SIZE_T uv_offset = 0;
    SIZE_T slot_size = 0;
    if (format == VIDEO_PIXEL_NV12) {
        uint32_t uv_w = (width + 1u) / 2u;
        uint32_t uv_h = (height + 1u) / 2u;
        y_stride = width;
        uv_stride = uv_w * 2u;
        uv_offset = (SIZE_T)y_stride * height;
        slot_size = uv_offset + (SIZE_T)uv_stride * uv_h;
    } else {
        y_stride = width * 4u;
        slot_size = (SIZE_T)y_stride * height;
    }
    BYTE *data = HeapAlloc(GetProcessHeap(), 0,
        (SIZE_T)WO_VIDEO_FRAMES_SLOTS * slot_size);
    if (data == NULL) {
        return false;
    }
    memset(buffer, 0, sizeof(*buffer));
    buffer->data = data;
    buffer->width = width;
    buffer->height = height;
    buffer->y_stride = y_stride;
    buffer->uv_stride = uv_stride;
    buffer->uv_offset = uv_offset;
    buffer->slot_size = slot_size;
    buffer->format = format;
    buffer->bt709 = bt709;
    buffer->full_range = full_range;
    buffer->latest = -1;
    buffer->notify_window = notify_window;
    return true;
}

LONG video_frame_buffer_write_bgra(VideoFrameBuffer *buffer, const BYTE *top,
    LONG pitch)
{
    LONG slot = buffer->latest == 0 ? 1 : 0;
    BYTE *target = buffer->data + (SIZE_T)slot * buffer->slot_size;
    uint32_t row = buffer->width * 4u;
    InterlockedIncrement(&buffer->sequence[slot]);
    if (pitch == (LONG)buffer->y_stride && buffer->y_stride == row) {
        memcpy(target, top, (SIZE_T)row * buffer->height);
    } else {
        for (uint32_t y = 0; y < buffer->height; ++y) {
            memcpy(target + (SIZE_T)y * buffer->y_stride,
                top + (LONG_PTR)y * pitch, row);
        }
    }
    InterlockedIncrement(&buffer->sequence[slot]);
    return slot;
}

LONG video_frame_buffer_write_nv12(VideoFrameBuffer *buffer,
    const BYTE *y_top, LONG y_pitch, const BYTE *uv_top, LONG uv_pitch)
{
    LONG slot = buffer->latest == 0 ? 1 : 0;
    BYTE *y_dst = buffer->data + (SIZE_T)slot * buffer->slot_size;
    BYTE *uv_dst = y_dst + buffer->uv_offset;
    uint32_t uv_h = (buffer->height + 1u) / 2u;
    InterlockedIncrement(&buffer->sequence[slot]);
    if (y_pitch == (LONG)buffer->y_stride) {
        memcpy(y_dst, y_top, (SIZE_T)buffer->y_stride * buffer->height);
    } else {
        for (uint32_t y = 0; y < buffer->height; ++y) {
            memcpy(y_dst + (SIZE_T)y * buffer->y_stride,
                y_top + (LONG_PTR)y * y_pitch, buffer->width);
        }
    }
    if (uv_pitch == (LONG)buffer->uv_stride) {
        memcpy(uv_dst, uv_top, (SIZE_T)buffer->uv_stride * uv_h);
    } else {
        for (uint32_t y = 0; y < uv_h; ++y) {
            memcpy(uv_dst + (SIZE_T)y * buffer->uv_stride,
                uv_top + (LONG_PTR)y * uv_pitch, buffer->uv_stride);
        }
    }
    InterlockedIncrement(&buffer->sequence[slot]);
    return slot;
}

void video_frame_buffer_publish(VideoFrameBuffer *buffer, LONG slot)
{
    InterlockedExchange(&buffer->latest, slot);
    InterlockedIncrement(&buffer->published);
    if (buffer->notify_window != NULL &&
        InterlockedCompareExchange(&buffer->notified, 1, 0) == 0) {
        PostMessageW(buffer->notify_window, WM_NULL, 0, 0);
    }
}

bool video_frame_buffer_present(VideoFrameBuffer *buffer, VideoCrt *crt,
    LONG *last_shown, bool sync)
{
    if (buffer == NULL || crt == NULL || buffer->data == NULL) {
        return false;
    }
    InterlockedExchange(&buffer->notified, 0);
    LONG count = InterlockedCompareExchange(&buffer->published, 0, 0);
    LONG slot = InterlockedCompareExchange(&buffer->latest, 0, 0);
    if (count == *last_shown || slot < 0 || slot >= WO_VIDEO_FRAMES_SLOTS) {
        return false;
    }
    LONG before = InterlockedCompareExchange(&buffer->sequence[slot], 0, 0);
    if ((before & 1) != 0) {
        return false;
    }
    MemoryBarrier();
    const BYTE *base = buffer->data + (SIZE_T)slot * buffer->slot_size;
    bool uploaded = false;
    if (buffer->format == VIDEO_PIXEL_NV12) {
        uploaded = video_crt_upload_nv12(crt, buffer->width, buffer->height,
            base, buffer->y_stride, base + buffer->uv_offset,
            buffer->uv_stride, buffer->bt709, buffer->full_range);
    } else {
        uploaded = video_crt_upload_bgra(crt, buffer->width, buffer->height,
            base, buffer->y_stride);
    }
    MemoryBarrier();
    LONG after = InterlockedCompareExchange(&buffer->sequence[slot], 0, 0);
    if (!uploaded || after != before) {
        return false;
    }
    *last_shown = count;
    return video_crt_render(crt, sync);
}

void video_frame_buffer_close(VideoFrameBuffer *buffer)
{
    if (buffer->data != NULL) {
        HeapFree(GetProcessHeap(), 0, buffer->data);
        buffer->data = NULL;
    }
    buffer->latest = -1;
}

typedef void (*AnyFunction)(void);

static AnyFunction frames_symbol(const wchar_t *library, const char *name)
{
    HMODULE module = LoadLibraryExW(library, NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    return module != NULL ? (AnyFunction)GetProcAddress(module, name) : NULL;
}

/* Copies the top-left of one sample, the size shown, into a free slot and
 * publishes it as the newest. The first frame creates the sink; false when
 * it cannot. */
static bool frames_publish(VideoFrames *frames, IMFSample *sample, LONGLONG time)
{
    IMFMediaBuffer *buffer = NULL;
    IMF2DBuffer *plane = NULL;
    BYTE *first = NULL;
    LONG pitch = frames->source_stride;
    BYTE *data = NULL;
    DWORD length = 0;
    bool locked2d = false;
    if (frames->local_mode) {
        if (frames->local.data == NULL &&
            !video_frame_buffer_create(&frames->local, frames->info.width,
                frames->info.height, frames->format, frames->bt709,
                frames->full_range, frames->notify_window)) {
            return false;
        }
    } else {
        if (frames->slots.view == NULL &&
            !video_frame_slots_create(&frames->slots, frames->info.width,
                frames->info.height)) {
            return false;
        }
    }
    if (FAILED(IMFSample_ConvertToContiguousBuffer(sample, &buffer))) {
        return true;
    }

    if (frames->format == VIDEO_PIXEL_NV12) {
        uint32_t uv_h = (frames->info.height + 1u) / 2u;
        uint32_t src_uv_h = (frames->source_height + 1u) / 2u;
        uint32_t row = frames->info.width;
        const BYTE *uv_first = NULL;
        if (SUCCEEDED(IMFMediaBuffer_QueryInterface(buffer, &IID_IMF2DBuffer,
                (void **)&plane)) &&
            SUCCEEDED(IMF2DBuffer_Lock2D(plane, &first, &pitch))) {
            locked2d = true;
            if (pitch <= 0 || (uint32_t)pitch < row) {
                first = NULL;
            } else {
                uv_first = first + (SIZE_T)(uint32_t)pitch * frames->source_height;
            }
        } else if (SUCCEEDED(IMFMediaBuffer_Lock(buffer, &data, NULL, &length))) {
            if (pitch <= 0 || (uint32_t)pitch < row ||
                length < (ULONGLONG)(uint32_t)pitch * (frames->source_height + src_uv_h) ||
                src_uv_h < uv_h) {
                IMFMediaBuffer_Unlock(buffer);
                data = NULL;
            } else {
                first = data;
                uv_first = data + (SIZE_T)(uint32_t)pitch * frames->source_height;
            }
        }
        if (first != NULL && uv_first != NULL) {
            video_frame_buffer_publish(&frames->local,
                video_frame_buffer_write_nv12(&frames->local, first, pitch,
                    uv_first, pitch));
        }
    } else {
        DWORD row = frames->info.width * 4;
        if (SUCCEEDED(IMFMediaBuffer_QueryInterface(buffer, &IID_IMF2DBuffer,
                (void **)&plane)) &&
            SUCCEEDED(IMF2DBuffer_Lock2D(plane, &first, &pitch))) {
            locked2d = true;
            if ((pitch < 0 ? -(DWORD)pitch : (DWORD)pitch) < row) {
                first = NULL;
            }
        } else if (SUCCEEDED(IMFMediaBuffer_Lock(buffer, &data, NULL, &length))) {
            DWORD span = pitch < 0 ? -(DWORD)pitch : (DWORD)pitch;
            if (span < row || length < (ULONGLONG)span * (frames->source_height - 1) + row) {
                IMFMediaBuffer_Unlock(buffer);
                data = NULL;
            } else {
                first = pitch < 0 ? data + (SIZE_T)span * (frames->source_height - 1) : data;
            }
        }
        if (first != NULL) {
            if (frames->local_mode) {
                video_frame_buffer_publish(&frames->local,
                    video_frame_buffer_write_bgra(&frames->local, first, pitch));
            } else {
                video_frame_slots_publish(&frames->slots,
                    video_frame_slots_write(&frames->slots, first, pitch, time));
            }
        }
    }

    if (locked2d) {
        IMF2DBuffer_Unlock2D(plane);
    } else if (data != NULL) {
        IMFMediaBuffer_Unlock(buffer);
    }
    if (plane != NULL) {
        IMF2DBuffer_Release(plane);
    }
    IMFMediaBuffer_Release(buffer);
    return true;
}

/* Waits until the clock reaches time; false when stopped first. */
static bool frames_wait(VideoFrames *frames, LONGLONG time)
{
    while (!frames->stop) {
        LONGLONG now = 0;
        if (frames->clock(frames->context, &now)) {
            if (now >= time) {
                return true;
            }
            LONGLONG ms = (time - now) / 10000;
            Sleep(ms > 10 ? 10 : (ms < 1 ? 1 : (DWORD)ms));
        } else {
            Sleep(5);
        }
    }
    return false;
}

/* Takes the reader's current video type. The size shown is fixed at open:
 * the display aperture where it starts at the corner, else the frame size.
 * A decoder may change type mid-stream, typically on the first sample to
 * frames padded to whole macroblocks (1080 rows to 1088): any frame still
 * holding the size shown is cropped to it, with its own stride; a smaller
 * one ends the frames rather than copying past a buffer. */
static bool frames_take_type(VideoFrames *frames)
{
    IMFMediaType *type = NULL;
    UINT64 size = 0;
    UINT32 stride = 0;
    UINT32 matrix = 0;
    UINT32 nominal = 0;
    MFVideoArea area;
    if (FAILED(IMFSourceReader_GetCurrentMediaType(frames->reader,
            (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, &type))) {
        return false;
    }
    bool ok = SUCCEEDED(IMFMediaType_GetUINT64(type, &MF_MT_FRAME_SIZE, &size));
    uint32_t width = (uint32_t)(size >> 32);
    uint32_t height = (uint32_t)size;
    uint32_t shown_width = width;
    uint32_t shown_height = height;
    if (ok && SUCCEEDED(IMFMediaType_GetBlob(type, &MF_MT_MINIMUM_DISPLAY_APERTURE,
            (UINT8 *)&area, sizeof(area), NULL)) &&
        area.OffsetX.value == 0 && area.OffsetY.value == 0 &&
        area.Area.cx > 0 && area.Area.cy > 0 &&
        (uint32_t)area.Area.cx <= width && (uint32_t)area.Area.cy <= height) {
        shown_width = (uint32_t)area.Area.cx;
        shown_height = (uint32_t)area.Area.cy;
    }
    LONG default_stride = (frames->format == VIDEO_PIXEL_NV12) ?
        (LONG)width : -(LONG)(width * 4);
    LONG source_stride = SUCCEEDED(IMFMediaType_GetUINT32(type,
        &MF_MT_DEFAULT_STRIDE, &stride)) ? (LONG)stride : default_stride;
    if (frames->format == VIDEO_PIXEL_NV12) {
        if (SUCCEEDED(IMFMediaType_GetUINT32(type, &MF_MT_YUV_MATRIX, &matrix))) {
            frames->bt709 = (matrix == MFVideoTransferMatrix_BT709);
        } else {
            frames->bt709 = shown_height >= 720;
        }
        if (SUCCEEDED(IMFMediaType_GetUINT32(type, &MF_MT_VIDEO_NOMINAL_RANGE,
                &nominal))) {
            frames->full_range = (nominal == MFNominalRange_0_255);
        } else {
            frames->full_range = false;
        }
    }
    IMFMediaType_Release(type);
    if (!ok || width == 0 || height == 0) {
        return false;
    }
    if (frames->info.width == 0) {
        frames->info.width = shown_width;
        frames->info.height = shown_height;
    } else if (width < frames->info.width || height < frames->info.height) {
        return false;
    }
    frames->source_width = width;
    frames->source_height = height;
    frames->source_stride = source_stride;
    return true;
}

static HRESULT frames_open_reader(VideoFrames *frames);

static DWORD WINAPI frames_worker(void *parameter)
{
    VideoFrames *frames = parameter;
    bool first = true;
    ULONGLONG shown_tick = 0;
    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    frames->open_error = frames_open_reader(frames);
    SetEvent(frames->opened);
    while (SUCCEEDED(frames->open_error) && !frames->stop) {
        DWORD flags = 0;
        LONGLONG time = 0;
        IMFSample *sample = NULL;
        HRESULT hr = IMFSourceReader_ReadSample(frames->reader,
            (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, NULL, &flags, &time, &sample);
        bool usable = SUCCEEDED(hr) && !(flags & MF_SOURCE_READERF_ENDOFSTREAM) &&
            (!(flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED) || frames_take_type(frames));
        if (!usable) {
            if (sample != NULL) {
                IMFSample_Release(sample);
            }
            break;
        }
        if (sample == NULL) {
            continue;
        }
        bool show = first;
        if (!first && frames_wait(frames, time)) {
            LONGLONG now = 0;
            show = !frames->clock(frames->context, &now) || now - time < FRAMES_LATE_HNS ||
                GetTickCount64() - shown_tick >= FRAMES_STALE_MS;
            if (!show) {
                InterlockedIncrement(&frames->dropped);
            }
        }
        bool published = true;
        if (show && !frames->stop) {
            published = frames_publish(frames, sample, time);
            shown_tick = GetTickCount64();
            InterlockedExchange(&frames->cued, published ? 1 : 0);
        }
        IMFSample_Release(sample);
        if (!published) {
            break;
        }
        if (first) {
            /* The first frame is decoded before the start, so starting the
             * decoder is hidden as the prepared intro hides it. */
            first = false;
            WaitForSingleObject(frames->run, INFINITE);
        }
    }
    InterlockedExchange(&frames->ended, 1);
    if (frames->reader != NULL) {
        IMFSourceReader_Release(frames->reader);
        frames->reader = NULL;
    }
    CoUninitialize();
    return 0;
}

static HRESULT frames_try_subtype(VideoFrames *frames,
    CreateMediaTypeFn create_type, const GUID *subtype)
{
    IMFMediaType *type = NULL;
    HRESULT hr = create_type(&type);
    if (SUCCEEDED(hr)) {
        hr = IMFMediaType_SetGUID(type, &MF_MT_MAJOR_TYPE, &MFMediaType_Video);
    }
    if (SUCCEEDED(hr)) {
        hr = IMFMediaType_SetGUID(type, &MF_MT_SUBTYPE, subtype);
    }
    if (SUCCEEDED(hr)) {
        hr = IMFSourceReader_SetCurrentMediaType(frames->reader,
            (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, NULL, type);
    }
    if (type != NULL) {
        IMFMediaType_Release(type);
    }
    return hr;
}

static HRESULT frames_configure(VideoFrames *frames, CreateMediaTypeFn create_type,
    const char **what)
{
    *what = "Media Foundation cannot select the video stream";
    HRESULT hr = IMFSourceReader_SetStreamSelection(frames->reader,
        (DWORD)MF_SOURCE_READER_ALL_STREAMS, FALSE);
    if (SUCCEEDED(hr)) {
        hr = IMFSourceReader_SetStreamSelection(frames->reader,
            (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, TRUE);
    }
    if (SUCCEEDED(hr)) {
        *what = "Media Foundation cannot decode the video";
        if (frames->local_mode &&
            SUCCEEDED(frames_try_subtype(frames, create_type,
                &MFVideoFormat_NV12))) {
            frames->format = VIDEO_PIXEL_NV12;
            hr = S_OK;
        } else {
            frames->format = VIDEO_PIXEL_BGRA;
            hr = frames_try_subtype(frames, create_type, &MFVideoFormat_RGB32);
        }
    }
    if (SUCCEEDED(hr) && !frames_take_type(frames)) {
        *what = "Media Foundation cannot report the frame size";
        hr = MF_E_INVALIDMEDIATYPE;
    }
    if (SUCCEEDED(hr) && (frames->info.width > WO_VIDEO_FRAMES_MAX_WIDTH ||
            frames->info.height > WO_VIDEO_FRAMES_MAX_HEIGHT)) {
        *what = "the frame size is unsupported";
        hr = MF_E_INVALIDMEDIATYPE;
    }
    return hr;
}

/* On the worker. */
static HRESULT frames_open_reader(VideoFrames *frames)
{
    CreateAttributesFn create_attributes =
        (CreateAttributesFn)frames_symbol(L"mfplat.dll", "MFCreateAttributes");
    CreateMediaTypeFn create_type =
        (CreateMediaTypeFn)frames_symbol(L"mfplat.dll", "MFCreateMediaType");
    CreateReaderFn create_reader =
        (CreateReaderFn)frames_symbol(L"mfreadwrite.dll", "MFCreateSourceReaderFromURL");
    frames->open_what = "the Media Foundation source reader is unavailable";
    if (create_attributes == NULL || create_type == NULL || create_reader == NULL) {
        return HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
    }
    IMFAttributes *attributes = NULL;
    HRESULT hr = create_attributes(&attributes, 1);
    if (SUCCEEDED(hr)) {
        /* Enables RGB32 fallback while letting NV12 pass straight through
         * from the native decoder without CPU color conversion. */
        hr = IMFAttributes_SetUINT32(attributes,
            &MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
    }
    frames->open_what = "Media Foundation could not open the file for frames";
    if (SUCCEEDED(hr)) {
        hr = create_reader(frames->path, attributes, &frames->reader);
    }
    if (attributes != NULL) {
        IMFAttributes_Release(attributes);
    }
    if (SUCCEEDED(hr)) {
        hr = frames_configure(frames, create_type, &frames->open_what);
    }
    return hr;
}

VideoFrames *video_frames_open(const wchar_t *path, bool local,
    HWND notify_window, VideoInfo *info, HRESULT *error, const char **what)
{
    size_t length = wcslen(path) + 1;
    VideoFrames *frames = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*frames));
    *error = E_OUTOFMEMORY;
    *what = "out of memory";
    if (frames == NULL) {
        return NULL;
    }
    frames->local_mode = local;
    frames->notify_window = notify_window;
    frames->local.latest = -1;
    frames->path = HeapAlloc(GetProcessHeap(), 0, length * sizeof(wchar_t));
    frames->opened = CreateEventW(NULL, TRUE, FALSE, NULL);
    frames->run = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (frames->path != NULL && frames->opened != NULL && frames->run != NULL) {
        memcpy(frames->path, path, length * sizeof(wchar_t));
        frames->thread = CreateThread(NULL, 0, frames_worker, frames, 0, NULL);
    }
    if (frames->thread == NULL) {
        video_frames_close(frames);
        return NULL;
    }
    HANDLE waits[2] = { frames->opened, frames->thread };
    bool opened = WaitForMultipleObjects(2, waits, FALSE, 15000) == WAIT_OBJECT_0;
    *error = opened ? frames->open_error : HRESULT_FROM_WIN32(ERROR_TIMEOUT);
    *what = opened ? frames->open_what : "the frame reader did not open in time";
    if (FAILED(*error)) {
        video_frames_close(frames);
        return NULL;
    }
    *info = frames->info;
    return frames;
}

bool video_frames_run(VideoFrames *frames, VideoFramesClock clock, void *context)
{
    frames->clock = clock;
    frames->context = context;
    return SetEvent(frames->run) != 0;
}

bool video_frames_cued(const VideoFrames *frames)
{
    return frames->cued != 0;
}

bool video_frames_ended(const VideoFrames *frames)
{
    return frames->ended != 0;
}

const volatile LONG *video_frames_counter(const VideoFrames *frames)
{
    return frames->local_mode ? &frames->local.published : &frames->slots.published;
}

LONG video_frames_dropped(const VideoFrames *frames)
{
    return frames->dropped;
}

bool video_frames_present(VideoFrames *frames, VideoCrt *crt, LONG *last_shown,
    bool sync)
{
    if (frames == NULL || !frames->local_mode) {
        return false;
    }
    return video_frame_buffer_present(&frames->local, crt, last_shown, sync);
}

void video_frames_close(VideoFrames *frames)
{
    if (frames == NULL) {
        return;
    }
    if (frames->thread != NULL) {
        InterlockedExchange(&frames->stop, 1);
        SetEvent(frames->run);
        /* A ReadSample in progress finishes within a frame's decoding. A
         * worker still in a decoder after that keeps the memory it uses:
         * the player is exiting, so it is left, not freed under it. */
        bool exited = WaitForSingleObject(frames->thread, 5000) == WAIT_OBJECT_0;
        CloseHandle(frames->thread);
        if (!exited) {
            return;
        }
    }
    if (frames->opened != NULL) {
        CloseHandle(frames->opened);
    }
    if (frames->run != NULL) {
        CloseHandle(frames->run);
    }
    if (frames->path != NULL) {
        HeapFree(GetProcessHeap(), 0, frames->path);
    }
    video_frame_buffer_close(&frames->local);
    video_frame_slots_close(&frames->slots);
    HeapFree(GetProcessHeap(), 0, frames);
}
