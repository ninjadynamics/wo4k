/* Included by test_render_init.c after the native font/header checks. */
static BOOL frame_near(float a,float b){float d=a-b;return d>-.0001f && d<.0001f;}
static void test_menu_frame(BYTE *image,BYTE *buffer){
    #include "native_frame_refs.h"
    DWORD delta=(DWORD)(UINT_PTR)image-0x400000;
    for(UINT i=0;i<sizeof(native_frame_refs)/sizeof(native_frame_refs[0]);++i){
        DWORD *operand=(DWORD *)(image+native_frame_refs[i].rva);
        if(*operand!=native_frame_refs[i].expected)ExitProcess(91);
        *operand+=delta;
    }
    DWORD *vertex_data=(DWORD *)((BYTE *)old_vertex+7);
    if(*vertex_data!=0xa6add8)ExitProcess(92);
    *vertex_data+=delta;
    /* Execute the original InitMenuBackPolys, including SetPolyFT4 and all
     * fourteen native rectangles. No synthetic replacement geometry. */
    ((void (__cdecl *)(void))(image+0x3cb30))();
    const float widths[]={640,1920,3840,3440,1080},heights[]={480,1080,2160,1440,1920};
    const float scales_x[]={1,.75f,.75f,24.0f/43,1},scales_y[]={1,1,1,1,.421875f};
    *(int *)(image+0x8c9d4)=1;title_active=FALSE;menu_history_valid=FALSE;
    for(UINT hd=0;hd<2;++hd)for(UINT aspect=0;aspect<5;++aspect){
        for(UINT i=1;i<=3;++i)ui_hd_pages[i].handle=hd?300+i:0;
        *(float *)(image+0xaf0b4)=widths[aspect];*(float *)(image+0xc8630)=heights[aspect];
        ui_x=scales_x[aspect];ui_y=scales_y[aspect];
        for(UINT tile=0;tile<14;++tile){
            const BYTE *p=image+0x511740+tile*40;
            float left=*(const SHORT *)(p+8),right=*(const SHORT *)(p+16);
            float top=*(const SHORT *)(p+10),bottom=*(const SHORT *)(p+26);
            *(UINT *)(buffer+0x39210)=*(UINT *)(buffer+0x39214)=0;*(SHORT *)(image+0x87040)=0;
            quad(.99f,p,0);
            UINT count=*(UINT *)(buffer+0x39214);
            if(!count || count%4 || count>36)ExitProcess(93);
            const TLVertex *v=(const TLVertex *)(buffer+0xa410);
            float min_u=10,max_u=-10,min_v=10,max_v=-10,min_x=100000,max_x=-100000;
            for(UINT i=0;i<count;++i){
                if(v[i].u<min_u)min_u=v[i].u;if(v[i].u>max_u)max_u=v[i].u;
                if(v[i].v<min_v)min_v=v[i].v;if(v[i].v>max_v)max_v=v[i].v;
                if(v[i].x<min_x)min_x=v[i].x;if(v[i].x>max_x)max_x=v[i].x;
                if(v[i].x<widths[aspect]*menu_x(left)-.01f || v[i].x>widths[aspect]*menu_x(right)+.01f ||
                   v[i].y<heights[aspect]*menu_y(top)-.01f || v[i].y>heights[aspect]*menu_y(bottom)+.01f)ExitProcess(94);
                /* Shared corners of the responsive cap/edge subdivisions
                 * must sample precisely the same point, with no byte rounding. */
                if(hd)for(UINT j=0;j<i;++j)if(frame_near(v[i].x,v[j].x) && frame_near(v[i].y,v[j].y) &&
                    (!frame_near(v[i].u,v[j].u) || !frame_near(v[i].v,v[j].v)))ExitProcess(95);
            }
            float divisor=hd?256:255; /* The original EXE normalizes byte UVs by255. */
            float expected_right=(hd && (tile==0 || tile==1 || tile==3 || tile==4 || tile==8 || tile==9 || tile==11 || tile==12))?1:p[20]/divisor;
            if(hd && (tile==2 || tile==5 || tile==10 || tile==13))expected_right=64.0f/256;
            if(!frame_near(min_u,p[12]/divisor) || !frame_near(max_u,expected_right) ||
               !frame_near(min_v,p[13]/divisor) || !frame_near(max_v,p[29]/divisor)){
                WCHAR report_path[MAX_PATH];
                if(GetEnvironmentVariableW(L"WO_TEST_FRAME_REPORT",report_path,MAX_PATH)){
                    const float report[]={hd,aspect,tile,min_u,max_u,min_v,max_v,p[12]/divisor,expected_right,p[13]/divisor,p[29]/divisor};
                    HANDLE f=CreateFileW(report_path,GENERIC_WRITE,0,0,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,0);DWORD written;
                    if(f!=INVALID_HANDLE_VALUE){WriteFile(f,report,sizeof(report),&written,0);CloseHandle(f);}
                }
                ExitProcess(96);
            }
            if(!frame_near(min_x/widths[aspect],menu_x(left)) || !frame_near(max_x/widths[aspect],menu_x(right)))ExitProcess(97);
            if(!hd && count){
                /* Native SD fallback keeps its reserved columns exactly. */
                if(!frame_near(max_u,p[20]/255.0f))ExitProcess(98);
            }
        }
    }
    /* Real execute-buffer packets: frame edges clamp; the following SD draw
     * and the packet's final state recover the inherited sampler mode. */
    BYTE packet[52]={8,8,1,0},out[512];
    *(DWORD *)(packet+4)=1;packet[12]=3;packet[13]=8;*(WORD *)(packet+14)=1;
    packet[24]=8;packet[25]=8;*(WORD *)(packet+26)=1;*(DWORD *)(packet+28)=1;*(DWORD *)(packet+32)=777;
    packet[36]=3;packet[37]=8;*(WORD *)(packet+38)=1;packet[48]=11;
    for(UINT slot=1;slot<=3;++slot)for(UINT address=1;address<=2;++address){
        ui_hd_pages[slot].handle=300+slot;*(DWORD *)(packet+8)=300+slot;ui_hd_original_address=address;
        UINT size=ui_hd_rewrite(packet,sizeof(packet),out,sizeof(out),0,0),mode=address,draws=0;
        if(!size)ExitProcess(99);
        for(UINT at=0;at<size;){
            UINT op=out[at],stride=out[at+1],n=*(WORD *)(out+at+2);
            if(op==8)for(UINT i=0;i<n;++i)if(*(DWORD *)(out+at+4+i*8)==3)mode=*(DWORD *)(out+at+8+i*8);
            if(op==3){if(mode!=(draws?address:D3DTADDRESS_CLAMP))ExitProcess(100);++draws;}
            at+=4+stride*n;
        }
        if(draws!=2 || mode!=address)ExitProcess(101);
    }
    ui_hd_original_address=1;for(UINT i=1;i<=3;++i)ui_hd_pages[i].handle=0;
    *(float *)(image+0xaf0b4)=640;*(float *)(image+0xc8630)=480;ui_x=.75f;ui_y=1;
}
