/* The original camera-centred sky mesh is much smaller than the full circuit.
 * It must not become a depth-writing wall around the newly complete scene. */
static LitVertex backdrop_vertices[2048];
static UINT backdrop_count;
static void capture_backdrop(int before) {
    int after=*(int *)(game+0x53f19c);
    if(!distance_active || !exhaust_active || before<0 || after!=before+1)return;
    BYTE *entry=render_entries+before*96;
    UINT count=*(UINT *)(entry+0x4c);
    if(!count || count>2048 || *(UINT *)(entry+0x40))return;
    copy((BYTE *)backdrop_vertices,*(const BYTE **)(entry+0x48),count*32);
    for(UINT i=0;i<count;++i)backdrop_vertices[i].reserved=SKY_VERTEX_TAG;
    *(void **)(entry+0x48)=backdrop_vertices;backdrop_count=count;
}
