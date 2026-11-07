/* Replace only the native header logo, retaining the atlas's other widgets. */
static BYTE *ui_hd_logo_buffer;
static UINT ui_hd_logo_count;
static int ui_hd_logo_left,ui_hd_logo_top;
static BOOL ui_hd_logo_part(const BYTE *p,BOOL second) {
    int width=second?210:254,top=second?64:0;
    return *(const WORD *)(p+22)==35 &&
        p[12]==0 && p[13]==top && p[20]==width && p[21]==top &&
        p[28]==0 && p[29]==top+64 && p[36]==width && p[37]==top+64 &&
        *(const SHORT *)(p+16)==*(const SHORT *)(p+8)+width &&
        *(const SHORT *)(p+24)==*(const SHORT *)(p+8) &&
        *(const SHORT *)(p+32)==*(const SHORT *)(p+16) &&
        *(const SHORT *)(p+18)==*(const SHORT *)(p+10) &&
        *(const SHORT *)(p+26)==*(const SHORT *)(p+10)+64 &&
        *(const SHORT *)(p+34)==*(const SHORT *)(p+26);
}
static BOOL ui_hd_draw_logo(float z,const BYTE *p,int blend) {
    if(!menu_active() || !ui_hd_logo.handle || !ui_hd_pages[25].handle){ui_hd_logo_buffer=0;return FALSE;}
    BYTE *buffer=*(BYTE **)(game+0x87038);
    if(!buffer){ui_hd_logo_buffer=0;return FALSE;}
    UINT count=*(UINT *)(buffer+0x39214);
    BOOL second=ui_hd_logo_part(p,TRUE) && buffer==ui_hd_logo_buffer && count==ui_hd_logo_count &&
        *(const SHORT *)(p+8)==ui_hd_logo_left+254 && *(const SHORT *)(p+10)==ui_hd_logo_top;
    ui_hd_logo_buffer=0;
    if(second)return TRUE;
    if(!ui_hd_logo_part(p,FALSE))return FALSE;
    BYTE joined[40];copy(joined,p,sizeof(joined));
    int left=*(const SHORT *)(p+8);
    *(SHORT *)(joined+16)=*(SHORT *)(joined+32)=(SHORT)(left+464);
    joined[20]=joined[36]=232;joined[29]=joined[37]=128;
    draw_quad(old_scaled_quad,z,joined,blend,640,TRUE);
    if(*(BYTE **)(game+0x87038)==buffer && *(UINT *)(buffer+0x39214)==count+4){
        TLVertex *v=(TLVertex *)(buffer+0xa410)+count;
        /* The native logo sits left of centre (anchor x141), leaving room
         * for the swirl beside it at 4:3. The HD title is centred in its
         * image; centre it on the frame, whose caps keep the swirl apart. */
        float low=v[0].x,high=v[0].x;
        for(UINT i=1;i<4;++i){if(v[i].x<low)low=v[i].x;if(v[i].x>high)high=v[i].x;}
        float shift=*(float *)(game+0xaf0b4)*menu_frame_x(164)-(low+high)*0.5f;
        for(UINT i=0;i<4;++i){
            v[i].specular=UI_HD_LOGO_TAG;
            v[i].x+=shift;
            v[i].u=(i&1)?232.0f/256:0;
            v[i].v=(i&2)?128.0f/256:0;
        }
        ui_hd_logo_buffer=buffer;ui_hd_logo_count=count+4;
        ui_hd_logo_left=left;ui_hd_logo_top=*(const SHORT *)(p+10);
    }
    return TRUE;
}
