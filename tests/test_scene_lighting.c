static UINT scene_finish_calls;
static BOOL scene_expect_world_reset=TRUE;
static void __cdecl fixture_scene_finish(void){
    ++scene_finish_calls;
    if(scene_expect_world_reset){
        BYTE *p=*(BYTE **)(game+0x53f1a0),*end=*(BYTE **)(game+0x631d54);
        if(end-p<12 || p[0]!=8 || p[1]!=8 || *(WORD *)(p+2)!=1 ||
           *(DWORD *)(p+4)!=15 || *(DWORD *)(p+8)!=0)ExitProcess(146);
        /* Consume the checked prologue so later fixtures start with the same
         * native stream; a real native buffer is retired after this callback. */
        copy(p,p+12,(UINT)(end-p-12));*(BYTE **)(game+0x631d54)=end-12;
    }
}
static UINT scene_test_state(BYTE *p,UINT type,UINT value) {
    p[0]=8;p[1]=8;*(WORD *)(p+2)=1;*(DWORD *)(p+4)=type;*(DWORD *)(p+8)=value;return 12;
}
static UINT scene_test_triangle(BYTE *p,UINT a,UINT b,UINT c) {
    p[0]=3;p[1]=8;*(WORD *)(p+2)=1;*(WORD *)(p+4)=(WORD)a;*(WORD *)(p+6)=(WORD)b;
    *(WORD *)(p+8)=(WORD)c;*(WORD *)(p+10)=0;return 12;
}
static void test_scene_lighting(BYTE *image,const WCHAR *settings) {
    DWORD saved_view=*(DWORD *)(image+0x8c98c),saved_menu=*(DWORD *)(image+0x8c9d4),saved_modal=*(DWORD *)(image+0x89cb0);
    BOOL saved_race=refresh_race,saved_shadows=shadows_active,saved_exhaust=exhaust_active;
    FinishFn saved_finish=old_finish_transform;
    BYTE *saved_buffer=*(BYTE **)(image+0x53f190),*saved_start=*(BYTE **)(image+0x53f1a0),*saved_end=*(BYTE **)(image+0x631d54);
    UINT saved_size=*(UINT *)(image+0xc8610),saved_count=*(UINT *)(image+0x631d50);
    WritePrivateProfileStringW(L"Render",L"ShadowLiftPercent",L"0",settings);
    WritePrivateProfileStringW(L"Render",L"HighlightsPercent",L"100",settings);
    initialize_scene_lighting(settings);
    if(scene_shadow_lift || scene_highlights!=100 || scene_light_color(0x80402010)!=0x80402010)ExitProcess(120);
    *(DWORD *)(image+0x8c98c)=1;*(DWORD *)(image+0x8c9d4)=*(DWORD *)(image+0x89cb0)=0;refresh_race=TRUE;
    const SHORT keys[]={0x0d,0x4e,0x0c,0x4a,0xc9,0xc9,0xd1,0xd1};
    const UINT wanted[]={5,10,5,0,105,110,105,100};
    for(UINT i=0;i<8;++i){
        observed_native_key=0;*(SHORT *)(image+0x66496a)=keys[i];
        ((RefreshVoidFn)(image+0x283f0))();
        const WCHAR *pref=i<4?L"ShadowLiftPercent":L"HighlightsPercent";
        UINT value=i<4?scene_shadow_lift:scene_highlights;
        if(value!=wanted[i] || *(SHORT *)(image+0x66496a)!=-1 || observed_native_key ||
           GetPrivateProfileIntW(L"Render",pref,999,settings)!=wanted[i] ||
           (i<4?scene_highlights!=100:scene_shadow_lift!=0))ExitProcess(121);
    }
    WritePrivateProfileStringW(L"Render",L"ShadowLiftPercent",L"275",settings);
    WritePrivateProfileStringW(L"Render",L"HighlightsPercent",L"80",settings);initialize_scene_lighting(settings);
    if(scene_shadow_lift!=275 || scene_highlights!=80)ExitProcess(122);
    for(UINT strength=0;strength<=300;strength+=5)for(UINT highlights=0;highlights<=300;highlights+=5){
        scene_light_set(strength);scene_highlights_set(highlights);UINT previous=0;
        for(UINT c=0;c<256;++c){UINT y=scene_light_curve[c];if(y<previous)ExitProcess(123);previous=y;}
        if(scene_light_curve[0] || (highlights==100 && scene_light_curve[255]!=255))ExitProcess(124);
    }
    scene_light_key(0x4e);scene_light_key(0xc9);if(scene_shadow_lift!=300 || scene_highlights!=300)ExitProcess(125);
    scene_light_set(0);scene_highlights_set(0);scene_light_key(0x4a);scene_light_key(0xd1);
    if(scene_shadow_lift || scene_highlights)ExitProcess(126);
    scene_highlights_set(100);scene_light_set(100);
    if(scene_light_curve[32]!=56 || scene_light_curve[224]!=227 || scene_light_color(0x80402010)!=0x80643219)ExitProcess(127);
    scene_light_set(0);scene_highlights_set(50);
    if(scene_light_curve[64]!=64 || scene_light_curve[255]!=192)ExitProcess(139);
    scene_highlights_set(150);if(scene_light_curve[160]!=176 || scene_light_curve[32]!=32)ExitProcess(140);
    scene_highlights_set(100);scene_light_set(100);
    static BYTE execute[2048],screen[128],stream_copy[512];LitVertex *v=(LitVertex *)execute;
    const LitVertex source={1,2,3,0,0x80402010,0x12345678,.25f,.75f};
    for(UINT i=0;i<10;++i)v[i]=source;
    v[1].color=0xffffffff;v[6].reserved=TRAIL_VERTEX_TAG;v[7].reserved=SKY_VERTEX_TAG;v[8].reserved=SPRITE_VERTEX_TAG;
    BYTE *stream=execute+1024;UINT length=0;
    length+=scene_test_state(stream+length,27,0);
    length+=scene_test_triangle(stream+length,0,1,2);
    length+=scene_test_state(stream+length,27,1);
    length+=scene_test_state(stream+length,19,3); /* SRCCOLOR */
    length+=scene_test_state(stream+length,20,4); /* INVSRCCOLOR */
    length+=scene_test_triangle(stream+length,2,3,4); /* v2 shared with opaque */
    length+=scene_test_state(stream+length,27,0);
    length+=scene_test_triangle(stream+length,5,6,7);
    length+=scene_test_triangle(stream+length,5,7,8);
    copy(stream_copy,stream,length);
    for(UINT i=0;i<sizeof(screen);++i)screen[i]=(BYTE)i;
    BYTE *saved_screen=*(BYTE **)(image+0x7c9690);*(BYTE **)(image+0x7c9690)=screen;
    *(BYTE **)(image+0x53f190)=execute;*(BYTE **)(image+0x53f1a0)=stream;*(BYTE **)(image+0x631d54)=stream+length;
    *(UINT *)(image+0xc8610)=sizeof(execute);*(UINT *)(image+0x631d50)=10;
    shadows_active=exhaust_active=FALSE;old_finish_transform=fixture_scene_finish;
    finish_transform();
    if(scene_finish_calls!=1 || v[0].color!=0x80643219 || v[1].color!=0xffffffff || v[5].color!=0x80643219)ExitProcess(128);
    for(UINT i=2;i<10;++i)if(i!=5 && v[i].color!=source.color)ExitProcess(141);
    DWORD lifted=v[0].color;v[0].color=source.color;
    if(!equal((BYTE *)v,(const BYTE *)&source,sizeof(source)) || !equal(stream,stream_copy,length))ExitProcess(129);
    for(UINT i=0;i<sizeof(screen);++i)if(screen[i]!=(BYTE)i)ExitProcess(130);
    scene_light_set(300);scene_highlights_set(300);exhaust_active=TRUE;
    finish_transform();
    for(UINT i=2;i<=4;++i)if(v[i].color!=source.color)ExitProcess(142);
    exhaust_active=FALSE;scene_highlights_set(100);scene_light_set(100);v[0]=source;
    *(DWORD *)(image+0x8c9d4)=1;finish_transform();
    if(v[0].color!=source.color || scene_light_key(0x0d) || scene_light_key(0xc9))ExitProcess(131);
    *(SHORT *)(image+0x66496a)=0x0d;observed_native_key=0;detail_keys();if(observed_native_key!=0x0d)ExitProcess(132);
    *(DWORD *)(image+0x8c9d4)=0;*(DWORD *)(image+0x89cb0)=1;
    if(scene_light_key(0x4a) || scene_light_key(0xd1))ExitProcess(133);*(DWORD *)(image+0x89cb0)=0;
    *(DWORD *)(image+0x8c98c)=0;finish_transform();if(v[0].color!=source.color)ExitProcess(134);
    *(DWORD *)(image+0x8c98c)=1;*(UINT *)(image+0x631d50)=33;scene_expect_world_reset=FALSE;
    finish_transform();scene_expect_world_reset=TRUE;if(v[0].color!=source.color)ExitProcess(135);
    *(UINT *)(image+0x631d50)=10;scene_light_set(0);finish_transform();if(v[0].color!=source.color)ExitProcess(136);
    scene_light_set(100);copy(stream,stream_copy,length);*(WORD *)(stream+length-8)=10;
    finish_transform();if(v[0].color!=source.color)ExitProcess(143);
    copy(stream,stream_copy,length);v[0]=source;finish_transform();if(v[0].color!=lifted)ExitProcess(137);
    BYTE point[8]={1,4,1,0,2,0,1,0};
    if(!scene_light_classify(point,sizeof(point),4) || scene_light_usage[0] || scene_light_usage[1]!=2 || scene_light_usage[2]!=2 || scene_light_usage[3])ExitProcess(144);
    BYTE line[8]={2,4,1,0,0,0,3,0};
    if(!scene_light_classify(line,sizeof(line),4) || scene_light_usage[0]!=2 || scene_light_usage[3]!=2)ExitProcess(145);
    WritePrivateProfileStringW(L"Render",L"ShadowLiftPercent",L"999",settings);
    WritePrivateProfileStringW(L"Render",L"HighlightsPercent",L"999",settings);initialize_scene_lighting(settings);
    if(scene_shadow_lift || scene_highlights!=100)ExitProcess(138);
    WritePrivateProfileStringW(L"Render",L"ShadowLiftPercent",L"0",settings);
    WritePrivateProfileStringW(L"Render",L"HighlightsPercent",L"100",settings);scene_light_notice=FALSE;
    *(DWORD *)(image+0x8c98c)=saved_view;*(DWORD *)(image+0x8c9d4)=saved_menu;*(DWORD *)(image+0x89cb0)=saved_modal;
    refresh_race=saved_race;shadows_active=saved_shadows;exhaust_active=saved_exhaust;old_finish_transform=saved_finish;
    *(BYTE **)(image+0x53f190)=saved_buffer;*(BYTE **)(image+0x53f1a0)=saved_start;*(BYTE **)(image+0x631d54)=saved_end;
    *(BYTE **)(image+0x7c9690)=saved_screen;*(UINT *)(image+0xc8610)=saved_size;*(UINT *)(image+0x631d50)=saved_count;
}
