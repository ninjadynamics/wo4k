/* Extend both original audio screens without replacing their artwork or bars. */
typedef void (__cdecl *AudioDataFn)(BYTE *);
typedef void (__cdecl *AudioPauseFn)(BYTE *,BYTE *);
typedef const char *(__cdecl *AudioNameFn)(int);
static AudioDataFn old_audio_config,old_audio_game_data,old_audio_pause_input;
static AudioPauseFn old_audio_pause_draw;
static AudioNameFn old_audio_title,old_audio_artist;
static void (__cdecl *audio_shuffle)(void);
static void (__cdecl *audio_play)(int,int);
static void (__cdecl *audio_effect_volume)(int),(__cdecl *audio_music_volume)(int);
static void (__cdecl *audio_note)(int,int,int,int,int);
/* Original SLES_003.27 tables at file offsets81e60/81e94 match the BIN/CUE
 * audio order; retail soundtrack-album ordering is different. */
static const char *const ps1_titles[12]={"SHUFFLE","WE HAVE EXPLOSIVE","LANDMASS","ATOMBOMB INSTR.",
    "V6","DUST UP BEATS","LOOPS OF FURY","THE THIRD SEQUENCE","TIN THERE [EDIT]",
    "FIRESTARTER INSTR.","CANADA","BODY IN MOTION"};
static const char *const ps1_artists[12]={"SHUFFLE","F.S.O.L.","F.S.O.L.","FLUKE","FLUKE",
    "CHEMICAL BROTHERS","CHEMICAL BROTHERS","PHOTEK","UNDERWORLD","THE PRODIGY",
    "COLD STORAGE","COLD STORAGE"};
