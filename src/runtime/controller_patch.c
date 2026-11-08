#include "controller_wire.h"
static ControllerWire *controller_wire;
static HANDLE controller_mapping;
static BOOL controller_started,controller_active,controller_ready;
static DWORD controller_buttons,controller_menu,controller_repeat;
static WORD controller_previous_pad;
static LONG controller_left,controller_right;
static WORD controller_keyboard;
static void (__cdecl *old_controller_brake)(BYTE *);
/* While its helper runs, SDL owns the joystick backend even when waiting or
 * unfocused; otherwise legacy WinMM can inject duplicate buttons or bypass
 * the focus guard. The helper writes every few milliseconds: one that never
 * started, or died, leaves the pad to WinMM rather than dead all session. */
BOOL wo_controller_present(void) {
    return controller_ready && controller_wire && controller_wire->magic==CONTROLLER_MAGIC &&
        GetTickCount()-controller_wire->timestamp<1000;
}
static LONG controller_axis(LONG v) {
    const LONG dead=4096;
    if(v>=-dead && v<=dead)return 0;
    if(v>0)return (v-dead)*32767/(32767-dead);
    return (v+dead)*32767/(32768-dead);
}
static WCHAR controller_path[MAX_PATH],controller_command[MAX_PATH+48];
/* Starting a process can block for seconds, for example while security
 * software scans a newly updated helper, so it never runs on the game's
 * thread. The wire stays silent until the helper writes to it. */
