/* Original menu artwork, with independent layout for frame, text and models.
 * Frame caps keep their proportions; the flat panel spans grow to fill the
 * display. The repeating backdrop is tiled further across the expanded area.
 */
#include "menu_frame_art.h"
typedef void (__cdecl *MenuModelFn)(int,int,int,SHORT,SHORT,BYTE *);
typedef void (__cdecl *Text4Fn)(const char *,int,int,int);
typedef void (__cdecl *Text5Fn)(const char *,int,int,int,int);
typedef void (__cdecl *FlatFn)(float,const BYTE *);
typedef void (__cdecl *NumberFn)(int,int,int,int,int);
typedef void (__cdecl *TimeFn)(int,int,int,int,int,UINT);
typedef void (__cdecl *SpeedoFn)(void *,void *,int);
static MenuModelFn old_menu_model;
static Text4Fn old_print_text,old_print_center;
static Text5Fn old_add_text,old_center_text,old_center_text2;
static FlatFn old_flat_quad;
static QuadFn old_pixel_quad;
static NumberFn old_number_text,old_justify_number;
static TimeFn old_time_text;
static SpeedoFn old_speedo;
static int menu_model_depth,menu_frame_depth,anchor_depth;
static int audio_config_depth;
static BOOL menu_patching;
static BOOL menu_thin_x,menu_thin_y;
static void (__cdecl *old_slide_menu)(void);
static BYTE menu_previous[14][40];
static BOOL menu_history_valid;
static float menu_motion_dx;
static void __cdecl slide_menu(void) {
    if(refresh_active && *(DWORD *)(game+0x89c7c)) {
        copy((BYTE *)menu_previous,game+0x511740,sizeof(menu_previous));menu_history_valid=TRUE;
    }
    old_slide_menu();
}
static float panel_motion(const BYTE *p,float basis) {
    if(!menu_history_valid || !*(DWORD *)(game+0x8c990) || p<game+0x511740 || p>=game+0x511740+14*40 || (p-game-0x511740)%40)return 0;
    int i=(int)(p-game-0x511740)/40;
    float before=*(SHORT *)(menu_previous[i]+8),now=*(const SHORT *)(p+8);
    return (before-now)*(1-ui_panel_phase())/basis;
}
static float menu_center_x,menu_center_y;
static BOOL menu_active(void) {return installed && menu_patching && *(int *)(game+0x8c9d4)!=0;}
static float menu_axis(float value,float size,float first,float last,float scale) {
    if(value<=first)return value*scale/size;
    if(value>=last)return 1-(size-value)*scale/size;
    float left=first*scale/size,right=1-(size-last)*scale/size;
    return left+(value-first)*(right-left)/(last-first);
}
static float menu_x(float x) {return menu_axis(x,320,32,296,ui_x);}
static float menu_y(float y) {return menu_axis(y,240,44,204,ui_y);}
typedef struct MenuArtBounds {
    float outer_left,inner_left,inner_right,outer_right,start_top,footer_top,footer_bottom,footer_end;
    UiHdFrameArt source;
} MenuArtBounds;
static BOOL menu_frame_art_bounds(MenuArtBounds *bounds){
    UiHdFrameArt art;if(!ui_hd_frame_art(&art))return FALSE;
    const BYTE *left=game+0x511740,*right=left+2*40,*bottom=left+3*40;
    float lx=*(const SHORT *)(left+8),lw=*(const SHORT *)(left+16)-lx;
    float rx=*(const SHORT *)(right+8),rw=*(const SHORT *)(right+16)-rx;
    float by=*(const SHORT *)(bottom+10),bh=*(const SHORT *)(bottom+26)-by;
    float ru=(float)right[20]-right[12],bv=(float)bottom[29]-bottom[13];
    if(right[12]==0 && right[20]==63)ru=64;
    if(lw<=0 || rw<=0 || bh<=0 || ru<=0 || bv<=0)return FALSE;
    bounds->outer_left=lx;bounds->inner_left=lx+lw*art.left_inner/256;
    bounds->inner_right=rx+rw*(art.right_inner-right[12])/ru;
    bounds->outer_right=rx+rw*(art.right_outer-right[12])/ru;
    bounds->start_top=by+bh*(art.start_top-bottom[13])/bv;
    bounds->footer_top=by+bh*(art.footer_top-bottom[13])/bv;
    bounds->footer_bottom=by+bh*(art.footer_bottom-bottom[13])/bv;
    bounds->footer_end=by+bh*(art.footer_end-bottom[13])/bv;
    bounds->source=art;return TRUE;
}
static void menu_adjust_vertex(TLVertex *v) {
    float width=*(float *)(game+0xaf0b4),height=*(float *)(game+0xc8630);
    float x=v->x*320/width,y=v->y*240/height;
    v->x=width*(menu_thin_x?menu_x(menu_center_x)+(x-menu_center_x)*ui_x/320:menu_x(x));
    v->y=height*(menu_thin_y?menu_y(menu_center_y)+(y-menu_center_y)*ui_y/240:menu_y(y));
}
static BOOL is_menu_frame(const BYTE *p) {
    return menu_active() && p>=game+0x511740 && p<game+0x511740+14*40 && !((p-game-0x511740)%40);
}
static BYTE menu_uv(BYTE first,BYTE last,float fraction) {
    return (BYTE)((float)first+((float)last-first)*fraction+0.5f);
}
static void draw_menu_frame(float z,const BYTE *p,int blend) {
    float saved_motion=menu_motion_dx;menu_motion_dx=panel_motion(p,320);
    float left=*(const SHORT *)(p+8),right=*(const SHORT *)(p+16);
    float top=*(const SHORT *)(p+10),bottom=*(const SHORT *)(p+26);
    UINT page=*(const WORD *)(p+22);
    BOOL hd=page>=6 && page<=8 && ui_hd_frame_page(page-5);
    float u0=p[12],u1=p[20];
    MenuArtBounds art;UINT tile=(UINT)(p-game-0x511740)/40;
    BOOL side=hd && (tile==6 || tile==7) && menu_frame_art_bounds(&art);
    if(side){
        if(tile==6){left=art.outer_left;right=art.inner_left;u0=0;u1=art.source.left_inner;}
        else{left=art.inner_right;right=*(SHORT *)(game+0x511740+2*40+16);u0=art.source.right_inner;u1=64;}
    }
    /* Native SD tiles reserve two columns at the page edge. Authored HD
     * frame PNGs use the complete page, including the logo across its join. */
    if(hd && page<8 && u0==0 && u1==254)u1=256;
    if(hd && page==8 && u0==0 && u1==63)u1=64;
    const float xs[4]={left,32,296,right},ys[4]={top,44,204,bottom};
    /* Split at cap boundaries before remapping, so a corner and a long blue
     * span never share a stretched primitive. UVs stay in the original art. */
    for(UINT yi=0;yi<3;++yi)for(UINT xi=0;xi<3;++xi) {
        float x0=xs[xi]>left?xs[xi]:left,x1=xs[xi+1]<right?xs[xi+1]:right;
        float y0=ys[yi]>top?ys[yi]:top,y1=ys[yi+1]<bottom?ys[yi+1]:bottom;
        if(x1<=x0 || y1<=y0 || right<=left || bottom<=top)continue;
        BYTE part[40];copy(part,p,40);
        if(side && tile==6)*(WORD *)(part+22)=6; /* sample the actual left cap's edge */
        for(UINT corner=0;corner<4;++corner) {
            UINT offset=8+corner*8;float x=(corner&1)?x1:x0,y=(corner&2)?y1:y0;
            *(SHORT *)(part+offset)=(SHORT)x;*(SHORT *)(part+offset+2)=(SHORT)y;
            part[offset+4]=menu_uv(p[12],p[20],(x-left)/(right-left));
            part[offset+5]=menu_uv(p[13],p[29],(y-top)/(bottom-top));
        }
        BYTE *buffer=*(BYTE **)(game+0x87038);UINT before=buffer?*(UINT *)(buffer+0x39214):0;
        ++menu_frame_depth;old_quad(z,part,blend);--menu_frame_depth;
        if(hd && buffer && *(BYTE **)(game+0x87038)==buffer && *(UINT *)(buffer+0x39214)==before+4){
            TLVertex *v=(TLVertex *)(buffer+0xa410)+before;
            for(UINT corner=0;corner<4;++corner){
                float x=(corner&1)?x1:x0,y=(corner&2)?y1:y0;
                v[corner].u=(u0+(u1-u0)*(x-left)/(right-left))/256;
                v[corner].v=side?50.0f/256:(p[13]+((float)p[29]-p[13])*(y-top)/(bottom-top))/256;
                if(side){
                    v[corner].x=*(float *)(game+0xaf0b4)*menu_x(x+menu_motion_dx*320);
                    v[corner].y=*(float *)(game+0xc8630)*menu_y(y);
                }
            }
        }
    }
    menu_motion_dx=saved_motion;
}
static void __cdecl flat_quad(float z,const BYTE *p) {
    if(!menu_active()) {old_flat_quad(z,p);return;}
    float x0=*(const SHORT *)(p+8),x1=*(const SHORT *)(p+12);
    float y0=*(const SHORT *)(p+10),y1=*(const SHORT *)(p+18);
    float dx=x1-x0,dy=y1-y0;
    MenuArtBounds art;BOOL fitted=menu_frame_art_bounds(&art);
    float min_x=x0,max_x=x0,min_y=y0,max_y=y0;
    for(UINT i=1;i<4;++i){
        float x=*(const SHORT *)(p+8+i*4),y=*(const SHORT *)(p+10+i*4);
        if(x<min_x)min_x=x;if(x>max_x)max_x=x;if(y<min_y)min_y=y;if(y>max_y)max_y=y;
    }
    fitted=fitted && max_x>min_x && max_y>min_y && min_x>=20 && max_x<=308 && min_y>=82 && max_y<=216;
    BOOL white=p[4]==255 && p[5]==255 && p[6]==255;
    if(fitted && white){
        /* The authored caps/columns already supply these outer edges and
         * horizontal bands. Native duplicate lines can overpaint their AA. */
        if(max_x-min_x<=2 && (min_x==21 || min_x==306))return;
        if(max_y-min_y<=1 && (min_y==82 || min_y==179 || min_y==196 || min_y==214))return;
    }
    menu_thin_x=dx>=-2 && dx<=2;menu_thin_y=dy>=-2 && dy<=2;
    menu_center_x=(x0+x1)/2;menu_center_y=(y0+y1)/2;
    BYTE *buffer=*(BYTE **)(game+0x87038);UINT before=buffer?*(UINT *)(buffer+0x39214):0;
    ++menu_frame_depth;old_flat_quad(z,p);--menu_frame_depth;
    if(fitted && buffer && *(BYTE **)(game+0x87038)==buffer && *(UINT *)(buffer+0x39214)==before+4){
        float left=min_x,right=max_x,top=min_y,bottom=max_y;
        if(left<art.inner_left)left=art.inner_left;if(right>art.inner_right)right=art.inner_right;
        if(min_y>=196){
            /* MENU.DAT puts the footer's first divider at112, but the main
             * column above is111. Move its fill and outline as one unit. */
            if(white && max_x-min_x<=2 && min_x==112){left-=1;right-=1;}
            if(!white && min_x==113 && max_x==210){left=112;right=209;}
            if(top<art.footer_bottom)top=art.footer_bottom;
            if(bottom>art.footer_end)bottom=art.footer_end;
        }else if(!white && min_y>=179 && max_y<=197){
            if(top<art.start_top)top=art.start_top;
            if(bottom>art.footer_top)bottom=art.footer_top;
        }else if(max_y<=180){
            if(top<83)top=83;if(bottom>179)bottom=179;
        }
        if(right>left && bottom>top){
            TLVertex *v=(TLVertex *)(buffer+0xa410)+before;
            for(UINT i=0;i<4;++i){
                float x=*(const SHORT *)(p+8+i*4),y=*(const SHORT *)(p+10+i*4);
                x=left+(right-left)*(x-min_x)/(max_x-min_x);
                y=top+(bottom-top)*(y-min_y)/(max_y-min_y);
                v[i].x=*(float *)(game+0xaf0b4)*(menu_thin_x?menu_x((left+right)/2)+(x-(left+right)/2)*ui_x/320:menu_x(x));
                v[i].y=*(float *)(game+0xc8630)*(menu_thin_y?menu_y((top+bottom)/2)+(y-(top+bottom)/2)*ui_y/240:menu_y(y));
            }
        }
    }
    menu_thin_x=menu_thin_y=FALSE;
}
static void __cdecl pixel_quad(float z,const BYTE *p,int blend) {
    UINT count=menu_active()?*(WORD *)(game+0x8ca28):0;
    if(!count || p<game+0x512010 || p>=game+0x512010+count*40) {old_pixel_quad(z,p,blend);return;}
    float width=*(float *)(game+0xaf0b4),height=*(float *)(game+0xc8630);
    for(UINT y=0;(float)y*ui_y<1;++y)for(UINT x=0;(float)x*ui_x<1;++x) {
        BYTE *buffer=*(BYTE **)(game+0x87038);UINT before=*(UINT *)(buffer+0x39214);
        old_pixel_quad(z,p,blend);
        UINT after=*(UINT *)(buffer+0x39214);
        if(after-before!=4)continue;
        TLVertex *vertices=(TLVertex *)(buffer+0xa410)+before;
        for(UINT i=0;i<4;++i) {
            vertices[i].x=(vertices[i].x+x*width)*ui_x;
            vertices[i].y=(vertices[i].y+y*height)*ui_y;
        }
    }
}
static void menu_anchor(int x,int y) {
    anchor_x=ui_x==1?0.5f:(menu_x((float)x)-ui_x*x/320)/(1-ui_x);
    anchor_y=ui_y==1?0.5f:(menu_y((float)y)-ui_y*y/240)/(1-ui_y);
}
static void text_anchor(int x,int y,BOOL centered) {
    if(anchor_depth)return;
    if(menu_active())menu_anchor(centered?160:x,y);
    else {
        anchor_x=centered?0.5f:(x<128?0:(x>208?1:0.5f));
        anchor_y=y<90?0:(y>150?1:0.5f);
    }
}
static void text4(Text4Fn original,const char *text,int x,int y,int color) {
    float ax=anchor_x,ay=anchor_y;int depth=ui_depth,ad=anchor_depth;
    if(menu_active() || hud_depth) {text_anchor(x,y,FALSE);++ui_depth;++anchor_depth;}
    original(text,x,y,color);
    anchor_x=ax;anchor_y=ay;ui_depth=depth;anchor_depth=ad;
}
static BOOL title_prompt_pending;
static void text5(Text5Fn original,const char *text,int x,int y,int size,int color,BOOL centered) {
    BOOL title=title_input_active() && text==(const char *)(game+0x8c868);
    text=title_prompt(text);
    if(title){centered=TRUE;title_prompt_pending=TRUE;}
    if(audio_config_depth && x==30) {
        if(y==108)y=119;else if(y==147)y=151;
    }
    float ax=anchor_x,ay=anchor_y;int depth=ui_depth,ad=anchor_depth;
    float previous_message_motion=hud_message_motion_x;
    BOOL menu=menu_active();
    /* Native AUTOPILOT/DISENGAGING are left-aligned at 120/110. Other race
     * announcements call CentreText outside DrawRaceHud, so hud_depth alone
     * cannot identify them. Measure the whole glyph run in both cases. */
    BOOL notification=installed && !menu && (title || centered ||
        (hud_depth && ((x==120 && y==108) || (x==110 && y==120))));
    if(notification && (size==8 || size==16)) {
        float origin;
        const BYTE *font=game+(size==16?0x6e36c0:0x6e39f0);
        if(hud_text_origin(game+0x87640,font,text,&origin)) {
            original=old_add_text;x=(int)origin;
            hud_message_motion_x=origin-x;centered=TRUE;
        }
    }
    if(menu || hud_depth || notification) {text_anchor(x,y,centered);++ui_depth;++anchor_depth;}
    if(notification && !hud_depth)anchor_y=.5f; /* retain native overlay height */
    if(menu_active() && *(DWORD *)(game+0x89cb0) && y==120 &&
       ((text[0]=='Y' && text[1]=='E' && text[2]=='S' && !text[3]) ||
        (text[0]=='N' && text[1]=='O' && !text[2])))anchor_x=anchor_y=.5f;
    static BOOL title_text_logged;
    if(title && !title_text_logged){
        title_text_logged=TRUE;wo_render_event("title_prompt_draw",1);
        wo_render_event("title_prompt_x",(DWORD)x);wo_render_event("title_prompt_y",(DWORD)y);
    }
    original(text,x,y,size,color);
    hud_message_motion_x=previous_message_motion;
    anchor_x=ax;anchor_y=ay;ui_depth=depth;anchor_depth=ad;
}
static void __cdecl print_text(const char *t,int x,int y,int c) {text4(old_print_text,t,x,y,c);}
static void __cdecl print_center(const char *t,int x,int y,int c) {
    if(installed && hud_depth && !menu_active())x=160; /* REAR LOCK */
    text4(old_print_center,t,x,y,c);
}
static void __cdecl add_text(const char *t,int x,int y,int s,int c) {text5(old_add_text,t,x,y,s,c,FALSE);}
static void __cdecl center_text(const char *t,int x,int y,int s,int c) {text5(old_center_text,t,x,y,s,c,TRUE);}
static void __cdecl center_text2(const char *t,int x,int y,int s,int c) {text5(old_center_text2,t,x,y,s,c,FALSE);}
static void number5(NumberFn original,int value,int x,int y,int size,int color) {
    if(installed && hud_depth && !menu_active() && x==160 && y==128 && value>=0) {
        /* Keep the autopilot countdown centered beneath its announcement. */
        char text[12],reverse[12];UINT digits=0,length=0,n=(UINT)value;
        do {reverse[digits++]=(char)('0'+n%10);n/=10;}while(n);
        while(digits)text[length++]=reverse[--digits];text[length]=0;
        text5(old_center_text,text,160,y,size,color,TRUE);return;
    }
    float ax=anchor_x,ay=anchor_y;int depth=ui_depth,ad=anchor_depth;
    if(menu_active() || hud_depth) {text_anchor(x,y,FALSE);++ui_depth;++anchor_depth;}
    original(value,x,y,size,color);
    anchor_x=ax;anchor_y=ay;ui_depth=depth;anchor_depth=ad;
}
static void __cdecl number_text(int n,int x,int y,int s,int c) {number5(old_number_text,n,x,y,s,c);}
static void __cdecl justify_number(int n,int x,int y,int s,int c) {number5(old_justify_number,n,x,y,s,c);}
static void __cdecl time_text(int n,int x,int y,int s,int c,UINT flags) {
    float ax=anchor_x,ay=anchor_y;int depth=ui_depth,ad=anchor_depth;
    if(menu_active() || hud_depth) {text_anchor(x,y,FALSE);++ui_depth;++anchor_depth;}
    old_time_text(n,x,y,s,c,flags);
    anchor_x=ax;anchor_y=ay;ui_depth=depth;anchor_depth=ad;
}
static void __cdecl speedo(void *ships,void *config,int choice) {
    float ax=anchor_x,ay=anchor_y;int depth=ui_depth,ad=anchor_depth;
    if(installed) {anchor_x=1;anchor_y=1;++ui_depth;++anchor_depth;}
    old_speedo(ships,config,choice);
    anchor_x=ax;anchor_y=ay;ui_depth=depth;anchor_depth=ad;
}
static void __cdecl menu_model(int x,int y,int z,SHORT dir,SHORT scale,BYTE *object) {
    dir=interpolate_menu_angle(object,x,y,z,dir,scale);
    ++menu_model_depth;old_menu_model(x,y,z,dir,scale,object);--menu_model_depth;
}
static void menu_position_model(float *matrix) {
    if(!menu_active() || !menu_model_depth)return;
    float *view=(float *)(game+0x8b1d0);
    float z=matrix[14]*view[10]+view[14];
    if(z<=0 || view[0]==0 || view[5]==0)return;
    float x=0.5f+matrix[12]*view[0]/z,y=0.5f+matrix[13]*view[5]/z;
    matrix[12]=(menu_x(x*320)-0.5f)*z/(view[0]*projection_x);
    matrix[13]=(menu_y(y*240)-0.5f)*z/view[5];
    /* Portrait menus use the same physical scale as their text and frame caps. */
    if(ui_y<1)for(UINT row=0;row<3;++row)for(UINT col=0;col<3;++col)matrix[row*4+col]*=ui_y;
}
static void initialize_menu(void) {
#define MENU_VERIFY(name) if(!equal(game+RVA_##name,entry_##name,sizeof(entry_##name)))return
    MENU_VERIFY(menu_model);MENU_VERIFY(print_text);MENU_VERIFY(print_center);MENU_VERIFY(add_text);
    MENU_VERIFY(center_text);MENU_VERIFY(center_text2);MENU_VERIFY(flat_quad);MENU_VERIFY(pixel_quad);
    MENU_VERIFY(number_text);MENU_VERIFY(justify_number);MENU_VERIFY(time_text);MENU_VERIFY(speedo);
#define MENU_INSTALL(name,type) old_##name=(type)hook(RVA_##name,entry_##name,sizeof(entry_##name),name)
    MENU_INSTALL(menu_model,MenuModelFn);MENU_INSTALL(print_text,Text4Fn);MENU_INSTALL(print_center,Text4Fn);
    MENU_INSTALL(add_text,Text5Fn);MENU_INSTALL(center_text,Text5Fn);MENU_INSTALL(center_text2,Text5Fn);
    MENU_INSTALL(flat_quad,FlatFn);MENU_INSTALL(pixel_quad,QuadFn);
    MENU_INSTALL(number_text,NumberFn);MENU_INSTALL(justify_number,NumberFn);
    MENU_INSTALL(time_text,TimeFn);MENU_INSTALL(speedo,SpeedoFn);
    menu_patching=old_menu_model && old_print_text && old_print_center && old_add_text && old_center_text && old_center_text2 && old_flat_quad && old_pixel_quad;
    menu_patching=menu_patching && old_number_text && old_justify_number && old_time_text && old_speedo;
    wo_render_event("render_responsive_menu_test",menu_patching);
}
