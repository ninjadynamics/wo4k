/*60Hz ship dynamics with30Hz gameplay/event callbacks. Separate test switch. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "build/render_hooks.h"
#include "simulation_api.h"
#include "simulation_dynamics.c"
#include "simulation_drone.c"
#include "simulation_player.c"
void wo_render_event(const char *,DWORD);
typedef void (__cdecl *SimPlayerFn)(ShipData *,void *,short,void *);
typedef void (__cdecl *SimShipsFn)(ShipData *,void *,Object **,void *,int);
static SimPlayerFn old_sim_player,old_sim_drone;
static SimShipsFn old_sim_ships;
static void (__cdecl *old_sim_camera)(void *,ShipData *,void *);
static BOOL sim_enabled,sim_scope,sim_pending;
static DWORD sim_extras;
static unsigned short sim_pad_latched,sim_keys_latched;
static float sim_trail_displacement[15][3];
static const BYTE *sim_legacy_kinds[15];
static double sim_camera_vel[3],sim_camera_lift;
static short sim_camera_vel_stored[3];
static int sim_camera_lift_stored;
static BOOL sim_camera_valid;
static void sim_copy(BYTE *out,const BYTE *in,UINT count) {while(count--)*out++=*in++;}
static BOOL sim_equal(const BYTE *a,const BYTE *b,UINT count) {while(count--)if(*a++!=*b++)return FALSE;return TRUE;}
static void sim_jump(BYTE *entry,const void *target) {entry[0]=0xe9;*(LONG *)(entry+1)=(LONG)((const BYTE *)target-entry-5);}
static void *sim_hook(UINT rva,const BYTE *prefix,UINT bytes,const void *target) {
    BYTE *entry=sim_image+rva;DWORD protection,ignored;
    if(!sim_equal(entry,prefix,bytes))return 0;
    /* Never writable and executable at once: write, then seal as code. */
    BYTE *trampoline=VirtualAlloc(0,bytes+5,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    if(!trampoline)return 0;
    sim_copy(trampoline,entry,bytes);sim_jump(trampoline+bytes,entry+bytes);
    if(!VirtualProtect(trampoline,bytes+5,PAGE_EXECUTE_READ,&ignored)){VirtualFree(trampoline,0,MEM_RELEASE);return 0;}
    FlushInstructionCache(GetCurrentProcess(),trampoline,bytes+5);
    if(!VirtualProtect(entry,bytes,PAGE_EXECUTE_READWRITE,&protection)){VirtualFree(trampoline,0,MEM_RELEASE);return 0;}
    sim_jump(entry,target);for(UINT i=5;i<bytes;++i)entry[i]=0x90;
    VirtualProtect(entry,bytes,protection,&ignored);FlushInstructionCache(GetCurrentProcess(),entry,bytes);
    return trampoline;
}
static BOOL sim_supported(const BYTE *callback) {return callback==sim_image+0x63680 || callback==sim_image+0x59450;}
static void __cdecl sim_player(ShipData *ships,void *shapes,short no,void *weapons) {
    if(!sim_enabled || !sim_scope) {old_sim_player(ships,shapes,no,weapons);return;}
    sim_player_update(ships,shapes,no,weapons);
}
static void __cdecl sim_drone(ShipData *ships,void *shapes,short no,void *weapons) {
    if(!sim_enabled || !sim_scope) {old_sim_drone(ships,shapes,no,weapons);return;}
    sim_drone_update(ships,shapes,no,weapons);
}
typedef struct SimCamera {BYTE *bone;TrackSection *section;short hdg,pitch,roll,vhdg,vpitch,vroll;short velocity[4];} SimCamera;
static void __cdecl sim_camera(void *raw,ShipData *ship,void *droid) {
    if(!sim_enabled) {old_sim_camera(raw,ship,droid);return;}
    SimCamera *camera=raw;VECTOR pos,best,distance;
    pos.vx=ship->ppivot.vx-(ship->unitVecNose.vx>>2);
    pos.vy=ship->ppivot.vy-(ship->unitVecNose.vy>>2)-200;
    pos.vz=ship->ppivot.vz-(ship->unitVecNose.vz>>2);
    camera->section=((TrackSection *(*)(TrackSection *,VECTOR))(sim_image+0x41df0))(camera->section,pos);
    GetTargetPos(&best,pos,camera->section->nextSection.ptr->centre,camera->section->centre);
    distance.vx=pos.vx-best.vx;distance.vy=pos.vy-best.vy;distance.vz=pos.vz-best.vz;
    long force[3]={-(distance.vx>>6),-(distance.vy>>6),-(distance.vz>>6)};
    double r=sim_sqrt(.875),gain=.875/(1+r);
    for(UINT i=0;i<3;++i) {
        if(!sim_camera_valid || sim_camera_vel_stored[i]!=camera->velocity[i])sim_camera_vel[i]=camera->velocity[i];
        sim_camera_vel[i]=sim_camera_vel[i]*r+force[i]*gain;
        camera->velocity[i]=sim_camera_vel_stored[i]=(short)sim_camera_vel[i];
    }
    pos.vx+=camera->velocity[0];pos.vy+=camera->velocity[1];pos.vz+=camera->velocity[2];
    int dist=GetMagnitude(distance.vx,distance.vy,distance.vz);
    int *lift=(int *)(sim_image+0x89c58);
    if(!sim_camera_valid || *lift!=sim_camera_lift_stored)sim_camera_lift=*lift;
    r=sim_sqrt(.9375);gain=.9375/(1+r);
    sim_camera_lift=sim_camera_lift*r+(dist>>7)*gain;
    *lift=sim_camera_lift_stored=(int)sim_camera_lift;pos.vy-=*lift;sim_camera_valid=TRUE;
    ((void (*)(BYTE *,long,long,long))(sim_image+0x4d080))(camera->bone,-pos.vx,-pos.vy,-pos.vz);
    camera->pitch=-ship->pitch;camera->roll=0;camera->hdg=-ship->hdg;
    BYTE *parent=*(BYTE **)(camera->bone+0x44);
    ((void (*)(BYTE *,short,short,short))(sim_image+0x4d0a0))(parent,camera->pitch,camera->hdg,0);
    *(short *)(parent+0x40)=1;
}
void wo_sim_trail_shift(int no,float *out) {
    for(UINT i=0;i<3;++i)out[i]=sim_enabled && no>=0 && no<15?sim_trail_displacement[no][i]:0;
}
DWORD wo_sim_extra_serial(void) {return sim_extras;}
void wo_sim_latch_input(void) {
    sim_pad_latched|=padPressed;sim_keys_latched|=*(unsigned short *)(sim_image+0x65f7cc);
}
void wo_sim_restore_input(void) {
    if(!sim_enabled)return;
    padPressed|=sim_pad_latched;*(unsigned short *)(sim_image+0x65f7cc)|=sim_keys_latched;
    sim_pad_latched=sim_keys_latched=0;
}
BOOL wo_sim_wants_half_tick(void) {return sim_enabled && sim_pending && *(int *)(sim_image+0x518270)==-1;}
BOOL wo_sim_half_tick(void) {
    if(!wo_sim_wants_half_tick())return FALSE;
    sim_pending=FALSE;sim_dt=.5;sim_legacy=0;
    ShipData *ships=*(ShipData **)(sim_image+0x518030);
    Object **shapes=*(Object ***)(sim_image+0x517dcc);
    void *weapons=*(void **)(sim_image+0x518290);BOOL moved=FALSE;
    if(!ships || !shapes) {sim_legacy=1;return FALSE;}
    for(int order=0;order<activeShipCount && order<15;++order) {
        int no=shipOrder[order][0];if(no<0 || no>=15)continue;
        ShipData *ship=ships+no;const BYTE *callback=(const BYTE *)ship->update;
        if(!sim_supported(callback) || (ship->combatAttr&DESTROYED))continue;
        VECTOR before=ship->ppivot;
        if(callback==sim_image+0x63680)sim_player_update(ships,shapes,(short)no,weapons);
        else sim_drone_update(ships,shapes,(short)no,weapons);
        if(*(int *)(sim_image+0x65c364)!=1)
            ((void (*)(short,ShipData *,Object **))(sim_image+0x153d0))((short)no,ships,shapes);
        long dx=ship->ppivot.vx-before.vx,dy=ship->ppivot.vy-before.vy,dz=ship->ppivot.vz-before.vz;
        ship->moveDist=(short)(GetMagnitude(dx,dy,dz)*2);
        sim_trail_displacement[no][0]+=dx;sim_trail_displacement[no][1]+=dy;sim_trail_displacement[no][2]+=dz;
        moved=TRUE;
    }
    if(moved) {
        ((void (*)(ShipData *))(sim_image+0x3e240))(ships);
        void *camera=(void *)(sim_image+0x517bf0),*droid=(void *)(sim_image+0x517d80);
        if((BYTE *)CameraUpdate==sim_image+0x13f70)sim_camera(camera,ships+ownShip,droid);
        else if((BYTE *)CameraUpdate==sim_image+0x14200)
            ((void (*)(void *,ShipData *,void *))CameraUpdate)(camera,ships+ownShip,droid);
        ++sim_extras;
    }
    sim_legacy=1;return moved;
}
static void __cdecl sim_ships(ShipData *ships,void *bars,Object **shapes,void *weapons,int mode) {
    if(!sim_enabled) {old_sim_ships(ships,bars,shapes,weapons,mode);return;}
    /* Complete the other half before catch-up starts another legacy tick. */
    wo_sim_half_tick();
    sim_dt=.5;sim_legacy=1;sim_scope=TRUE;BOOL any=FALSE;
    for(UINT no=0;no<15;++no) {
        sim_legacy_kinds[no]=(const BYTE *)ships[no].update;
        if(sim_supported(sim_legacy_kinds[no]) && !(ships[no].combatAttr&DESTROYED))any=TRUE;
        for(UINT axis=0;axis<3;++axis)sim_trail_displacement[no][axis]=0;
    }
    old_sim_ships(ships,bars,shapes,weapons,mode);sim_scope=FALSE;
    for(UINT no=0;no<15;++no)if(sim_supported(sim_legacy_kinds[no]))ships[no].moveDist*=2;
    sim_pending=any;
}
void wo_sim_reset(void) {
    sim_pending=FALSE;sim_extras=0;sim_pad_latched=sim_keys_latched=0;sim_camera_valid=FALSE;
    for(UINT i=0;i<32;++i) {sim_fractions[i].ship=0;for(UINT j=0;j<120;++j)sim_fractions[i].valid[j]=0;}
    for(UINT i=0;i<15;++i)for(UINT j=0;j<3;++j)sim_trail_displacement[i][j]=0;
}
BOOL wo_sim_initialize(BYTE *image) {
    WCHAR enabled[8];if(!GetEnvironmentVariableW(L"WO2097_SIM60_TEST",enabled,8) || enabled[0]!=L'1')return FALSE;
    sim_image=image;
#define SIM_VERIFY(name) if(!sim_equal(image+RVA_##name,entry_##name,sizeof(entry_##name)))return FALSE
    SIM_VERIFY(sim_player);SIM_VERIFY(sim_drone);SIM_VERIFY(sim_ships);SIM_VERIFY(sim_camera);
#define SIM_HOOK(name,type) old_##name=(type)sim_hook(RVA_##name,entry_##name,sizeof(entry_##name),name)
    SIM_HOOK(sim_player,SimPlayerFn);SIM_HOOK(sim_drone,SimPlayerFn);SIM_HOOK(sim_ships,SimShipsFn);
    old_sim_camera=(void *)sim_hook(RVA_sim_camera,entry_sim_camera,sizeof(entry_sim_camera),sim_camera);
    sim_enabled=old_sim_player && old_sim_drone && old_sim_ships && old_sim_camera;
    wo_sim_reset();wo_render_event("simulation_60hz_ships",sim_enabled);return sim_enabled;
}
