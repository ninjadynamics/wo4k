/* Separate high-refresh experiment. Original simulation functions still run
 * only at30Hz; render matrices interpolate between their completed states.
 * No game pose is overwritten to manufacture an intermediate frame. */
typedef int (__cdecl *RefreshClockFn)(int);
typedef void (__cdecl *RefreshVoidFn)(void);
typedef const BYTE *(__cdecl *RefreshSkeletonFn)(BYTE *);
static RefreshClockFn old_refresh_clock;
static int (__cdecl *old_refresh_present)(void);
static LARGE_INTEGER menu_profile_last,menu_profile_window;
static DWORD menu_profile_frames,menu_profile_max_us,menu_profile_late;
static DWORD stall_present_max_us,stall_frame_max_us,stall_frame_count,stall_present_count;
static LARGE_INTEGER profile_frame_end;
static BOOL profile_frame_valid;
static RefreshVoidFn old_refresh_reset,old_refresh_input;
static RefreshSkeletonFn old_refresh_skeleton;
static LARGE_INTEGER refresh_frequency,refresh_last_time,refresh_measure_time;
static double refresh_phase;
static float refresh_alpha;
static DWORD refresh_extra_serial;
static DWORD refresh_epoch,refresh_pose_epoch;
static void (__cdecl *old_ship_effects)(void *);
static LARGE_INTEGER ui_last_time;
static double ui_phase,ui_angle_phase;
static float ui_draw_phase;
static float ui_panel_phase(void) {return ui_draw_phase;}
static DWORD ui_steps,ui_ticks,ui_angle_step,ui_units,model_units;
static DWORD ui_present_serial;
static void advance_ui_clock(double seconds) {
    ++ui_present_serial;
    if(seconds<0)seconds=0;
    ui_draw_phase=(float)ui_phase;
    ui_phase+=seconds*30;
    ui_steps=(DWORD)(ui_phase+1e-9);ui_phase-=ui_steps;
    ui_ticks+=ui_steps;
    ui_units=ui_steps*2;model_units=ui_steps*2;
    ui_angle_phase+=seconds*(128.0*30.0);
    ui_angle_step=(DWORD)(ui_angle_phase+1e-9);ui_angle_phase-=ui_angle_step;
}
static RefreshVoidFn old_ui_particles;
static RefreshVoidFn old_loading_ui;
static void __cdecl loading_ui(void) {
    if(!refresh_active) {old_loading_ui();return;}
    BYTE bars[47];copy(bars,game+0x65aee0,47);
    *(SHORT *)(game+0x65af70)+=(SHORT)(ui_steps-1);
    *(DWORD *)(game+0x65adbc)=(*(DWORD *)(game+0x65adbc)+10-ui_steps*10)&255;
    old_loading_ui();
    if(!ui_steps)copy(game+0x65aee0,bars,47);
    else for(UINT n=1;n<ui_steps;++n)for(UINT i=0;i<47;++i) {
        UINT step=((int (__cdecl *)(void))(game+0xc820))()&3;
        signed char *height=(signed char *)(game+0x65aee0+i);
        if(step==1 && *height>8)*height-=2;
        if(step==2 && *height<40)*height+=2;
    }
}
static void __cdecl ui_particles(void) {
    if(!refresh_active) {old_ui_particles();return;}
    int *life=*(int **)(game+0x65af74),*x=*(int **)(game+0x65af78),*y=*(int **)(game+0x65af7c);
    int *vx=*(int **)(game+0x65af84),*vy=*(int **)(game+0x65af88);
    BYTE *polys=*(BYTE **)(game+0x65adc0);
    if(!life || !x || !y || !vx || !vy || !polys)return;
    static int seen_x[128],seen_y[128],seen_life[128],previous_x[128],previous_y[128];
    static BOOL valid[128];
    for(UINT i=0;i<128;++i)if(life[i]>0) {
        if(!valid[i] || x[i]!=seen_x[i] || y[i]!=seen_y[i] || life[i]>seen_life[i]) {
            previous_x[i]=x[i];previous_y[i]=y[i];valid[i]=TRUE;
        }
        for(UINT n=0;n<ui_steps && life[i]>0;++n) {
            previous_x[i]=x[i];previous_y[i]=y[i];
            life[i]-=3;x[i]+=vx[i];y[i]+=vy[i];++vy[i];
        }
        seen_x[i]=x[i];seen_y[i]=y[i];seen_life[i]=life[i];
        if(life[i]<=0) {valid[i]=FALSE;continue;}
        BYTE *p=polys+i*20;p[4]=p[5]=(BYTE)(life[i]*2);p[6]=0;
        SHORT px=(SHORT)(x[i]>>7),py=(SHORT)(y[i]>>7);
        *(SHORT *)(p+8)=px;*(SHORT *)(p+10)=py;
        *(SHORT *)(p+12)=px+1;*(SHORT *)(p+14)=py;
        *(SHORT *)(p+16)=px;*(SHORT *)(p+18)=py+1;
        particle_motion_x=(previous_x[i]+(x[i]-previous_x[i])*(float)ui_phase)/128-px;
        particle_motion_y=(previous_y[i]+(y[i]-previous_y[i])*(float)ui_phase)/128-py;
        ((FlatFn)(game+0x1350))(0,p);particle_motion_x=particle_motion_y=0;
    }
}
static DWORD target_animation_epoch;
static BOOL target_animation_valid;
/* Angular speed7680 engine units/second = 11.78rad/s. Trail-bank history and
 * fading use elapsed60Hz ticks; missile acquisition retains the physics clock. */
