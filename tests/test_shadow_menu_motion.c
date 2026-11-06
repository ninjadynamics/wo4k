/* Production hooks + original x86 shadow and complete team-stat routines. */
static void shadow_menu_relocate(BYTE *image){
    #include "native_shadow_menu_refs.h"
    DWORD delta=(DWORD)(UINT_PTR)image-0x400000;
    for(UINT i=0;i<sizeof(native_shadow_menu_refs)/sizeof(native_shadow_menu_refs[0]);++i){
        UINT rva=native_shadow_menu_refs[i].rva;
        DWORD *operand=rva==RVA_draw_shadow+2?(DWORD *)((BYTE *)old_draw_shadow+2):(DWORD *)(image+rva);
        if(*operand==native_shadow_menu_refs[i].expected)fixture_write_code(operand,*operand+delta);
        else if(*operand!=native_shadow_menu_refs[i].expected+delta)ExitProcess(221);
    }
}
static UINT stat_label_count;
static void __cdecl stat_label(const char *text,int x,int y,int color){
    (void)text;(void)x;(void)y;(void)color;++stat_label_count;
}
static void test_team_stat_gauges(BYTE *image,BYTE *buffer){
    shadow_menu_relocate(image);
    const float widths[]={640,1920,3840,3440,1080},heights[]={480,1080,2160,1440,1920};
    const float sx[]={1,.75f,.75f,24.0f/43,1},sy[]={1,1,1,1,.421875f};
    *(int *)(image+0x8c9d4)=1;title_active=FALSE;
    Text4Fn saved_print=old_print_text;old_print_text=stat_label;
    static TLVertex original[15][40];
    for(UINT aspect=0;aspect<5;++aspect){
        *(float *)(image+0xaf0b4)=widths[aspect];*(float *)(image+0xc8630)=heights[aspect];ui_x=sx[aspect];ui_y=sy[aspect];
        for(UINT choice=0;choice<15;++choice){
            image[0x510482]=(BYTE)choice;frame_clear(image,buffer);stat_label_count=0;
            /* Exact caller arguments from TeamMenu at 43895a: draw all five
             * labels and ten real FT4 packets, including native stat values,
             * 2x coordinates and the red/green UV strips on texture page32. */
            ((void (__cdecl *)(void *,int,int))(image+0x38230))(0,155,142);
            if(*(UINT *)(buffer+0x39214)!=40 || stat_label_count!=5 || image[0x510482]!=choice)ExitProcess(222);
            const TLVertex *v=(const TLVertex *)(buffer+0xa410);
            if(!aspect)copy((BYTE *)original[choice],(const BYTE *)v,40*sizeof(TLVertex));
            for(UINT row=0;row<5;++row){
                const TLVertex *red=v+row*8,*green=red+4;
                if(!frame_near(red[1].x,green[0].x) || !frame_near(red[3].x,green[2].x) ||
                   !frame_near(red[1].y,green[0].y) || !frame_near(red[3].y,green[2].y))ExitProcess(223);
                if(!frame_near(red[0].x,v[0].x) || !frame_near(green[1].x,v[5].x))ExitProcess(224);
                if(!frame_near(red[0].x/widths[aspect],menu_x(232)) || !frame_near(green[1].x/widths[aspect],menu_x(302)))ExitProcess(225);
            }
            for(UINT i=0;i<40;++i)if(v[i].color!=original[choice][i].color ||
                v[i].u!=original[choice][i].u || v[i].v!=original[choice][i].v)ExitProcess(226);
            if(menu_frame_depth || ui_depth || anchor_depth)ExitProcess(227);
        }
    }
    old_print_text=saved_print;
}
static LitVertex native_shadow_capture[SHADOW_MAX_VERTICES*SHADOW_LAYERS];
static UINT native_shadow_count;
static void __cdecl shadow_native_matrix(BYTE *skeleton){(void)skeleton;}
static void __cdecl shadow_capture(BYTE *skeleton,void *vertices,int count,void *triangles,void *quads,int quad_count,int mode){
    (void)skeleton;(void)triangles;(void)quads;(void)quad_count;(void)mode;
    if(count<0 || count>SHADOW_MAX_VERTICES*SHADOW_LAYERS)ExitProcess(229);
    native_shadow_count=(UINT)count;copy((BYTE *)native_shadow_capture,vertices,(UINT)count*sizeof(LitVertex));
}
static void test_projected_shadow_motion(BYTE *image){
    static BYTE ships[15*240],before[15*240],camera[80],skeleton[80],section[160],track[32],faces[20],points[16],scratch[8192];
    static BYTE *camera_pointer;camera_pointer=camera;
    for(UINT i=0;i<sizeof(ships);++i)ships[i]=0;
    *(BYTE **)(image+0x665110)=(BYTE *)&camera_pointer;
    *(BYTE **)(camera+0x44)=skeleton;*(BYTE **)(image+0x517bf0)=camera;
    *(BYTE **)(image+0x518030)=ships;*(BYTE **)(image+0x65f794)=track;
    *(BYTE **)(track+12)=points;*(BYTE **)(track+16)=faces;
    faces[15]=1;*(SHORT *)(faces+10)=4096;
    *(WORD *)(image+0xc8620)=22;*(DWORD *)(image+0x65c364)=0;*(DWORD *)(image+0x8ace0)=0;
    *(DWORD *)(image+0x8c98c)=1;*(DWORD *)(image+0x8c9d4)=0;
    BYTE saved_matrix[5];copy(saved_matrix,image+0x4d7f0,5);jump(image+0x4d7f0,shadow_native_matrix);
    SkeletonFn saved_submit=old_render_skeleton;old_render_skeleton=shadow_capture;
    BOOL saved_refresh=refresh_active,saved_race=refresh_race,saved_sim=simulation_active,saved_soft=shadows_active;
    refresh_active=refresh_race=TRUE;simulation_active=FALSE;
    for(UINT ship=0;ship<15;++ship){
        BYTE *s=ships+ship*240;*(BYTE **)(s+4)=*(BYTE **)(s+8)=section;
        if(ship==0 || ship==3)*(DWORD *)(s+12)=0x200;
        *(LONG *)(s+0x18)=4096;*(LONG *)(s+0x20)=4096;*(LONG *)(s+0x44)=-256;
    }
    const UINT rates[]={30,60,165,240};
    for(UINT slope=0;slope<2;++slope)for(UINT soft=0;soft<2;++soft)for(UINT rate=0;rate<4;++rate){
        *(SHORT *)(faces+12)=slope?1024:0;
        shadows_active=soft!=0;refresh_pose_valid=FALSE;refresh_epoch=0;
        *(LONG *)(ships+0x40)=*(LONG *)(ships+3*240+0x40)=0;
        *(LONG *)(ships+0x48)=8192;*(LONG *)(ships+3*240+0x48)=16384;
        sample_refresh_pose();
        float last=-1000;
        for(UINT frame=0;frame<rates[rate];++frame){
            double ticks=(double)frame*30/rates[rate];UINT step=(UINT)ticks;
            refresh_epoch=step+1;refresh_alpha=(float)(ticks-step);
            *(LONG *)(ships+0x48)=8192+(LONG)(step+1)*512;
            *(LONG *)(ships+3*240+0x48)=16384-(LONG)(step+1)*256;
            sample_refresh_pose();copy(before,ships,sizeof(ships));
            *(BYTE **)(image+0x65d020)=scratch;native_shadow_count=0;
            /* Enter the actual patched entry; its native projection and
             * compacted visibility order (ships 0 and 3) both execute. */
            ((ShadowFn)(image+RVA_draw_shadow))(0,ships);
            if(native_shadow_count!=(soft?96u:8u) || shadow_depth || !equal(before,ships,sizeof(ships)))ExitProcess(230);
            for(UINT ship=0;ship<2;++ship){
                float center=0;for(UINT c=0;c<4;++c)center+=native_shadow_capture[ship*4+c].z;
                center*=.25f;
                double z=(ship?16384:8192)+ticks*(ship?-256:512);
                /* Project onto y+z/4=0 for the sloped face. Integer plane
                 * intersection can round by up to two native world units. */
                float expected=(float)(slope?(16*z+4*256)/17:z)/4096;
                float error=center-expected;if(error<-.0006f || error>.0006f)ExitProcess(231);
                if(!ship){if(center<last)ExitProcess(232);last=center;}
            }
        }
    }
    old_render_skeleton=saved_submit;copy(image+0x4d7f0,saved_matrix,5);
    refresh_active=saved_refresh;refresh_race=saved_race;simulation_active=saved_sim;shadows_active=saved_soft;
    refresh_pose_valid=FALSE;
}