static DWORD WINAPI controller_launch(void *unused) {
    STARTUPINFOW startup={0};PROCESS_INFORMATION child={0};startup.cb=sizeof(startup);
    startup.dwFlags=STARTF_USESHOWWINDOW;startup.wShowWindow=SW_HIDE;
    wo_render_event("controller_launching",1);
    if(CreateProcessW(controller_path,controller_command,0,0,FALSE,CREATE_NO_WINDOW,0,0,&startup,&child)) {
        CloseHandle(child.hThread);CloseHandle(child.hProcess);wo_render_event("controller_sdl3_helper",1);
    } else wo_render_event("controller_sdl3_helper",0);
    return 0;
}
static void start_controller(void) {
    if(controller_started)return;controller_started=TRUE;
    WCHAR name[64];
    const WCHAR *prefix=L"Local\\WO2097Input-";UINT prefix_size=0;
    do {name[prefix_size]=prefix[prefix_size];}while(prefix[prefix_size++]);
    WCHAR digits[16];UINT n=0,end=0;DWORD id=GetCurrentProcessId(),value=id;
    do {digits[n++]=L'0'+value%10;value/=10;}while(value);
    while(name[end])++end;for(UINT i=n;i>0;--i)name[end++]=digits[i-1];name[end]=0;
    controller_mapping=CreateFileMappingW(INVALID_HANDLE_VALUE,0,PAGE_READWRITE,0,sizeof(ControllerWire),name);
    if(!controller_mapping)return;
    controller_wire=MapViewOfFile(controller_mapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(ControllerWire));
    if(!controller_wire)return;
    WCHAR *path=controller_path,*command=controller_command;
    UINT length=GetModuleFileNameW(0,path,MAX_PATH);while(length && path[length-1]!=L'\\')--length;
    const WCHAR *suffix=L"wo2097-controller.exe";
    if(!length || length+22>MAX_PATH)return;
    while((*((WCHAR *)path+length++)=*suffix++)) {}
    UINT j=0;command[j++]=L'"';for(UINT i=0;path[i];++i)command[j++]=path[i];command[j++]=L'"';command[j++]=L' ';
    for(UINT i=n;i>0;--i)command[j++]=digits[i-1];command[j]=0;
    HANDLE thread=CreateThread(0,0,controller_launch,0,0,0);
    if(thread)CloseHandle(thread);else controller_launch(0);
}
static BOOL read_controller(ControllerWire *state) {
    if(!controller_ready)return FALSE;start_controller();if(!controller_wire)return FALSE;
    for(UINT i=0;i<3;++i) {
        LONG first=controller_wire->sequence;if(first&1)continue;
        copy((BYTE *)state,(const BYTE *)controller_wire,sizeof(*state));
        if(first==controller_wire->sequence && state->magic==CONTROLLER_MAGIC && state->connected &&
           GetTickCount()-state->timestamp<250) {
            DWORD foreground=0;GetWindowThreadProcessId(GetForegroundWindow(),&foreground);
            return foreground==GetCurrentProcessId();
        }
    }
    return FALSE;
}
static void apply_controller_state(const ControllerWire *sample,BOOL present) {
    ControllerWire state;if(present)copy((BYTE *)&state,(const BYTE *)sample,sizeof(state));
    BOOL was_active=controller_active;controller_active=present;WORD pad=0,menu=0;
    controller_keyboard=*(WORD *)(game+0x665108);
    LONG pitch=0;
    if(present) {
        DWORD b=state.buttons;LONG x=controller_axis(state.x);pitch=controller_axis(state.y);
        if(b&1) {pad|=0x4000;menu|=0x4000;} // south / Cross
        if(b&4)pad|=0x2000;               // west / Square
        if(b&8)pad|=0x8000;               // north / Triangle
        if((b&2) && !(controller_buttons&2))*(SHORT *)(game+0x66496a)=0x3b; // Circle: camera (F1)
        if(b&(1u<<10))pad|=0x40;          // R1: rear-fire modifier
        if(pitch>0)pad|=0x40;else if(pitch<0)pad|=0x10;
        if(x<0)pad|=0x80;else if(x>0)pad|=0x20;
        *(WORD *)(game+0x65f7ce)=x?(WORD)(128+x*(x<0?128:127)/32767):
            ((controller_keyboard&0x20)?255:((controller_keyboard&0x80)?0:128));
        if(b&(1u<<11))menu|=0x10;if(b&(1u<<12))menu|=0x40;
        if(b&(1u<<13))menu|=0x80;if(b&(1u<<14))menu|=0x20;
        BOOL racing=refresh_race && *(DWORD *)(game+0x8c98c) && !*(DWORD *)(game+0x89cb0);
        if((b&4) && !racing)menu|=0x1000;
        if(b&(1u<<6))menu|=(WORD)title_start_mask(title_input_active()); // Options / Start
        controller_left=state.left>1600?(state.left-1600)*256/(32767-1600):0;
        controller_right=state.right>1600?(state.right-1600)*256/(32767-1600):0;
        WORD edges=menu&~controller_menu;DWORD now=GetTickCount();
        if((menu&0xf0)!=(controller_menu&0xf0))controller_repeat=now+300;
        else if((menu&0xf0) && (LONG)(now-controller_repeat)>=0) {edges|=menu&0xf0;controller_repeat=now+90;}
        /* Native PollKeyboard keeps menuHeld/menuKeys silent while a modal
         * owns keysPressed. Injecting both channels bypasses that guard and
         * makes YES/NO navigate or activate the menu drawn underneath. Keep
         * tracking held buttons so dismissing NO cannot replay its accept. */
        if(!*(DWORD *)(game+0x89cb0)) {
            *(WORD *)(game+0x65f680)|=menu;
            *(WORD *)(game+0x65f5b0)|=edges;
        }
        *(WORD *)(game+0x65f7cc)|=edges;
        *(WORD *)(game+0x65f7b8)|=pad&~controller_previous_pad;controller_previous_pad=pad;
        controller_buttons=b;
    } else {
        controller_left=controller_right=0;controller_buttons=0;controller_previous_pad=0;
        if(was_active)*(WORD *)(game+0x65f7ce)=(controller_keyboard&0x20)?255:((controller_keyboard&0x80)?0:128);
    }
    controller_menu=menu;*(WORD *)(game+0x665108)|=pad;
    BYTE magnitude=30;
    if(present && !(controller_keyboard&0x50))magnitude=(BYTE)((pitch<0?-pitch:pitch)*30/32767);
    if(controller_ready && game[0x63800]!=magnitude) {
        game[0x63800]=game[0x6380c]=magnitude;
        FlushInstructionCache(GetCurrentProcess(),game+0x637fc,17);
    }
}
static void controller_input(void) {
    ControllerWire state;BOOL present=read_controller(&state);apply_controller_state(&state,present);
    wo_audio_flush(FALSE);
}
static void __cdecl controller_brake(BYTE *ship) {
    old_controller_brake(ship);
    if(controller_active) {
        /* Native ship fields are left at+90 and right at+92. */
        if(!(controller_keyboard&0x400))*(SHORT *)(ship+0x90)=(SHORT)controller_left;
        if(!(controller_keyboard&0x800))*(SHORT *)(ship+0x92)=(SHORT)controller_right;
    }
}
static void initialize_controller(void) {
    if(game[0x63800]!=30 || game[0x6380c]!=30)return;
    DWORD protection;
    if(!VirtualProtect(game+0x637fc,17,PAGE_EXECUTE_READWRITE,&protection))return;
    old_controller_brake=(void *)hook(RVA_controller_brake,entry_controller_brake,sizeof(entry_controller_brake),controller_brake);
    controller_ready=old_controller_brake!=0;
    /* Start the helper now, in the background, so it is connected by the
     * first menu instead of being launched by the first input poll. */
    if(controller_ready)start_controller();
}
