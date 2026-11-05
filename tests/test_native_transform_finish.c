/* Run the original x86 closing routine, including its retained pointer into
 * the instruction buffer. A parser/GPU test alone misses pointer relocation. */
static UINT native_transform_unlocks;
static BOOL native_transform_corrupt_probe;
static BOOL native_transform_ps1_probe;
static UINT native_transform_corruption_caught;
static BYTE *native_transform_process,*native_transform_stream;
static HRESULT WINAPI fixture_transform_unlock(IDirect3DExecuteBuffer *unused) {
    (void)unused;++native_transform_unlocks;
    BYTE *p=native_transform_process;
    if(p[0]!=9 || p[1]!=16 || *(WORD *)(p+2)!=1 || *(DWORD *)(p+4)!=1 ||
       *(DWORD *)(p+12)!=(native_transform_ps1_probe?6u:3u) || *(DWORD *)(p+16))ExitProcess(150);
    p+=20;
    if(native_transform_corrupt_probe){
        /* The old prefix moved the real placeholder here; the native closer
         * wrote a second packet into the preceding commands instead. */
        if(p[0]!=9 || *(DWORD *)(p+12)!=0)ExitProcess(160);
        ++native_transform_corruption_caught;return 0;
    }
    if(p[0]!=8 || p[1]!=8 || *(WORD *)(p+2)!=1 || *(DWORD *)(p+4)!=15 || *(DWORD *)(p+8))ExitProcess(151);
    BYTE *end=*(BYTE **)(game+0x631d54);UINT triangles=0;
    for(p=native_transform_stream;p<end;){
        if(end-p<4)ExitProcess(152);
        UINT opcode=p[0],size=p[1],n=*(WORD *)(p+2),bytes=4+size*n;
        if(bytes>(UINT)(end-p))ExitProcess(153);
        if(opcode==3){
            UINT base=native_transform_ps1_probe && triangles!=1?3:0;
            if(size!=8 || n!=1 || *(WORD *)(p+4)!=base || *(WORD *)(p+6)!=base+1 || *(WORD *)(p+8)!=base+2)ExitProcess(154);
            ++triangles;
        }
        if(opcode==11 && (bytes!=4 || p+4!=end))ExitProcess(155);
        p+=bytes;
    }
    if(triangles!=(native_transform_ps1_probe?3u:1u) || end[-4]!=11)ExitProcess(156);
    return 0;
}
static void test_native_transform_finish(BYTE *image) {
    /* Only the fade-disabled path is executed. Rebase its exact absolute
     * operands in this privately mapped image; relocated queue references
     * already point at the production renderer's allocated arrays. */
    const UINT refs[]={0x1c7ca,0x1c7d4,0x1c7dc,0x1c7e5,0x1c7ed,0x1c7f2,
        0x1c803,0x1c80f,0x1c82c,0x1c833,0x1c83f,0x1c844};
    DWORD saved_refs[sizeof(refs)/sizeof(refs[0])],delta=(DWORD)(UINT_PTR)image-0x400000;
    for(UINT i=0;i<sizeof(refs)/sizeof(refs[0]);++i){
        DWORD *operand=(DWORD *)(image+refs[i]);saved_refs[i]=*operand;
        if(*operand>=0x400000 && *operand<0xbee000)*operand+=delta;
    }
    DWORD *fade_operand=(DWORD *)((BYTE *)old_finish_transform+4),saved_fade_operand=*fade_operand;
    if(*fade_operand!=0x919290)ExitProcess(157);*fade_operand+=delta;
    const UINT globals[]={0x53f190,0x53f1a0,0x631d54,0x631d50,0xc8610,0x63f820,0x631d48,0x8f8dc};
    DWORD saved_globals[sizeof(globals)/sizeof(globals[0])];
    for(UINT i=0;i<sizeof(globals)/sizeof(globals[0]);++i)saved_globals[i]=*(DWORD *)(image+globals[i]);
    BYTE saved_fade=image[0x519290];BOOL saved_shadows=shadows_active,saved_exhaust=exhaust_active,saved_race=refresh_race;
    DWORD saved_buffer=distance_execute[0];
    static IDirect3DExecuteBufferVtbl vtable={.Unlock=fixture_transform_unlock};
    IDirect3DExecuteBuffer execute={&vtable};distance_execute[0]=(DWORD)(UINT_PTR)&execute;
    static BYTE storage[2048];LitVertex *vertices=(LitVertex *)storage;
    const LitVertex original={1,2,3,0,0xff402010,0,.25f,.75f};
    for(UINT i=0;i<3;++i)vertices[i]=original;
    BYTE *stream=storage+1536;UINT length=0;
    length+=scene_test_state(stream+length,7,1);length+=scene_test_state(stream+length,14,1);length+=scene_test_state(stream+length,27,0);
    native_transform_process=stream+length;native_transform_stream=stream;
    for(UINT i=0;i<20;++i)stream[length+i]=0;
    stream[length]=9;stream[length+1]=16;*(WORD *)(stream+length+2)=1;length+=20;
    length+=scene_test_state(stream+length,1,123);length+=scene_test_triangle(stream+length,0,1,2);
    *(BYTE **)(image+0x53f190)=storage;*(BYTE **)(image+0x53f1a0)=stream;*(BYTE **)(image+0x631d54)=stream+length;
    *(UINT *)(image+0x631d50)=3;*(UINT *)(image+0xc8610)=sizeof(storage);
    *(BYTE **)(image+0x63f820)=native_transform_process;*(DWORD *)(image+0x631d48)=0;*(DWORD *)(image+0x8f8dc)=0;
    image[0x519290]=0;shadows_active=exhaust_active=refresh_race=FALSE;
    /* Reproduce the broken prepend with the original native epilogue before
     * verifying the corrected production path. This is never sent to a GPU. */
    BYTE native_input[128];copy(native_input,stream,length);UINT prefixed=20;
    const BYTE old_reset[20]={8,8,2,0,15,0,0,0,0,0,0,0,41,0,0,0,0,0,0,0};copy(stream,old_reset,20);
    copy(stream+prefixed,native_input,length);*(BYTE **)(image+0x631d54)=stream+prefixed+length;
    native_transform_corrupt_probe=TRUE;old_finish_transform();native_transform_corrupt_probe=FALSE;
    if(native_transform_corruption_caught!=1)ExitProcess(162);
    copy(stream,native_input,length);*(BYTE **)(image+0x631d54)=stream+length;
    *(BYTE **)(image+0x63f820)=native_transform_process;
    finish_transform();
    if(native_transform_unlocks!=2 || *(BYTE **)(image+0x631d54)!=stream+length+16)ExitProcess(158);
    for(UINT i=0;i<3;++i)if(!equal((const BYTE *)(vertices+i),(const BYTE *)&original,sizeof(original)))ExitProcess(159);
    copy(stream,native_input,length);*(BYTE **)(image+0x631d54)=stream+length;
    *(BYTE **)(image+0x63f820)=native_transform_process;*(UINT *)(image+0x631d50)=3;
    for(UINT i=0;i<3;++i)vertices[i].reserved=PS1_SUB_VERTEX_TAG;
    native_transform_ps1_probe=TRUE;finish_transform();native_transform_ps1_probe=FALSE;
    if(native_transform_unlocks!=3 || *(UINT *)(image+0x631d50)!=6)ExitProcess(240);
    for(UINT i=0;i<3;++i)if(vertices[i].reserved || vertices[i].color!=original.color || vertices[i+3].reserved || vertices[i+3].color!=0xffffffff)ExitProcess(241);
    /* The retained native PROCESSVERTICES pointer must also survive HD world
     * texture/state packets, with no additional or relocated vertices. */
    copy(stream,native_input,length);*(BYTE **)(image+0x631d54)=stream+length;
    *(BYTE **)(image+0x63f820)=native_transform_process;*(UINT *)(image+0x631d50)=3;
    for(UINT i=0;i<3;++i)vertices[i]=original;
    ui_hd_pages[100].handle=123;ui_hd_pages[100].world=TRUE;ui_hd_opaque_pages[100].handle=124;
    finish_transform();
    if(native_transform_unlocks!=4 || *(UINT *)(image+0x631d50)!=3)ExitProcess(242);
    ui_hd_pages[100]=(UiHdPage){0};ui_hd_opaque_pages[100]=(UiHdPage){0};
    for(UINT i=0;i<sizeof(globals)/sizeof(globals[0]);++i)*(DWORD *)(image+globals[i])=saved_globals[i];
    image[0x519290]=saved_fade;shadows_active=saved_shadows;exhaust_active=saved_exhaust;refresh_race=saved_race;
    distance_execute[0]=saved_buffer;*fade_operand=saved_fade_operand;
    for(UINT i=0;i<sizeof(refs)/sizeof(refs[0]);++i)*(DWORD *)(image+refs[i])=saved_refs[i];
}
