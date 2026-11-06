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
static void __cdecl dx_error(void) {
    HRESULT result=*(HRESULT *)(game+0xc8c84);
    if(result) {
        wo_render_event("dx_error_line",*(DWORD *)(game+0xc8c74));
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