static void __cdecl ship_effects(void *camera) {
    if(!refresh_active || !refresh_race || !*(DWORD *)(game+0x8c98c)) {
        old_ship_effects(camera);return;
    }
    BOOL advance=!target_animation_valid || target_animation_epoch!=refresh_epoch;
    DWORD spin=*(DWORD *)(game+0x8c96c);
    BYTE bank=game[0x8c974],green[32],flags[8];
    copy(flags,game+0x5165c0,8);
    SHORT last_target=*(SHORT *)(game+0x8c970);
    SHORT timer=*(SHORT *)(game+0x8bb9c);
    const BYTE *ships=*(const BYTE **)(game+0x518030);
    int own=*(SHORT *)(game+0x664820);
    BOOL rotating=ships && own>=0 && own<15 && ships[own*240+0xb8] && !*(DWORD *)(game+0x89cb0);
    if(rotating)*(DWORD *)(game+0x8c96c)=spin+ui_angle_step-128;
    for(UINT i=0;i<32;++i)green[i]=game[0x5163c5+i*16];
    old_ship_effects(camera);
    if(rotating) {
        BYTE native_bank=game[0x8c974],wanted=(bank+ui_steps)&7;
        if(native_bank!=wanted) {
            copy(game+0x5163c0+wanted*64,game+0x5163c0+native_bank*64,64);
            game[0x5165c0+wanted]=game[0x5165c0+native_bank];
            game[0x5165c0+native_bank]=last_target==*(SHORT *)(game+0x8c970)?flags[native_bank]:0;
        }
        game[0x8c974]=wanted;
    }
    for(UINT i=0;i<32;++i) {
        BYTE *value=game+0x5163c5+i*16;
        if(*value==(BYTE)(green[i]-28)) {
            BYTE g=green[i];
            for(UINT n=0;n<ui_steps && g;++n)g-=28;
            *value=g;
        }
    }
    /* Lock acquisition is gameplay, so retain its accepted30Hz clock. */
    if(!advance)*(SHORT *)(game+0x8bb9c)=timer;
    target_animation_epoch=refresh_epoch;target_animation_valid=TRUE;
    finish_race_ui_effects();
}
static DWORD refresh_frames_measured,refresh_ticks_measured,refresh_measured_fps;
static BOOL refresh_pose_valid,refresh_show_fps;
static float camera_previous[3],camera_current[3],ship_previous[15][3],ship_current[15][3];
typedef struct RenderHistory {
    const BYTE *skeleton;
    DWORD epoch,frame;
    float previous[16],current[16];
} RenderHistory;
static RenderHistory render_history[4096];
static DWORD motion_frame_seen=0xffffffffu,motion_raw_previous,motion_draw_previous;
static DWORD motion_frames,motion_raw_changes,motion_draw_changes,motion_history_gaps,motion_history_resets;
static DWORD motion_hash(const float *matrix,const LONG *position) {
    DWORD hash=2166136261u;
    for(UINT i=0;i<16;++i)hash=(hash^((const DWORD *)matrix)[i])*16777619u;
    if(position)for(UINT i=0;i<3;++i)hash=(hash^(DWORD)position[i])*16777619u;
    return hash;
}

