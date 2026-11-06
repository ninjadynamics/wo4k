/* A race's last frame is still queued when the menu pages load over the
 * track's slots, and the game executes it during the next present. HD pages
 * replaced by that load must stay alive until that present has run. */
static UINT retire_log[8],retire_count,retire_count_at_present;
static ULONG STDMETHODCALLTYPE fixture_retire_release(IUnknown *object){
    if(retire_count>=8)ExitProcess(180);
    retire_log[retire_count++]=((const UINT *)object)[1];return 0;
}
static int __cdecl fixture_retire_present(void){retire_count_at_present=retire_count;return 7;}
static void test_hd_retire(BYTE *image){
    static void *vtable[3]={0,0,(void *)fixture_retire_release};
    static struct {void **vtable;UINT id;} texture={vtable,1},surface={vtable,2},texture2={vtable,3},surface2={vtable,4};
    DWORD *handles=(DWORD *)(image+0xc7910),saved=handles[30];
    if(ui_hd_deferred_release!=refresh_active || !refresh_active)ExitProcess(182);
    UiHdPage old={.surface=(IDirectDrawSurface *)&surface,.texture=(IDirect3DTexture *)&texture,.handle=901,.width=512};
    UiHdPage none={0};
    ui_hd_commit_page(30,&old);ui_hd_commit_page(30,&none);
    if(retire_count || ui_hd_pages[30].handle || ui_hd_handle(901))ExitProcess(183);
    /* Freed once the present has executed the queued frame: texture
     * interface before its surface, exactly once. */
    old_refresh_present=fixture_retire_present;title_active=FALSE;
    if(refresh_present()!=7 || retire_count_at_present)ExitProcess(181); /* the queued frame still uses them */
    if(retire_count!=2 || retire_log[0]!=1 || retire_log[1]!=2)ExitProcess(184);
    if(refresh_present()!=7 || retire_count!=2)ExitProcess(185);
    /* Without the present hook nothing would free them: release at once. */
    ui_hd_deferred_release=FALSE;retire_count=0;
    UiHdPage other={.surface=(IDirectDrawSurface *)&surface2,.texture=(IDirect3DTexture *)&texture2,.handle=902,.width=512};
    ui_hd_commit_page(30,&other);ui_hd_commit_page(30,&none);
    if(retire_count!=2 || retire_log[0]!=3 || retire_log[1]!=4)ExitProcess(188);
    ui_hd_deferred_release=TRUE;retire_count=0;handles[30]=saved;
}
