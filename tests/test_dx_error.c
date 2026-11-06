/* A DirectX error must reach the game's own handler with a message that fits
 * its 100-byte buffer: DDERR_SURFACELOST's text did not, and the overrun made
 * shutdown release text as interfaces. */
static UINT dx_error_native_calls;
static char dx_error_long[200];
static void __cdecl fixture_dx_error(void){++dx_error_native_calls;}
static const char *__cdecl fixture_dx_error_text(HRESULT result){(void)result;return dx_error_long;}
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
    *result=saved_result;*line=saved_line;old_dx_error=native;old_dx_error_text=native_text;
}
