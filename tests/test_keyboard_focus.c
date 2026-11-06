/* Alt+Tab: held keys are dropped when focus goes, and the native keyboard is
 * reacquired on return, retrying while Windows still refuses. */
static UINT keyboard_acquires,keyboard_refusals;
static LONG __stdcall fixture_keyboard_acquire(void *device) {
    (void)device;++keyboard_acquires;
    if(keyboard_refusals){--keyboard_refusals;return (LONG)0x80070005;} /* DIERR_OTHERAPPHASPRIO */
    return 0;
}
static void test_keyboard_focus(BYTE *image) {
    void *vtable[11]={0};vtable[7]=(void *)fixture_keyboard_acquire;
    void *device[1]={vtable};
    void *saved=*(void **)(image+0x518e78);
    *(void **)(image+0x518e78)=device;
    keyboard_unfocused=FALSE;keyboard_acquires=0;
    keyboard_follow_focus(TRUE);if(keyboard_acquires)ExitProcess(173);
    *(SHORT *)(image+0x66496a)=0x1e;*(WORD *)(image+0x65f7ce)=0;
    *(WORD *)(image+0x8bdac)=0x14;*(WORD *)(image+0x8bdb0)=1;
    *(DWORD *)(image+0x8bc44)=*(DWORD *)(image+0x8bc48)=1;
    keyboard_follow_focus(FALSE);
    if(*(SHORT *)(image+0x66496a)!=-1 || *(WORD *)(image+0x65f7ce)!=0x80 || *(WORD *)(image+0x8bdac) ||
       *(WORD *)(image+0x8bdb0) || *(DWORD *)(image+0x8bc44) || *(DWORD *)(image+0x8bc48) || keyboard_acquires)ExitProcess(174);
    /* Input read while away stays the native code's business. */
    *(WORD *)(image+0x8bdac)=0x20;keyboard_follow_focus(FALSE);
    if(*(WORD *)(image+0x8bdac)!=0x20 || keyboard_acquires)ExitProcess(175);
    keyboard_refusals=1;keyboard_follow_focus(TRUE);
    if(keyboard_acquires!=1 || !keyboard_unfocused)ExitProcess(176);
    keyboard_follow_focus(TRUE);if(keyboard_acquires!=2 || keyboard_unfocused)ExitProcess(177);
    keyboard_follow_focus(TRUE);if(keyboard_acquires!=2)ExitProcess(178);
    /* Without DirectInput the game reads window messages: nothing to acquire. */
    *(void **)(image+0x518e78)=0;
    keyboard_follow_focus(FALSE);keyboard_follow_focus(TRUE);
    if(keyboard_unfocused || keyboard_acquires!=2)ExitProcess(179);
    *(void **)(image+0x518e78)=saved;
}
