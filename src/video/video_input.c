#include "video/video_input.h"

#include <stdio.h>
#include <string.h>

/* An inherited handle value must name a waitable object here; anything else
 * is ignored rather than trusted. */
HANDLE video_input_check_event(HANDLE handle)
{
    if (handle == NULL || handle == INVALID_HANDLE_VALUE) {
        return NULL;
    }
    DWORD flags = 0;
    if (!GetHandleInformation(handle, &flags)) {
        return NULL;
    }
    DWORD waited = WaitForSingleObject(handle, 0);
    if (waited != WAIT_OBJECT_0 && waited != WAIT_TIMEOUT) {
        return NULL;
    }
    return handle;
}

static void video_input_open_controller(VideoInput *input, DWORD game_pid)
{
    wchar_t name[64];
    int written = swprintf(name, 64, L"Local\\WO2097Input-%lu",
        (unsigned long)game_pid);
    if (written < 0 || written >= 64) {
        return;
    }
    input->mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, name);
    if (input->mapping == NULL) {
        return;
    }
    input->view = MapViewOfFile(input->mapping, FILE_MAP_READ, 0, 0,
        sizeof(ControllerWire));
    if (input->view == NULL) {
        CloseHandle(input->mapping);
        input->mapping = NULL;
        return;
    }
    input->wire = input->view;
}

void video_input_open(VideoInput *input, const VideoOptions *options,
    HWND owner)
{
    memset(input, 0, sizeof(*input));
    input->owner = owner;
    input->skip_event = video_input_check_event(options->skip_event);
    if (options->game_pid == 0) {
        return;
    }
    input->game_process = OpenProcess(SYNCHRONIZE, FALSE, options->game_pid);
    video_input_open_controller(input, options->game_pid);
}

DWORD video_input_handles(const VideoInput *input, HANDLE handles[2])
{
    DWORD count = 0;
    if (input->skip_event != NULL) {
        handles[count] = input->skip_event;
        ++count;
    }
    if (input->game_process != NULL) {
        handles[count] = input->game_process;
        ++count;
    }
    return count;
}

VideoInputEvent video_input_poll(VideoInput *input)
{
    if (input->owner != NULL && GetForegroundWindow() != input->owner) {
        input->focus_lost = true;
    }
    if (input->skip_event != NULL &&
        WaitForSingleObject(input->skip_event, 0) == WAIT_OBJECT_0) {
        return VIDEO_INPUT_SKIP_KEY;
    }
    if (video_controller_pressed(&input->controller, input->wire)) {
        return VIDEO_INPUT_CONTROLLER;
    }
    if (input->game_process != NULL &&
        WaitForSingleObject(input->game_process, 0) == WAIT_OBJECT_0) {
        return VIDEO_INPUT_GAME_EXITED;
    }
    return VIDEO_INPUT_NONE;
}

void video_input_close(VideoInput *input)
{
    if (input->view != NULL) {
        UnmapViewOfFile(input->view);
    }
    if (input->mapping != NULL) {
        CloseHandle(input->mapping);
    }
    if (input->game_process != NULL) {
        CloseHandle(input->game_process);
    }
    if (input->skip_event != NULL) {
        CloseHandle(input->skip_event);
    }
    memset(input, 0, sizeof(*input));
}
