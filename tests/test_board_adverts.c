/* The PlayStation's boards show a logo and a slogan by turns, twenty steps
 * each. Where the track's scenery has the logo, its place on the texture
 * page is painted with the slogan, stretched to fit, and with the logo
 * again. */
static UINT advert_fixture_pack(BYTE *out,const BYTE *const *pictures,const UINT *sizes,UINT count){
    for(UINT i=0;i<sizeof(cycle_packed);++i)cycle_packed[i]=0;
    *(DWORD *)cycle_packed=count;cycle_bit=(4+count*4)*8;
    for(UINT i=0;i<count;++i){
        *(DWORD *)(cycle_packed+4+i*4)=sizes[i];
        for(UINT j=0;j<sizes[i];++j)cycle_put(0x100|pictures[i][j],9);
    }
    cycle_put(0,14);
    UINT packed=(cycle_bit+7)/8;
    for(UINT i=0;i<packed;++i)out[i]=cycle_packed[i];
    return packed;
}
/* A track archive whose scenery has `count` pictures. */
static UINT advert_fixture_archive(BYTE *out,const BYTE *const *pictures,const UINT *sizes,UINT count){
    static BYTE packed[512];UINT bytes=advert_fixture_pack(packed,pictures,sizes,count),n=2+25;
    *(WORD *)out=1;
    for(UINT j=0;j<16;++j)out[2+j]=0xcc;
    for(UINT j=0;j<10;++j)out[2+j]=(BYTE)"SCENE.CMP"[j];
    *(DWORD *)(out+18)=*(DWORD *)(out+22)=bytes;out[26]=0;
    for(UINT i=0;i<bytes;++i)out[n++]=packed[i];
    return n;
}
static UINT advert_steps;
static void __cdecl fixture_advert_step(void){++advert_steps;}
/* The game's own files, where they are installed with the PlayStation's
 * scenery: Valparaiso and Phenitia Park each have a board with the logo. */
