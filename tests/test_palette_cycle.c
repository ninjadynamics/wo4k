/* Valparaiso's waterfall: the game turns sixteen colours at every step and
 * the PC game never shows them. The picture and its palette come from the
 * track's archive, the game's own routine turns the palette, and the place
 * of the picture on its texture page is painted again after a turn. */
static BYTE cycle_packed[512];static UINT cycle_bit;
static void cycle_put(UINT value,UINT count){
    while(count--){
        if(value>>count&1)cycle_packed[cycle_bit>>3]|=(BYTE)(0x80>>(cycle_bit&7));
        ++cycle_bit;
    }
}
static const WORD cycle_fixture_palette[16]={0,0x001f,0x03e0,0x7c00,0x1084,0x14a5,0x18c6,0x1ce7,
    0x2108,0x2529,0x294a,0x2d6b,0x318c,0x35ad,0x39ce,0x3def};
/* A picture with a palette: how many colours it says it has, its palette's
 * place in video memory and its own. */
static UINT cycle_fixture_picture(BYTE *out,UINT depth,UINT palette_y,UINT x,UINT y,UINT words,UINT height,const BYTE *texels){
    UINT n=0;
    *(DWORD *)(out+n)=0x10;*(DWORD *)(out+n+4)=8|depth;n+=8;
    *(DWORD *)(out+n)=12+32;*(WORD *)(out+n+4)=880;*(WORD *)(out+n+6)=(WORD)palette_y;*(WORD *)(out+n+8)=16;*(WORD *)(out+n+10)=1;n+=12;
    for(UINT i=0;i<16;++i,n+=2)*(WORD *)(out+n)=cycle_fixture_palette[i];
    *(DWORD *)(out+n)=12+words*2*height;*(WORD *)(out+n+4)=(WORD)x;*(WORD *)(out+n+6)=(WORD)y;
    *(WORD *)(out+n+8)=(WORD)words;*(WORD *)(out+n+10)=(WORD)height;n+=12;
    for(UINT i=0;i<words*2*height;++i)out[n++]=texels[i];
    return n;
}
/* A face of a model: its kind, as many bytes as the kind has and, for a
 * kind with a picture, the picture's number where the kind keeps it. */
static UINT cycle_fixture_face(BYTE *out,UINT kind,UINT bytes,UINT picture_at,UINT picture){
    out[0]=0;out[1]=(BYTE)kind;out[2]=out[3]=0;
    for(UINT i=0;i<bytes;++i)out[4+i]=0x77;
    if(picture_at){out[4+picture_at]=(BYTE)(picture>>8);out[5+picture_at]=(BYTE)picture;}
    return 4+bytes;
}
static UINT cycle_fixture_object(BYTE *out,const char *name,UINT corners,UINT normals,UINT faces){
    for(UINT i=0;i<144;++i)out[i]=0;
    for(UINT i=0;name[i];++i)out[i]=(BYTE)name[i];
    out[17]=(BYTE)corners;out[25]=(BYTE)normals;out[33]=(BYTE)faces;
    for(UINT i=0;i<(corners+normals)*8;++i)out[144+i]=0xee;
    return 144+(corners+normals)*8;
}
/* A track archive. Its models: one with a plain face and a pictured one,
 * then the waterfall under the name given, a sprite and a face drawn with
 * the numbered picture. Its pictures, packed as the game packs them (the
 * last four bytes as a repeat of the four before): one of 256 colours whose
 * palette lies where the PlayStation keeps the waterfall's, as a sign's
 * does in the PC's archive, and the sixteen-colour waterfall. */
