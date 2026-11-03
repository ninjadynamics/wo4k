#ifndef WO_MENU_FRAME_ART_H
#define WO_MENU_FRAME_ART_H
/* Measured pixel edges in the authored frame's256-unit texture space. */
typedef struct UiHdFrameArt {
    float left_inner,right_inner,right_outer,start_top,footer_top,footer_bottom,footer_end;
    BOOL valid;
} UiHdFrameArt;
static BOOL ui_hd_frame_page(UINT slot);
static BOOL ui_hd_frame_art(UiHdFrameArt *art);
#endif
