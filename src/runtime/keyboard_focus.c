/* Keep the native DirectInput keyboard working across Alt+Tab.
 * Windows unacquires the game's foreground keyboard whenever another window
 * takes the foreground. Native PollKeyboard (28f10) reacquires only on
 * DIERR_INPUTLOST, which it reads once, on its first poll in the background,
 * where Acquire must fail; every later read returns DIERR_NOTACQUIRED, so the
 * keyboard stayed dead after returning to the game. Key releases made while
 * unfocused never arrive either, so held keys are dropped when focus goes. */
typedef LONG (__stdcall *KeyboardAcquireFn)(void *);
static BOOL keyboard_unfocused;
static void keyboard_follow_focus(BOOL focused) {
    if(!focused) {
        if(keyboard_unfocused)return;
        keyboard_unfocused=TRUE;
        *(SHORT *)(game+0x66496a)=-1;  /* last key code */
        *(WORD *)(game+0x65f7ce)=0x80; /* centred keyboard steering */
        *(WORD *)(game+0x8bdac)=0;     /* held game keys */
        *(WORD *)(game+0x8bdb0)=0;     /* held mouse buttons */
        *(DWORD *)(game+0x8bc44)=*(DWORD *)(game+0x8bc48)=0; /* steering keys */
        wo_render_event("input_focus",0);
        return;
    }
    if(!keyboard_unfocused)return;
    void **device=*(void ***)(game+0x518e78);
    /* IDirectInputDevice::Acquire; a refusal is retried on the next poll. */
    if(device && ((KeyboardAcquireFn)((void **)*device)[7])(device)<0)return;
    keyboard_unfocused=FALSE;
    wo_render_event("input_focus",1);
}
static void keyboard_focus(void) {
    DWORD foreground=0;GetWindowThreadProcessId(GetForegroundWindow(),&foreground);
    keyboard_follow_focus(foreground==GetCurrentProcessId());
}