static UINT cycle_fixture_archive(BYTE *out,const char *fall,UINT picture){
    static BYTE pictures[2][96],models[512];
    static const BYTE other[4]={0x11,0x11,0x22,0x22},rows[8]={0x10,0x32,0x54,0x76,0x10,0x32,0x54,0x76};
    UINT sizes[2]={cycle_fixture_picture(pictures[0],1,241,320,0,1,2,other),
                   cycle_fixture_picture(pictures[1],0,230,656,128,2,2,rows)};
    for(UINT i=0;i<sizeof(cycle_packed);++i)cycle_packed[i]=0;
    *(DWORD *)cycle_packed=2;*(DWORD *)(cycle_packed+4)=sizes[0];*(DWORD *)(cycle_packed+8)=sizes[1];cycle_bit=12*8;
    for(UINT i=0;i<sizes[0];++i)cycle_put(0x100|pictures[0][i],9);
    for(UINT i=0;i<sizes[1]-4;++i)cycle_put(0x100|pictures[1][i],9);
    cycle_put(0,1);cycle_put(1+sizes[0]+sizes[1]-8,13);cycle_put(4-3,4);
    cycle_put(0,14);
    UINT packed=(cycle_bit+7)/8,shapes=0,n=0;
    shapes+=cycle_fixture_object(models+shapes,"rock",1,0,2);
    shapes+=cycle_fixture_face(models+shapes,1,12,0,0);
    shapes+=cycle_fixture_face(models+shapes,8,40,8,0);
    shapes+=cycle_fixture_object(models+shapes,fall,0,1,2);
    shapes+=cycle_fixture_face(models+shapes,11,12,0,0);
    shapes+=cycle_fixture_face(models+shapes,6,32,6,picture);
    *(WORD *)out=3;n=2;
    static const char *const names[3]={"scene.prm","track.trf","SCENE.CMP"};const UINT bytes[3]={shapes,3,packed};
    for(UINT i=0;i<3;++i,n+=25){
        for(UINT j=0;j<16;++j)out[n+j]=0xcc; /* what the game leaves after a name */
        UINT j=0;for(;names[i][j];++j)out[n+j]=(BYTE)names[i][j];out[n+j]=0;
        *(DWORD *)(out+n+16)=*(DWORD *)(out+n+20)=bytes[i];out[n+24]=0;
    }
    for(UINT i=0;i<shapes;++i)out[n++]=models[i];
    out[n++]=1;out[n++]=2;out[n++]=3;
    for(UINT i=0;i<packed;++i)out[n++]=cycle_packed[i];
    return n;
}
typedef struct CycleSurface {IDirectDrawSurfaceVtbl *vtable;UINT id;} CycleSurface;
static BYTE cycle_locked[2][8*64*4];
static RECT cycle_lock_rect[2];
static UINT cycle_locks,cycle_unlocks,cycle_lock_order[4];
static HRESULT cycle_lock_result;
static HRESULT WINAPI fixture_cycle_lock(IDirectDrawSurface *surface,RECT *rect,DDSURFACEDESC *desc,DWORD flags,HANDLE event){
    (void)event;UINT id=((CycleSurface *)surface)->id;
    if(cycle_locks<4)cycle_lock_order[cycle_locks]=id;
    ++cycle_locks;
    if(!rect || !(flags&DDLOCK_WRITEONLY) || desc->dwSize!=sizeof(*desc))ExitProcess(285);
    if(cycle_lock_result<0)return cycle_lock_result;
    cycle_lock_rect[id]=*rect;desc->lpSurface=cycle_locked[id];desc->lPitch=64*4;return 0;
}
static HRESULT WINAPI fixture_cycle_unlock(IDirectDrawSurface *surface,void *data){
    if(data!=cycle_locked[((CycleSurface *)surface)->id])ExitProcess(285);
    ++cycle_unlocks;return 0;
}
static UINT cycle_reads,cycle_turns;
static void __cdecl fixture_cycle_read(void){++cycle_reads;}
/* As the game does: the last twelve colours move along by one place. */
static void __cdecl fixture_cycle_turn(void){
    WORD *palette=(WORD *)(game+PALETTE_CYCLE_STORE),last=palette[15];
    for(UINT i=15;i>4;--i)palette[i]=palette[i-1];
    palette[4]=last;++cycle_turns;
}
static BOOL cycle_make_folder(WCHAR *path,const WCHAR *name){
    if(!hd_append(path,MAX_PATH,name))return FALSE;
    CreateDirectoryW(path,0); /* or it is there from the last run */
    DWORD kind=GetFileAttributesW(path);
    return kind!=INVALID_FILE_ATTRIBUTES && (kind&FILE_ATTRIBUTE_DIRECTORY) && hd_append(path,MAX_PATH,L"\\");
}
static BOOL cycle_write_file(const WCHAR *path,const BYTE *data,UINT length){
    HANDLE file=CreateFileW(path,GENERIC_WRITE,0,0,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,0);DWORD written=0;
    if(file==INVALID_HANDLE_VALUE)return FALSE;
    BOOL ok=WriteFile(file,data,length,&written,0) && written==length;
    CloseHandle(file);return ok;
}
/* The game's own files, where they are installed: the picture found in
 * Valparaiso's archive, painted with its palette as read, is exactly what
 * the track's sheet shows at that place. True of the PlayStation's scenery
 * and of the PC's, which keep the picture in different places. */
