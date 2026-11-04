/* Translate complete HUD groups after native emission. Measure the submitted
 * glyphs/tiles, including HD alpha, rather than guessing from a text pen or
 * treating the four item tiles as separate centered widgets. */
enum {HUD_NONE,HUD_TOP_LEFT,HUD_TOP_RIGHT,HUD_BOTTOM_LEFT,HUD_BOTTOM_RIGHT,HUD_ITEM,HUD_FPS,HUD_GROUPS};
typedef struct HudLayoutVertex {TLVertex *vertex;UINT group;} HudLayoutVertex;
typedef struct HudLayoutBounds {float left,top,right,bottom;BOOL visible;} HudLayoutBounds;
static HudLayoutVertex hud_layout_vertices[4096];
static HudLayoutBounds hud_layout_bounds[HUD_GROUPS];
static UINT hud_layout_count,hud_layout_group;
static BOOL hud_layout_active,hud_layout_fps;
static void hud_layout_begin(BOOL fps) {
    hud_layout_count=0;hud_layout_group=HUD_NONE;hud_layout_fps=fps;
    hud_layout_active=installed && !menu_active();
    for(UINT i=0;i<HUD_GROUPS;++i)hud_layout_bounds[i]=(HudLayoutBounds){0};
}
static void hud_layout_vertex(TLVertex *v) {
    if(!hud_layout_active || !hud_layout_group)return;
    if(hud_layout_count==4096){hud_layout_active=FALSE;return;}
    hud_layout_vertices[hud_layout_count++]=(HudLayoutVertex){v,hud_layout_group};
}
static UINT hud_layout_classify(const BYTE *p) {
    if(!hud_layout_active || !hud_depth)return HUD_NONE;
    if(hud_layout_fps)return HUD_FPS;
    if(*(const WORD *)(p+22)==34)return HUD_ITEM;
    if(anchor_x==0)return anchor_y==0?HUD_TOP_LEFT:(anchor_y==1?HUD_BOTTOM_LEFT:HUD_NONE);
    if(anchor_x==1)return anchor_y==0?HUD_TOP_RIGHT:(anchor_y==1?HUD_BOTTOM_RIGHT:HUD_NONE);
    return HUD_NONE;
}
static void hud_layout_quad(const BYTE *p,UINT first) {
    if(!hud_layout_active || !hud_layout_group || hud_layout_count-first!=4)return;
    TLVertex *v[4];for(UINT i=0;i<4;++i)v[i]=hud_layout_vertices[first+i].vertex;
    float left=v[0]->x,right=left,top=v[0]->y,bottom=top;
    for(UINT i=1;i<4;++i){
        if(v[i]->x<left)left=v[i]->x;if(v[i]->x>right)right=v[i]->x;
        if(v[i]->y<top)top=v[i]->y;if(v[i]->y>bottom)bottom=v[i]->y;
    }
    UINT slot=*(const WORD *)(p+22);slot=slot>=10?slot-10:UI_HD_PAGE_COUNT;
    int u0=p[12],v0=p[13],u1=p[20],v1=p[29];float art[4];
    if(slot<UI_HD_PAGE_COUNT && ui_hd_pages[slot].handle && ui_hd_pages[slot].hud_coverage && u1>u0 && v1>v0){
        if(!hud_coverage_bounds(ui_hd_pages[slot].hud_coverage,ui_hd_pages[slot].width,u0,v0,u1,v1,art))return;
        float x=left,y=top,w=right-left,h=bottom-top;
        left=x+w*(art[0]-u0)/(u1-u0);right=x+w*(art[2]-u0)/(u1-u0);
        top=y+h*(art[1]-v0)/(v1-v0);bottom=y+h*(art[3]-v0)/(v1-v0);
    }
    HudLayoutBounds *b=hud_layout_bounds+hud_layout_group;
    if(!b->visible || left<b->left)b->left=left;if(!b->visible || right>b->right)b->right=right;
    if(!b->visible || top<b->top)b->top=top;if(!b->visible || bottom>b->bottom)b->bottom=bottom;b->visible=TRUE;
}
static void hud_layout_finish(void) {
    if(hud_layout_active){
        float width=*(float *)(game+0xaf0b4),height=*(float *)(game+0xc8630);
        /* Twenty 320x240 HUD units are 180 physical pixels at 3840x2160.
         * ui_x/ui_y retain equal physical padding for every display shape. */
        float pad=20*width*ui_x/320,dx[HUD_GROUPS]={0},dy[HUD_GROUPS]={0};
        for(UINT g=1;g<HUD_GROUPS;++g){
            HudLayoutBounds *b=hud_layout_bounds+g;if(!b->visible)continue;
            if(g==HUD_TOP_LEFT || g==HUD_BOTTOM_LEFT)dx[g]=pad-b->left;
            if(g==HUD_TOP_RIGHT || g==HUD_BOTTOM_RIGHT)dx[g]=width-pad-b->right;
            if(g==HUD_ITEM || g==HUD_FPS)dx[g]=width*.5f-(b->left+b->right)*.5f;
            if(g==HUD_TOP_LEFT || g==HUD_TOP_RIGHT || g==HUD_ITEM)dy[g]=pad-b->top;
            if(g==HUD_BOTTOM_LEFT || g==HUD_BOTTOM_RIGHT || g==HUD_FPS)dy[g]=height-pad-b->bottom;
        }
        for(UINT i=0;i<hud_layout_count;++i){HudLayoutVertex *entry=hud_layout_vertices+i;entry->vertex->x+=dx[entry->group];entry->vertex->y+=dy[entry->group];}
    }
    hud_layout_active=FALSE;hud_layout_group=HUD_NONE;
}

/* DrawWeapon is called BEFORE DrawRaceHud by the native race loop. Give
 * that separate pass its own HUD scope so its four tiles share one center. */
static void (__cdecl *old_hud_item)(const BYTE *ship);
static void __cdecl hud_item(const BYTE *ship) {
    hud_layout_begin(FALSE);++hud_depth;
    old_hud_item(ship);
    --hud_depth;hud_layout_finish();
}
static void initialize_hud_layout(void) {
    static const BYTE entry[]={0x53,0x33,0xc9,0x8b,0x44,0x24,0x08};
    old_hud_item=(void *)hook(0x9d60,entry,sizeof(entry),hud_item);
    wo_render_event("render_hud_padding",old_hud_item!=0);
}
