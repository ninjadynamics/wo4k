/* A DirectX error must reach the game's own handler with a message that fits
 * its 100-byte buffer: DDERR_SURFACELOST's text did not, and the overrun made
 * shutdown release text as interfaces. */
static UINT dx_error_native_calls;
static char dx_error_long[200];
static void __cdecl fixture_dx_error(void){++dx_error_native_calls;}
static const char *__cdecl fixture_dx_error_text(HRESULT result){(void)result;return dx_error_long;}
static UINT dx_restore_calls;
static HRESULT dx_restore_result;
static HRESULT WINAPI fixture_dx_restore(IDirectDrawSurface *surface){(void)surface;++dx_restore_calls;return dx_restore_result;}
static void test_dx_error(BYTE *image){
    if(!old_dx_error || !old_dx_error_text || image[RVA_dx_error]!=0xe9 || image[RVA_dx_error_text]!=0xe9)ExitProcess(189);
    if(image+RVA_dx_error+5+*(LONG *)(image+RVA_dx_error+1)!=(BYTE *)dx_error)ExitProcess(190);
    for(UINT i=0;i<sizeof(dx_error_long)-1;++i)dx_error_long[i]=(char)('a'+i%26);
    RefreshVoidFn native=old_dx_error;DxErrorTextFn native_text=old_dx_error_text;
    old_dx_error=fixture_dx_error;old_dx_error_text=fixture_dx_error_text;
    /* Called through the patched native entry, as LogDXError calls it. */
    const char *text=((DxErrorTextFn)(image+RVA_dx_error_text))((HRESULT)0x887601c2);
    UINT n=0;while(text[n])++n;
    if(text==dx_error_long || 27+n>=100 || n<60 || text[0]!='a' || text[n-1]!=dx_error_long[n-1])ExitProcess(202);
    HRESULT *result=(HRESULT *)(image+0xc8c84);DWORD *line=(DWORD *)(image+0xc8c74);
    HRESULT saved_result=*result;DWORD saved_line=*line;
    *result=0;((RefreshVoidFn)(image+RVA_dx_error))();
    *result=(HRESULT)0x887601c2;*line=1573;((RefreshVoidFn)(image+RVA_dx_error))();
    if(dx_error_native_calls!=2)ExitProcess(209); /* every call still reaches the game's handler */
    /* A lost primary at the Flip drops the frame instead of quitting: after
     * a Restore, or while DDrawCompat says the game is not the active
     * fullscreen window; the next good Flip ends the episode. */
    static IDirectDrawSurfaceVtbl primary_vtable={.Restore=fixture_dx_restore};
    IDirectDrawSurface primary={&primary_vtable};
    IDirectDrawSurface **primary_slot=(IDirectDrawSurface **)(image+0x8f8d8),*saved_primary=*primary_slot;
    *primary_slot=&primary;dx_error_native_calls=0;
    const HRESULT restores[2]={0,(HRESULT)0x887600e1};
    for(UINT i=0;i<2;++i){
        dx_restore_result=restores[i];*result=(HRESULT)0x887601c2;*line=1573;
        ((RefreshVoidFn)(image+RVA_dx_error))();
        if(dx_error_native_calls || *result || dx_restore_calls!=i+1 || dx_error_dropped!=i+1)ExitProcess(243);
    }
    *result=0;((RefreshVoidFn)(image+RVA_dx_error))();
    if(dx_error_native_calls!=1 || dx_error_dropped)ExitProcess(244);
    /* Any other Restore failure, or a lost surface elsewhere, stays fatal. */
    dx_restore_result=(HRESULT)0x80004005;*result=(HRESULT)0x887601c2;
    ((RefreshVoidFn)(image+RVA_dx_error))();
    if(dx_error_native_calls!=2 || *result!=(HRESULT)0x887601c2)ExitProcess(245);
    *line=1667;((RefreshVoidFn)(image+RVA_dx_error))();
    if(dx_error_native_calls!=3 || dx_restore_calls!=3)ExitProcess(246);
    *primary_slot=saved_primary;dx_error_dropped=dx_error_streak=0;
    *result=saved_result;*line=saved_line;old_dx_error=native;old_dx_error_text=native_text;
}
