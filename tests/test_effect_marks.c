/* Trail markers live in vertices the game hands out again every frame, to
 * the track before anything else. They must be gone once the frame is drawn:
 * a marker that stays makes the slot's next owner draw as a trail. */
static UINT effect_mark_finishes;
static void __cdecl fixture_effect_finish(void){++effect_mark_finishes;}
static void test_effect_marks(BYTE *image){
    static LitVertex store[24];
    BOOL saved_exhaust=exhaust_active;int saved_depth=trail_depth;FinishFn saved_finish=old_finish_render;
    BYTE *saved_buffer=*(BYTE **)(image+0x7c9690);
    exhaust_active=TRUE;trail_depth=1;old_finish_render=fixture_effect_finish;*(BYTE **)(image+0x7c9690)=0;
    for(UINT i=0;i<24;++i)store[i]=(LitVertex){.color=0x80112233};
    prepare_trail_vertices(store,12);prepare_trail_vertices(store+12,6);
    for(UINT i=0;i<24;++i)if(store[i].reserved!=(i<18?TRAIL_VERTEX_TAG:0))ExitProcess(61);
    /* The frame ends with the screen pass; the next one gives these slots
     * to the track, whose writer leaves the marker field alone. */
    finish_render();
    if(effect_mark_finishes!=1 || trail_mark_count)ExitProcess(62);
    for(UINT i=0;i<24;++i)if(store[i].reserved || (store[i].color&0xffffff)!=0x112233)ExitProcess(62);
    /* A slot something else has marked since is not a trail's to unmark. */
    prepare_trail_vertices(store,6);store[2].reserved=SKY_VERTEX_TAG;
    finish_render();
    if(store[2].reserved!=SKY_VERTEX_TAG || store[0].reserved || store[5].reserved)ExitProcess(63);
    store[2].reserved=0;
    /* More trails than can be remembered stay as the game draws them. */
    for(UINT i=0;i<TRAIL_MARK_CAPACITY;++i)prepare_trail_vertices(store,6);
    prepare_trail_vertices(store+6,6);
    if(store[0].reserved!=TRAIL_VERTEX_TAG || store[6].reserved)ExitProcess(64);
    finish_render();
    for(UINT i=0;i<24;++i)if(store[i].reserved)ExitProcess(64);
    exhaust_active=saved_exhaust;trail_depth=saved_depth;old_finish_render=saved_finish;
    *(BYTE **)(image+0x7c9690)=saved_buffer;
}
static BOOL trail_near(LONG actual,float expected){float d=(float)actual-expected;return d>=-1.5f && d<=1.5f;}
static void test_exhaust_trail_interpolation(BYTE *image){
    static BYTE ships[15*240],camera[80],trail[0x4c0],original[0x4c0],out[0x4c0];
    for(UINT i=0;i<sizeof(ships);++i)ships[i]=0;
    for(UINT i=0;i<sizeof(camera);++i)camera[i]=0;
    BYTE *saved_ships=*(BYTE **)(image+0x518030),*saved_camera=*(BYTE **)(image+0x517bf0);
    SHORT saved_own=*(SHORT *)(image+0x664820);
    BOOL saved_refresh=refresh_active,saved_race=refresh_race,saved_sim=simulation_active,saved_pose=refresh_pose_valid;
    DWORD saved_epoch=refresh_epoch;float saved_alpha=refresh_alpha;
    *(BYTE **)(image+0x518030)=ships;*(BYTE **)(image+0x517bf0)=camera;*(SHORT *)(image+0x664820)=0;
    refresh_active=refresh_race=TRUE;simulation_active=FALSE;refresh_pose_valid=FALSE;
    /* Tick 0: player ship pointing along +Z, wing along +X. */
    LONG *p0=(LONG *)(ships+0x40),*n0=(LONG *)(ships+0x10),*w0=(LONG *)(ships+0x20);
    p0[0]=0;p0[1]=-256;p0[2]=0;n0[0]=0;n0[1]=0;n0[2]=4096;w0[0]=4096;w0[1]=0;w0[2]=0;
    /* AI ship 3 at tick 0. */
    LONG *p3=(LONG *)(ships+3*240+0x40),*n3=(LONG *)(ships+3*240+0x10),*w3=(LONG *)(ships+3*240+0x20);
    p3[0]=1000;p3[1]=-200;p3[2]=2000;n3[0]=0;n3[1]=0;n3[2]=4096;w3[0]=4096;w3[1]=0;w3[2]=0;
    refresh_epoch=1;sample_refresh_pose();
    /* Tick 1: both ships advance and yaw by ~2 degrees (~143/4096). */
    p0[0]=120;p0[1]=-256;p0[2]=500;n0[0]=143;n0[1]=0;n0[2]=4094;w0[0]=4094;w0[1]=0;w0[2]=-143;
    p3[0]=1100;p3[1]=-200;p3[2]=2400;n3[0]=-200;n3[1]=0;n3[2]=4091;w3[0]=4091;w3[1]=0;w3[2]=200;
    refresh_epoch=2;sample_refresh_pose();
    /* Build native player beam at tick 1 matching UpdateShipTrails + update_beam. */
    for(UINT i=0;i<sizeof(trail);++i)trail[i]=0;
    *(int *)(trail+12)=0;
    LONG left0[3],right0[3],step[3];
    for(UINT a=0;a<3;++a){
        LONG center=p0[a]-((420*n0[a])>>12)-(a==1?15:0);
        LONG half=(40*w0[a])>>12;
        left0[a]=center-half;right0[a]=center+half;step[a]=n0[a]>>5;
    }
    for(UINT s=0;s<30;++s){
        BYTE *item=trail+16+s*40;item[0]=1;
        *(WORD *)(item+2)=(WORD)(200-s*5);*(WORD *)(item+4)=(WORD)(180-s*4);*(WORD *)(item+6)=(WORD)(255-s*6);
        for(UINT a=0;a<3;++a){
            ((LONG *)(item+8))[a]=left0[a]-step[a]*(LONG)s;
            ((LONG *)(item+24))[a]=right0[a]-step[a]*(LONG)s;
        }
    }
    copy(original,trail,sizeof(trail));
    LONG prev_tail_x=100000;
    for(UINT k=0;k<=10;++k){
        float alpha=k/10.0f;refresh_alpha=alpha;
        if(!copy_interpolated_trail(out,trail,0) || !equal(trail,original,sizeof(trail)))ExitProcess(235);
        float px=120.0f*alpha,py=-256.0f,pz=500.0f*alpha;
        float nx=143.0f*alpha,nz=4096.0f+(4094.0f-4096.0f)*alpha;
        float wx=4096.0f+(4094.0f-4096.0f)*alpha,wz=-143.0f*alpha;
        float cx=px-nx*(420.0f/4096.0f),cy=py-15.0f,cz=pz-nz*(420.0f/4096.0f);
        float hx=wx*(40.0f/4096.0f),hz=wz*(40.0f/4096.0f);
        for(UINT s=0;s<30;++s){
            const BYTE *item=out+16+s*40;
            if(item[0]!=1 || *(const WORD *)(item+2)!=(WORD)(200-s*5) ||
               *(const WORD *)(item+4)!=(WORD)(180-s*4) || *(const WORD *)(item+6)!=(WORD)(255-s*6))ExitProcess(236);
            const LONG *l=(const LONG *)(item+8),*r=(const LONG *)(item+24);
            float sx=nx*(1.0f/32.0f)*(float)s,sz=nz*(1.0f/32.0f)*(float)s;
            if(!trail_near(l[0],cx-hx-sx) || !trail_near(l[1],cy) || !trail_near(l[2],cz-hz-sz) ||
               !trail_near(r[0],cx+hx-sx) || !trail_near(r[1],cy) || !trail_near(r[2],cz+hz-sz))ExitProcess(237);
        }
        /* Tail segment must rotate smoothly from tick 0 (x=-40) to tick 1, never snapping. */
        LONG tail_x=((const LONG *)(out+16+29*40+8))[0];
        if(!k && !trail_near(tail_x,-40.0f))ExitProcess(238);
        if(k && tail_x>prev_tail_x)ExitProcess(239);
        prev_tail_x=tail_x;
    }
    /* AI ring-buffer trail: interpolate only the active head segment, leave history intact. */
    for(UINT i=0;i<sizeof(trail);++i)trail[i]=0;
    *(int *)(trail+12)=7;
    BYTE *h_curr=trail+16+7*40,*h_prev=trail+16+8*40;
    h_curr[0]=1;h_prev[0]=1;
    ((LONG *)(h_prev+8))[0]=960;((LONG *)(h_prev+8))[1]=-215;((LONG *)(h_prev+8))[2]=1580;
    ((LONG *)(h_prev+24))[0]=1040;((LONG *)(h_prev+24))[1]=-215;((LONG *)(h_prev+24))[2]=1580;
    refresh_alpha=0.5f;
    if(!copy_interpolated_trail(out,trail,3))ExitProcess(240);
    const LONG *hl=(const LONG *)(out+16+7*40+8),*hr=(const LONG *)(out+16+7*40+24);
    float c3x=1050.0f-(-100.0f)*(420.0f/4096.0f),c3z=2200.0f-4093.5f*(420.0f/4096.0f);
    float h3x=4093.5f*(40.0f/4096.0f),h3z=100.0f*(40.0f/4096.0f);
    if(!trail_near(hl[0],c3x-h3x) || !trail_near(hl[1],-215.0f) || !trail_near(hl[2],c3z-h3z) ||
       !trail_near(hr[0],c3x+h3x) || !trail_near(hr[1],-215.0f) || !trail_near(hr[2],c3z+h3z))ExitProcess(241);
    if(!equal(out+16+8*40,h_prev,40))ExitProcess(242);
    /* Discontinuous ship jump (>8192 units) snaps previous pose to current. */
    p0[0]=20000;p0[2]=20000;n0[0]=0;n0[2]=4096;w0[0]=4096;w0[2]=0;
    refresh_epoch=3;sample_refresh_pose();refresh_alpha=0.5f;
    if(!copy_interpolated_trail(out,original,0))ExitProcess(243);
    if(!trail_near(((const LONG *)(out+16+8))[0],20000.0f-40.0f))ExitProcess(244);
    *(BYTE **)(image+0x518030)=saved_ships;*(BYTE **)(image+0x517bf0)=saved_camera;*(SHORT *)(image+0x664820)=saved_own;
    refresh_active=saved_refresh;refresh_race=saved_race;simulation_active=saved_sim;refresh_pose_valid=saved_pose;
    refresh_epoch=saved_epoch;refresh_alpha=saved_alpha;
}
