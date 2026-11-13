/* Lift low world-lighting values before native 3D submission. The separate
 * screen-space buffer (HUD/text) and source meshes/textures are never edited. */
static UINT scene_shadow_lift;
static UINT scene_highlights=100;
static BYTE scene_light_curve[256];
static BYTE scene_light_usage[65536];
static WCHAR scene_light_settings[MAX_PATH];
static DWORD scene_light_notice_until;
static BOOL scene_light_notice;
/* For maintainers, with WO4K_LIGHT_CAPTURE=1 in the environment
 * (tools\capture-light-preview.cmd): F9 in a race holds the scene still and
 * takes three screenshots of it: lit as the game lights it, fully lit, and
 * unlit, which leaves what these settings never touch. Then the race runs
 * on. Setup's preview of these settings is made from the three
 * (tools/Build-LightPreview.py). The screenshot code steps through them. */
enum {SCENE_CAPTURE_OFF,SCENE_CAPTURE_LIT,SCENE_CAPTURE_FULL,SCENE_CAPTURE_BARE,SCENE_CAPTURE_STATES};
static BOOL scene_light_capture_offered;
static UINT scene_light_capture;

static BOOL scene_light_racing(void) {
    return installed && refresh_race && *(DWORD *)(game+0x8c98c) && !*(DWORD *)(game+0x8c9d4);
}
static void scene_light_rebuild(void) {
    for(UINT c=0;c<256;++c) {
        /* y=x+s*x*(1-x)^2. Black/white stay fixed; the lift tapers toward
         * highlights. s<=3 keeps the curve monotonic, including at the cap. */
        UINT lift=(scene_shadow_lift*c*(255-c)*(255-c)+3251250u)/6502500u;
        UINT value=c+lift;
        /* Adjust the upper half around a fixed midpoint. 100 is neutral;
         * 0 compresses highlights to the midpoint, 300 increases their gain.
         * This remains monotonic even with maximum shadow lift. */
        if(value>128)value=128+((value-128)*scene_highlights+50)/100;
        scene_light_curve[c]=(BYTE)(value>255?255:value);
    }
}
static void scene_light_set(UINT percent) {
    scene_shadow_lift=percent>300?300:percent;scene_light_rebuild();
}
static void scene_highlights_set(UINT percent) {
    scene_highlights=percent>300?300:percent;scene_light_rebuild();
}
static void scene_light_number(WCHAR *text,UINT n) {
    WCHAR reverse[4];UINT at=0,used=0;
    do{reverse[used++]=(WCHAR)(L'0'+n%10);n/=10;}while(n);
    while(used)text[at++]=reverse[--used];text[at]=0;
}
static BOOL scene_light_key(SHORT key) {
    if(key==0x43 && scene_light_capture_offered && scene_light_racing()) { /* DirectInput F9. */
        if(!scene_light_capture)scene_light_capture=SCENE_CAPTURE_LIT;
        wo_render_event("render_light_capture",scene_light_capture);
        return TRUE;
    }
    BOOL highlights=key==0xc9 || key==0xd1; /* DirectInput Page Up / Page Down. */
    int delta=(key==0x0d || key==0x4e || key==0xc9)?5:((key==0x0c || key==0x4a || key==0xd1)?-5:0);
    if(!delta || !scene_light_racing() || *(DWORD *)(game+0x89cb0))return FALSE;
    int next=(int)(highlights?scene_highlights:scene_shadow_lift)+delta;
    if(highlights)scene_highlights_set(next<0?0:(UINT)next);
    else scene_light_set(next<0?0:(UINT)next);
    scene_light_notice=TRUE;scene_light_notice_until=GetTickCount()+2500;
    WCHAR value[4];scene_light_number(value,highlights?scene_highlights:scene_shadow_lift);
    WritePrivateProfileStringW(L"Render",highlights?L"HighlightsPercent":L"ShadowLiftPercent",value,scene_light_settings);
    wo_render_event(highlights?"render_highlights_percent":"render_shadow_lift_percent",highlights?scene_highlights:scene_shadow_lift);
    return TRUE;
}
static DWORD scene_light_color(DWORD color) {
    UINT r=(color>>16)&255,g=(color>>8)&255,b=color&255;
    UINT peak=r>g?r:g;if(b>peak)peak=b;
    if(!peak)return color;
    UINT lifted=scene_light_curve[peak];
    if(lifted==peak)return color;
    /* Scale by the brightest channel so saturated lights retain their hue. */
    r=(r*lifted+peak/2)/peak;g=(g*lifted+peak/2)/peak;b=(b*lifted+peak/2)/peak;
    return (color&0xff000000u)|(r<<16)|(g<<8)|b;
}
/* Color-dependent legacy blend modes use RGB as opacity, even when alpha is
 * unchanged. Only adjust vertices used exclusively by depth-writing opaque
 * triangles. Shared opaque/blended vertices retain their exact original color.
 * Validate the whole stream before editing any vertices. */