static void test_board_advert_assets(BYTE *config){
    WCHAR enabled[2];static WCHAR root[MAX_PATH];
    if(!GetEnvironmentVariableW(L"WO_TEST_HD_REGION_ASSETS",enabled,2))return;
    UINT n=GetEnvironmentVariableW(L"WO_TEST_GAME_ROOT",root,MAX_PATH);if(!n || n>=MAX_PATH-2)return;
    for(UINT i=0;i<n;++i)ui_hd_game_root[i]=root[i];
    ui_hd_game_root[n]=L'\\';ui_hd_game_root[n+1]=0;
    config[1]=13;
    if(board_advert_claim("Pics\\Hi-res\\NRACE03.TGA",0,11)==5){
        if(board_advert.x || board_advert.y || board_advert.width!=64 || board_advert.height!=64 ||
           board_advert.slogan_width!=64 || board_advert.slogan_height!=32)ExitProcess(345);
        UINT width=0;BYTE *page=board_advert_page("Pics\\Hi-res\\NRACE03.TGA",5,11,0,&width);
        if(!page || width!=256 || !board_advert.live || !equal(page,board_advert.pictures[0],64*4))ExitProcess(346);
        VirtualFree(page,0,MEM_RELEASE);
    }
    config[1]=20;
    if(board_advert_claim("Pics\\Hi-res\\NRACE04.TGA",0,11)==10 &&
       (board_advert.x || board_advert.y || board_advert.width!=128 || board_advert.height!=128 || board_advert.slogan_width!=64))ExitProcess(347);
    board_advert_end();
}
/* Kept out of the caller: its frame has no room for these paths. */
static void __attribute__((noinline)) test_board_adverts(BYTE *image){
    if(!old_board_step || image[0x2bfc0]!=0xe9 || image+0x2bfc5+*(LONG *)(image+0x2bfc1)!=(BYTE *)board_advert_step)ExitProcess(330);
    if(!board_advert_letter(13) || !board_advert_letter(20) || board_advert_letter(3) || board_advert_letter(0))ExitProcess(331);
    /* A game folder of its own: track 13's archive, whose scenery ends with
     * the logo, the track's two adverts and a one-page sheet of one colour. */
    static const BYTE other[4]={0x11,0x11,0x22,0x22};
    static const BYTE logo_rows[16]={0x11,0x11,0x11,0x11,0x22,0x22,0x22,0x22,0x33,0x33,0x33,0x33,0x44,0x44,0x44,0x44};
    static const BYTE slogan_rows[4]={0x21,0x43,0x10,0x02};
    static BYTE pictures[3][96],archive[512],adverts[512];
    UINT sizes[3]={cycle_fixture_picture(pictures[0],0,241,320,0,1,2,other),
                   cycle_fixture_picture(pictures[1],0,230,656,128,2,4,logo_rows),
                   cycle_fixture_picture(pictures[2],0,231,700,0,1,2,slogan_rows)};
    const BYTE *const scene[2]={pictures[0],pictures[1]},*const pair[2]={pictures[1],pictures[2]};
    const UINT pair_sizes[2]={sizes[1],sizes[2]};
    static WCHAR saved_root[MAX_PATH],saved_auth[MAX_PATH],saved_upscaled[MAX_PATH],saved_sd[MAX_PATH],folder[MAX_PATH],path[MAX_PATH],archive_path[MAX_PATH];
    for(UINT i=0;i<MAX_PATH;++i){
        saved_root[i]=ui_hd_game_root[i];saved_auth[i]=ui_hd_directory[i];
        saved_upscaled[i]=ui_upscaled_directory[i];saved_sd[i]=ui_sd_directory[i];
    }
    UINT n=GetEnvironmentVariableW(L"WO_TEST_SETTINGS",folder,MAX_PATH);if(!n || n>=MAX_PATH-80)ExitProcess(332);
    while(n && folder[n-1]!=L'\\')--n;folder[n]=0;
    if(!cycle_make_folder(folder,L"advert"))ExitProcess(332);
    /* No folder of replacements has the slogan. */
    for(UINT i=0;i<MAX_PATH;++i)ui_hd_game_root[i]=ui_hd_directory[i]=ui_sd_directory[i]=folder[i];
    ui_upscaled_directory[0]=0;
    if(!hd_append(ui_hd_directory,MAX_PATH,L"auth\\") || !hd_append(ui_sd_directory,MAX_PATH,L"sd\\"))ExitProcess(332);
    for(UINT i=0;i<MAX_PATH;++i)archive_path[i]=folder[i];
    if(!cycle_make_folder(archive_path,L"TRACK13") || !hd_append(archive_path,MAX_PATH,L"TRACK.WAD") ||
       !cycle_write_file(archive_path,archive,advert_fixture_archive(archive,scene,sizes,2)))ExitProcess(332);
    for(UINT i=0;i<MAX_PATH;++i)path[i]=folder[i];
    if(!cycle_make_folder(path,L"textures") || !cycle_make_folder(path,L"ps1-source") || !cycle_make_folder(path,L"TEXTURES") ||
       !hd_append(path,MAX_PATH,L"NURED.CMP") || !cycle_write_file(path,adverts,advert_fixture_pack(adverts,pair,pair_sizes,2)))ExitProcess(332);
    for(UINT i=0;i<MAX_PATH;++i)path[i]=folder[i];
    BYTE *sheet=VirtualAlloc(0,18+256*256*2,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);if(!sheet)ExitProcess(332);
    sheet[2]=2;sheet[16]=16;*(WORD *)(sheet+12)=256;*(WORD *)(sheet+14)=256;
    for(UINT i=0;i<256*256;++i)*(WORD *)(sheet+18+i*2)=0x1234;
    if(!cycle_make_folder(path,L"textures") || !cycle_make_folder(path,L"sd") || !cycle_make_folder(path,L"Pics") ||
       !cycle_make_folder(path,L"Hi-res") || !hd_append(path,MAX_PATH,L"NRACE99.TGA") || !cycle_write_file(path,sheet,18+256*256*2))ExitProcess(332);
    VirtualFree(sheet,0,MEM_RELEASE);
    static BYTE config[4];config[1]=13;
    BYTE *saved_config=*(BYTE **)(image+0x517958);*(BYTE **)(image+0x517958)=config;
    BoardAdvertFn saved_step=old_board_step;old_board_step=fixture_advert_step;
    board_advert_end();
    /* Only a scenery sheet over the logo's page takes it, on a track that
     * has adverts and whose scenery has the logo. */
    static const char sheet_name[]="Pics\\Hi-res\\NRACE99.TGA";
    if(board_advert_claim("testmenu.tga",5,1)!=-1 || board_advert_claim(sheet_name,0,5)!=-1 || board_advert_claim(sheet_name,6,5)!=-1 ||
       board_advert.slogan)ExitProcess(333);
    config[1]=3;if(board_advert_claim(sheet_name,5,1)!=-1)ExitProcess(334);
    config[1]=13;
    if(!cycle_write_file(archive_path,archive,advert_fixture_archive(archive,scene,sizes,1)) ||
       board_advert_claim(sheet_name,5,1)!=-1 || board_advert.slogan)ExitProcess(334);
    if(!cycle_write_file(archive_path,archive,advert_fixture_archive(archive,scene,sizes,2)) || board_advert_claim(sheet_name,5,1)!=0)ExitProcess(335);
    static const WCHAR png[]=L"textures-nured-cmp-1.png";
    if(board_advert.live || board_advert.slot!=5 || board_advert.x!=64 || board_advert.y!=128 || board_advert.width!=8 || board_advert.height!=4 ||
       board_advert.slogan_width!=4 || board_advert.slogan_height!=2 || !equal((const BYTE *)board_advert.png,(const BYTE *)png,sizeof(png)))ExitProcess(335);
    /* No replacement on the page: it is made from the game's sheet, and the
     * logo is what the sheet shows there. */
    UINT width=0;DWORD *page=(DWORD *)board_advert_page(sheet_name,0,1,0,&width);
    const DWORD sheet_colour=0xff208ba4,*kept=(const DWORD *)board_advert.pictures[0];
    if(!page || width!=256 || !board_advert.live || board_advert.scale!=1 || page[128*256+64]!=sheet_colour ||
       kept[0]!=sheet_colour || kept[8*4-1]!=sheet_colour)ExitProcess(336);
    VirtualFree(page,0,MEM_RELEASE);
    /* On a page twice the size the logo is kept as that page has it, and
     * each texel of the slogan covers two of the logo's each way: four
     * pixels. Colour 0 is see-through. */
    if(board_advert_claim(sheet_name,5,1)!=0)ExitProcess(337);
    DWORD *large=VirtualAlloc(0,512*512*4,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);if(!large)ExitProcess(337);
    for(UINT i=0;i<512*512;++i)large[i]=0xff000000u|i;
    width=512;if((DWORD *)board_advert_page(sheet_name,0,1,(BYTE *)large,&width)!=large || width!=512 || board_advert.scale!=2)ExitProcess(337);
    kept=(const DWORD *)board_advert.pictures[0];const DWORD *slogan=(const DWORD *)board_advert.pictures[1];
    if(kept[0]!=large[256*512+128] || kept[15]!=large[256*512+143] || kept[7*16+15]!=large[263*512+143])ExitProcess(338);
    if(slogan[0]!=0xffff0000 || slogan[3]!=0xffff0000 || slogan[4]!=0xff00ff00 || slogan[8]!=0xff0000ff || slogan[12]!=0xff202020 ||
       slogan[3*16+15]!=0xff202020 || slogan[4*16] || slogan[4*16+4]!=0xffff0000 || slogan[7*16+8]!=0xff00ff00 || slogan[7*16+15])ExitProcess(339);
    VirtualFree(large,0,MEM_RELEASE);
    /* Nothing is written for twenty steps; then the slogan goes into the
     * logo's rectangle of both copies, once, and after twenty more the logo. */
    static IDirectDrawSurfaceVtbl vtable={.Lock=fixture_cycle_lock,.Unlock=fixture_cycle_unlock};
    static CycleSurface keyed={&vtable,0},opaque={&vtable,1};
    UiHdPage saved_page=ui_hd_pages[5],saved_opaque=ui_hd_opaque_pages[5];
    ui_hd_pages[5]=(UiHdPage){.surface=(IDirectDrawSurface *)&keyed,.width=512};
    ui_hd_opaque_pages[5]=(UiHdPage){.surface=(IDirectDrawSurface *)&opaque,.width=512};
    cycle_locks=cycle_unlocks=0;cycle_lock_result=0;advert_steps=0;
    board_advert_present();
    for(UINT i=0;i<19;++i)board_advert_step();
    board_advert_present();if(cycle_locks || advert_steps!=19)ExitProcess(340);
    board_advert_step();board_advert_present();board_advert_present();
    if(cycle_locks!=2 || cycle_unlocks!=2 || cycle_lock_order[0]!=0 || cycle_lock_order[1]!=1)ExitProcess(341);
    for(UINT i=0;i<2;++i)if(cycle_lock_rect[i].left!=128 || cycle_lock_rect[i].top!=256 || cycle_lock_rect[i].right!=144 || cycle_lock_rect[i].bottom!=264)ExitProcess(341);
    const DWORD *seen=(const DWORD *)cycle_locked[0],*solid=(const DWORD *)cycle_locked[1];
    if(seen[0]!=0xffff0000 || seen[12]!=0xff202020 || seen[4*64] || seen[4*64+4]!=0xffff0000 ||
       solid[0]!=0xffff0000 || solid[4*64]!=0xff000000 || solid[7*64+8]!=0xff00ff00)ExitProcess(342);
    for(UINT i=0;i<20;++i)board_advert_step();
    board_advert_present();
    if(cycle_locks!=4 || seen[0]!=(0xff000000u|(256*512+128)) || seen[7*64+15]!=(0xff000000u|(263*512+143)) || solid[0]!=seen[0])ExitProcess(343);
    /* A page that cannot be written, or one of another size, ends it; so
     * does a sheet loading over the page. */
    cycle_lock_result=E_FAIL;for(UINT i=0;i<20;++i)board_advert_step();
    board_advert_present();if(cycle_locks!=5 || board_advert.live)ExitProcess(344);
    cycle_lock_result=0;board_advert.live=TRUE;ui_hd_pages[5].width=256;
    for(UINT i=0;i<20;++i)board_advert_step();
    board_advert_present();if(cycle_locks!=5 || board_advert.live)ExitProcess(344);
    board_advert.live=TRUE;
    if(board_advert_claim("testmenu.tga",1,8)!=-1 || board_advert.live || board_advert.pictures[0] || board_advert.slogan)ExitProcess(344);
    ui_hd_pages[5]=saved_page;ui_hd_opaque_pages[5]=saved_opaque;
    test_board_advert_assets(config);
    old_board_step=saved_step;
    *(BYTE **)(image+0x517958)=saved_config;
    for(UINT i=0;i<MAX_PATH;++i){
        ui_hd_game_root[i]=saved_root[i];ui_hd_directory[i]=saved_auth[i];
        ui_upscaled_directory[i]=saved_upscaled[i];ui_sd_directory[i]=saved_sd[i];
    }
}
