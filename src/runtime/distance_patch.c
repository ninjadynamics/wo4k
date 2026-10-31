/* Included by render_patch.c. Full-scene experiment, separately gated by
 * WO2097_DISTANCE_TEST=1. Uses original track tessellation and D3D submission.
 * Larger frame storage plus bounded native batches replace visibility limits.
 */
#include "build/render_refs.h"
#define DISTANCE_QUEUE_CAPACITY 4096
#define DISTANCE_EXECUTE_CAPACITY 256
typedef void (__cdecl *FrameFn)(void);
typedef void (__cdecl *TrackFn)(BYTE *);
static TrackFn old_new_track,old_all_track;
static FrameFn old_render_queue,old_release_matrices;
static BYTE *distance_queue,*distance_primitives;
static BYTE *distance_ordered;
static DWORD *distance_execute,*distance_states;
static DWORD retained_matrices[DISTANCE_QUEUE_CAPACITY*3];
static UINT retained_count;
static BOOL have_matrices;
static DWORD distance_batches,distance_sections;
static DWORD track_sections_last,track_faces_last,track_vertices_last,track_incomplete_last;

static BOOL valid_refs(const OperandRef *refs,UINT n) {
    for(UINT i=0;i<n;++i)if(*(UINT *)(game+refs[i].rva)!=refs[i].expected)return FALSE;
    return TRUE;
}
static BOOL write_operand(UINT rva,UINT value) {
    DWORD old,ignored;
    if(!VirtualProtect(game+rva,4,PAGE_EXECUTE_READWRITE,&old))return FALSE;
    *(UINT *)(game+rva)=value;
    VirtualProtect(game+rva,4,old,&ignored);
    return TRUE;
}
static BOOL relocate_refs(const OperandRef *refs,UINT n,BYTE *base) {
    for(UINT i=0;i<n;++i)
        if(!write_operand(refs[i].rva,(UINT)(base+refs[i].offset)))return FALSE;
    return TRUE;
}
static void restore_refs(const OperandRef *refs,UINT n) {
    for(UINT i=0;i<n;++i)write_operand(refs[i].rva,refs[i].expected);
}

static void draw_complete_track(BYTE *camera) {
    BYTE *track=*(BYTE **)(game+0x65f794);
    typedef BYTE *(__cdecl *DrawFn)(BYTE *,BYTE *,SHORT);
    DrawFn draw=(DrawFn)(game+0x1d2a0);
    FrameFn next=(FrameFn)(game+0x1d290);
    next();
    if(!track)return;
    int count=*(int *)(track+8);
    BYTE *sections=*(BYTE **)(track+0x14),*skeleton=*(BYTE **)camera;
    track_sections_last=track_faces_last=track_vertices_last=track_incomplete_last=0;
    backdrop_count=0;
    /* One section per native call keeps temporary per-texture face buckets
     * bounded, including long tracks. Every original face is submitted. */
    for(int i=0;i<count;++i) {
        int before=*(int *)(game+0x53f19c);
        draw(sections+i*156,skeleton,1);
        int after=*(int *)(game+0x53f19c);
        UINT faces=*(WORD *)(sections+i*156+0x90);
        if(before>=0 && after==before+1 && after<=DISTANCE_QUEUE_CAPACITY) {
            UINT vertices=*(UINT *)(render_entries+before*96+0x4c);
            track_vertices_last+=vertices;
            if(vertices==faces*4) {++track_sections_last;track_faces_last+=faces;}
            else ++track_incomplete_last;
        } else ++track_incomplete_last;
        ++distance_sections;
    }
}
static void __cdecl new_track(BYTE *camera) {
    if(distance_active)draw_complete_track(camera);else old_new_track(camera);
}
static void __cdecl all_track(BYTE *camera) {
    if(distance_active)draw_complete_track(camera);else old_all_track(camera);
}

/* Independent limits in the preserved native renderer: 7000 source vertices,
 * 1800 triangles in EACH texture/blend bucket, 1800 temporary billboard tris.
 * Count both opaque/translucent together, conservatively, before appending.
 */
