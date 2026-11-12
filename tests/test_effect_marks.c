/* Trail markers live in vertices the game hands out again every frame, to
 * the track before anything else. They must be gone once the frame is drawn:
 * a marker that stays makes the slot's next owner draw as a trail. */
static UINT effect_mark_finishes;
static void __cdecl fixture_effect_finish(void){++effect_mark_finishes;}
static void test_effect_marks(BYTE *image){
    static LitVertex store[24];
    BOOL saved_exhaust=exhaust_active;int saved_depth=trail_depth;FinishFn saved_finish=old_finish_render;
    BYTE *saved_buffer=*(BYTE **)(image+0x7c9690);
    exhaust_active=TRUE;trail_depth=1;old_finish_render=fixture_effect_finish;*(BYTE **)(image+0x7c9690)=0;
    for(UINT i=0;i<24;++i)store[i]=(LitVertex){.color=0x80112233};
    prepare_trail_vertices(store,12);prepare_trail_vertices(store+12,6);
    for(UINT i=0;i<24;++i)if(store[i].reserved!=(i<18?TRAIL_VERTEX_TAG:0))ExitProcess(61);
    /* The frame ends with the screen pass; the next one gives these slots
     * to the track, whose writer leaves the marker field alone. */
    finish_render();
    if(effect_mark_finishes!=1 || trail_mark_count)ExitProcess(62);
    for(UINT i=0;i<24;++i)if(store[i].reserved || (store[i].color&0xffffff)!=0x112233)ExitProcess(62);
    /* A slot something else has marked since is not a trail's to unmark. */
    prepare_trail_vertices(store,6);store[2].reserved=SKY_VERTEX_TAG;
    finish_render();
    if(store[2].reserved!=SKY_VERTEX_TAG || store[0].reserved || store[5].reserved)ExitProcess(63);
    store[2].reserved=0;
    /* More trails than can be remembered stay as the game draws them. */
    for(UINT i=0;i<TRAIL_MARK_CAPACITY;++i)prepare_trail_vertices(store,6);
    prepare_trail_vertices(store+6,6);
    if(store[0].reserved!=TRAIL_VERTEX_TAG || store[6].reserved)ExitProcess(64);
    finish_render();
    for(UINT i=0;i<24;++i)if(store[i].reserved)ExitProcess(64);
    exhaust_active=saved_exhaust;trail_depth=saved_depth;old_finish_render=saved_finish;
    *(BYTE **)(image+0x7c9690)=saved_buffer;
}