static void test_palette_cycle_assets(BYTE *image){
    WCHAR enabled[2],root[MAX_PATH];
    if(!GetEnvironmentVariableW(L"WO_TEST_HD_REGION_ASSETS",enabled,2))return;
    UINT n=GetEnvironmentVariableW(L"WO_TEST_GAME_ROOT",root,MAX_PATH);if(!n || n>=MAX_PATH-2)return;
    for(UINT i=0;i<n;++i)ui_hd_game_root[i]=root[i];
    ui_hd_game_root[n]=L'\\';ui_hd_game_root[n+1]=0;
    static const char sheet[]="Pics\\Hi-res\\NRACE03.TGA";
    UINT length=0;BYTE *archive=palette_cycle_archive(13,&length);if(!archive)return;
    BOOL found=palette_cycle_find(archive,length,(WORD *)(image+PALETTE_CYCLE_STORE));
    VirtualFree(archive,0,MEM_RELEASE);if(!found)ExitProcess(299);
    BYTE *native=hd_native_page(sheet,palette_cycle.slot,11,256);if(!native)return;
    UINT width=0;BYTE *painted=palette_cycle_page(sheet,palette_cycle.slot,11,0,&width);
    if(!painted || width!=256 || palette_cycle.width<16 || palette_cycle.height<16)ExitProcess(300);
    if(!equal(native,painted,256*256*4))ExitProcess(301);
    VirtualFree(native,0,MEM_RELEASE);VirtualFree(painted,0,MEM_RELEASE);
}
/* Kept out of the caller: its frame has no room for these paths. */
static void __attribute__((noinline)) test_palette_cycle(BYTE *image){
    static BYTE archive[1024];WORD palette[16];
    PaletteCycle saved=palette_cycle;
    /* The picture is the one the model named "wfall" is drawn with, not the
     * one whose palette lies where the PlayStation's routine writes. */
    UINT length=cycle_fixture_archive(archive,"wfall",1);
    for(UINT i=0;i<16;++i)palette[i]=0xffff;
    if(!palette_cycle_find(archive,length,palette))ExitProcess(270);
    if(palette_cycle.slot!=5 || palette_cycle.x!=64 || palette_cycle.y!=128 || palette_cycle.width!=8 || palette_cycle.height!=2)ExitProcess(271);
    for(UINT i=0;i<16;++i)if(palette_cycle_texels[i]!=(i&7))ExitProcess(272);
    for(UINT i=0;i<16;++i)if(palette[i]!=cycle_fixture_palette[i])ExitProcess(273);
    /* An archive cut short; no such model; a model drawn with a picture
     * that has no sixteen colours to turn, or with one the archive lacks. */
    if(palette_cycle_find(archive,length-1,palette) || palette_cycle_find(archive,2,palette))ExitProcess(274);
    length=cycle_fixture_archive(archive,"wfalls",1);if(palette_cycle_find(archive,length,palette))ExitProcess(275);
    length=cycle_fixture_archive(archive,"wfall",0);if(palette_cycle_find(archive,length,palette))ExitProcess(275);
    length=cycle_fixture_archive(archive,"wfall",2);if(palette_cycle_find(archive,length,palette))ExitProcess(275);
    if(!old_palette_read || !old_palette_turn || image[0x114d0]!=0xe9 || image[0x11470]!=0xe9 ||
       image+0x114d5+*(LONG *)(image+0x114d1)!=(BYTE *)palette_cycle_read ||
       image+0x11475+*(LONG *)(image+0x11471)!=(BYTE *)palette_cycle_turn)ExitProcess(276);
    /* A game folder of its own beside the test's settings: the archive of
     * track 13 and a one-page sheet of a single colour. */
    WCHAR saved_root[MAX_PATH],folder[MAX_PATH],path[MAX_PATH];
    for(UINT i=0;i<MAX_PATH;++i)saved_root[i]=ui_hd_game_root[i];
    UINT n=GetEnvironmentVariableW(L"WO_TEST_SETTINGS",folder,MAX_PATH);if(!n || n>=MAX_PATH-64)ExitProcess(277);
    while(n && folder[n-1]!=L'\\')--n;folder[n]=0;
    if(!cycle_make_folder(folder,L"cycle"))ExitProcess(277);
    for(UINT i=0;i<MAX_PATH;++i)ui_hd_game_root[i]=folder[i];
    for(UINT i=0;i<MAX_PATH;++i)path[i]=folder[i];
    length=cycle_fixture_archive(archive,"wfall",1);
    if(!cycle_make_folder(path,L"TRACK13") || !hd_append(path,MAX_PATH,L"TRACK.WAD") || !cycle_write_file(path,archive,length))ExitProcess(277);
    for(UINT i=0;i<MAX_PATH;++i)path[i]=folder[i];
    BYTE *sheet=VirtualAlloc(0,18+256*256*2,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);if(!sheet)ExitProcess(277);
    sheet[2]=2;sheet[16]=16;*(WORD *)(sheet+12)=256;*(WORD *)(sheet+14)=256;
    for(UINT i=0;i<256*256;++i)*(WORD *)(sheet+18+i*2)=0x1234;
    if(!cycle_make_folder(path,L"textures") || !cycle_make_folder(path,L"sd") || !cycle_make_folder(path,L"Pics") ||
       !cycle_make_folder(path,L"Hi-res") || !hd_append(path,MAX_PATH,L"CYCLE.TGA") || !cycle_write_file(path,sheet,18+256*256*2))ExitProcess(277);
    VirtualFree(sheet,0,MEM_RELEASE);
    /* The game asks for the palette: it is read from the archive of the
     * track being loaded into the game's own store, and waits for the sheet. */
    static BYTE config[4];config[1]=13;
    BYTE *saved_config=*(BYTE **)(image+0x517958);*(BYTE **)(image+0x517958)=config;
    WORD *store=(WORD *)(image+PALETTE_CYCLE_STORE),saved_store[16];
    for(UINT i=0;i<16;++i){saved_store[i]=store[i];store[i]=0;}
    PaletteCycleFn saved_read=old_palette_read,saved_turn=old_palette_turn;
    old_palette_read=fixture_cycle_read;old_palette_turn=fixture_cycle_turn;
    palette_cycle=(PaletteCycle){0};palette_cycle_read();
    if(cycle_reads!=1 || !palette_cycle.waiting || palette_cycle.live || palette_cycle.slot!=5)ExitProcess(278);
    for(UINT i=0;i<16;++i)if(store[i]!=cycle_fixture_palette[i])ExitProcess(278);
    /* A sheet over other pages leaves it waiting; the scenery sheet over
     * its page takes it, for that one page. */
    static const char sheet_name[]="Pics\\Hi-res\\CYCLE.TGA";
    if(palette_cycle_claim(sheet_name,0,5)!=-1 || palette_cycle_claim(sheet_name,6,5)!=-1 || !palette_cycle.waiting)ExitProcess(279);
    if(palette_cycle_claim(sheet_name,5,1)!=0 || palette_cycle.waiting || palette_cycle.live)ExitProcess(280);
    /* No replacement on the page: it is made from the game's sheet, at the
     * game's size, with the picture as the palette has it. Colour 0 is
     * see-through; red, green and blue are where the page keeps them. */
    UINT width=0;DWORD *page=(DWORD *)palette_cycle_page(sheet_name,0,1,0,&width);
    if(!page || width!=256 || !palette_cycle.live)ExitProcess(281);
    const DWORD *corner=page+128*256+64,sheet_colour=0xff208ba4;
    if(corner[0] || corner[1]!=0xffff0000 || corner[2]!=0xff00ff00 || corner[3]!=0xff0000ff || corner[7]!=0xff393939 ||
       corner[256+1]!=0xffff0000 || corner[-1]!=sheet_colour || corner[8]!=sheet_colour || corner[-256]!=sheet_colour || corner[2*256]!=sheet_colour)ExitProcess(282);
    VirtualFree(page,0,MEM_RELEASE);
    /* On a page twice the size every texel is two pixels each way. */
    DWORD *large=VirtualAlloc(0,512*512*4,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);if(!large)ExitProcess(283);
    width=512;if((DWORD *)palette_cycle_page(sheet_name,0,1,(BYTE *)large,&width)!=large || width!=512)ExitProcess(283);
    corner=large+256*512+128;
    if(corner[0] || corner[1] || corner[2]!=0xffff0000 || corner[3]!=0xffff0000 || corner[4]!=0xff00ff00 || corner[512+2]!=0xffff0000 ||
       corner[3*512+5]!=0xff00ff00 || corner[16] || corner[4*512] || corner[-1])ExitProcess(283);
    VirtualFree(large,0,MEM_RELEASE);
    /* Nothing is written until the palette turns; then once a frame, however
     * many steps turned it, into the picture's rectangle of both copies. */
    static IDirectDrawSurfaceVtbl vtable={.Lock=fixture_cycle_lock,.Unlock=fixture_cycle_unlock};
    static CycleSurface keyed={&vtable,0},opaque={&vtable,1};
    UiHdPage saved_page=ui_hd_pages[5],saved_opaque=ui_hd_opaque_pages[5];
    ui_hd_pages[5]=(UiHdPage){.surface=(IDirectDrawSurface *)&keyed,.width=512};
    ui_hd_opaque_pages[5]=(UiHdPage){.surface=(IDirectDrawSurface *)&opaque,.width=512};
    palette_cycle_present();if(cycle_locks)ExitProcess(284);
    palette_cycle_turn();palette_cycle_turn();palette_cycle_present();
    if(cycle_turns!=2 || cycle_locks!=2 || cycle_unlocks!=2 || cycle_lock_order[0]!=0 || cycle_lock_order[1]!=1)ExitProcess(286);
    for(UINT i=0;i<2;++i)if(cycle_lock_rect[i].left!=128 || cycle_lock_rect[i].top!=256 || cycle_lock_rect[i].right!=144 || cycle_lock_rect[i].bottom!=260)ExitProcess(286);
    /* Texels 4 and 5 now show what were the last two colours; the first
     * four never move. The copy drawn without the key has no holes. */
    const DWORD *seen=(const DWORD *)cycle_locked[0],*solid=(const DWORD *)cycle_locked[1];
    if(seen[0] || seen[2]!=0xffff0000 || seen[6]!=0xff0000ff || seen[8]!=0xff737373 || seen[10]!=0xff7b7b7b || seen[12]!=0xff202020 ||
       seen[3*64+9]!=0xff737373 || solid[0]!=0xff000000 || solid[8]!=0xff737373 || solid[64+2]!=0xffff0000)ExitProcess(287);
    palette_cycle_present();if(cycle_locks!=2)ExitProcess(287);
    /* A page that cannot be written ends it, and so does a sheet loading over the page. */
    cycle_lock_result=E_FAIL;palette_cycle_turn();palette_cycle_present();
    if(cycle_locks!=3 || palette_cycle.live)ExitProcess(288);
    palette_cycle_turn();palette_cycle_present();if(cycle_locks!=3)ExitProcess(288);
    cycle_lock_result=0;palette_cycle.live=TRUE;
    if(palette_cycle_claim("testmenu.tga",1,8)!=-1 || palette_cycle.live)ExitProcess(289);
    ui_hd_pages[5]=saved_page;ui_hd_opaque_pages[5]=saved_opaque;
    old_palette_read=saved_read;old_palette_turn=saved_turn;
    test_palette_cycle_assets(image);
    for(UINT i=0;i<16;++i)store[i]=saved_store[i];
    *(BYTE **)(image+0x517958)=saved_config;
    for(UINT i=0;i<MAX_PATH;++i)ui_hd_game_root[i]=saved_root[i];
    palette_cycle=saved;
}
/* On the real GPU, through the wrapper the game runs with: a rectangle of a
 * texture that has already been drawn is written in place, the right way
 * up, and the rest of the texture stays as it was. */