static BOOL entry_budget(const BYTE *entry,UINT *vertices,UINT *triangles) {
    int count=*(const int *)(entry+0x4c),quads=*(const int *)(entry+0x40);
    const BYTE *lists=*(const BYTE *const *)(entry+0x50);
    const BYTE *sprites=*(const BYTE *const *)(entry+0x44);
    UINT sets=*(WORD *)(game+0xc8620)+1;
    if(count<0 || quads<0 || sets>32)return FALSE;
    *vertices=(UINT)count+(UINT)quads*4;
    for(UINT j=0;j<32;++j)triangles[j]=0;
    for(UINT j=0;j<sets;++j) {
        if(lists)triangles[j]=*(const UINT *)(lists+j*8);
    }
    for(int j=0;j<quads;++j) {
        UINT set=*(const UINT *)(sprites+j*32+8);
        if(set>=sets)return FALSE;
        triangles[set]+=2;
    }
    return TRUE;
}
static void retain_current_matrices(void) {
    if(!have_matrices)return;
    retained_matrices[retained_count++]=*(DWORD *)(game+0x53f198);
    retained_matrices[retained_count++]=*(DWORD *)(game+0x53f1d4);
    retained_matrices[retained_count++]=*(DWORD *)(game+0x53f1b0);
}
static UINT render_stage(const BYTE *entry) {
    const void *vertices=*(const void *const *)(entry+0x48);
    if(backdrop_count && vertices==backdrop_vertices)return 0;
    if(*(UINT *)(entry+0x54))return 3;
    if(shadows_active && vertices==shadow_vertices)return 2;
    return 1;
}
static void order_render_effects(UINT total) {
    UINT output=0;
    for(UINT stage=0;stage<4;++stage)for(UINT i=0;i<total;++i)
        if(render_stage(distance_queue+i*96)==stage) {
            copy(distance_ordered+output*96,distance_queue+i*96,96);++output;
        }
    copy(distance_queue,distance_ordered,total*96);
}
static void __cdecl render_queue(void) {
    if(!distance_active) {old_render_queue();return;}
    UINT total=*(UINT *)(game+0x53f19c),start=0;
    if(exhaust_active && total<=DISTANCE_QUEUE_CAPACITY)order_render_effects(total);
    DWORD fade_before=*(DWORD *)(game+0x8b2d0);
    if(!total) {
        retain_current_matrices();old_render_queue();have_matrices=TRUE;return;
    }
    while(start<total) {
        UINT end=start,vertices=0,billboard_vertices=0,triangles[32];
        for(UINT j=0;j<32;++j)triangles[j]=0;
        while(end<total) {
            /* Opaque geometry must finish before shadow and additive-effect
             * batches. Disabling effect depth writes alone cannot provide
             * correct compositing when later opaque batches overdraw them. */
            if(exhaust_active && end>start && render_stage(distance_queue+end*96)!=render_stage(distance_queue+start*96))break;
            UINT add_vertices,add_triangles[32];BOOL fits=TRUE;
            if(!entry_budget(distance_queue+end*96,&add_vertices,add_triangles))break;
            if(vertices+add_vertices>6500)fits=FALSE;
            UINT add_billboards=*(UINT *)(distance_queue+end*96+0x40)*4;
            if(billboard_vertices+add_billboards>1000)fits=FALSE;
            for(UINT j=0;j<32;++j)if(triangles[j]+add_triangles[j]>1800)fits=FALSE;
            if(!fits)break;
            vertices+=add_vertices;
            billboard_vertices+=add_billboards;
            for(UINT j=0;j<32;++j)triangles[j]+=add_triangles[j];
            ++end;
        }
        if(end==start) {
            /* An individual model too large for the original renderer is an
             * explicit diagnostic, never an unchecked native buffer write. */
            wo_render_event("distance_model_exceeds_native_capacity",start);
            break;
        }
        /* Destination precedes source, so forward copy is overlap-safe. All
         * entries after this batch remain intact for the next iteration. */
        if(start)copy(distance_queue,distance_queue+start*96,(end-start)*96);
        *(UINT *)(game+0x53f19c)=end-start;
        /* Native high-brightness fades skip alternate FRAMES. Every batch
         * must see the same prior-frame flag, rather than alternate batches. */
        *(DWORD *)(game+0x8b2d0)=fade_before;
        retain_current_matrices();old_render_queue();have_matrices=TRUE;
        ++distance_batches;start=end;
    }
    *(UINT *)(game+0x53f19c)=total;
}
static void __cdecl release_matrices(void) {
    if(distance_active) {
        void *device=*(void **)(game+0xc8d10);
        typedef LONG (__stdcall *DeleteFn)(void *,DWORD);
        if(device) {
            DeleteFn release=(DeleteFn)(*(void ***)device)[18];
            for(UINT i=0;i<retained_count;++i)release(device,retained_matrices[i]);
        }
        retained_count=0;have_matrices=FALSE;
        static UINT frames;
        if(!(frames++%300)) {
            wo_render_event("distance_sections_submitted",distance_sections);
            wo_render_event("distance_native_batches",distance_batches);
            wo_render_event("distance_execute_buffers",*(UINT *)(game+0x8f8dc));
            wo_render_event("distance_track_sections_last",track_sections_last);
            wo_render_event("distance_track_faces_last",track_faces_last);
            wo_render_event("distance_track_vertices_last",track_vertices_last);
            wo_render_event("distance_track_incomplete_last",track_incomplete_last);
            wo_render_event("distance_backdrop_vertices",backdrop_count);
        }
    }
    old_release_matrices();
}

