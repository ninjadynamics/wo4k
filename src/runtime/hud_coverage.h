/* Compact subpixel alpha bounds for each native 256x256 atlas cell. Keep
 * authored transparent padding out of HUD placement without changing UVs. */
static BYTE *hud_coverage_create(const BYTE *pixels,UINT width) {
    if(width<256 || width>8192 || width%256)return 0;
    UINT scale=width/256;
    BYTE *grid=VirtualAlloc(0,256*256*4,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    if(!grid)return 0;
    for(UINT y=0;y<width;++y)for(UINT x=0;x<width;++x){
        if(!pixels[(y*width+x)*4+3])continue;
        BYTE *cell=grid+((y/scale)*256+x/scale)*4;
        UINT sx=x%scale,sy=y%scale;
        if(!cell[2]){cell[0]=(BYTE)sx;cell[1]=(BYTE)sy;}
        if(sx<cell[0])cell[0]=(BYTE)sx;if(sy<cell[1])cell[1]=(BYTE)sy;
        if(sx+1>cell[2])cell[2]=(BYTE)(sx+1);if(sy+1>cell[3])cell[3]=(BYTE)(sy+1);
    }
    return grid;
}
static BOOL hud_coverage_bounds(const BYTE *grid,UINT width,int left,int top,int right,int bottom,float *bounds) {
    if(!grid || !width || left<0 || top<0 || right>256 || bottom>256 || right<=left || bottom<=top)return FALSE;
    float scale=(float)width/256;BOOL visible=FALSE;
    for(int y=top;y<bottom;++y)for(int x=left;x<right;++x){
        const BYTE *cell=grid+(y*256+x)*4;if(!cell[2])continue;
        float a=x+cell[0]/scale,b=y+cell[1]/scale,c=x+cell[2]/scale,d=y+cell[3]/scale;
        if(!visible || a<bounds[0])bounds[0]=a;if(!visible || b<bounds[1])bounds[1]=b;
        if(!visible || c>bounds[2])bounds[2]=c;if(!visible || d>bounds[3])bounds[3]=d;visible=TRUE;
    }
    return visible;
}
