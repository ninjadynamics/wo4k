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
/* The game keeps keyboard focus during a movie, so its own loop sees the
 * skip keys. Only fresh presses count; held keys auto-repeat. */
static BOOL movie_skip_key(const MSG *message,HWND window) {
    if(message->message!=WM_KEYDOWN || (message->lParam&0x40000000))return FALSE;
    if(window && message->hwnd!=window)return FALSE;
    return message->wParam==VK_ESCAPE || message->wParam==VK_RETURN || message->wParam==VK_SPACE;
}
/* Restrict inheritance to the listed events alone: the player receives no
 * other inheritable handle the game may hold. handles must stay valid until
 * the process is created. */
static BOOL movie_handle_list(STARTUPINFOEXW *startup,ULONG_PTR *storage,SIZE_T capacity,HANDLE *handles,UINT count) {
    SIZE_T size=0;
    InitializeProcThreadAttributeList(0,1,0,&size);
    if(!size || size>capacity)return FALSE;
    startup->lpAttributeList=(LPPROC_THREAD_ATTRIBUTE_LIST)storage;
    if(!InitializeProcThreadAttributeList(startup->lpAttributeList,1,0,&size)){startup->lpAttributeList=0;return FALSE;}
    if(UpdateProcThreadAttribute(startup->lpAttributeList,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,handles,count*sizeof(*handles),0,0))return TRUE;
    DeleteProcThreadAttributeList(startup->lpAttributeList);startup->lpAttributeList=0;return FALSE;
}
static BOOL movie_executable(WCHAR *executable) {
    /* The installer places the player beside the game executable. */
    executable[0]=0;
    return movie_append(executable,MAX_PATH,movie_root) && movie_append(executable,MAX_PATH,L"wo4k-video.exe") &&
        GetFileAttributesW(executable)!=INVALID_FILE_ATTRIBUTES;
}
/* Start the player for movie, owned by window. *skip is passed when the
 * handle list can be built; otherwise it is closed and cleared. A start
 * event makes the player open its file hidden and wait for the event, and
 * a stage event (with it) stages the window; that mode needs the handle
 * list, so it fails without one. */
static BOOL movie_spawn(UINT movie,HWND window,HANDLE *skip,HANDLE start,HANDLE stage,PROCESS_INFORMATION *child) {
    WCHAR executable[MAX_PATH],command[MAX_PATH*4];command[0]=0;
    if(!movie_executable(executable))return FALSE;
    if(!start)stage=0;
    HANDLE handles[3];UINT count=0;
    if(*skip)handles[count++]=*skip;
    if(start)handles[count++]=start;
    if(stage)handles[count++]=stage;
    STARTUPINFOEXW startup={0};startup.StartupInfo.cb=sizeof(startup.StartupInfo);
    startup.StartupInfo.dwFlags=STARTF_FORCEOFFFEEDBACK;
    ULONG_PTR storage[32];
    BOOL listed=count && movie_handle_list(&startup,storage,sizeof(storage),handles,count);
    if(listed)startup.StartupInfo.cb=sizeof(startup);
    else if(start)return FALSE;
    else if(*skip){CloseHandle(*skip);*skip=0;}
    BOOL ok=movie_append(command,MAX_PATH*4,L"\"") && movie_append(command,MAX_PATH*4,executable) &&
        movie_append(command,MAX_PATH*4,L"\" --game-root \"") && movie_append(command,MAX_PATH*4,movie_root) &&
        /* Extra dot prevents a quoted trailing backslash escaping the quote. */
        movie_append(command,MAX_PATH*4,L".\" --movie ") && movie_append(command,MAX_PATH*4,movie_name(movie)) &&
        movie_append(command,MAX_PATH*4,L" --game-pid ") && movie_append_number(command,MAX_PATH*4,GetCurrentProcessId()) &&
        movie_append(command,MAX_PATH*4,L" --owner-window ") && movie_append_number(command,MAX_PATH*4,(DWORD)(UINT_PTR)window);
    if(ok && *skip)ok=movie_append(command,MAX_PATH*4,L" --skip-event ") && movie_append_number(command,MAX_PATH*4,(DWORD)(UINT_PTR)*skip);
    if(ok && start)ok=movie_append(command,MAX_PATH*4,L" --start-event ") && movie_append_number(command,MAX_PATH*4,(DWORD)(UINT_PTR)start);
    if(ok && stage)ok=movie_append(command,MAX_PATH*4,L" --stage-event ") && movie_append_number(command,MAX_PATH*4,(DWORD)(UINT_PTR)stage);
    wo_render_event(start?"video_preparing":"video_launching",movie);
    BOOL started=ok && CreateProcessW(executable,command,0,0,listed,listed?EXTENDED_STARTUPINFO_PRESENT:0,0,movie_root,&startup.StartupInfo,child);
    /* The list only matters while the child is created. */
    if(listed)DeleteProcThreadAttributeList(startup.lpAttributeList);
    if(started)CloseHandle(child->hThread);
    return started;
}
/* The intro player is started while the splash shows, on a background
 * thread: it opens its file hidden and waits for the start event. Process
 * start-up, any security scan of the player and opening the movie then
 * overlap the splash, and the movie appears as soon as the title asks. */