static const GUID cycle_surface_iid={0x6c14db81,0xa733,0x11ce,{0xa5,0x21,0x00,0x20,0xaf,0x0b,0xe5,0x60}};
static DWORD cycle_target_pixel(IDirectDrawSurface7 *surface,UINT x,UINT y){
    DDSURFACEDESC2 lock={0};lock.dwSize=sizeof(lock);
    material_check(surface->lpVtbl->Lock(surface,0,&lock,DDLOCK_WAIT|DDLOCK_READONLY,0),290);
    DWORD pixel=*(DWORD *)((BYTE *)lock.lpSurface+y*lock.lPitch+x*4)&0xffffff;
    surface->lpVtbl->Unlock(surface,0);return pixel;
}
static void cycle_draw(IDirect3DDevice7 *device,float left,float top){
    /* The 4x4 pixels at (left,top) of the 512x512 texture over the whole target. */
    TLVertex v[4]={{0,0,.5f,1,0xffffffff,0,left/512,top/512},{64,0,.5f,1,0xffffffff,0,(left+4)/512,top/512},
                   {0,64,.5f,1,0xffffffff,0,left/512,(top+4)/512},{64,64,.5f,1,0xffffffff,0,(left+4)/512,(top+4)/512}};
    material_check(device->lpVtbl->Clear(device,0,0,D3DCLEAR_TARGET,0x205020,1,0),291);
    material_check(device->lpVtbl->BeginScene(device),291);
    material_check(device->lpVtbl->DrawPrimitive(device,D3DPT_TRIANGLESTRIP,D3DFVF_TLVERTEX,v,4,0),291);
    material_check(device->lpVtbl->EndScene(device),291);
}
static void test_palette_cycle_gpu(IDirectDraw7 *dd,IDirect3DDevice7 *device,IDirectDrawSurface7 *target,DDSURFACEDESC2 *format){
    IDirectDrawSurface7 *texture=0;IDirectDrawSurface *page=0;DDSURFACEDESC2 desc=*format;
    desc.dwWidth=desc.dwHeight=512;desc.ddsCaps.dwCaps=DDSCAPS_TEXTURE|DDSCAPS_VIDEOMEMORY;
    desc.ddpfPixelFormat.dwFlags=DDPF_RGB|DDPF_ALPHAPIXELS;desc.ddpfPixelFormat.dwRGBAlphaBitMask=0xff000000;
    material_check(dd->lpVtbl->CreateSurface(dd,&desc,&texture,0),292);
    material_check(texture->lpVtbl->QueryInterface(texture,&cycle_surface_iid,(void **)&page),293);
    DDSURFACEDESC2 lock={0};lock.dwSize=sizeof(lock);
    material_check(texture->lpVtbl->Lock(texture,0,&lock,DDLOCK_WAIT,0),294);
    for(UINT y=0;y<512;++y)for(UINT x=0;x<512;++x)*(DWORD *)((BYTE *)lock.lpSurface+y*lock.lPitch+x*4)=0xff906030;
    texture->lpVtbl->Unlock(texture,0);
    device->lpVtbl->SetTexture(device,0,texture);
    device->lpVtbl->SetTextureStageState(device,0,D3DTSS_COLOROP,D3DTOP_SELECTARG1);
    device->lpVtbl->SetTextureStageState(device,0,D3DTSS_COLORARG1,D3DTA_TEXTURE);
    device->lpVtbl->SetTextureStageState(device,0,D3DTSS_ALPHAOP,D3DTOP_SELECTARG1);
    device->lpVtbl->SetTextureStageState(device,0,D3DTSS_ALPHAARG1,D3DTA_TEXTURE);
    device->lpVtbl->SetRenderState(device,D3DRENDERSTATE_ZENABLE,0);
    device->lpVtbl->SetRenderState(device,D3DRENDERSTATE_CULLMODE,1);
    device->lpVtbl->SetRenderState(device,D3DRENDERSTATE_ALPHABLENDENABLE,0);
    device->lpVtbl->SetRenderState(device,D3DRENDERSTATE_ALPHATESTENABLE,0);
    device->lpVtbl->SetRenderState(device,D3DRENDERSTATE_COLORKEYENABLE,0);
    PaletteCycle saved=palette_cycle;BYTE saved_texels[4];
    for(UINT i=0;i<4;++i){saved_texels[i]=palette_cycle_texels[i];palette_cycle_texels[i]=(BYTE)(i+1);}
    palette_cycle=(PaletteCycle){.x=64,.y=128,.width=2,.height=2};
    static const UINT at[4][2]={{16,16},{48,16},{16,48},{48,48}};
    cycle_draw(device,128,256);
    for(UINT i=0;i<4;++i)if(cycle_target_pixel(target,at[i][0],at[i][1])!=0x906030)ExitProcess(295);
    DWORD colours[16]={0,0xffff0000,0xff00ff00,0xff0000ff,0xffffff00};
    for(UINT round=0;round<3;++round){
        if(!palette_cycle_write(page,2,colours,round==2?0xff000000u:0))ExitProcess(296);
        cycle_draw(device,128,256);
        for(UINT i=0;i<4;++i)if(cycle_target_pixel(target,at[i][0],at[i][1])!=(colours[i+1]&0xffffff))ExitProcess(297);
        /* Beside it and beneath it nothing has changed. */
        cycle_draw(device,124,256);
        for(UINT i=0;i<4;++i)if(cycle_target_pixel(target,at[i][0],at[i][1])!=0x906030)ExitProcess(298);
        cycle_draw(device,128,260);
        for(UINT i=0;i<4;++i)if(cycle_target_pixel(target,at[i][0],at[i][1])!=0x906030)ExitProcess(298);
        DWORD first=colours[1];for(UINT i=1;i<4;++i)colours[i]=colours[i+1];colours[4]=first;
    }
    for(UINT i=0;i<4;++i)palette_cycle_texels[i]=saved_texels[i];
    palette_cycle=saved;
    device->lpVtbl->SetTexture(device,0,0);page->lpVtbl->Release(page);texture->lpVtbl->Release(texture);
}
