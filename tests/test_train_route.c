/* Gare d'Europa's train line, measured where the scenery does not name the
 * pylons at its ends (the PlayStation's model names all seventeen "pylon"). */
typedef struct FixturePylon {BYTE object[0x40];BYTE skeleton[0x48];float corners[2][8];} FixturePylon;
static UINT train_route_native_calls;
static void __cdecl fixture_train_route(BYTE **pylons,int count,BYTE *route){
    (void)pylons;(void)count;++train_route_native_calls;
    for(UINT i=0;i<0x30;++i)route[i]=0; /* what the game measures between two pylons it cannot find */
}
static void fixture_pylon(FixturePylon *pylon,const char *name,LONG x,LONG y,LONG z,int top){
    for(UINT i=0;i<sizeof(*pylon);++i)((BYTE *)pylon)[i]=0;
    for(UINT i=0;name[i];++i)pylon->object[i]=(BYTE)name[i];
    *(SHORT *)(pylon->object+0x10)=2;
    *(float **)(pylon->object+0x2c)=&pylon->corners[0][0];*(BYTE **)(pylon->object+0x30)=pylon->skeleton;
    LONG *origin=(LONG *)(pylon->skeleton+0x14);origin[0]=x;origin[1]=y;origin[2]=z;
    pylon->corners[0][1]=(float)-top/4096;pylon->corners[1][1]=(float)top/4096;
}
static void test_train_route(BYTE *image){
    /* Five of the PlayStation's pylons in the order the game lists them:
     * the far end is neither first nor last. Origins, and how far the top
     * stands above each. */
    static FixturePylon pylons[5];BYTE *list[5];
    fixture_pylon(pylons+0,"pylon",80598,-529,-29650,-5649);
    fixture_pylon(pylons+1,"pylon",59836,3995,-75586,-5261);
    fixture_pylon(pylons+2,"pylon",77603,374,-36285,-5846);
    fixture_pylon(pylons+3,"pylon",104225,-5907,22634,-5839);
    fixture_pylon(pylons+4,"pylon",107123,-6590,29047,-5839);
    for(UINT i=0;i<5;++i)list[i]=pylons[i].object;
    const LONG waiting[3]={98791,-8592,10949};
    BYTE route[0x30];for(UINT i=0;i<sizeof(route);++i)route[i]=0x55;
    if(!measure_train_route(list,5,waiting,route))ExitProcess(107);
    const LONG *step=(const LONG *)(route+4),*line=(const LONG *)(route+0x24);
    /* From the far pylon's top to the top of the one by the trains. */
    if(*(WORD *)route || line[0]!=47287 || line[1]!=-11163 || line[2]!=104633 ||
       step[0]!=236 || step[1]!=-55 || step[2]!=523)ExitProcess(108);
    /* The PC's model names its ends; what the game measured stands. */
    fixture_pylon(pylons+1,"pylon16",59560,1615,-76514,-2783);
    fixture_pylon(pylons+4,"pylon0",106882,-9691,28217,-2650);
    for(UINT i=0;i<sizeof(route);++i)route[i]=0x55;
    if(measure_train_route(list,5,waiting,route))ExitProcess(109);
    for(UINT i=0;i<sizeof(route);++i)if(route[i]!=0x55)ExitProcess(109);
    /* Through the hook, after the game's own measurement. */
    fixture_pylon(pylons+1,"pylon",59836,3995,-75586,-5261);
    fixture_pylon(pylons+4,"pylon",107123,-6590,29047,-5839);
    TrainRouteFn saved=old_train_route;LONG saved_waiting[3];
    for(UINT i=0;i<3;++i){saved_waiting[i]=*(LONG *)(image+0x5180b0+i*4);*(LONG *)(image+0x5180b0+i*4)=waiting[i];}
    old_train_route=fixture_train_route;train_route(list,0x70005,route);old_train_route=saved;
    for(UINT i=0;i<3;++i)*(LONG *)(image+0x5180b0+i*4)=saved_waiting[i];
    if(train_route_native_calls!=1 || line[0]!=47287 || line[1]!=-11163 || line[2]!=104633 || step[2]!=523)ExitProcess(147);
    if(!saved || image[0x11810]!=0xe9)ExitProcess(147);
}