static BOOL scene_light_classify(const BYTE *input,UINT length,UINT count) {
    UINT offset=0;DWORD blend=0,zwrite=1;
    if(count>65535)return FALSE;
    for(UINT i=0;i<count;++i)scene_light_usage[i]=0;
    while(offset<length) {
        if(length-offset<4)return FALSE;
        UINT opcode=input[offset],size=input[offset+1],n=*(const WORD *)(input+offset+2),bytes=4+size*n;
        if(bytes>length-offset)return FALSE;
        if(opcode==8) {
            if(size!=8)return FALSE;
            for(UINT i=0;i<n;++i) {
                DWORD type=*(const DWORD *)(input+offset+4+i*8),value=*(const DWORD *)(input+offset+8+i*8);
                if(type==27)blend=value;if(type==14)zwrite=value;
            }
        } else if(opcode==3) {
            if(size!=8)return FALSE;
            BYTE usage=!blend && zwrite?1:2;
            for(UINT i=0;i<n;++i)for(UINT corner=0;corner<3;++corner) {
                UINT index=*(const WORD *)(input+offset+4+i*8+corner*2);
                if(index>=count)return FALSE;
                scene_light_usage[index]|=usage;
            }
        } else if(opcode==1 || opcode==2) {
            /* Points/lines can share triangle vertices. Preserve their color. */
            if(size!=4)return FALSE;
            for(UINT i=0;i<n;++i) {
                UINT first=*(const WORD *)(input+offset+4+i*4),last;
                if(opcode==1) {
                    UINT amount=first;first=*(const WORD *)(input+offset+6+i*4);
                    if(first>count || amount>count-first)return FALSE;
                    for(last=first;last<first+amount;++last)scene_light_usage[last]|=2;
                } else {
                    last=*(const WORD *)(input+offset+6+i*4);if(first>=count || last>=count)return FALSE;
                    scene_light_usage[first]|=2;scene_light_usage[last]|=2;
                }
            }
        } else if(opcode==12 || opcode==13) {
            /* Branch-dependent state cannot be classified with a linear scan. */
            return FALSE;
        }
        offset+=bytes;
    }
    return TRUE;
}
static void scene_lighting_finish(void) {
    if(scene_light_capture==SCENE_CAPTURE_LIT || !scene_light_racing())return;
    if(!scene_shadow_lift && scene_highlights==100 && !scene_light_capture)return;
    BYTE *buffer=*(BYTE **)(game+0x53f190),*start=*(BYTE **)(game+0x53f1a0),*end=*(BYTE **)(game+0x631d54);
    UINT size=*(UINT *)(game+0xc8610),count=*(UINT *)(game+0x631d50);
    if(!buffer || start<buffer || end<start || end>=buffer+size || count>(UINT)(start-buffer)/32)return;
    if(!scene_light_classify(start,(UINT)(end-start),count))return;
    for(UINT i=0;i<count;++i) {
        BYTE *v=buffer+i*32;DWORD tag=*(DWORD *)(v+12);
        if(scene_light_usage[i]!=1 || tag==TRAIL_VERTEX_TAG || tag==SKY_VERTEX_TAG || tag==SPRITE_VERTEX_TAG)continue;
        DWORD colour=*(DWORD *)(v+16);
        *(DWORD *)(v+16)=scene_light_capture==SCENE_CAPTURE_FULL?colour|0xffffffu:
            scene_light_capture==SCENE_CAPTURE_BARE?colour&0xff000000u:scene_light_color(colour);
    }
}
static void draw_scene_light_status(void) {
    if(!scene_light_notice || !scene_light_racing())return;
    if((LONG)(scene_light_notice_until-GetTickCount())<=0){scene_light_notice=FALSE;return;}
    char text[48]="SHADOWS ";WCHAR number[4];UINT at=8;
    scene_light_number(number,scene_shadow_lift);
    for(UINT i=0;number[i];++i)text[at++]=(char)number[i];
    text[at++]='%';const char *label="   HIGHLIGHTS ";while(*label)text[at++]=*label++;
    scene_light_number(number,scene_highlights);for(UINT i=0;number[i];++i)text[at++]=(char)number[i];
    text[at++]='%';text[at]=0;
    ++hud_depth;center_text(text,160,214,8,0);--hud_depth;
}
static void initialize_scene_lighting(const WCHAR *settings) {
    UINT n=0;while(settings[n] && n<MAX_PATH-1){scene_light_settings[n]=settings[n];++n;}
    scene_light_settings[n]=0;
    WCHAR capture[4];
    scene_light_capture_offered=GetEnvironmentVariableW(L"WO4K_LIGHT_CAPTURE",capture,4) && capture[0]==L'1';
    UINT percent=GetPrivateProfileIntW(L"Render",L"ShadowLiftPercent",0,settings);
    UINT highlights=GetPrivateProfileIntW(L"Render",L"HighlightsPercent",100,settings);
    scene_highlights=highlights>300?100:highlights;
    scene_light_set(percent>300?0:percent);
    wo_render_event("render_shadow_lift_percent",scene_shadow_lift);
    wo_render_event("render_highlights_percent",scene_highlights);
}
