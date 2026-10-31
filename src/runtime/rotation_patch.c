/* Keep the original integer rotations for gameplay. Record a separate float
 * rendering matrix from the exact same input angles, before trig amplitudes
 * and products are quantized to SHORT/12-bit fixed point. Cache reads require
 * the original matrix bytes and parent to match, and allocation/scene changes
 * invalidate entries. Nothing is written into a game skeleton by this code.
 */
#include "rotation_math.h"
typedef void (__cdecl *RotationFn)(BYTE *,SHORT,SHORT,SHORT);
typedef BYTE *(__cdecl *NewSkeletonFn)(void);
typedef void (__cdecl *SceneRadiusFn)(void *);
typedef void (__cdecl *ConvertFn)(float *,const BYTE *);
typedef struct RotationCache {
    const BYTE *skeleton,*parent;
    SHORT native[9];
    float rotation[9];
} RotationCache;
static RotationCache rotation_cache[2048];
static RotationFn old_direction_xyz,old_direction_hrp,old_direction_hpr,old_direction_rph;
static NewSkeletonFn old_new_skeleton;
static SceneRadiusFn old_scene_radius;
static ConvertFn old_convert_matrix;
static BOOL rotations_active;
static DWORD float_rotation_reads;
static UINT rotation_hash(const BYTE *skeleton) {return ((UINT)skeleton>>4)&2047;}
static void forget_rotation(const BYTE *skeleton) {
    UINT start=rotation_hash(skeleton);
    for(UINT i=0;i<8;++i) {
        RotationCache *entry=rotation_cache+((start+i)&2047);
        if(entry->skeleton==skeleton)entry->skeleton=0;
    }
}
static void record_rotation(const BYTE *skeleton,UINT order,SHORT a,SHORT b,SHORT c) {
    UINT start=rotation_hash(skeleton);RotationCache *chosen=0,*free=0;
    for(UINT i=0;i<8;++i) {
        RotationCache *entry=rotation_cache+((start+i)&2047);
        if(entry->skeleton==skeleton) {chosen=entry;break;}
        if(!entry->skeleton && !free)free=entry;
    }
    if(!chosen)chosen=free?free:rotation_cache+start;
    chosen->skeleton=skeleton;chosen->parent=*(const BYTE *const *)(skeleton+0x44);
    copy((BYTE *)chosen->native,skeleton,18);
    rotation_float(chosen->rotation,order,a,b,c);
}
static const float *cached_rotation(const BYTE *skeleton) {
    if(!rotations_active)return 0;
    UINT start=rotation_hash(skeleton);
    for(UINT i=0;i<8;++i) {
        const RotationCache *entry=rotation_cache+((start+i)&2047);
        if(entry->skeleton==skeleton && entry->parent==*(const BYTE *const *)(skeleton+0x44) &&
           equal((const BYTE *)entry->native,skeleton,18)) {++float_rotation_reads;return entry->rotation;}
    }
    return 0;
}
static const BYTE *matrix_owner(const BYTE *matrix) {
    if(refresh_active && matrix==refresh_last_matrix)return refresh_last_skeleton;
    /* GetSkeletonMatrix returns absolute at +32. A caller explicitly passing
     * the relative matrix must retain that local-space meaning. */
    const BYTE *candidate=matrix-32;
    UINT start=rotation_hash(candidate);
    for(UINT i=0;i<8;++i) {
        const RotationCache *entry=rotation_cache+((start+i)&2047);
        if(entry->skeleton==candidate && cached_rotation(candidate))return candidate;
    }
    return 0;
}
static void __cdecl convert_matrix(float *out,const BYTE *matrix) {
    update_projection();old_convert_matrix(out,matrix);
    if(rotations_active && out && matrix) {
        const BYTE *skeleton=matrix_owner(matrix);
        if(skeleton)precise_transform(out,skeleton,0);
    }
}
static void direction(RotationFn original,UINT order,BYTE *skeleton,SHORT a,SHORT b,SHORT c) {
    original(skeleton,a,b,c);
    if(rotations_active)record_rotation(skeleton,order,a,b,c);
}
static void __cdecl direction_xyz(BYTE *s,SHORT a,SHORT b,SHORT c) {direction(old_direction_xyz,0,s,a,b,c);}
static void __cdecl direction_hrp(BYTE *s,SHORT a,SHORT b,SHORT c) {direction(old_direction_hrp,1,s,a,b,c);}
static void __cdecl direction_hpr(BYTE *s,SHORT a,SHORT b,SHORT c) {direction(old_direction_hpr,2,s,a,b,c);}
static void __cdecl direction_rph(BYTE *s,SHORT a,SHORT b,SHORT c) {direction(old_direction_rph,3,s,a,b,c);}
static BYTE *__cdecl new_skeleton(void) {
    BYTE *skeleton=old_new_skeleton();
    if(skeleton)forget_rotation(skeleton);
    return skeleton;
}
static void __cdecl scene_radius(void *objects) {
    for(UINT i=0;i<2048;++i)rotation_cache[i].skeleton=0;
    old_scene_radius(objects);
}
static void initialize_rotations(const WCHAR *settings) {
    if(!GetPrivateProfileIntW(L"Render",L"FloatRotations",1,settings))return;
#define ROTATION_VERIFY(name) if(!equal(game+RVA_##name,entry_##name,sizeof(entry_##name)))return
    ROTATION_VERIFY(direction_xyz);ROTATION_VERIFY(direction_hrp);ROTATION_VERIFY(direction_hpr);ROTATION_VERIFY(direction_rph);
    ROTATION_VERIFY(new_skeleton);ROTATION_VERIFY(scene_radius);
    ROTATION_VERIFY(convert_matrix);
#define ROTATION_INSTALL(name,type) old_##name=(type)hook(RVA_##name,entry_##name,sizeof(entry_##name),name)
    ROTATION_INSTALL(direction_xyz,RotationFn);ROTATION_INSTALL(direction_hrp,RotationFn);
    ROTATION_INSTALL(direction_hpr,RotationFn);ROTATION_INSTALL(direction_rph,RotationFn);
    ROTATION_INSTALL(new_skeleton,NewSkeletonFn);ROTATION_INSTALL(scene_radius,SceneRadiusFn);
    ROTATION_INSTALL(convert_matrix,ConvertFn);
    rotations_active=old_direction_xyz && old_direction_hrp && old_direction_hpr && old_direction_rph && old_new_skeleton && old_scene_radius;
    rotations_active=rotations_active && old_convert_matrix;
    wo_render_event("render_float_rotations_test",rotations_active);
}
