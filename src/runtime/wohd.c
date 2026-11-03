/* Original-game CD-audio adapter. No disc, driver, registry, or system changes.
 * Only the separately patched EXE imports this module. Other WinMM calls are
 * forwarded unchanged by wohd.def. Music files live beside it in Music/.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include "audio_api.h"

#define VIRTUAL_CD 0x2097
#define LAST_TRACK 25
static HMODULE module;
static WCHAR root[MAX_PATH], files[LAST_TRACK+1][MAX_PATH];
static DWORD lengths[LAST_TRACK+1], starts[LAST_TRACK+1];
static DWORD startframes[LAST_TRACK+1];
static DWORD total, format=MCI_FORMAT_MSF, current, volume=0xffffffff;
static BOOL initialized, opened, player;
static HANDLE logfile=INVALID_HANDLE_VALUE;
void wo_render_initialize(BYTE *image,const WCHAR *root);
BOOL wo_render_active(void);

static void copyw(WCHAR *dst, const WCHAR *src) { while ((*dst++=*src++)) {} }
static UINT lenw(const WCHAR *s) { UINT n=0; while(s[n]) ++n; return n; }
static DWORD number(const WCHAR *s) { DWORD n=0; while(*s>=L'0'&&*s<=L'9') n=n*10+(*s++-L'0'); return n; }
static void appendnum(WCHAR *s,DWORD n) {
    WCHAR b[16]; UINT i=0,j=lenw(s);
    do { b[i++]=L'0'+n%10; n/=10; } while(n);
    while(i) s[j++]=b[--i]; s[j]=0;
}
static void logevent(const char *name,DWORD value) {
    char line[128],digits[16]; DWORD n=0,i=0,written;
    while(*name && n<90) line[n++]=*name++;
    line[n++]=' ';
    do {digits[i++]='0'+value%10; value/=10;} while(value);
    while(i) line[n++]=digits[--i]; line[n++]='\r'; line[n++]='\n';
    if(logfile!=INVALID_HANDLE_VALUE) WriteFile(logfile,line,n,&written,0);
}
void wo_render_event(const char *name,DWORD value) {logevent(name,value);}
#include "portable_paths.c"
#include "startup_launch.c"
static MCIERROR send(const WCHAR *s,WCHAR *answer,UINT size) {
    LARGE_INTEGER begin,end,frequency;
    QueryPerformanceCounter(&begin);
    MCIERROR error=mciSendStringW(s,answer,size,0);
    QueryPerformanceCounter(&end);
    if(QueryPerformanceFrequency(&frequency) && frequency.QuadPart) {
        double us=(double)(end.QuadPart-begin.QuadPart)*1000000.0/frequency.QuadPart;
        if(us>=20000) {logevent("profile_music_mci_block_us",(DWORD)us);logevent("profile_music_command",(DWORD)s[0]);}
    }
    if(error) logevent("file_player_error",error);
    return error;
}
BOOL wo_audio_movie_pause(void) {
    WCHAR mode[16];
    BOOL playing=player && !send(L"status wo2097music mode",mode,16) && mode[0]==L'p' && mode[1]==L'l';
    return playing && !send(L"pause wo2097music",0,0);
}
void wo_audio_movie_resume(BOOL playing) {if(playing && player)send(L"resume wo2097music",0,0);}
static BOOL resolve_root(void) {
    UINT n=GetModuleFileNameW(module,root,MAX_PATH);
    if(!n || n>=MAX_PATH-40) return FALSE;
    while(n && root[n-1]!=L'\\') --n;root[n]=0;
    return TRUE;
}
static void prepare_game_folder(void) {
    BYTE *image=(BYTE *)GetModuleHandleW(0);
    IMAGE_DOS_HEADER *dos=(IMAGE_DOS_HEADER *)image;
    IMAGE_NT_HEADERS32 *nt=(IMAGE_NT_HEADERS32 *)(image+dos->e_lfanew);
    /* Only the supported original-game image has these path buffers. */
    if(nt->OptionalHeader.Magic!=0x10b || nt->OptionalHeader.SizeOfImage!=0x7ee000) return;
    /* The debugger may place a breakpoint on the first WinMain byte. */
    if(image[0x5b8b1]!=0xec || image[0x5b8b2]!=0x48 || image[0x5b8b3]!=0x53) return;
    SetCurrentDirectoryW(root);
    wo_paths_initialize(image);
    WCHAR settings[MAX_PATH];copyw(settings,root);copyw(settings+lenw(settings),L"Wipeout2097.ini");
    wo_startup_install(image,settings);
}
static MCIERROR fileopen(UINT track) {
    WCHAR command[MAX_PATH+96]; MCIERROR error;
    if(player) {send(L"close wo2097music",0,0);player=FALSE;}
    if(track<2 || track>total || !files[track][0]) return MCIERR_OUTOFRANGE;
    copyw(command,L"open \""); copyw(command+lenw(command),files[track]);
    copyw(command+lenw(command),L"\" type mpegvideo alias wo2097music");
    error=send(command,0,0);
    if(error) return error;
    player=TRUE;current=track;
    return send(L"set wo2097music time format milliseconds",0,0);
}
static void setvolume(void) {
    WCHAR command[80]; DWORD v=((volume&65535)+(volume>>16))/2;
    if(!player) return;
    copyw(command,L"setaudio wo2097music volume to ");
    appendnum(command,v*1000/65535);send(command,0,0);
}
#include "music_library.c"
static void initialize(void) {
    WCHAR path[MAX_PATH]; UINT n;
    if(initialized) return; initialized=TRUE;
    if(!resolve_root()) return;n=lenw(root);
    copyw(path,root);copyw(path+n,L"..\\logs\\game.log");
    logfile=CreateFileW(path,GENERIC_WRITE,FILE_SHARE_READ,0,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,0);
    logevent("startup_setup_bypassed",wo_startup_bypassed);
    logevent("startup_sound_mode",wo_startup_sound_mode);
    audio_setting_path();
    UINT bank=wo_audio_pref(L"Soundtrack",0);
    if(!audio_load_bank(bank==1?1:0))audio_load_bank(0);
    wo_render_initialize((BYTE *)GetModuleHandleW(0),root);
    logevent("render_widescreen_test",wo_render_active());
    current=total?2:0;
}
static DWORD msf(DWORD ms) {
    DWORD frames=ms*75/1000;
    return (frames/(60*75))|((frames/75%60)<<8)|((frames%75)<<16);
}
static DWORD framemsf(DWORD frames) {return (frames/(60*75))|((frames/75%60)<<8)|((frames%75)<<16);}
static DWORD fromms(DWORD ms,UINT track,BOOL position) {
    if(format==MCI_FORMAT_MILLISECONDS) return ms;
    if(format==MCI_FORMAT_TMSF && position) return (track&255)|(msf(ms)<<8);
    return msf(ms);
}
static DWORD toms(DWORD value) {
    if(format==MCI_FORMAT_MILLISECONDS) return value;
    if(format==MCI_FORMAT_TMSF) value>>=8;
    return ((value&255)*60+((value>>8)&255))*1000+((value>>16)&255)*1000/75;
}
static UINT findtrack(DWORD absolute) {
    UINT i=total;
    while(i>1 && starts[i]>absolute) --i;
    return i;
}

