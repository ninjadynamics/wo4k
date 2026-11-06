#include "video/video_backend.h"

#include <stdio.h>

void video_backend_fail(VideoBackend *backend, HRESULT error,
    const char *what)
{
    if (backend->state == VIDEO_STATE_FAILED) {
        return;
    }
    backend->state = VIDEO_STATE_FAILED;
    backend->error = error;
    int written = snprintf(backend->reason, sizeof(backend->reason),
        "%s (0x%08lX)", what, (unsigned long)error);
    if (written < 0) {
        backend->reason[0] = '\0';
    }
}