#define AUDIO_METADATA_SLOTS 13
static const char *audio_pc_titles[AUDIO_METADATA_SLOTS],*audio_pc_artists[AUDIO_METADATA_SLOTS];
static BOOL audio_metadata_saved;
static void audio_update_metadata(void) {
    /* Pause-menu rendering inlines GetCdTrackName. Keep its real pointer
     * tables in sync too, including the initial bank restored at startup. */
    const char **titles=(const char **)(game+0x8aed8);
    const char **artists=(const char **)(game+0x8af10);
    if(!audio_metadata_saved) {
        for(UINT i=0;i<AUDIO_METADATA_SLOTS;++i) {audio_pc_titles[i]=titles[i];audio_pc_artists[i]=artists[i];}
        audio_metadata_saved=TRUE;
    }
    DWORD protection,ignored;
    UINT span=0x38+AUDIO_METADATA_SLOTS*sizeof(*titles);
    if(!VirtualProtect(titles,span,PAGE_READWRITE,&protection)) {
        wo_render_event("audio_metadata_update_failed",1);return;
    }
    BOOL ps1=wo_audio_bank()!=0;
    for(UINT i=0;i<AUDIO_METADATA_SLOTS;++i) {
        titles[i]=ps1?(i<12?ps1_titles[i]:""):audio_pc_titles[i];
        artists[i]=ps1?(i<12?ps1_artists[i]:""):audio_pc_artists[i];
    }
    VirtualProtect(titles,span,protection,&ignored);
}
static const char *__cdecl audio_title(int track) {
    if(track<0 || (UINT)track>=wo_audio_tracks())return "";
    if(wo_audio_bank())return track<12?ps1_titles[track]:"";
    return old_audio_title(track);
}
static const char *__cdecl audio_artist(int track) {
    if(track<0 || (UINT)track>=wo_audio_tracks())return "";
    if(wo_audio_bank())return track<12?ps1_artists[track]:"";
    return old_audio_artist(track);
}
static void audio_move_note(void) {audio_note(8,255,15,15,256);}
static void audio_remember(BYTE *config,BOOL menu,BOOL force) {
    if(menu)wo_audio_save(*(WORD *)(game+0x510476),*(WORD *)(game+0x510478),game[0x51047e]);
    else wo_audio_save(*(WORD *)(config+0xe),*(WORD *)(config+0x10),config[6]);
    wo_audio_flush(force);
}
static void audio_change_bank(BYTE *config,BOOL menu) {
    if(!wo_audio_switch(1-wo_audio_bank()))return;
    audio_update_metadata();
    *(DWORD *)(game+0x664834)=wo_audio_tracks();
    audio_shuffle();
    UINT choice=menu?game[0x51047e]:config[6];
    if(choice<1 || choice>wo_audio_tracks())choice=1;
    config[6]=(BYTE)choice;
    if(menu)game[0x51047e]=(BYTE)choice;
    UINT play=choice==1?game[0x4e1f00]:choice;
    *(DWORD *)(game+0x66483c)=play;game[0x4e1f20]=0;
    *(DWORD *)(game+0x87598)=480;
    audio_play((int)play,0);
    if(!menu)*(DWORD *)(game+0x518274)=1; // continue preview when leaving pause
    audio_move_note();
}
static void audio_library_label(int x,int y,BOOL selected) {
    int color=selected && *(DWORD *)(game+0x65f684)?1:0;
    add_text("SOUNDTRACK",x,y,8,color);
    const char *bank=wo_audio_bank()?(x==85?"PS1":"PLAYSTATION"):"PC";
    float origin;
    if(audio_config_depth && hud_text_origin(game+0x87640,game+0x6e39f0,bank,&origin)) {
        /* Match the volume bars' physical right edge. Their page-35 quads
         * share the artwork anchor at141; the native right edge is284.
         * Both bank names have no leading blank, so origin gives their width. */
        float start=284-(160-origin)*2;
        float ax=anchor_x,ay=anchor_y,motion=hud_message_motion_x;
        int depth=ui_depth,ad=anchor_depth;
        menu_anchor(141,y);++ui_depth;++anchor_depth;
        hud_message_motion_x=start-(int)start;
        old_add_text(bank,(int)start,y,8,color);
        anchor_x=ax;anchor_y=ay;ui_depth=depth;anchor_depth=ad;hud_message_motion_x=motion;
    } else add_text(bank,x+125,y,8,color);
}
static const BYTE *audio_layout_quad(const BYTE *p,BYTE *out) {
    if(!audio_config_depth || *(const WORD *)(p+22)!=35)return p;
    int y=*(const SHORT *)(p+10),shift=0;
    /* Move each bar and its knob together with its corresponding label. */
    if(y==200 || y==206)shift=22;
    else if(y==278 || y==284)shift=8;
    if(!shift)return p;
    copy(out,p,40);
    for(UINT i=0;i<4;++i)*(SHORT *)(out+10+i*8)+=(SHORT)shift;
    return out;
}
static int audio_menu_row(int row,WORD keys) {
    if(row<0 || row>3)row=0;
    /* Visible order: soundtrack(3), music(0), SFX(1), track(2). */
    if(keys&0x10)row=row==3?3:(row==0?3:row-1);
    if(keys&0x40)row=row==3?0:(row<2?row+1:2);
    return row;
}
static WORD audio_volume_pad(WORD pad,WORD held) {
    pad|=held&0xa0;
    if(refresh_active && !ui_steps)pad&=~0xa0;
    return pad;
}
static void __cdecl audio_config(BYTE *config) {
    WORD saved_keys=*(WORD *)(game+0x65f5b0),keys=saved_keys,pad=*(WORD *)(game+0x665108);
    BOOL modal=*(DWORD *)(game+0x89cb0)!=0;
    if(modal)keys=0;
    int before=*(SHORT *)(game+0x510484),row=audio_menu_row(before,keys);
    if(row!=before)audio_move_note();
    if(row==3 && (keys&0xa0))audio_change_bank(config,TRUE);
    *(WORD *)(game+0x65f5b0)=keys&~0x50;
    *(WORD *)(game+0x665108)=row==3 || modal?pad&~0xa0:audio_volume_pad(pad,*(WORD *)(game+0x65f680));
    *(SHORT *)(game+0x510484)=(SHORT)(row==3?0:row);
    DWORD blink=*(DWORD *)(game+0x65f684);
    if(row==3) {*(DWORD *)(game+0x65f684)=0;*(WORD *)(game+0x65f5b0)&=~0xa0;}
    ++audio_config_depth;old_audio_config(config);
    *(DWORD *)(game+0x65f684)=blink;
    *(SHORT *)(game+0x510484)=(SHORT)row;
    *(WORD *)(game+0x65f5b0)=saved_keys;*(WORD *)(game+0x665108)=pad;
    if(row==3)add_text("MUSIC",30,108,8,0);
    audio_library_label(30,94,row==3);
    --audio_config_depth;
    audio_remember(config,TRUE,(keys&0x5000)!=0);
}
static void __cdecl audio_game_data(BYTE *config) {
    old_audio_game_data(config);
    UINT music=wo_audio_pref(L"MusicVolume",*(WORD *)(config+0xe));
    UINT effects=wo_audio_pref(L"EffectsVolume",*(WORD *)(config+0x10));
    UINT track=wo_audio_pref(L"Track",config[6]);
    if(music>100)music=75;if(effects>100)effects=75;
    if(track<1 || track>wo_audio_tracks())track=1;
    *(WORD *)(config+0xe)=*(WORD *)(config+0x12)=(WORD)music;
    *(WORD *)(config+0x10)=*(WORD *)(config+0x14)=(WORD)effects;
    config[6]=(BYTE)track;
    audio_effect_volume(effects*255/100);audio_music_volume(music*255/100);
}
static BOOL audio_pause_open(BYTE *config) {return *(DWORD *)(game+0x518270)==3 && config[0x16];}
static void __cdecl audio_pause_input(BYTE *config) {
    if(!audio_pause_open(config)) {old_audio_pause_input(config);return;}
    WORD keys=*(WORD *)(game+0x65f7cc),pad=*(WORD *)(game+0x665108);
    UINT row=game[0x8aea0];if(row>3)row=0;
    if((keys&0x10) && row)--row;else if((keys&0x40) && row<3)++row;
    game[0x8aea0]=(BYTE)row;
    *(WORD *)(game+0x65f7cc)=keys&~0x50;
    if(row==3)*(WORD *)(game+0x65f7cc)&=~0xa0;
    *(WORD *)(game+0x665108)=row==3?pad&~0xa0:audio_volume_pad(pad,(WORD)controller_menu);
    old_audio_pause_input(config);
    *(WORD *)(game+0x65f7cc)=keys;*(WORD *)(game+0x665108)=pad;
    if(audio_pause_open(config) && row==3 && (keys&0xa0))audio_change_bank(config,FALSE);
    audio_remember(config,FALSE,!audio_pause_open(config) || (keys&0x5000));
}
static void __cdecl audio_pause_draw(BYTE *config,BYTE *box) {
    old_audio_pause_draw(config,box);
    if(!audio_pause_open(config))return;
    BOOL selected=game[0x8aea0]==3;
    if(selected) {
        center_text("CD TRACK",0,155,8,0);
        print_center(audio_title(config[6]?config[6]-1:0),160,167,0);
        add_text("MUSIC",85,185,8,0);add_text("SFX",85,200,8,0);
    }
    audio_library_label(85,212,selected);
}
static void initialize_audio_menu(void) {
    old_audio_config=(void *)hook(RVA_audio_config,entry_audio_config,sizeof(entry_audio_config),audio_config);
    old_audio_game_data=(void *)hook(RVA_audio_game_data,entry_audio_game_data,sizeof(entry_audio_game_data),audio_game_data);
    old_audio_pause_input=(void *)hook(RVA_audio_pause_input,entry_audio_pause_input,sizeof(entry_audio_pause_input),audio_pause_input);
    old_audio_pause_draw=(void *)hook(RVA_audio_pause_draw,entry_audio_pause_draw,sizeof(entry_audio_pause_draw),audio_pause_draw);
    old_audio_title=(void *)hook(RVA_audio_title,entry_audio_title,sizeof(entry_audio_title),audio_title);
    old_audio_artist=(void *)hook(RVA_audio_artist,entry_audio_artist,sizeof(entry_audio_artist),audio_artist);
    audio_shuffle=(void *)(game+0x4e60c);audio_play=(void *)(game+0x8450);
    audio_effect_volume=(void *)(game+0x21a70);audio_music_volume=(void *)(game+0x21ac0);
    audio_note=(void *)(game+0x78e0);
    audio_update_metadata();
    wo_render_event("audio_library_menu",old_audio_config && old_audio_game_data && old_audio_pause_input && old_audio_pause_draw && old_audio_title && old_audio_artist);
}
