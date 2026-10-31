/* TOC metadata avoids opening every song at startup. */
static UINT audio_bank;
static WCHAR audio_settings[MAX_PATH];
static UINT audio_music=101,audio_effects=101,audio_track=0;
static BOOL audio_dirty;
static DWORD audio_changed;
static void audio_setting_path(void) {
    copyw(audio_settings,root);copyw(audio_settings+lenw(audio_settings),L"Wipeout2097.ini");
}
UINT wo_audio_bank(void) {return audio_bank;}
UINT wo_audio_tracks(void) {return total;}
UINT wo_audio_pref(const WCHAR *key,UINT fallback) {
    return GetPrivateProfileIntW(L"Audio",key,fallback,audio_settings);
}
static void audio_mark_changed(void) {audio_dirty=TRUE;audio_changed=GetTickCount();}
void wo_audio_save(UINT music,UINT effects,UINT track) {
    if(music>100 || effects>100 || track<1 || track>total)return;
    if(audio_music==music && audio_effects==effects && audio_track==track)return;
    audio_music=music;audio_effects=effects;audio_track=track;audio_mark_changed();
}
static BOOL audio_write(const WCHAR *key,UINT value) {
    WCHAR number[16];number[0]=0;appendnum(number,value);
    return WritePrivateProfileStringW(L"Audio",key,number,audio_settings);
}
void wo_audio_flush(BOOL force) {
    if(!audio_dirty || (!force && GetTickCount()-audio_changed<400))return;
    BOOL ok=audio_write(L"Soundtrack",audio_bank);
    if(audio_music<=100)ok=audio_write(L"MusicVolume",audio_music)&&ok;
    if(audio_effects<=100)ok=audio_write(L"EffectsVolume",audio_effects)&&ok;
    if(audio_track)ok=audio_write(L"Track",audio_track)&&ok;
    if(ok)audio_dirty=FALSE;else {audio_changed=GetTickCount();logevent("audio_settings_write_failed",1);}
}
static BOOL audio_load_bank(UINT bank) {
    WCHAR directory[MAX_PATH],toc[MAX_PATH],section[16];
    copyw(directory,root);copyw(directory+lenw(directory),bank?L"music\\psx\\":L"music\\pc\\");
    copyw(toc,directory);copyw(toc+lenw(toc),L"tracks.ini");
    UINT advertised=GetPrivateProfileIntW(L"CD",L"Tracks",0,toc);
    if(advertised<2 || advertised>LAST_TRACK)return FALSE;
    for(UINT i=2;i<=advertised;++i) {
        WCHAR path[MAX_PATH];copyw(path,directory);UINT at=lenw(path);
        copyw(path+at,L"track00.wav");path[at+5]=L'0'+i/10;path[at+6]=L'0'+i%10;
        if(GetFileAttributesW(path)==INVALID_FILE_ATTRIBUTES) {
            copyw(path+at+8,L"mp3");
            if(GetFileAttributesW(path)==INVALID_FILE_ATTRIBUTES)return FALSE;
        }
        copyw(section,L"Track00");section[5]=L'0'+i/10;section[6]=L'0'+i%10;
        if(!GetPrivateProfileIntW(section,L"Frames",0,toc))return FALSE;
    }
    if(player) {send(L"close wo2097music",0,0);player=FALSE;}
    total=advertised;audio_bank=bank;
    for(UINT i=1;i<=LAST_TRACK;++i) {
        files[i][0]=0;lengths[i]=starts[i]=startframes[i]=0;
        if(i>total)continue;
        copyw(section,L"Track00");section[5]=L'0'+i/10;section[6]=L'0'+i%10;
        startframes[i]=GetPrivateProfileIntW(section,L"StartFrame",0,toc);
        starts[i]=startframes[i]*1000/75;
        lengths[i]=GetPrivateProfileIntW(section,L"Frames",150,toc)*1000/75;
        if(i==1)continue;
        copyw(files[i],directory);UINT at=lenw(files[i]);copyw(files[i]+at,L"track00.wav");
        files[i][at+5]=L'0'+i/10;files[i][at+6]=L'0'+i%10;
        if(GetFileAttributesW(files[i])==INVALID_FILE_ATTRIBUTES)copyw(files[i]+at+8,L"mp3");
    }
    current=current>=2 && current<=total?current:2;
    logevent("soundtrack_bank",bank);logevent("local_audio_tracks",total-1);
    return TRUE;
}
BOOL wo_audio_switch(UINT bank) {
    if(bank>1)return FALSE;if(bank==audio_bank)return TRUE;
    if(!audio_load_bank(bank))return FALSE;
    audio_mark_changed();return TRUE;
}
