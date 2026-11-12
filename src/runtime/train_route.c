/* Gare d'Europa's trains travel the line of pylons beside the track. The game
 * measures that line between the two pylons its scenery names "pylon16" and
 * "pylon0". The PlayStation's model has the same seventeen pylons, every one
 * of them named "pylon": the game found neither, measured a line of no
 * length and left the trains standing where they wait.
 *
 * Where the scenery does not name them, the ends are the two pylons furthest
 * apart, and the line runs to the one beside the waiting trains from the one
 * at the far end, as it does between the named ones. Its fall is measured
 * between the pylons' tops, which carry the rail: their origins sit at mid
 * height, and the PlayStation's pylons are not all one height. */
#define TRAIN_PYLONS 32
typedef void (__cdecl *TrainRouteFn)(BYTE **,int,BYTE *);
static TrainRouteFn old_train_route;
static BOOL pylon_named(const BYTE *pylon,const char *name,UINT length) {
    for(UINT i=0;i<length;++i)if(pylon[i]!=(BYTE)name[i])return FALSE;
    return TRUE;
}
/* A pylon's origin, raised to its highest corner (up is negative). The
 * corners are the ones the game draws: 32 bytes each, in 4096ths. */
static void pylon_top(const BYTE *pylon,float *top) {
    const LONG *origin=(const LONG *)(*(const BYTE *const *)(pylon+0x30)+0x14);
    const float *corners=*(const float *const *)(pylon+0x2c);int count=*(const SHORT *)(pylon+0x10);
    float highest=corners && count>0?corners[1]:0;
    for(int i=1;corners && i<count;++i)if(corners[i*8+1]<highest)highest=corners[i*8+1];
    top[0]=(float)origin[0];top[1]=(float)origin[1]+highest*4096;top[2]=(float)origin[2];
}
static BOOL measure_train_route(BYTE **pylons,int count,const LONG *waiting,BYTE *route) {
    if(count<2 || count>TRAIN_PYLONS)return FALSE;
    BOOL first=FALSE,last=FALSE;
    for(int i=0;i<count;++i) {
        if(pylon_named(pylons[i],"pylon16",7))first=TRUE;
        if(pylon_named(pylons[i],"pylon0",6))last=TRUE;
    }
    if(first && last)return FALSE;
    float tops[TRAIN_PYLONS][3],widest=-1;int from=0,to=1;
    for(int i=0;i<count;++i)pylon_top(pylons[i],tops[i]);
    for(int i=0;i<count;++i)for(int j=i+1;j<count;++j) {
        float x=tops[j][0]-tops[i][0],z=tops[j][2]-tops[i][2],apart=x*x+z*z;
        if(apart>widest){widest=apart;from=i;to=j;}
    }
    float to_x=tops[to][0]-(float)waiting[0],to_z=tops[to][2]-(float)waiting[2];
    float from_x=tops[from][0]-(float)waiting[0],from_z=tops[from][2]-(float)waiting[2];
    if(from_x*from_x+from_z*from_z<to_x*to_x+to_z*to_z){int other=from;from=to;to=other;}
    /* As the game leaves it: the lap last seen, a 200th of the line, the line. */
    LONG *step=(LONG *)(route+4),*line=(LONG *)(route+0x24);
    for(UINT axis=0;axis<3;++axis) {
        line[axis]=(LONG)(tops[to][axis]-tops[from][axis]);step[axis]=line[axis]/200;
    }
    *(WORD *)route=0;
    return TRUE;
}
static void __cdecl train_route(BYTE **pylons,int count,BYTE *route) {
    old_train_route(pylons,count,route);
    /* The game passes the count as a 16-bit value in a 32-bit slot. */
    if(measure_train_route(pylons,(SHORT)count,(const LONG *)(game+0x5180b0),route))
        wo_render_event("train_route_measured",(DWORD)(SHORT)count);
}
static void initialize_train_route(void) {
    static const BYTE entry[]={0x83,0xec,0x10,0x53,0x56};
    old_train_route=(TrainRouteFn)hook(0x11810,entry,sizeof(entry),train_route);
    wo_render_event("train_route_hook",old_train_route!=0);
}
