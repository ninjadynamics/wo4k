static void (__cdecl *old_show_credits)(int);
static int credits_previous,credits_current;
static DWORD credits_serial;
static float credits_phase;
static BOOL credits_valid,credits_moving;
static float interpolated_credits(int y,float phase,DWORD serial) {
    if(!credits_valid || serial>credits_serial+1 || y-credits_current>8 || y-credits_current<-8) {
        credits_previous=credits_current=y;credits_valid=TRUE;credits_moving=FALSE;
    } else if(y!=credits_current) {
        credits_previous=credits_current;credits_current=y;credits_moving=TRUE;
    } else if(phase<credits_phase && serial!=credits_serial)credits_moving=FALSE;
    credits_phase=phase;credits_serial=serial;
    return credits_moving?credits_previous+(credits_current-credits_previous)*phase:(float)y;
}
static void __cdecl show_credits(int y) {
    if(!refresh_active) {old_show_credits(y);return;}
    float phase=refresh_race && *(DWORD *)(game+0x8c98c)?refresh_alpha:ui_draw_phase;
    float position=interpolated_credits(y,phase,ui_present_serial);
    int whole=(int)position;float saved=credits_motion_y;credits_motion_y=position-whole;
    old_show_credits(whole);credits_motion_y=saved;
}
static void initialize_credits(void) {
    if(refresh_active)old_show_credits=(void *)hook(RVA_show_credits,entry_show_credits,sizeof(entry_show_credits),show_credits);
}
