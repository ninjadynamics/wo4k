/* Every texture the game loads without a fixed place gets one from two
 * cursors: one for its image, restarted for each race, and one for its
 * palette, which only ever advances (62 rows after the first race, 122 after
 * the second, 189, 257...). The palette's place ends up in the texture's
 * record, so the same track is set up with different records each time it is
 * raced in a session, and those records are the one thing in a race's memory
 * that differs between a first and a later visit. Start the palette cursor
 * where the first race found it, every race. */
static void (__cdecl *old_race_textures)(void) __attribute__((used));
static WORD palette_cursor_start[2];
static BOOL palette_cursor_known;
static void __cdecl palette_cursor_restart(void) __attribute__((used,noinline));
static void __cdecl palette_cursor_restart(void) {
    WORD *cursor=(WORD *)(game+0x664960);
    if(!palette_cursor_known){palette_cursor_start[0]=cursor[0];palette_cursor_start[1]=cursor[1];palette_cursor_known=TRUE;}
    else{cursor[0]=palette_cursor_start[0];cursor[1]=palette_cursor_start[1];}
}
__attribute__((naked)) static void race_textures(void) {
    __asm__ volatile("pushal\n\tcall _palette_cursor_restart\n\tpopal\n\tjmp *_old_race_textures");
}
static void initialize_palette_cursor(void) {
    static const BYTE entry[]={0x53,0x33,0xc9,0x66,0x89,0x0d,0xd4,0xf7,0xa5,0x00};
    old_race_textures=(void *)hook(0x2fa80,entry,sizeof(entry),race_textures);
    wo_render_event("palette_cursor_hook",old_race_textures!=0);
}
