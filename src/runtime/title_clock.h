#ifndef WO_TITLE_CLOCK_H
#define WO_TITLE_CLOCK_H
static unsigned title_frames(double seconds) {
    if(seconds<=0)return 0;
    if(seconds>=71582788.0)return 0x7fffffffu;
    return (unsigned)(seconds*30.0+1e-7);
}
/* Start is Esc (the pause menu) in a race, Enter everywhere else. */
static unsigned title_start_mask(int racing) {return racing?0x1000u:0x4000u;}
#endif