/* Time the real driver call and the work between presentations separately.
 * Keep logging out of the per-frame hot path; emit aggregates once a second. */
static DWORD refresh_elapsed_us(LARGE_INTEGER begin,LARGE_INTEGER end) {
    double us=(double)(end.QuadPart-begin.QuadPart)*1000000.0/(double)refresh_frequency.QuadPart;
    return us<0?0:(us>4294967295.0?0xffffffffu:(DWORD)us);
}
static int __cdecl refresh_present(void) {
    if(!refresh_active)return old_refresh_present();
    hide_game_cursor();
    prepare_intro_movie();
    startup_present();
    LARGE_INTEGER begin,end;QueryPerformanceCounter(&begin);
    BOOL race=refresh_race && *(DWORD *)(game+0x8c98c);
    if(!race)profile_frame_valid=FALSE;
    else if(profile_frame_valid) {
        DWORD us=refresh_elapsed_us(profile_frame_end,begin);
        if(us>stall_frame_max_us)stall_frame_max_us=us;
        if(us>=50000)++stall_frame_count;
    }
    title_overlay_present();
    int result=old_refresh_present();QueryPerformanceCounter(&end);
    double ui_seconds=ui_last_time.QuadPart?(double)(end.QuadPart-ui_last_time.QuadPart)/refresh_frequency.QuadPart:0;
    ui_last_time=end;advance_ui_clock(ui_seconds);
    *(DWORD *)(game+0x664824)=ui_ticks*2;
    *(DWORD *)(game+0x65f684)=(ui_ticks*2)%30<20;
    if(!race) {
        /* Original menu rotations/slides consume60Hz units. Two units per
         * clock tick gives the requested2x speed, independent of presentation. */
        *(DWORD *)(game+0x89c7c)=ui_units;
        if(menu_profile_last.QuadPart) {
            DWORD us=refresh_elapsed_us(menu_profile_last,end);
            if(us>menu_profile_max_us)menu_profile_max_us=us;
            if(us>=50000)++menu_profile_late;
        } else {menu_profile_window=end;menu_profile_frames=menu_profile_max_us=menu_profile_late=0;}
        menu_profile_last=end;++menu_profile_frames;
        double seconds=(double)(end.QuadPart-menu_profile_window.QuadPart)/refresh_frequency.QuadPart;
        if(seconds>=1) {
            wo_render_event("menu_present_fps_milli",(DWORD)(menu_profile_frames*1000.0/seconds));
            wo_render_event("menu_present_max_interval_us",menu_profile_max_us);
            wo_render_event("menu_present_over_50ms",menu_profile_late);
            menu_profile_window=end;menu_profile_frames=menu_profile_max_us=menu_profile_late=0;
        }
    }
    if(race) {
        DWORD us=refresh_elapsed_us(begin,end);
        if(us>stall_present_max_us)stall_present_max_us=us;
        if(us>=50000)++stall_present_count;
        profile_frame_end=end;profile_frame_valid=TRUE;
    }
    return result;
}
static UINT advance_refresh_clock(double seconds) {
    if(seconds<0)seconds=0;
    refresh_phase+=seconds*30;
    UINT steps=refresh_phase>=8?8:(UINT)refresh_phase;
    refresh_phase-=steps;
    refresh_alpha=refresh_phase<1?(float)refresh_phase:1;
    if(simulation_active && refresh_phase<1) {refresh_alpha=(float)(refresh_phase*2);if(refresh_alpha>=1)refresh_alpha-=1;}
    return steps;
}
static UINT schedule_next_refresh(double seconds) {
    float completed_pose_alpha=refresh_phase<1?(float)refresh_phase:1;
    UINT steps=advance_refresh_clock(seconds);
    if(!simulation_active)refresh_alpha=completed_pose_alpha;
    return steps;
}
static BYTE *refresh_camera(void) {return *(BYTE **)(game+0x517bf0);}
static void sample_refresh_pose(void) {
    BYTE *camera=refresh_camera(),*ships=*(BYTE **)(game+0x518030);
    if(!camera || !ships)return;
    if(refresh_pose_valid && refresh_pose_epoch==refresh_epoch)return;
    BOOL continuous=refresh_pose_valid && refresh_pose_epoch+1==refresh_epoch;
    for(UINT i=0;i<3;++i) {
        float value=(float)((LONG *)(camera+0x14))[i];
        camera_previous[i]=continuous?camera_current[i]:value;camera_current[i]=value;
        for(UINT ship=0;ship<15;++ship) {
            value=(float)((LONG *)(ships+ship*240+0x40))[i];
            ship_previous[ship][i]=continuous?ship_current[ship][i]:value;
            ship_current[ship][i]=value;
        }
    }
    refresh_pose_epoch=refresh_epoch;refresh_pose_valid=TRUE;
}
static void refresh_ship_delta(int ship,float *delta) {
    float trail_shift[3]={0,0,0};
    if(simulation_active)wo_sim_trail_shift(ship,trail_shift);
    for(UINT i=0;i<3;++i)delta[i]=refresh_active && refresh_race && refresh_pose_valid && ship>=0 && ship<15?
        (ship_previous[ship][i]-ship_current[ship][i])*(1-refresh_alpha)+trail_shift[i]:0;
}
static void interpolate_render_matrix(float *matrix,const BYTE *skeleton) {
    if(!refresh_active || !refresh_race || !*(DWORD *)(game+0x8c98c))return;
    BYTE *motion_camera=refresh_camera();
    BOOL camera_root=motion_camera && skeleton==*(BYTE **)(motion_camera+0x44);
    DWORD raw_hash=camera_root?motion_hash(matrix,(LONG *)(motion_camera+0x14)):0;
    UINT start=((UINT)skeleton>>4)&4095;RenderHistory *entry=0;
    for(UINT i=0;i<8;++i) {
        RenderHistory *candidate=render_history+((start+i)&4095);
        if(candidate->skeleton==skeleton || !candidate->skeleton) {entry=candidate;break;}
    }
    if(!entry)entry=render_history+start;
    if(entry->skeleton!=skeleton) {
        entry->skeleton=skeleton;entry->epoch=refresh_epoch;entry->frame=refresh_frame;
        copy((BYTE *)entry->previous,(const BYTE *)matrix,64);copy((BYTE *)entry->current,(const BYTE *)matrix,64);
    } else if(entry->frame!=refresh_frame) {
        if(entry->epoch!=refresh_epoch) {
            if(camera_root && entry->epoch+1!=refresh_epoch)++motion_history_gaps;
            const float *previous=entry->epoch+1==refresh_epoch?entry->current:matrix;
            copy((BYTE *)entry->previous,(const BYTE *)previous,64);
        }
        copy((BYTE *)entry->current,(const BYTE *)matrix,64);
        entry->epoch=refresh_epoch;entry->frame=refresh_frame;
        float distance=0;
        for(UINT i=12;i<15;++i) {float d=entry->current[i]-entry->previous[i];distance+=d*d;}
        if(distance>4) {copy((BYTE *)entry->previous,(const BYTE *)entry->current,64);if(camera_root)++motion_history_resets;}
    }
    for(UINT i=0;i<16;++i)matrix[i]=entry->previous[i]+(entry->current[i]-entry->previous[i])*refresh_alpha;
    /* Track and effects already contain the current camera translation in
     * their input vertices. Compensate it through their interpolated parent. */
    BYTE *camera=refresh_camera();
    if(refresh_pose_valid && camera && skeleton==*(BYTE **)(camera+0x44)) {
        float delta[3];
        for(UINT i=0;i<3;++i)delta[i]=(camera_previous[i]-camera_current[i])*(1-refresh_alpha)/4096;
        for(UINT i=0;i<3;++i)matrix[12+i]+=delta[0]*matrix[i]+delta[1]*matrix[4+i]+delta[2]*matrix[8+i];
    }
    if(camera_root && motion_frame_seen!=refresh_frame) {
        DWORD draw_hash=motion_hash(matrix,0);
        ++motion_frames;if(raw_hash!=motion_raw_previous)++motion_raw_changes;
        if(draw_hash!=motion_draw_previous)++motion_draw_changes;
        motion_raw_previous=raw_hash;motion_draw_previous=draw_hash;motion_frame_seen=refresh_frame;
    }
}
static const BYTE *__cdecl refresh_skeleton(BYTE *skeleton) {
    const BYTE *matrix=old_refresh_skeleton(skeleton);
    refresh_last_matrix=matrix;refresh_last_skeleton=skeleton;
    return matrix;
}
static void __cdecl __attribute__((noinline)) refresh_input(void) {
    if(refresh_active && refresh_race && __builtin_return_address(0)==game+0x2abae) {
        ++refresh_epoch;++refresh_ticks_measured;
        /* Every original simulation iteration receives exactly one30Hz step,
         * including when the outer loop catches up multiple ticks. */
        *(DWORD *)(game+0x89c7c)=2;
    }
    keyboard_focus();
    old_refresh_input();
    controller_input();
    if(simulation_active)wo_sim_restore_input();
}
static void __cdecl __attribute__((noinline)) refresh_reset(void) {
    old_refresh_reset();
    if(!refresh_active || __builtin_return_address(0)!=game+0x2ab07)return;
    refresh_race=TRUE;refresh_phase=0;refresh_alpha=1;
    apply_race_preferences();
    target_animation_valid=FALSE;
    refresh_epoch=refresh_frame=refresh_pose_epoch=0;
    refresh_extra_serial=0;if(simulation_active)wo_sim_reset();refresh_pose_valid=FALSE;
    refresh_frames_measured=refresh_ticks_measured=refresh_measured_fps=0;
    profile_frame_valid=FALSE;stall_present_max_us=stall_frame_max_us=stall_frame_count=stall_present_count=0;
    for(UINT i=0;i<4096;++i)render_history[i].skeleton=0;
    QueryPerformanceCounter(&refresh_last_time);refresh_measure_time=refresh_last_time;
    *(DWORD *)(game+0x89c7c)=2;
}
static int __cdecl __attribute__((noinline)) refresh_clock(int unused) {
    if(!refresh_active || !refresh_race || __builtin_return_address(0)!=game+0x2b870)return old_refresh_clock(unused);
    if(simulation_active) {
        if(refresh_phase>=.5 && wo_sim_wants_half_tick()) {
            old_refresh_input();wo_sim_latch_input();wo_sim_half_tick();
        }
        DWORD serial=wo_sim_extra_serial(),ticks=serial-refresh_extra_serial;
        refresh_epoch+=ticks;refresh_ticks_measured+=ticks;refresh_extra_serial=serial;
    }
    sample_refresh_pose();
    int result=old_refresh_clock(unused);
    LARGE_INTEGER now;QueryPerformanceCounter(&now);
    double delta=(double)(now.QuadPart-refresh_last_time.QuadPart)/(double)refresh_frequency.QuadPart;
    refresh_last_time=now;
    /* The loop has consumed the steps scheduled LAST time. Its completed
     * poses therefore belong to the previous clock phase. Advancing the clock
     * now schedules NEXT iteration's physics; applying that new remainder to
     * these old poses jumps backwards whenever it wraps from near1 to near0. */
    UINT steps=schedule_next_refresh(delta);
    *(DWORD *)(game+0x89c7c)=steps*2;
    ++refresh_frame;++refresh_frames_measured;
    double elapsed=(double)(now.QuadPart-refresh_measure_time.QuadPart)/(double)refresh_frequency.QuadPart;
    if(elapsed>=1) {
        refresh_measured_fps=(UINT)(refresh_frames_measured/elapsed+0.5);
        wo_render_event("refresh_render_fps_milli",(DWORD)(refresh_frames_measured*1000.0/elapsed));
        wo_render_event("refresh_physics_hz_milli",(DWORD)(refresh_ticks_measured*1000.0/elapsed));
        wo_render_event("motion_camera_frames",motion_frames);
        wo_render_event("motion_camera_raw_changes",motion_raw_changes);
        wo_render_event("motion_camera_draw_changes",motion_draw_changes);
        wo_render_event("motion_history_gaps",motion_history_gaps);
        wo_render_event("motion_history_resets",motion_history_resets);
        motion_frames=motion_raw_changes=motion_draw_changes=motion_history_gaps=motion_history_resets=0;
        wo_render_event("profile_present_max_us",stall_present_max_us);
        wo_render_event("profile_work_max_us",stall_frame_max_us);
        wo_render_event("profile_present_over_50ms",stall_present_count);
        wo_render_event("profile_work_over_50ms",stall_frame_count);
        stall_present_max_us=stall_frame_max_us=stall_frame_count=stall_present_count=0;
        refresh_frames_measured=refresh_ticks_measured=0;refresh_measure_time=now;
    }
    return result;
}
static void draw_refresh_stats(void) {
    if(!refresh_active || !refresh_race || !refresh_show_fps || !*(DWORD *)(game+0x8c98c))return;
    char text[16],reverse[10];UINT length=0,n=refresh_measured_fps;
    if(!n) {text[length++]='-';text[length++]='-';}
    else {
        UINT digits=0;while(n && digits<10) {reverse[digits++]=(char)('0'+n%10);n/=10;}
        while(digits)text[length++]=reverse[--digits];
    }
    text[length++]=' ';text[length++]='F';text[length++]='P';text[length++]='S';text[length]=0;
    hud_layout_begin(TRUE);
    ++hud_depth;center_text(text,160,226,8,0);--hud_depth;
    hud_layout_finish();
}
static void initialize_refresh(const WCHAR *settings) {
    WCHAR enabled[8];
    if(!GetEnvironmentVariableW(L"WO2097_REFRESH_TEST",enabled,8) || enabled[0]!=L'1')return;
    if(!QueryPerformanceFrequency(&refresh_frequency) || !refresh_frequency.QuadPart)return;
    /* This read serves only the race/pause menu card rotations, not physics. */
    if(*(DWORD *)(game+0x2bca9)!=0x489c7c)return;
    if(*(DWORD *)(game+0x33d1d)!=0x489c7c)return;
    const BYTE minimum[8]={0xc7,0x44,0x24,0x30,1,0,0,0};
    if(!equal(game+0x2ab8e,minimum,8))return;
#define REFRESH_VERIFY(name) if(!equal(game+RVA_##name,entry_##name,sizeof(entry_##name)))return
    REFRESH_VERIFY(refresh_present);REFRESH_VERIFY(refresh_clock);REFRESH_VERIFY(refresh_reset);REFRESH_VERIFY(refresh_input);REFRESH_VERIFY(refresh_skeleton);REFRESH_VERIFY(ship_effects);REFRESH_VERIFY(ui_particles);REFRESH_VERIFY(loading_ui);
    DWORD protection,ignored;
    if(!VirtualProtect(game+0x2bca9,4,PAGE_EXECUTE_READWRITE,&protection))return;
    *(DWORD *)(game+0x2bca9)=(DWORD)&model_units;
    if(VirtualProtect(game+0x33d1d,4,PAGE_EXECUTE_READWRITE,&protection)) {
        *(DWORD *)(game+0x33d1d)=(DWORD)&model_units;
        VirtualProtect(game+0x33d1d,4,protection,&ignored);
    }
    VirtualProtect(game+0x2bca9,4,protection,&ignored);
    if(!VirtualProtect(game+0x2ab92,4,PAGE_EXECUTE_READWRITE,&protection))return;
    *(DWORD *)(game+0x2ab92)=0;VirtualProtect(game+0x2ab92,4,protection,&ignored);
    old_refresh_present=(void *)hook(RVA_refresh_present,entry_refresh_present,sizeof(entry_refresh_present),refresh_present);
    old_refresh_clock=(RefreshClockFn)hook(RVA_refresh_clock,entry_refresh_clock,sizeof(entry_refresh_clock),refresh_clock);
    old_refresh_reset=(RefreshVoidFn)hook(RVA_refresh_reset,entry_refresh_reset,sizeof(entry_refresh_reset),refresh_reset);
    old_refresh_input=(RefreshVoidFn)hook(RVA_refresh_input,entry_refresh_input,sizeof(entry_refresh_input),refresh_input);
    old_refresh_skeleton=(RefreshSkeletonFn)hook(RVA_refresh_skeleton,entry_refresh_skeleton,sizeof(entry_refresh_skeleton),refresh_skeleton);
    old_ship_effects=(void *)hook(RVA_ship_effects,entry_ship_effects,sizeof(entry_ship_effects),ship_effects);
    old_ui_particles=(RefreshVoidFn)hook(RVA_ui_particles,entry_ui_particles,sizeof(entry_ui_particles),ui_particles);
    old_loading_ui=(RefreshVoidFn)hook(RVA_loading_ui,entry_loading_ui,sizeof(entry_loading_ui),loading_ui);
    refresh_active=old_refresh_present && old_refresh_clock && old_refresh_reset && old_refresh_input && old_refresh_skeleton && old_ship_effects && old_ui_particles && old_loading_ui;
    if(!refresh_active) {
        if(VirtualProtect(game+0x2bca9,4,PAGE_EXECUTE_READWRITE,&protection)) {
            *(DWORD *)(game+0x2bca9)=0x489c7c;VirtualProtect(game+0x2bca9,4,protection,&ignored);
        }
        if(VirtualProtect(game+0x2ab92,4,PAGE_EXECUTE_READWRITE,&protection)) {
            *(DWORD *)(game+0x2ab92)=1;VirtualProtect(game+0x2ab92,4,protection,&ignored);
        }
    }
    refresh_show_fps=GetPrivateProfileIntW(L"Render",L"ShowFPS",1,settings)!=0;
    initialize_race_ui(settings);
    FlushInstructionCache(GetCurrentProcess(),game+0x2ab92,4);
    wo_render_event("render_high_refresh_test",refresh_active);
    if(refresh_active)wo_render_event("render_ui_animation_hz",30);
    old_slide_menu=(void *)hook(RVA_slide_menu,entry_slide_menu,sizeof(entry_slide_menu),slide_menu);
    initialize_sprites();
    initialize_controller();
    initialize_credits();
}