#define REF_COUNT(name) (sizeof(refs_##name)/sizeof(refs_##name[0]))
static BOOL initialize_distance(void) {
    WCHAR value[8];
    if(!GetEnvironmentVariableW(L"WO2097_DISTANCE_TEST",value,8) || value[0]!=L'1')return FALSE;
#define CHECK_REFS(name) if(!valid_refs(refs_##name,REF_COUNT(name)))return FALSE
    CHECK_REFS(queue);CHECK_REFS(primitive);CHECK_REFS(execute);CHECK_REFS(execute_state);
#define CHECK_ENTRY(name) if(!equal(game+RVA_##name,entry_##name,sizeof(entry_##name)))return FALSE
    CHECK_ENTRY(new_track);CHECK_ENTRY(all_track);CHECK_ENTRY(render_queue);CHECK_ENTRY(release_matrices);
    distance_queue=VirtualAlloc(0,DISTANCE_QUEUE_CAPACITY*96,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    distance_ordered=VirtualAlloc(0,DISTANCE_QUEUE_CAPACITY*96,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    distance_primitives=VirtualAlloc(0,8*1024*1024,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    distance_execute=VirtualAlloc(0,DISTANCE_EXECUTE_CAPACITY*4,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    distance_states=VirtualAlloc(0,DISTANCE_EXECUTE_CAPACITY*4,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    if(!distance_queue || !distance_ordered || !distance_primitives || !distance_execute || !distance_states)return FALSE;
    old_new_track=(TrackFn)hook(RVA_new_track,entry_new_track,sizeof(entry_new_track),new_track);
    old_all_track=(TrackFn)hook(RVA_all_track,entry_all_track,sizeof(entry_all_track),all_track);
    old_render_queue=(FrameFn)hook(RVA_render_queue,entry_render_queue,sizeof(entry_render_queue),render_queue);
    old_release_matrices=(FrameFn)hook(RVA_release_matrices,entry_release_matrices,sizeof(entry_release_matrices),release_matrices);
    if(!old_new_track || !old_all_track || !old_render_queue || !old_release_matrices)return FALSE;
    if(!relocate_refs(refs_queue,REF_COUNT(queue),distance_queue) ||
       !relocate_refs(refs_primitive,REF_COUNT(primitive),distance_primitives) ||
       !relocate_refs(refs_execute,REF_COUNT(execute),(BYTE *)distance_execute) ||
       !relocate_refs(refs_execute_state,REF_COUNT(execute_state),(BYTE *)distance_states)) {
        restore_refs(refs_queue,REF_COUNT(queue));restore_refs(refs_primitive,REF_COUNT(primitive));
        restore_refs(refs_execute,REF_COUNT(execute));restore_refs(refs_execute_state,REF_COUNT(execute_state));
        return FALSE;
    }
    render_entries=distance_queue;
    *(BYTE **)(game+0x65d020)=distance_primitives;
    FlushInstructionCache(GetCurrentProcess(),0,0);
    distance_active=TRUE;
    wo_render_event("render_full_distance_test",1);
    return TRUE;
}
