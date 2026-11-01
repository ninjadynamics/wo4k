/* Native present flips first, then renders queued D3D buffers for the next
 * frame. Startup bitmap blits are immediate, so the next title bitmap erases
 * that delayed lettering. Present the already-built title buffers before the
 * flip as well, using their original font, geometry, states and flash timing. */
static void title_overlay_present(void) {
    BOOL pending=title_prompt_pending;title_prompt_pending=FALSE;
    if(!pending || !title_active || startup_visible!=0 || !startup_art[0].surface)return;
    IDirect3DDevice *device=*(IDirect3DDevice **)(game+0xc8d10);
    IDirect3DViewport *viewport=*(IDirect3DViewport **)(game+0xc8c88);
    UINT count=*(UINT *)(game+0x8f8dc);
    if(!device || !viewport || !count || count>DISTANCE_EXECUTE_CAPACITY)return;
    DWORD *buffers=distance_active?distance_execute:(DWORD *)(game+0x4d9620);
    DWORD *states=distance_active?distance_states:(DWORD *)(game+0xc7c20);
    if(!buffers || !states)return;
    /* Do not consume/reset the queue: the native tail still owns resource
     * retirement and its regular execution after the flip. */
    for(UINT i=0;i<count;++i)if(!buffers[i])return;
    HRESULT hr=device->lpVtbl->BeginScene(device);
    if(hr<0){wo_render_event("title_overlay_begin_error",(DWORD)hr);return;}
    for(UINT i=0;i<count && hr>=0;++i)
        hr=device->lpVtbl->Execute(device,(IDirect3DExecuteBuffer *)(UINT_PTR)buffers[i],viewport,states[i]);
    HRESULT ended=device->lpVtbl->EndScene(device);
    if(hr<0 || ended<0){wo_render_event("title_overlay_execute_error",(DWORD)(hr<0?hr:ended));return;}
    static BOOL logged;
    if(!logged){logged=TRUE;wo_render_event("title_prompt_presented_buffers",count);}
}
