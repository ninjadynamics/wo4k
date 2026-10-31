/* Interpolate native preview headings in their own angular space. This keeps
 * speed, authored pitch and position, and follows the same completed-pose
 * fraction as the menu panels. Never interpolate whole images or gameplay. */
typedef struct MenuModelMotion {
    const BYTE *object;
    int x,y,z;
    SHORT pitch,previous,current;
    DWORD serial;
    float phase;
    BOOL moving;
} MenuModelMotion;
static MenuModelMotion menu_model_motion[64];
static SHORT interpolate_menu_angle(const BYTE *object,int x,int y,int z,SHORT angle,SHORT pitch) {
    if(!refresh_active || !object)return angle;
    UINT index=(((UINT)object>>4)^(UINT)x^(UINT)y^(UINT)z)&63;
    MenuModelMotion *entry=0,*available=0;
    for(UINT i=0;i<64;++i) {
        MenuModelMotion *e=menu_model_motion+((index+i)&63);
        if(e->object==object && e->x==x && e->y==y && e->z==z && e->pitch==pitch) {entry=e;break;}
        if(!available && (!e->object || e->serial+1<ui_present_serial))available=e;
    }
    if(!entry)entry=available?available:menu_model_motion+index;
    if(entry->object!=object || entry->x!=x || entry->y!=y || entry->z!=z ||
       entry->pitch!=pitch || entry->serial+1<ui_present_serial) {
        entry->object=object;entry->x=x;entry->y=y;entry->z=z;entry->pitch=pitch;
        entry->previous=entry->current=angle;entry->moving=FALSE;
    } else if(angle!=entry->current) {
        int delta=((int)angle-entry->current+2048)&4095;delta-=2048;
        entry->previous=entry->current;entry->current=angle;
        /* A new selection or a large pose reset must appear immediately. */
        entry->moving=delta>=-240 && delta<=240;
    } else if(ui_draw_phase<entry->phase && entry->serial!=ui_present_serial) {
        /* Stop cleanly if the native menu no longer advances this preview. */
        entry->moving=FALSE;
    }
    entry->serial=ui_present_serial;entry->phase=ui_draw_phase;
    if(!entry->moving)return angle;
    int delta=((int)entry->current-entry->previous+2048)&4095;delta-=2048;
    float result=entry->previous+delta*ui_draw_phase;
    return (SHORT)(result<0?result-.5f:result+.5f);
}
