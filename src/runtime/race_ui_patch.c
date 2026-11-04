/* Race display preferences and presentation of simulation-owned UI state. */
static void (__cdecl *old_detail_keys)(void);
static WCHAR race_ui_settings[MAX_PATH];
static UINT race_trail_detail=2;
static BOOL race_ui_active;
static BOOL race_flash_visible(DWORD ticks) {return ticks%15<10;}
static void apply_race_preferences(void) {
    if(!race_ui_active)return;
    BYTE *config=*(BYTE **)(game+0x517958);
    if(config)config[0x19]=(BYTE)race_trail_detail;
    wo_render_event("render_engine_trails",race_trail_detail);
}
static void __cdecl detail_keys(void) {
    SHORT *key=(SHORT *)(game+0x66496a);SHORT pressed=*key;
    if(scene_light_key(pressed)){*key=-1;return;}
    if(pressed==0x29) { /* DirectInput grave/backtick; FPS only. */
        refresh_show_fps=!refresh_show_fps;*key=-1;
        wo_render_event("render_fps_visible",refresh_show_fps);
        WritePrivateProfileStringW(L"Render",L"ShowFPS",refresh_show_fps?L"1":L"0",race_ui_settings);
        return;
    }
    old_detail_keys();
    if(pressed==0x3e) { /* F4 cycles off/on/full; retain the selected mode. */
        BYTE *config=*(BYTE **)(game+0x517958);
        if(config && config[0x19]<=2) {
            race_trail_detail=config[0x19];
            const WCHAR *value=race_trail_detail==2?L"2":(race_trail_detail?L"1":L"0");
            WritePrivateProfileStringW(L"Render",L"EngineTrails",value,race_ui_settings);
        }
    }
}
static void finish_race_ui_effects(void) {
    if(refresh_active && refresh_race && *(DWORD *)(game+0x8c98c)) {
        /* The native loop clears extendedTimeFlag on EVERY render, but only
         * sets it in the 30Hz checkpoint update. Rebuild its presentation from
         * the still-live bonus timer and the real-time UI clock every frame. */
        BOOL bonus=*(int *)(game+0x87640)>0;
        static BOOL reported_bonus;
        if(bonus!=reported_bonus){wo_render_event("render_time_bonus_active",bonus);reported_bonus=bonus;}
        *(DWORD *)(game+0x8c17c)=bonus && race_flash_visible(ui_ticks);
    }
}
static void initialize_race_ui(const WCHAR *settings) {
    static const BYTE detail_entry[]={0x66,0xa1,0x6a,0x49,0xa6,0x00};
    /* DrawShipTrails is already hooked by ship_effects during refresh setup.
     * Extend that hook rather than rejecting its existing jump instruction. */
    if(!refresh_active || !old_ship_effects || !equal(game+0x283f0,detail_entry,sizeof(detail_entry))) {
        wo_render_event("render_race_ui_clock",0);return;
    }
    UINT n=0;while(settings[n] && n<MAX_PATH-1){race_ui_settings[n]=settings[n];++n;}
    if(settings[n])return;race_ui_settings[n]=0;
    race_trail_detail=GetPrivateProfileIntW(L"Render",L"EngineTrails",2,settings);
    if(race_trail_detail>2)race_trail_detail=2;
    old_detail_keys=(void *)hook(0x283f0,detail_entry,sizeof(detail_entry),detail_keys);
    race_ui_active=old_detail_keys!=0;
    wo_render_event("render_race_ui_clock",race_ui_active);
}
