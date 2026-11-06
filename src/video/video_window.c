#include "video/video_window.h"

#define VIDEO_WINDOW_CLASS L"WO4KVideoWindow"
#define VIDEO_PROBE_WIDTH 640
#define VIDEO_PROBE_HEIGHT 480

static VideoWindow *video_window_from(HWND hwnd)
{
    return (VideoWindow *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
}

static void video_window_layout(VideoWindow *window)
{
    RECT client;
    if (window->backend == NULL || !GetClientRect(window->hwnd, &client)) {
        return;
    }
    window->backend->ops->layout(window->backend, &client);
}

static void video_window_paint(VideoWindow *window)
{
    PAINTSTRUCT paint;
    HDC dc = BeginPaint(window->hwnd, &paint);
    if (dc == NULL) {
        return;
    }
    if (window->backend != NULL) {
        window->backend->ops->repaint(window->backend);
    }
    EndPaint(window->hwnd, &paint);
}

static LRESULT CALLBACK video_window_proc(HWND hwnd, UINT message,
    WPARAM wparam, LPARAM lparam)
{
    if (message == WM_NCCREATE) {
        const CREATESTRUCTW *create = (const CREATESTRUCTW *)lparam;
        VideoWindow *created = create->lpCreateParams;
        created->hwnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)created);
    }
    VideoWindow *window = video_window_from(hwnd);
    if (window == NULL) {
        return DefWindowProcW(hwnd, message, wparam, lparam);
    }
    switch (message) {
    case WM_MOUSEACTIVATE:
        /* Clicking must not take focus from the game. */
        return MA_NOACTIVATE;
    case WM_ACTIVATE:
        if (LOWORD(wparam) != WA_INACTIVE) {
            window->activated = true;
        }
        break;
    case WM_SETCURSOR:
        SetCursor(NULL);
        return TRUE;
    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_MBUTTONDOWN:
        window->clicked = true;
        return 0;
    case WM_XBUTTONDOWN:
        window->clicked = true;
        return TRUE;
    case WM_SIZE:
        video_window_layout(window);
        return 0;
    case WM_PAINT:
        video_window_paint(window);
        return 0;
    case WM_CLOSE:
        window->close_requested = true;
        return 0;
    case WM_DESTROY:
        window->destroyed = true;
        return 0;
    case WM_NCDESTROY:
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        window->hwnd = NULL;
        break;
    default:
        break;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

static bool video_window_register(HINSTANCE instance)
{
    WNDCLASSEXW window_class;
    ZeroMemory(&window_class, sizeof(window_class));
    window_class.cbSize = sizeof(window_class);
    window_class.lpfnWndProc = video_window_proc;
    window_class.hInstance = instance;
    window_class.hCursor = NULL;
    window_class.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    window_class.lpszClassName = VIDEO_WINDOW_CLASS;
    if (RegisterClassExW(&window_class) != 0) {
        return true;
    }
    return GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

/* The whole monitor showing the game, or a centred probe rectangle. */
static bool video_window_bounds(HWND owner, bool probe, RECT *bounds)
{
    MONITORINFO info;
    POINT origin = { 0, 0 };
    HMONITOR monitor = NULL;
    if (owner != NULL && !probe) {
        monitor = MonitorFromWindow(owner, MONITOR_DEFAULTTOPRIMARY);
    } else {
        monitor = MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY);
    }
    ZeroMemory(&info, sizeof(info));
    info.cbSize = sizeof(info);
    if (monitor == NULL || !GetMonitorInfoW(monitor, &info)) {
        return false;
    }
    if (!probe) {
        *bounds = info.rcMonitor;
        return true;
    }
    const RECT *work = &info.rcWork;
    LONG width = VIDEO_PROBE_WIDTH;
    LONG height = VIDEO_PROBE_HEIGHT;
    LONG work_width = work->right - work->left;
    LONG work_height = work->bottom - work->top;
    if (width > work_width) {
        width = work_width;
    }
    if (height > work_height) {
        height = work_height;
    }
    bounds->left = work->left + (work_width - width) / 2;
    bounds->top = work->top + (work_height - height) / 2;
    bounds->right = bounds->left + width;
    bounds->bottom = bounds->top + height;
    return true;
}

bool video_window_create(VideoWindow *window, HINSTANCE instance, HWND owner,
    bool probe, bool hidden)
{
    ZeroMemory(window, sizeof(*window));
    if (!video_window_register(instance) ||
        !video_window_bounds(owner, probe, &window->bounds)) {
        return false;
    }
    const RECT *bounds = &window->bounds;
    DWORD style = WS_POPUP | WS_CLIPCHILDREN;
    DWORD ex_style = WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW;
    if (!probe) {
        ex_style |= WS_EX_TOPMOST;
    }
    if (hidden) {
        /* Layered, so it can be staged on screen fully transparent and
         * click-through (see video_window_stage). */
        ex_style |= WS_EX_LAYERED | WS_EX_TRANSPARENT;
    }
    window->probe = probe;
    window->owner = owner;
    HWND hwnd = CreateWindowExW(ex_style, VIDEO_WINDOW_CLASS, L"WO4K video",
        style, bounds->left, bounds->top, bounds->right - bounds->left,
        bounds->bottom - bounds->top, owner, NULL, instance, window);
    if (hwnd == NULL) {
        return false;
    }
    window->layered = hidden &&
        SetLayeredWindowAttributes(hwnd, 0, 0, LWA_ALPHA);
    if (!hidden) {
        video_window_show(window);
    }
    return true;
}

/* The game's process, or the player's own: a window the movie belongs over. */
static bool video_window_ours(const VideoWindow *window, HWND other)
{
    DWORD game = 0;
    DWORD process = 0;
    GetWindowThreadProcessId(window->owner, &game);
    GetWindowThreadProcessId(other, &process);
    return process != 0 &&
        (process == game || process == GetCurrentProcessId());
}

/* Where a topmost window goes: over everything while the game is in front,
 * else just behind the program that is. */
static HWND video_window_insert_after(const VideoWindow *window)
{
    HWND foreground = GetForegroundWindow();
    if (window->owner != NULL && foreground != NULL &&
        !video_window_ours(window, foreground)) {
        return foreground;
    }
    return HWND_TOPMOST;
}

/* SetWindowPos, not ShowWindow: the game's STARTUPINFO must not pick the
 * first show state, and nothing here may activate the window. */
static void video_window_place(VideoWindow *window)
{
    const RECT *bounds = &window->bounds;
    UINT flags = SWP_NOACTIVATE | SWP_SHOWWINDOW;
    if (window->probe) {
        flags |= SWP_NOZORDER;
    }
    HWND after = window->probe ? NULL : video_window_insert_after(window);
    window->behind = after == HWND_TOPMOST ? NULL : after;
    SetWindowPos(window->hwnd, after, bounds->left, bounds->top,
        bounds->right - bounds->left, bounds->bottom - bounds->top, flags);
}

/* A visible window of the game's above the player covering all of it, such
 * as DDrawCompat's presentation window raised when the game regains focus.
 * Smaller ones (a message box) and other programs' windows do not count. */
static bool video_window_covered(const VideoWindow *window)
{
    RECT mine;
    if (!GetWindowRect(window->hwnd, &mine)) {
        return false;
    }
    for (HWND above = GetWindow(window->hwnd, GW_HWNDPREV); above != NULL;
        above = GetWindow(above, GW_HWNDPREV)) {
        RECT rect;
        if (IsWindowVisible(above) && video_window_ours(window, above) &&
            GetWindowRect(above, &rect) && rect.left <= mine.left &&
            rect.top <= mine.top && rect.right >= mine.right &&
            rect.bottom >= mine.bottom) {
            return true;
        }
    }
    return false;
}

VideoWindowMove video_window_arrange(VideoWindow *window, HWND foreground)
{
    if (window->hwnd == NULL || window->probe || window->owner == NULL ||
        !(window->shown || window->staged) || foreground == NULL) {
        return VIDEO_WINDOW_STAYED;
    }
    const UINT flags = SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE;
    if (video_window_ours(window, foreground)) {
        bool topmost = (GetWindowLongPtrW(window->hwnd, GWL_EXSTYLE) &
            WS_EX_TOPMOST) != 0;
        if (topmost && window->behind == NULL &&
            !video_window_covered(window)) {
            return VIDEO_WINDOW_STAYED;
        }
        window->behind = NULL;
        SetWindowPos(window->hwnd, HWND_TOPMOST, 0, 0, 0, 0, flags);
        return VIDEO_WINDOW_RAISED;
    }
    if (window->behind == foreground) {
        return VIDEO_WINDOW_STAYED;
    }
    /* After a window that is not topmost, the player leaves the topmost
     * band too; programs brought to the front later go over it anyway. */
    window->behind = foreground;
    SetWindowPos(window->hwnd, foreground, 0, 0, 0, 0, flags);
    return VIDEO_WINDOW_BEHIND;
}

VideoWindowMove video_window_follow_focus(VideoWindow *window)
{
    return video_window_arrange(window, GetForegroundWindow());
}

bool video_window_stage(VideoWindow *window)
{
    if (window->hwnd == NULL || !window->layered || window->staged ||
        window->shown) {
        return false;
    }
    video_window_place(window);
    window->staged = true;
    return true;
}

void video_window_show(VideoWindow *window)
{
    if (window->hwnd == NULL || window->shown) {
        return;
    }
    if (window->layered) {
        /* Opaque, and taking clicks (a click skips) from now on. */
        SetLayeredWindowAttributes(window->hwnd, 0, 255, LWA_ALPHA);
        LONG_PTR ex_style = GetWindowLongPtrW(window->hwnd, GWL_EXSTYLE);
        SetWindowLongPtrW(window->hwnd, GWL_EXSTYLE,
            ex_style & ~(LONG_PTR)WS_EX_TRANSPARENT);
    }
    if (!window->staged) {
        video_window_place(window);
        UpdateWindow(window->hwnd);
    }
    window->clicked = false;
    window->shown = true;
}

void video_window_set_backend(VideoWindow *window, VideoBackend *backend)
{
    window->backend = backend;
    video_window_layout(window);
    if (window->hwnd != NULL) {
        InvalidateRect(window->hwnd, NULL, TRUE);
    }
}

void video_window_destroy(VideoWindow *window)
{
    window->backend = NULL;
    if (window->hwnd != NULL) {
        DestroyWindow(window->hwnd);
    }
}