MCIERROR WINAPI wo_mci(MCIDEVICEID id,UINT msg,DWORD_PTR flags,DWORD_PTR parameter) {
    MCI_STATUS_PARMS *status=(MCI_STATUS_PARMS *)parameter;
    MCI_PLAY_PARMS *play=(MCI_PLAY_PARMS *)parameter;
    WCHAR answer[40],command[120];DWORD result=0,position=0;UINT track;
    if(msg==MCI_OPEN) {
        MCI_OPEN_PARMSA *p=(MCI_OPEN_PARMSA *)parameter;
        if(!p || !(flags&MCI_OPEN_TYPE_ID) || (UINT_PTR)p->lpstrDeviceType!=MCI_DEVTYPE_CD_AUDIO)
            return mciSendCommandA(id,msg,flags,parameter);
        initialize();opened=TRUE;p->wDeviceID=VIRTUAL_CD;logevent("virtual_cd_open",total);return 0;
    }
    if(id!=VIRTUAL_CD) return mciSendCommandA(id,msg,flags,parameter);
    if(msg==MCI_CLOSE) {if(player) send(L"close wo2097music",0,0);player=FALSE;opened=FALSE;return 0;}
    if(!opened) return MCIERR_INVALID_DEVICE_ID;
    switch(msg) {
    case MCI_SET:
        if(flags&MCI_SET_TIME_FORMAT) {
            DWORD f=((MCI_SET_PARMS *)parameter)->dwTimeFormat;
            if(f!=MCI_FORMAT_MSF && f!=MCI_FORMAT_TMSF && f!=MCI_FORMAT_MILLISECONDS) return MCIERR_BAD_TIME_FORMAT;
            format=f;
        }
        return 0;
    case MCI_STATUS:
        if(!status) return MCIERR_NULL_PARAMETER_BLOCK;
        track=(flags&MCI_TRACK)?status->dwTrack:current;
        switch(status->dwItem) {
        case MCI_STATUS_NUMBER_OF_TRACKS: result=total;break;
        case MCI_CDA_STATUS_TYPE_TRACK: result=(track==1)?MCI_CDA_TRACK_OTHER:MCI_CDA_TRACK_AUDIO;break;
        case MCI_STATUS_LENGTH:
            if(flags&MCI_TRACK) {if(track<1||track>total)return MCIERR_OUTOFRANGE;result=lengths[track];}
            else if(total) result=starts[total]+lengths[total];
            result=fromms(result,track,FALSE);break;
        case MCI_STATUS_POSITION:
            if(flags&MCI_TRACK) {
                if(track<1||track>total)return MCIERR_OUTOFRANGE;
                result=format==MCI_FORMAT_TMSF?track:(format==MCI_FORMAT_MSF?framemsf(startframes[track]):starts[track]);
            } else {
                if(player && !send(L"status wo2097music position",answer,40)) position=number(answer);
                result=fromms(format==MCI_FORMAT_TMSF?position:starts[current]+position,current,TRUE);
            }break;
        case MCI_STATUS_MODE:
            result=MCI_MODE_STOP;
            if(player && !send(L"status wo2097music mode",answer,40)) {
                if(answer[0]==L'p') result=answer[1]==L'l'?MCI_MODE_PLAY:MCI_MODE_PAUSE;
            }break;
        case MCI_STATUS_MEDIA_PRESENT: case MCI_STATUS_READY: result=total?TRUE:FALSE;break;
        case MCI_STATUS_TIME_FORMAT: result=format;break;
        case MCI_STATUS_CURRENT_TRACK: result=current;break;
        default:return MCIERR_UNSUPPORTED_FUNCTION;
        }
        status->dwReturn=result;return 0;
    case MCI_PLAY:
        if(!total)return 0;
        track=current;
        if(flags&MCI_FROM) {
            position=toms(play->dwFrom);
            if(format==MCI_FORMAT_TMSF) track=play->dwFrom&255;
            else {track=findtrack(position);position-=starts[track];}
        }
        logevent("play_track",track);
        result=fileopen(track);if(result)return result;
        setvolume();copyw(command,L"play wo2097music from ");appendnum(command,position);
        /* The original game's MCI_TO is the next track boundary. A file ends
         * there naturally. Do not notify the real CD driver or use its clock. */
        return send(command,0,0);
    case MCI_STOP: logevent("music_stop",current);return player?send(L"stop wo2097music",0,0):0;
    case MCI_PAUSE:return player?send(L"pause wo2097music",0,0):0;
    case MCI_RESUME:return player?send(L"resume wo2097music",0,0):0;
    default: logevent("unsupported_virtual_command",msg);return MCIERR_UNSUPPORTED_FUNCTION;
    }
}
UINT WINAPI wo_auxnum(void) {return 1;}
MMRESULT WINAPI wo_auxget(UINT id,LPDWORD value) {if(!value)return MMSYSERR_INVALPARAM;*value=volume;return 0;}
MMRESULT WINAPI wo_auxset(UINT id,DWORD value) {volume=value;setvolume();return 0;}
MMRESULT WINAPI wo_auxcaps(UINT_PTR id,LPAUXCAPSA caps,UINT size) {
    UINT i;BYTE *b=(BYTE *)caps;const char *name="WipEout local music";
    if(!caps || size<sizeof(*caps))return MMSYSERR_INVALPARAM;
    for(i=0;i<sizeof(*caps);++i)b[i]=0;
    for(i=0;name[i];++i)caps->szPname[i]=name[i];
    caps->wTechnology=AUXCAPS_CDAUDIO;caps->dwSupport=AUXCAPS_VOLUME|AUXCAPS_LRVOLUME;return 0;
}
BOOL WINAPI DllMain(HINSTANCE instance,DWORD reason,LPVOID reserved) {
    if(reason==DLL_PROCESS_ATTACH) {
        module=instance;
        /* Keep the game's short filename buffers safe. The EXE retains
         * relative roots and no longer changes CWD while assembling paths. */
        if(resolve_root())prepare_game_folder();
    }
    return TRUE;
}
BOOL wo_controller_present(void);
UINT WINAPI wo_joynum(void) {return joyGetNumDevs();}
MMRESULT WINAPI wo_joypos(UINT id,LPJOYINFOEX info) {return wo_controller_present()?JOYERR_UNPLUGGED:joyGetPosEx(id,info);}
MMRESULT WINAPI wo_joycaps(UINT_PTR id,LPJOYCAPSA caps,UINT size) {return joyGetDevCapsA(id,caps,size);}
MMRESULT WINAPI wo_timekill(UINT id) {return timeKillEvent(id);}
MMRESULT WINAPI wo_timecaps(LPTIMECAPS caps,UINT size) {return timeGetDevCaps(caps,size);}
MMRESULT WINAPI wo_timeset(UINT delay,UINT resolution,LPTIMECALLBACK callback,DWORD_PTR user,UINT flags) {return timeSetEvent(delay,resolution,callback,user,flags);}
DWORD WINAPI wo_timeget(void) {return timeGetTime();}
