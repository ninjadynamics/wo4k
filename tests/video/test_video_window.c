/* Unit tests for the player window's Z order (src/video/video_window.c): it
 * goes back over a window of the game's that covers it (DDrawCompat raises
 * its presentation window when the game regains focus) and steps behind
 * another program in front. Every window here is fully transparent. */
#include <stdio.h>

#include "video/video_window.h"

static int failures;

#define CHECK(expression) \
    do { \
        if (!(expression)) { \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, \
                #expression); \
            ++failures; \
        } \
    } while (0)

static bool topmost(HWND window)
{
    return (GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0;
}

/* True if upper is above lower in the Z order. */
static bool above(HWND upper, HWND lower)
{
    for (HWND window = GetWindow(lower, GW_HWNDPREV); window != NULL;
        window = GetWindow(window, GW_HWNDPREV)) {
        if (window == upper) {
            return true;
        }
    }
    return false;
}

/* An invisible topmost window of this (the "game's") process. */
static HWND transparent_window(HINSTANCE instance, const RECT *bounds)
{
    HWND window = CreateWindowExW(WS_EX_TOPMOST | WS_EX_LAYERED |
        WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, L"STATIC",
        L"", WS_POPUP, 0, 0, 1, 1, NULL, NULL, instance, NULL);
    if (window == NULL) {
        return NULL;
    }
    SetLayeredWindowAttributes(window, 0, 0, LWA_ALPHA);
    SetWindowPos(window, HWND_TOPMOST, bounds->left, bounds->top,
        bounds->right - bounds->left, bounds->bottom - bounds->top,
        SWP_NOACTIVATE | SWP_SHOWWINDOW);
    return window;
}

static void test_follow_focus(HINSTANCE instance)
{
    HWND owner = CreateWindowExW(WS_EX_TOOLWINDOW, L"STATIC", L"", WS_POPUP,
        0, 0, 1, 1, NULL, NULL, instance, NULL);
    VideoWindow window;
    CHECK(owner != NULL);
    CHECK(video_window_create(&window, instance, owner, false, true));
    /* Nothing to follow before the window is on screen. */
    CHECK(video_window_arrange(&window, owner) == VIDEO_WINDOW_STAYED);
    CHECK(video_window_stage(&window));
    video_window_arrange(&window, owner);
    CHECK(topmost(window.hwnd));
    CHECK(video_window_arrange(&window, owner) == VIDEO_WINDOW_STAYED);
    CHECK(video_window_arrange(&window, NULL) == VIDEO_WINDOW_STAYED);

    /* The game's full-screen window raised over the movie: back on top. */
    HWND cover = transparent_window(instance, &window.bounds);
    CHECK(cover != NULL && above(cover, window.hwnd));
    CHECK(video_window_arrange(&window, owner) == VIDEO_WINDOW_RAISED);
    CHECK(above(window.hwnd, cover));
    CHECK(video_window_arrange(&window, owner) == VIDEO_WINDOW_STAYED);

    /* A smaller window of the game's (a message box) stays over it. */
    RECT small = { window.bounds.left, window.bounds.top,
        window.bounds.left + 100, window.bounds.top + 100 };
    HWND box = transparent_window(instance, &small);
    CHECK(box != NULL && above(box, window.hwnd));
    CHECK(video_window_arrange(&window, owner) == VIDEO_WINDOW_STAYED);
    CHECK(above(box, window.hwnd));

    /* Another program in front: out of the topmost band, behind it, once;
     * back over the game when the game returns. */
    HWND shell = GetShellWindow();
    DWORD shell_process = 0;
    if (shell != NULL) {
        GetWindowThreadProcessId(shell, &shell_process);
    }
    if (shell_process != 0 && shell_process != GetCurrentProcessId()) {
        CHECK(video_window_arrange(&window, shell) == VIDEO_WINDOW_BEHIND);
        CHECK(!topmost(window.hwnd));
        CHECK(window.behind == shell);
        CHECK(video_window_arrange(&window, shell) == VIDEO_WINDOW_STAYED);
        CHECK(video_window_arrange(&window, owner) == VIDEO_WINDOW_RAISED);
        CHECK(topmost(window.hwnd) && window.behind == NULL);
        CHECK(above(window.hwnd, cover));
    } else {
        printf("SKIP: no shell window from another process; the "
            "behind-another-program case was not checked.\n");
    }

    /* A probe window is left alone. */
    VideoWindow probe;
    CHECK(video_window_create(&probe, instance, owner, true, true));
    CHECK(video_window_stage(&probe));
    CHECK(video_window_arrange(&probe, owner) == VIDEO_WINDOW_STAYED);
    video_window_destroy(&probe);

    video_window_destroy(&window);
    if (box != NULL) {
        DestroyWindow(box);
    }
    if (cover != NULL) {
        DestroyWindow(cover);
    }
    if (owner != NULL) {
        DestroyWindow(owner);
    }
}

int main(void)
{
    test_follow_focus(GetModuleHandleW(NULL));
    if (failures != 0) {
        fprintf(stderr, "FAIL: %d video window check(s) failed.\n", failures);
        return 1;
    }
    printf("PASS: the player window follows the game in and out of the "
        "front.\n");
    return 0;
}
