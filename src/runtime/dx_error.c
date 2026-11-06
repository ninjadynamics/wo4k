/* The game's DirectX error handler (LogDXError) appends "Direct X ERROR:Line
 * %05d \t<text>" to Debug.txt, shuts down and exits. It formats that into a
 * 100-byte buffer at 0x4c8b60 that its texture interface tables follow.
 * DDERR_SURFACELOST's 165-character text overran them, so shutdown released
 * message text as interfaces (Wipeout2097.exe+0x5E012); for codes it does not
 * know, the text even lives in that buffer, which wsprintfA then copies onto
 * itself. Hand it a bounded copy instead, so a DirectX error quits cleanly
 * with its Debug.txt line, and record the failing line and result in
 * game.log as well. */
typedef const char *(__cdecl *DxErrorTextFn)(HRESULT);
static RefreshVoidFn old_dx_error;
static DxErrorTextFn old_dx_error_text;
static char dx_error_text_copy[64]; /* with the 27-character prefix, < 100 */
static const char *__cdecl dx_error_text(HRESULT result) {
    const char *text=old_dx_error_text(result);
    UINT n=0;
    while(text && text[n] && n<sizeof(dx_error_text_copy)-1) {dx_error_text_copy[n]=text[n];++n;}
    dx_error_text_copy[n]=0;
    return dx_error_text_copy;
}
/* Under DDrawCompat a primary is lost when DDrawCompat's own presentation
 * surface is, and the game's Flip (line 1573) then fails, which the game
 * treats as fatal. Restore is DDrawCompat's way back: it recreates that
 * surface, or answers DDERR_NOEXCLUSIVEMODE while the game is not the active
 * fullscreen window. Drop the frame and retry on the next one, as DirectDraw
 * games do; the game redraws its whole back buffer every frame. Give up as
 * before only if it keeps failing for 300 frames in the foreground. */
#define DX_ERROR_FLIP_LINE 1573
#define DX_ERROR_SURFACE_LOST ((HRESULT)0x887601c2)
#define DX_ERROR_NO_EXCLUSIVE_MODE ((HRESULT)0x887600e1)
static DWORD dx_error_dropped,dx_error_streak;
static BOOL dx_error_foreground(void) {
    DWORD process=0;GetWindowThreadProcessId(GetForegroundWindow(),&process);
    return process==GetCurrentProcessId();
}
static BOOL dx_error_recover_flip(void) {
    IDirectDrawSurface *primary=*(IDirectDrawSurface **)(game+0x8f8d8);
    if(!primary)return FALSE;
    BOOL foreground=dx_error_foreground();
    dx_error_streak=foreground?dx_error_streak+1:0;
    if(dx_error_streak>300)return FALSE;
    HRESULT restored=primary->lpVtbl->Restore(primary);
    if(restored<0 && restored!=DX_ERROR_NO_EXCLUSIVE_MODE) {wo_render_event("dx_error_restore_failed",(DWORD)restored);return FALSE;}
    if(!dx_error_dropped++)wo_render_event("dx_error_flip_lost",foreground);
    if(!foreground)Sleep(15); /* no vsync wait while away: do not spin */
    return TRUE;
}
static void __cdecl dx_error(void) {
    HRESULT result=*(HRESULT *)(game+0xc8c84);
    DWORD line=*(DWORD *)(game+0xc8c74);
    if(result==DX_ERROR_SURFACE_LOST && line==DX_ERROR_FLIP_LINE && dx_error_recover_flip()) {
        *(HRESULT *)(game+0xc8c84)=0;return;
    }
    if(!result && line==DX_ERROR_FLIP_LINE && dx_error_dropped) {
        wo_render_event("dx_error_flip_restored",dx_error_dropped);
        dx_error_dropped=dx_error_streak=0;
    }
    if(result) {
        wo_render_event("dx_error_line",line);
        wo_render_event("dx_error_result",(DWORD)result);
    }
    old_dx_error();
}
static void initialize_dx_error(void) {
    if(!equal(game+RVA_dx_error,entry_dx_error,sizeof(entry_dx_error)) ||
       !equal(game+RVA_dx_error_text,entry_dx_error_text,sizeof(entry_dx_error_text)))return;
    old_dx_error_text=(DxErrorTextFn)hook(RVA_dx_error_text,entry_dx_error_text,sizeof(entry_dx_error_text),dx_error_text);
    if(old_dx_error_text)old_dx_error=(RefreshVoidFn)hook(RVA_dx_error,entry_dx_error,sizeof(entry_dx_error),dx_error);
    wo_render_event("dx_error_patch",old_dx_error_text && old_dx_error);
}
