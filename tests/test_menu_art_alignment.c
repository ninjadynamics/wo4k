static void frame_fixture_flat(BYTE *p,SHORT left,SHORT top,SHORT right,SHORT bottom,BOOL white){
    for(UINT i=0;i<24;++i)p[i]=0;
    p[4]=white?255:85;p[5]=white?255:0;p[6]=white?255:40;
    for(UINT i=0;i<4;++i){*(SHORT *)(p+8+i*4)=(i&1)?right:left;*(SHORT *)(p+10+i*4)=(i&2)?bottom:top;}
}
static void frame_clear(BYTE *image,BYTE *buffer){
    *(UINT *)(buffer+0x39210)=*(UINT *)(buffer+0x39214)=0;*(SHORT *)(image+0x87040)=0;
}
static float frame_cap_edge(BYTE *image,BYTE *buffer,UINT tile,float u){
    frame_clear(image,buffer);quad(.99f,image+0x511740+tile*40,0);
    UINT count=*(UINT *)(buffer+0x39214);const TLVertex *v=(const TLVertex *)(buffer+0xa410);
    for(UINT i=0;i<count;i+=4)if(u>=v[i].u-.00001f && u<=v[i+1].u+.00001f && v[i+1].u>v[i].u)
        return v[i].x+(v[i+1].x-v[i].x)*(u-v[i].u)/(v[i+1].u-v[i].u);
    ExitProcess(150);return 0;
}
static void test_menu_art_alignment(BYTE *image,BYTE *buffer){
    WCHAR root[MAX_PATH];if(!GetEnvironmentVariableW(L"WO_TEST_FRAME_ART_ROOT",root,MAX_PATH))ExitProcess(151);
    const WCHAR *names[]={L"\\menu-frame-left.png",L"\\menu-frame-right.png"};
    for(UINT i=0;i<2;++i){
        WCHAR path[MAX_PATH];path[0]=0;hd_append(path,MAX_PATH,root);hd_append(path,MAX_PATH,names[i]);
        UINT width=0,height=0;BYTE *pixels=read_art_png(path,&width,&height);
        if(!pixels || width!=height)ExitProcess(152);
        UiHdPage *page=ui_hd_pages+(i?3:1);page->handle=i?303:301;
        ui_hd_measure_frame(pixels,width,i?2:0,&page->frame_art);VirtualFree(pixels,0,MEM_RELEASE);
        if(!page->frame_art.valid)ExitProcess(153);
    }
    ui_hd_pages[2].handle=302;
    MenuArtBounds art;if(!menu_frame_art_bounds(&art))ExitProcess(154);
    WCHAR report_path[MAX_PATH];if(GetEnvironmentVariableW(L"WO_TEST_FRAME_REPORT",report_path,MAX_PATH)){
        HANDLE f=CreateFileW(report_path,GENERIC_WRITE,0,0,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,0);DWORD written;
        if(f!=INVALID_HANDLE_VALUE){WriteFile(f,&art,sizeof(art),&written,0);CloseHandle(f);}
    }
    /* These are measured from the supplied PNGs, including their antialiased
     * footer row587 and transparent right-cap column255. */
    if(!frame_near(art.inner_left,22.5f) || !frame_near(art.inner_right,305.375f) || !frame_near(art.outer_right,307.875f) ||
       !frame_near(art.start_top,180.5f) || !frame_near(art.footer_top,196.375f) || !frame_near(art.footer_bottom,197.75f) ||
       !frame_near(art.footer_end,213.625f))ExitProcess(155);
    const float widths[]={640,1920,3840,3440,1080},heights[]={480,1080,2160,1440,1920};
    const float scales_x[]={1,.75f,.75f,24.0f/43,1},scales_y[]={1,1,1,1,.421875f};
    for(UINT aspect=0;aspect<5;++aspect){
        *(float *)(image+0xaf0b4)=widths[aspect];*(float *)(image+0xc8630)=heights[aspect];ui_x=scales_x[aspect];ui_y=scales_y[aspect];
        for(UINT side=0;side<2;++side){
            float u0=side?art.source.right_inner/256:0;
            float u1=side?art.source.right_outer/256:art.source.left_inner/256;
            float cap0=frame_cap_edge(image,buffer,side?2:0,u0),cap1=frame_cap_edge(image,buffer,side?2:0,u1);
            frame_clear(image,buffer);quad(.99f,image+0x511740+(side?7:6)*40,0);
            if(*(UINT *)(buffer+0x39214)!=4)ExitProcess(156);
            const TLVertex *v=(const TLVertex *)(buffer+0xa410);
            if(!frame_near(v[0].v,50.0f/256) || !frame_near(v[3].v,50.0f/256))ExitProcess(157);
            float side0=v[0].x+(v[1].x-v[0].x)*(u0-v[0].u)/(v[1].u-v[0].u);
            float side1=v[0].x+(v[1].x-v[0].x)*(u1-v[0].u)/(v[1].u-v[0].u);
            /* Compare the real submitted vertices in display pixels, rather
             * than merely checking two copies of the geometry calculation. */
            float d0=side0-cap0,d1=side1-cap1;
            if(d0<-.002f || d0>.002f || d1<-.002f || d1>.002f)ExitProcess(158);
        }
        BYTE p[24];frame_fixture_flat(p,111,82,112,180,TRUE);frame_clear(image,buffer);flat_quad(.13f,p);
        if(*(UINT *)(buffer+0x39214)!=4)ExitProcess(159);
        const TLVertex *v=(const TLVertex *)(buffer+0xa410);float main_left=v[0].x,main_right=v[1].x;
        frame_fixture_flat(p,112,196,113,215,TRUE);frame_clear(image,buffer);flat_quad(.13f,p);
        if(*(UINT *)(buffer+0x39214)!=4)ExitProcess(160);v=(const TLVertex *)(buffer+0xa410);
        if(!frame_near(v[0].x/widths[aspect],main_left/widths[aspect]) || !frame_near(v[1].x/widths[aspect],main_right/widths[aspect]) ||
           !frame_near(v[0].y/heights[aspect],menu_y(art.footer_bottom)) || !frame_near(v[2].y/heights[aspect],menu_y(art.footer_end)))ExitProcess(161);
        /* The START band meets the frame's caps, which move with the frame. */
        frame_fixture_flat(p,22,180,307,197,FALSE);frame_clear(image,buffer);flat_quad(.13f,p);v=(const TLVertex *)(buffer+0xa410);
        if(*(UINT *)(buffer+0x39214)!=4 || !frame_near(v[0].x/widths[aspect],menu_frame_x(art.inner_left)) ||
           !frame_near(v[1].x/widths[aspect],menu_frame_x(art.inner_right)) || !frame_near(v[0].y/heights[aspect],menu_y(art.start_top)) ||
           !frame_near(v[2].y/heights[aspect],menu_y(art.footer_top)))ExitProcess(162);
        /* The selected footer cell's fill meets the drawn edges of both
         * dividers (113-209 in this menu, 113-210 in others), leaving no
         * strip of the panel between them. */
        frame_fixture_flat(p,209,196,210,215,TRUE);frame_clear(image,buffer);flat_quad(.13f,p);
        if(*(UINT *)(buffer+0x39214)!=4)ExitProcess(168);
        float divider_right=((const TLVertex *)(buffer+0xa410))[0].x;
        for(UINT end=209;end<=210;++end){
            frame_fixture_flat(p,113,197,end,215,FALSE);frame_clear(image,buffer);flat_quad(.13f,p);v=(const TLVertex *)(buffer+0xa410);
            if(*(UINT *)(buffer+0x39214)!=4 || !frame_near(v[0].x/widths[aspect],main_right/widths[aspect]) ||
               !frame_near(v[1].x/widths[aspect],divider_right/widths[aspect]) ||
               !frame_near(v[0].y/heights[aspect],menu_y(art.footer_bottom)) || !frame_near(v[2].y/heights[aspect],menu_y(art.footer_end)))ExitProcess(163);
        }
        frame_fixture_flat(p,112,196,209,197,TRUE);frame_clear(image,buffer);flat_quad(.13f,p);
        if(*(UINT *)(buffer+0x39214))ExitProcess(164); /* artwork already supplies this band */
        for(UINT side=0;side<2;++side){
            frame_fixture_flat(p,side?306:21,82,side?307:22,180,TRUE);frame_clear(image,buffer);flat_quad(.13f,p);
            if(*(UINT *)(buffer+0x39214))ExitProcess(165);
        }
    }
    ui_hd_pages[1].handle=0;BYTE p[24];frame_fixture_flat(p,21,82,22,180,TRUE);
    frame_clear(image,buffer);flat_quad(.13f,p);if(*(UINT *)(buffer+0x39214)!=4)ExitProcess(166);
    ui_hd_pages[1].handle=301;ui_hd_pages[2].handle=0;
    if(ui_hd_frame_art(&art.source))ExitProcess(167);
    for(UINT i=1;i<=3;++i){ui_hd_pages[i].handle=0;ui_hd_pages[i].frame_art.valid=FALSE;}
    *(float *)(image+0xaf0b4)=640;*(float *)(image+0xc8630)=480;ui_x=.75f;ui_y=1;
}
