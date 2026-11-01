/* The RIP retains the original ending flags but omits movie playback.
 * Wrap its verified race CALL, before native menu reloading consumes those
 * flags. Never manufacture an achievement or change the original save state. */
static WCHAR movie_root[MAX_PATH];
static void (__cdecl *old_movie_race)(BYTE *);
static BOOL movie_intro_seen;
static HWND cursor_window;
static WNDPROC cursor_previous_proc;
static LRESULT CALLBACK cursor_proc(HWND window,UINT message,WPARAM w,LPARAM l) {
    if(message==WM_SETCURSOR && LOWORD(l)==HTCLIENT){SetCursor(0);return TRUE;}
    return CallWindowProcA(cursor_previous_proc,window,message,w,l);
}
static void hide_game_cursor(void) {
    HWND window=GetForegroundWindow();DWORD owner=0;
    if(!window || !GetWindowThreadProcessId(window,&owner) || owner!=GetCurrentProcessId())return;
    if(!cursor_window) {
        WNDPROC previous=(WNDPROC)(UINT_PTR)SetWindowLongA(window,GWL_WNDPROC,(LONG)(UINT_PTR)cursor_proc);
        if(previous){cursor_previous_proc=previous;cursor_window=window;SetClassLongA(window,GCL_HCURSOR,0);}
    }
    if(window==cursor_window)SetCursor(0);
}
static BOOL movie_append(WCHAR *out,UINT capacity,const WCHAR *text) {
    UINT n=0;while(out[n])++n;
    while(*text){if(n+1>=capacity)return FALSE;out[n++]=*text++;}out[n]=0;return TRUE;
}
static BOOL movie_append_number(WCHAR *out,UINT capacity,DWORD value) {
    WCHAR reverse[16],number[16];UINT n=0,i=0;
    do{reverse[n++]=L'0'+value%10;value/=10;}while(value);
    while(n)number[i++]=reverse[--n];number[i]=0;return movie_append(out,capacity,number);
}
static const WCHAR *movie_name(UINT movie) {
    static const WCHAR *names[]={L"intro",L"xtro1",L"xtro2",L"xtro3",L"xtro4"};
    return movie<5?names[movie]:0;
}
static BOOL movie_available(UINT movie) {
    const WCHAR *names[3]={0,0,0};
    if(movie==0){names[0]=L"intro.mp4";names[1]=L"WXLINTR.mp4";names[2]=L"WXLINTR.MPG";}
    else if(movie<=4){
        static const WCHAR *mp4[]={L"",L"XTRO1.mp4",L"XTRO2.mp4",L"XTRO3.mp4",L"XTRO4.mp4"};
        static const WCHAR *mpg[]={L"",L"XTRO1.MPG",L"XTRO2.MPG",L"XTRO3.MPG",L"XTRO4.MPG"};
        names[0]=mp4[movie];names[1]=mpg[movie];
    }
    for(UINT i=0;i<3 && names[i];++i){
        WCHAR path[MAX_PATH];path[0]=0;
        if(!movie_append(path,MAX_PATH,movie_root) || !movie_append(path,MAX_PATH,L"movies\\") || !movie_append(path,MAX_PATH,names[i]))continue;
        DWORD attributes=GetFileAttributesW(path);if(attributes!=INVALID_FILE_ATTRIBUTES && !(attributes&FILE_ATTRIBUTE_DIRECTORY))return TRUE;
    }
    return FALSE;
}
static void play_game_movie(UINT movie) {
    if(!movie_root[0] || !movie_name(movie) || !movie_available(movie))return;
    WCHAR executable[MAX_PATH],script[MAX_PATH],command[MAX_PATH*4];executable[0]=script[0]=command[0]=0;
    UINT n=GetSystemDirectoryW(executable,MAX_PATH);
    if(!n || n>=MAX_PATH || !movie_append(executable,MAX_PATH,L"\\WindowsPowerShell\\v1.0\\powershell.exe"))return;
    if(!movie_append(script,MAX_PATH,movie_root) || !movie_append(script,MAX_PATH,L"..\\scripts\\Show-Video.ps1") || GetFileAttributesW(script)==INVALID_FILE_ATTRIBUTES)return;
    hide_game_cursor();HWND window=cursor_window;
    start_controller();
    BOOL ok=movie_append(command,MAX_PATH*4,L"\"") && movie_append(command,MAX_PATH*4,executable) &&
        movie_append(command,MAX_PATH*4,L"\" -NoProfile -STA -ExecutionPolicy Bypass -File \"") && movie_append(command,MAX_PATH*4,script) &&
        movie_append(command,MAX_PATH*4,L"\" -GameRoot \"") && movie_append(command,MAX_PATH*4,movie_root) &&
        /* Extra dot prevents a quoted trailing backslash escaping the quote. */
        movie_append(command,MAX_PATH*4,L".\" -Movie ") && movie_append(command,MAX_PATH*4,movie_name(movie)) &&
        movie_append(command,MAX_PATH*4,L" -ControllerOwner ") && movie_append_number(command,MAX_PATH*4,GetCurrentProcessId()) &&
        movie_append(command,MAX_PATH*4,L" -OwnerWindow ") && movie_append_number(command,MAX_PATH*4,(DWORD)(UINT_PTR)window);
    if(!ok)return;
    BOOL music=wo_audio_movie_pause();
    STARTUPINFOW startup={0};PROCESS_INFORMATION child={0};startup.cb=sizeof(startup);startup.dwFlags=STARTF_USESHOWWINDOW;startup.wShowWindow=SW_HIDE;
    if(CreateProcessW(executable,command,0,0,FALSE,CREATE_NO_WINDOW,0,0,&startup,&child)){
        CloseHandle(child.hThread);wo_render_event("video_started",movie);
        BOOL quit=FALSE;int quit_code=0;
        for(;;){
            DWORD waited=WaitForSingleObject(child.hProcess,20);if(waited!=WAIT_TIMEOUT)break;
            MSG message;
            while(PeekMessageA(&message,0,0,0,PM_REMOVE)){
                if(message.message==WM_QUIT){quit=TRUE;quit_code=(int)message.wParam;}
                else {TranslateMessage(&message);DispatchMessageA(&message);}
            }
        }
        DWORD result=1;GetExitCodeProcess(child.hProcess,&result);wo_render_event("video_exit_code",result);
        wo_render_event("video_game_focus_retained",!window || GetForegroundWindow()==window);
        CloseHandle(child.hProcess);if(window && GetForegroundWindow()!=window)SetForegroundWindow(window);
        if(quit)PostQuitMessage(quit_code);
    } else wo_render_event("video_launch_failed",movie);
    wo_audio_movie_resume(music);
    /* A cutscene is not a giant simulation/UI step. */
    QueryPerformanceCounter(&refresh_last_time);ui_last_time=refresh_last_time;
    menu_profile_last.QuadPart=0;profile_frame_valid=FALSE;
    hide_game_cursor();
}
static void (*movie_player)(UINT)=play_game_movie;
static UINT ending_movie(void) {
    if(*(WORD *)(game+0x8c764) && !*(WORD *)(game+0x8c768))return 1;
    if(*(WORD *)(game+0x8c76c) && !*(WORD *)(game+0x8c770))return 3;
    if(*(WORD *)(game+0x8c774))return 2;
    if(*(WORD *)(game+0x8c778))return 4;
    return 0;
}
static void __cdecl movie_race(BYTE *config) {
    old_movie_race(config);
    UINT movie=ending_movie();if(movie)movie_player(movie);
    /* The native StartRace tail handles all flag clearing, played markers,
     * menu restoration and save semantics after this wrapper returns. */
}
static void movie_before_title(void) {
    if(movie_intro_seen)return;movie_intro_seen=TRUE;movie_player(0);
}
static void initialize_movies(const WCHAR *root) {
    static const BYTE call[]={0xe8,0x40,0xa0,0xff,0xff};
    if(!refresh_active || !equal(game+0x2fb3b,call,sizeof(call)))return;
    UINT n=0;while(root[n] && n<MAX_PATH-50){movie_root[n]=root[n];++n;}
    movie_root[n]=0;if(root[n]){movie_root[0]=0;return;}
    DWORD protection,ignored;if(!VirtualProtect(game+0x2fb3b,5,PAGE_EXECUTE_READWRITE,&protection))return;
    old_movie_race=(void *)(game+0x29b80);game[0x2fb3b]=0xe8;
    *(LONG *)(game+0x2fb3c)=(LONG)((BYTE *)movie_race-(game+0x2fb40));
    VirtualProtect(game+0x2fb3b,5,protection,&ignored);FlushInstructionCache(GetCurrentProcess(),game+0x2fb3b,5);
    wo_render_event("video_race_hook",1);
}
