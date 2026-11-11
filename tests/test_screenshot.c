/* Included by test_render_init.c: a locked RGB565 surface becomes a PNG that
 * decodes to the same colours; a menu screenshot is described as such. */
static void test_screenshot(BYTE *image){
    static WORD surface[4*2+2];DDSURFACEDESC desc={0};
    desc.dwWidth=4;desc.dwHeight=2;desc.lPitch=10;desc.lpSurface=surface; /* a pitch wider than the row */
    desc.ddpfPixelFormat.dwFlags=DDPF_RGB;desc.ddpfPixelFormat.dwRGBBitCount=16;
    desc.ddpfPixelFormat.dwRBitMask=0xf800;desc.ddpfPixelFormat.dwGBitMask=0x07e0;desc.ddpfPixelFormat.dwBBitMask=0x001f;
    surface[0]=0xf800;surface[1]=0x07e0;surface[2]=0x001f;surface[3]=0xffff;surface[5]=0x8410;
    static Screenshot shot;shot.pixels=shot_copy(&desc);shot.width=4;shot.height=2;shot.stride=12;
    if(!shot.pixels || shot.pixels[2]!=255 || shot.pixels[1] || shot.pixels[4]!=255 || shot.pixels[6]!=255 ||
       shot.pixels[9]!=255 || shot.pixels[12]!=131 || shot.pixels[13]!=129)ExitProcess(260);
    desc.ddpfPixelFormat.dwRGBBitCount=8;if(shot_copy(&desc))ExitProcess(261);
    /* An all-black screen copy is refused, so the game's own frame is used. */
    static BYTE dark[64*64*3];if(!shot_black(dark,64,64,192))ExitProcess(265);
    dark[40*192+30*3]=200;if(shot_black(dark,64,64,192))ExitProcess(265);
    *(DWORD *)(image+0x8c98c)=0;shot_describe(&shot);
    if(shot.fields[0][0]!='M' || shot.fields[1][0] || shot.fields[5][0]!='W')ExitProcess(262);
    const WCHAR *path=L"build\\tests\\screenshot-test.png";UINT n=0;while((shot.path[n]=path[n]))++n;
    if(shot_encode(&shot)<0)ExitProcess(263);
    UINT w=0,h=0;BYTE *decoded=read_art_png(shot.path,&w,&h);
    if(!decoded || w!=4 || h!=2 || decoded[2]!=255 || decoded[0] || decoded[5]!=255 || decoded[8]!=255 || decoded[3]!=255)ExitProcess(264);
    VirtualFree(decoded,0,MEM_RELEASE);VirtualFree(shot.pixels,0,MEM_RELEASE);
}