static HANDLE movie_ready_thread,movie_ready_process,movie_ready_skip,movie_ready_start,movie_ready_stage;
static HWND movie_ready_window;
static BOOL movie_ready_attempted,movie_ready_signalled,movie_ready_staged;
/* Set by initialize_title: only then does the title wait for the intro. */
static BOOL movie_title_hooked;
static DWORD WINAPI movie_prepare(void *unused) {
    PROCESS_INFORMATION child={0};
    if(movie_spawn(0,movie_ready_window,&movie_ready_skip,movie_ready_start,movie_ready_stage,&child)) {
        movie_ready_process=child.hProcess;wo_render_event("video_prepared",0);
    } else wo_render_event("video_prepare_failed",0);
    return 0;
}
static void movie_ready_close(void) {
    if(movie_ready_process)CloseHandle(movie_ready_process);
    if(movie_ready_skip)CloseHandle(movie_ready_skip);
    if(movie_ready_start)CloseHandle(movie_ready_start);
    if(movie_ready_stage)CloseHandle(movie_ready_stage);
    movie_ready_process=movie_ready_skip=movie_ready_start=movie_ready_stage=0;
}
/* Called on every presented frame; acts once the game window is known. */
static void prepare_intro_movie(void) {
    if(movie_ready_attempted || movie_intro_seen || !movie_title_hooked || !movie_root[0] || !cursor_window)return;
    movie_ready_attempted=TRUE;
    /* WO2097_VIDEO_PREPARE=0 restores launching the player on demand. */
    WCHAR prepare[4],executable[MAX_PATH];
    if(GetEnvironmentVariableW(L"WO2097_VIDEO_PREPARE",prepare,4) && prepare[0]==L'0')return;
    if(!movie_available(0) || !movie_executable(executable))return;
    SECURITY_ATTRIBUTES inherit={sizeof(inherit),0,TRUE};
    movie_ready_window=cursor_window;
    movie_ready_skip=CreateEventW(&inherit,TRUE,FALSE,0);
    movie_ready_start=CreateEventW(&inherit,TRUE,FALSE,0);
    /* Optional: without it the player shows its window at the start. */
    movie_ready_stage=CreateEventW(&inherit,TRUE,FALSE,0);
    if(movie_ready_skip && movie_ready_start)movie_ready_thread=CreateThread(0,0,movie_prepare,0,0,0);
    if(!movie_ready_thread)movie_ready_close();
}
/* A few frames before the start, while the game still presents: the player
 * puts its window on screen, transparent, so the compositor has taken over
 * from the full-screen game by the time the movie must appear. */
static void stage_prepared_intro(void) {
    if(!movie_ready_stage || movie_ready_staged)return;
    movie_ready_staged=TRUE;
    SetEvent(movie_ready_stage);
    wo_render_event("video_stage_signalled",0);
}
/* Tell the prepared player to show and play. The event exists before the
 * player does, so a player still starting up plays as soon as it is ready. */
static void start_prepared_intro(void) {
    if(!movie_ready_start || movie_ready_signalled)return;
    movie_ready_signalled=TRUE;
    SetEvent(movie_ready_start);
    wo_render_event("video_start_signalled",0);
}
/* The prepared intro player, now told to show and play; 0 if there is none
 * (or it is not usable), in which case the player is launched as before. */
static HANDLE take_prepared_movie(UINT movie,HWND window,HANDLE *skip) {
    if(!movie_ready_thread)return 0;
    /* Normally finished long ago; otherwise the work is already under way. */
    WaitForSingleObject(movie_ready_thread,INFINITE);
    CloseHandle(movie_ready_thread);movie_ready_thread=0;
    HANDLE process=movie_ready_process;
    BOOL usable=movie==0 && process && movie_ready_window==window && WaitForSingleObject(process,0)==WAIT_TIMEOUT;
    if(!usable) {
        /* A waiting player exits on its skip event without showing. */
        if(movie_ready_skip)SetEvent(movie_ready_skip);
        movie_ready_close();
        return 0;
    }
    start_prepared_intro();CloseHandle(movie_ready_start);
    if(movie_ready_stage)CloseHandle(movie_ready_stage);
    *skip=movie_ready_skip;
    movie_ready_process=movie_ready_skip=movie_ready_start=movie_ready_stage=0;
    return process;
}
static void play_game_movie(UINT movie) {
    if(!movie_root[0] || !movie_name(movie) || !movie_available(movie))return;
    WCHAR executable[MAX_PATH];
    if(!movie_executable(executable)){wo_render_event("video_player_missing",movie);return;}
    hide_game_cursor();HWND window=cursor_window;
    start_controller();
    BOOL music=wo_audio_movie_pause();
    HANDLE skip=0;
    PROCESS_INFORMATION child={0};
    child.hProcess=take_prepared_movie(movie,window,&skip);
    BOOL started=child.hProcess!=0;
    if(!started) {
        SECURITY_ATTRIBUTES inherit={sizeof(inherit),0,TRUE};
        skip=CreateEventW(&inherit,TRUE,FALSE,0);
        started=movie_spawn(movie,window,&skip,0,0,&child);
    }
    if(started){
        wo_render_event("video_started",movie);
        BOOL quit=FALSE,skipped=FALSE;int quit_code=0;
        for(;;){
            DWORD waited=WaitForSingleObject(child.hProcess,20);if(waited!=WAIT_TIMEOUT)break;
            MSG message;
            while(PeekMessageA(&message,0,0,0,PM_REMOVE)){
                if(message.message==WM_QUIT){quit=TRUE;quit_code=(int)message.wParam;continue;}
                if(skip && !skipped && movie_skip_key(&message,window)){SetEvent(skip);skipped=TRUE;wo_render_event("video_skip_key",(DWORD)message.wParam);}
                TranslateMessage(&message);DispatchMessageA(&message);
            }
        }
        DWORD result=1;GetExitCodeProcess(child.hProcess,&result);wo_render_event("video_exit_code",result);
        wo_render_event("video_game_focus_retained",!window || GetForegroundWindow()==window);
        CloseHandle(child.hProcess);if(window && GetForegroundWindow()!=window)SetForegroundWindow(window);
        if(quit)PostQuitMessage(quit_code);
    } else wo_render_event("video_launch_failed",movie);
    if(skip)CloseHandle(skip);
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
