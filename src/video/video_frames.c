#include "video/video_frames.h"

#include <stdio.h>
#include <string.h>

#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>

#include "common/wo_video_frames.h"

/* A frame this far behind the clock is dropped to catch up. */
#define FRAMES_LATE_HNS 1000000

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
    VideoInfo info;
    HRESULT open_error;
    const char *open_what;
    HANDLE opened; /* set once the reader is open, or has failed */
    HANDLE run;    /* set by video_frames_run, or to stop */
    HANDLE mapping;
    WoVideoFrames *view;
    BYTE *slots;
    LONG source_stride; /* negative: bottom-up rows */
    HANDLE thread;
    volatile LONG stop;
    volatile LONG cued;
    volatile LONG ended;
    VideoFramesClock clock;
    void *context;
};

typedef void (*AnyFunction)(void);

static AnyFunction frames_symbol(const wchar_t *library, const char *name)
{
    HMODULE module = LoadLibraryExW(library, NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    return module != NULL ? (AnyFunction)GetProcAddress(module, name) : NULL;
}

/* Copies one sample into the slot not holding the newest frame, under its
 * sequence number, then publishes it as the newest. */
static void frames_publish(VideoFrames *frames, IMFSample *sample, LONGLONG time)
{
    WoVideoFrames *header = frames->view;
    IMFMediaBuffer *buffer = NULL;
    IMF2DBuffer *plane = NULL;
    BYTE *first = NULL;
    LONG pitch = frames->source_stride;
    BYTE *data = NULL;
    DWORD length = 0;
    bool locked2d = false;
    if (FAILED(IMFSample_ConvertToContiguousBuffer(sample, &buffer))) {
        return;
    }
    DWORD row = header->width * 4;
    if (SUCCEEDED(IMFMediaBuffer_QueryInterface(buffer, &IID_IMF2DBuffer, (void **)&plane)) &&
        SUCCEEDED(IMF2DBuffer_Lock2D(plane, &first, &pitch))) {
        locked2d = true;
        if ((pitch < 0 ? -(DWORD)pitch : (DWORD)pitch) < row) {
            first = NULL;
        }
    } else if (SUCCEEDED(IMFMediaBuffer_Lock(buffer, &data, NULL, &length))) {
        DWORD span = pitch < 0 ? -(DWORD)pitch : (DWORD)pitch;
        if (span < row || length < (ULONGLONG)span * (header->height - 1) + row) {
            IMFMediaBuffer_Unlock(buffer);
            data = NULL;
        } else {
            first = pitch < 0 ? data + (SIZE_T)span * (header->height - 1) : data;
        }
    }
    if (first != NULL) {
        LONG slot = header->latest == 0 ? 1 : 0;
        BYTE *target = frames->slots + (SIZE_T)slot * header->slot_size;
        InterlockedIncrement(&header->sequence[slot]);
        for (DWORD y = 0; y < header->height; ++y) {
            memcpy(target + (SIZE_T)y * header->stride, first + (LONG_PTR)y * pitch, row);
        }
        header->time_hns[slot] = time;
        InterlockedIncrement(&header->sequence[slot]);
        InterlockedExchange(&header->latest, slot);
        InterlockedIncrement(&header->frames);
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

static HRESULT frames_open_reader(VideoFrames *frames);
static HRESULT frames_read_type(VideoFrames *frames, VideoInfo *info);

/* A decoder may change its output type mid-stream. The shared memory keeps
 * the size it was made for: the same size continues with the new stride, a
 * different one ends the frames rather than copying past either buffer. */
static bool frames_type_still_fits(VideoFrames *frames)
{
    VideoInfo info = { 0 };
    return SUCCEEDED(frames_read_type(frames, &info)) &&
        info.width == frames->view->width && info.height == frames->view->height;
}

static DWORD WINAPI frames_worker(void *parameter)
{
    VideoFrames *frames = parameter;
    bool first = true;
    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    frames->open_error = frames_open_reader(frames);
    SetEvent(frames->opened);
    while (SUCCEEDED(frames->open_error) && !frames->stop) {
        DWORD flags = 0;
        LONGLONG time = 0;
        IMFSample *sample = NULL;
        HRESULT hr = IMFSourceReader_ReadSample(frames->reader,
            (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, NULL, &flags, &time, &sample);
        if (FAILED(hr) || (flags & MF_SOURCE_READERF_ENDOFSTREAM)) {
            if (sample != NULL) {
                IMFSample_Release(sample);
            }
            break;
        }
        if ((flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED) && !frames_type_still_fits(frames)) {
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
            show = !frames->clock(frames->context, &now) || now - time < FRAMES_LATE_HNS;
        }
        if (show && !frames->stop) {
            frames_publish(frames, sample, time);
            InterlockedExchange(&frames->cued, 1);
        }
        IMFSample_Release(sample);
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

static HRESULT frames_configure(VideoFrames *frames, CreateMediaTypeFn create_type,
    VideoInfo *info, const char **what)
{
    IMFMediaType *type = NULL;
    *what = "Media Foundation cannot select the video stream";
    HRESULT hr = IMFSourceReader_SetStreamSelection(frames->reader,
        (DWORD)MF_SOURCE_READER_ALL_STREAMS, FALSE);
    if (SUCCEEDED(hr)) {
        hr = IMFSourceReader_SetStreamSelection(frames->reader,
            (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, TRUE);
    }
    if (SUCCEEDED(hr)) {
        hr = create_type(&type);
    }
    if (SUCCEEDED(hr)) {
        hr = IMFMediaType_SetGUID(type, &MF_MT_MAJOR_TYPE, &MFMediaType_Video);
    }
    if (SUCCEEDED(hr)) {
        hr = IMFMediaType_SetGUID(type, &MF_MT_SUBTYPE, &MFVideoFormat_RGB32);
    }
    if (SUCCEEDED(hr)) {
        *what = "Media Foundation cannot decode the video to RGB";
        hr = IMFSourceReader_SetCurrentMediaType(frames->reader,
            (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, NULL, type);
    }
    if (type != NULL) {
        IMFMediaType_Release(type);
    }
    if (SUCCEEDED(hr)) {
        *what = "Media Foundation cannot report the RGB frame size";
        hr = frames_read_type(frames, info);
    }
    return hr;
}

/* Reads the current RGB frame size and row stride. */
static HRESULT frames_read_type(VideoFrames *frames, VideoInfo *info)
{
    IMFMediaType *type = NULL;
    HRESULT hr = IMFSourceReader_GetCurrentMediaType(frames->reader,
        (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, &type);
    UINT64 size = 0;
    if (SUCCEEDED(hr)) {
        hr = IMFMediaType_GetUINT64(type, &MF_MT_FRAME_SIZE, &size);
    }
    if (SUCCEEDED(hr)) {
        info->width = (uint32_t)(size >> 32);
        info->height = (uint32_t)size;
        UINT32 stride = 0;
        /* Absent, uncompressed RGB is bottom-up, as in a DIB. */
        frames->source_stride = SUCCEEDED(IMFMediaType_GetUINT32(type,
            &MF_MT_DEFAULT_STRIDE, &stride)) ? (LONG)stride : -(LONG)(info->width * 4);
    }
    if (type != NULL) {
        IMFMediaType_Release(type);
    }
    return hr;
}

static bool frames_map(VideoFrames *frames, uint32_t width, uint32_t height)
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
    frames->mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE,
        (DWORD)((ULONGLONG)total >> 32), (DWORD)total, name);
    if (frames->mapping != NULL) {
        frames->view = MapViewOfFile(frames->mapping, FILE_MAP_WRITE, 0, 0, total);
    }
    if (frames->view == NULL) {
        return false;
    }
    frames->slots = (BYTE *)frames->view + WO_VIDEO_FRAMES_HEADER;
    WoVideoFrames *header = frames->view;
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
        /* Decode and convert to RGB32 in software, in the reader. */
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
        hr = frames_configure(frames, create_type, &frames->info, &frames->open_what);
    }
    if (SUCCEEDED(hr) && !frames_map(frames, frames->info.width, frames->info.height)) {
        frames->open_what = "the frame size is unsupported or shared memory is unavailable";
        hr = MF_E_INVALIDMEDIATYPE;
    }
    return hr;
}

VideoFrames *video_frames_open(const wchar_t *path, VideoInfo *info,
    HRESULT *error, const char **what)
{
    size_t length = wcslen(path) + 1;
    VideoFrames *frames = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*frames));
    *error = E_OUTOFMEMORY;
    *what = "out of memory";
    if (frames == NULL) {
        return NULL;
    }
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
    return &frames->view->frames;
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
         * worker still opening the file after that keeps the memory it
         * uses: the player is exiting, so it is left, not freed under it. */
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
    if (frames->view != NULL) {
        UnmapViewOfFile(frames->view);
    }
    if (frames->mapping != NULL) {
        CloseHandle(frames->mapping);
    }
    HeapFree(GetProcessHeap(), 0, frames);
}
