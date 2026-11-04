/* Real native glyphs and the original InitHudPolys/quad/vertex instructions.
 * Independently scan the supplied PNG alpha to check submitted visible bounds,
 * including the off-center four-tile item indicator and padded gauge artwork. */
static BYTE *hud_test_pixels[3];static UINT hud_test_width[3];
static void hud_test_bounds(HudLayoutBounds *bounds) {
    for(UINT i=0;i<sizeof(HudLayoutBounds)*HUD_GROUPS;++i)((volatile BYTE *)bounds)[i]=0;
    for(UINT i=0;i<hud_layout_count;i+=4){
        UINT group=hud_layout_vertices[i].group;TLVertex *a=hud_layout_vertices[i].vertex;
        /* Each quad's tag uses a different native page handle. */
        UINT page=group==HUD_ITEM?2:(group==HUD_BOTTOM_RIGHT?1:0);
        UINT width=hud_test_width[page];BYTE *pixels=hud_test_pixels[page];
        float min_x=a->x,max_x=a->x,min_y=a->y,max_y=a->y,min_u=a->u,max_u=a->u,min_v=a->v,max_v=a->v;
        for(UINT k=1;k<4;++k){TLVertex *v=hud_layout_vertices[i+k].vertex;
            if(v->x<min_x)min_x=v->x;if(v->x>max_x)max_x=v->x;if(v->y<min_y)min_y=v->y;if(v->y>max_y)max_y=v->y;
            if(v->u<min_u)min_u=v->u;if(v->u>max_u)max_u=v->u;if(v->v<min_v)min_v=v->v;if(v->v>max_v)max_v=v->v;
        }
        int x0=(int)(min_u*width+.01f),y0=(int)(min_v*width+.01f),x1=(int)(max_u*width+.01f),y1=(int)(max_v*width+.01f);
        if(x0<0 || y0<0 || x1>(int)width || y1>(int)width || x1<=x0 || y1<=y0)ExitProcess(191);
        int left=x1,top=y1,right=x0,bottom=y0;
        for(int y=y0;y<y1;++y)for(int x=x0;x<x1;++x)if(pixels[(y*width+x)*4+3]){
            if(x<left)left=x;if(y<top)top=y;if(x+1>right)right=x+1;if(y+1>bottom)bottom=y+1;
        }
        if(right<=left)continue;
        float l=min_x+(max_x-min_x)*(left-x0)/(x1-x0),r=min_x+(max_x-min_x)*(right-x0)/(x1-x0);
        float t=min_y+(max_y-min_y)*(top-y0)/(y1-y0),d=min_y+(max_y-min_y)*(bottom-y0)/(y1-y0);
        HudLayoutBounds *out=bounds+group;
        if(!out->visible || l<out->left)out->left=l;if(!out->visible || r>out->right)out->right=r;
        if(!out->visible || t<out->top)out->top=t;if(!out->visible || d>out->bottom)out->bottom=d;out->visible=TRUE;
    }
}
static BOOL hud_test_near(float a,float b){float d=a-b;return d>-.003f && d<.003f;}
static void test_hud_layout(BYTE *image,BYTE *buffer) {
    #include "native_hud_refs.h"
    DWORD delta=(DWORD)(UINT_PTR)image-0x400000;
    for(UINT i=0;i<sizeof(native_hud_refs)/sizeof(native_hud_refs[0]);++i){
        DWORD *operand=(DWORD *)(image+native_hud_refs[i].rva);if(*operand!=native_hud_refs[i].expected)ExitProcess(192);*operand+=delta;
    }
    ((void (__cdecl *)(void))(image+0xc260))();
    WCHAR root[MAX_PATH];if(!GetEnvironmentVariableW(L"WO_TEST_FRAME_ART_ROOT",root,MAX_PATH))ExitProcess(193);
    const WCHAR *names[]={L"\\font-and-numbers.png",L"\\hud-gauges.png",L"\\hud-reticles.png"};UiHdPage previous[3];
    for(UINT i=0;i<3;++i){
        WCHAR path[MAX_PATH];path[0]=0;hd_append(path,MAX_PATH,root);hd_append(path,MAX_PATH,names[i]);UINT height;
        hud_test_pixels[i]=read_art_png(path,hud_test_width+i,&height);if(!hud_test_pixels[i] || height!=hud_test_width[i])ExitProcess(194);
        previous[i]=ui_hd_pages[22+i];ui_hd_pages[22+i]=(UiHdPage){0};
        ui_hd_pages[22+i].handle=600+i;ui_hd_pages[22+i].width=hud_test_width[i];
        ui_hd_pages[22+i].hud_coverage=hud_coverage_create(hud_test_pixels[i],hud_test_width[i]);
    }
    const float sizes[][2]={{640,480},{1920,1080},{3840,2160},{3440,1440},{1080,1920}};
    *(int *)(image+0x8c9d4)=0;title_active=FALSE;
    for(UINT aspect=0;aspect<5;++aspect){
        float width=sizes[aspect][0],height=sizes[aspect][1],ratio=(4.0f/3)/(width/height);
        ui_x=ratio<1?ratio:1;ui_y=ratio>1?1/ratio:1;
        *(float *)(image+0xaf0b4)=width;*(float *)(image+0xc8630)=height;
        frame_clear(image,buffer);hud_layout_begin(FALSE);++hud_depth;
        add_text("CHECK",20,20,8,0);add_text("0:38.0",20,30,16,0);
        add_text("POS",263,20,8,0);add_text("12",263,30,16,0);add_text("OF",279,38,8,0);add_text("12",285,30,16,0);
        add_text("LAP",263,46,8,0);add_text("0",263,56,16,0);add_text("OF",279,64,8,0);add_text("4",285,56,16,0);

        /* The speedometer wrapper anchors its entire assembly together. */
        anchor_x=anchor_y=1;++ui_depth;++anchor_depth;
        scaled_quad(.3f,image+0x6e3ba0,0);scaled_quad(.3f,image+0x6e3bc8,0);
        --anchor_depth;--ui_depth;anchor_x=anchor_y=.5f;
        --hud_depth;hud_layout_finish();
        HudLayoutBounds bounds[HUD_GROUPS];hud_test_bounds(bounds);float pad=height*(ratio>1?1/ratio:1)/12;
        if(aspect==2 && !hud_test_near(pad,180))ExitProcess(195);
        if(!hud_test_near(bounds[HUD_TOP_LEFT].left,pad) || !hud_test_near(bounds[HUD_TOP_LEFT].top,pad))ExitProcess(196);
        if(!hud_test_near(bounds[HUD_TOP_RIGHT].right,width-pad) || !hud_test_near(bounds[HUD_TOP_RIGHT].top,pad))ExitProcess(197);
        if(!hud_test_near(bounds[HUD_BOTTOM_RIGHT].right,width-pad) || !hud_test_near(bounds[HUD_BOTTOM_RIGHT].bottom,height-pad))ExitProcess(198);
        if(!old_hud_item || image[0x9d60]!=0xe9)ExitProcess(201);
        static BYTE ship[0x100];
        for(UINT fallback=0;fallback<2;++fallback)for(UINT item=0;item<=8;++item){
            ui_hd_pages[24].handle=fallback?0:602;
            ship[0xb8]=(BYTE)item;frame_clear(image,buffer);
            ((void (__cdecl *)(const BYTE *))(image+0x9d60))(ship);
            if(!fallback)hud_test_bounds(bounds);
            else{
                bounds[HUD_ITEM]=(HudLayoutBounds){0};
                for(UINT k=0;k<hud_layout_count;++k){
                    TLVertex *v=hud_layout_vertices[k].vertex;HudLayoutBounds *out=bounds+HUD_ITEM;
                    if(!out->visible || v->x<out->left)out->left=v->x;if(!out->visible || v->x>out->right)out->right=v->x;
                    if(!out->visible || v->y<out->top)out->top=v->y;if(!out->visible || v->y>out->bottom)out->bottom=v->y;out->visible=TRUE;
                }
            }
            if(!hud_test_near((bounds[HUD_ITEM].left+bounds[HUD_ITEM].right)/2,width/2) || !hud_test_near(bounds[HUD_ITEM].top,pad))ExitProcess(199);
        }
        ui_hd_pages[24].handle=602;
        BOOL saved_race=refresh_race,saved_fps=refresh_show_fps;UINT saved_measure=refresh_measured_fps;DWORD saved_view=*(DWORD *)(image+0x8c98c);
        refresh_race=refresh_show_fps=TRUE;refresh_measured_fps=165;*(DWORD *)(image+0x8c98c)=1;
        frame_clear(image,buffer);draw_refresh_stats();hud_test_bounds(bounds);
        refresh_race=saved_race;refresh_show_fps=saved_fps;refresh_measured_fps=saved_measure;*(DWORD *)(image+0x8c98c)=saved_view;
        if(!hud_test_near(bounds[HUD_FPS].bottom,height-pad) || !hud_test_near((bounds[HUD_FPS].left+bounds[HUD_FPS].right)/2,width/2))ExitProcess(200);
    }
    for(UINT i=0;i<3;++i){ui_hd_release(ui_hd_pages+22+i);ui_hd_pages[22+i]=previous[i];VirtualFree(hud_test_pixels[i],0,MEM_RELEASE);}
}
