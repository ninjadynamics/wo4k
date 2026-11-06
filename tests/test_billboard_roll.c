/* Sprites face the camera but stand along world Y, like the native billboards
 * and the scenery around them: a rolled view (first person) rolls them with
 * it, while an unrolled view keeps them exactly screen-upright as before. */
static float billboard_close(float a,float b){float d=a-b;return d<0?-d:d;}
/* Freestanding: Taylor series, ample for |x| <= pi/2. */
static float sinf_approx(float x){float term=x,sum=x;for(UINT n=1;n<10;++n){term*=-x*x/((2*n)*(2*n+1));sum+=term;}return sum;}
static float cosf_approx(float x){float term=1,sum=1;for(UINT n=1;n<10;++n){term*=-x*x/((2*n-1)*(2*n));sum+=term;}return sum;}
static void test_billboard_roll(void){
    static BYTE quad[32];SHORT *s=(SHORT *)quad;
    s[0]=4096;s[1]=-2048;s[2]=8192;s[3]=0; /* anchor (1,-.5,2) */
    s[8]=256;s[10]=256;s[9]=512;s[11]=256; /* width .25, height .125 */
    const float width=.25f,height=.125f;
    /* pitch (about X) then roll (about the view axis), degrees; the last one
     * looks straight along world Y, where there is no roll to follow. */
    const float pitches[5]={0,20,-35,20,90},rolls[5]={0,0,0,30,-50};
    for(UINT k=0;k<5;++k){
        float t=pitches[k]*3.14159265f/180,f=rolls[k]*3.14159265f/180;
        float ct=cosf_approx(t),st=sinf_approx(t),cf=cosf_approx(f),sf=sinf_approx(f);
        /* R=Rz(f)Rx(t) for column vectors; the queue matrix stores R's
         * columns (the images of local X, Y and Z) as its rows. */
        float r[3][3]={{cf,-sf*ct,sf*st},{sf,cf*ct,-cf*st},{0,st,ct}};
        float m[16]={0};for(UINT row=0;row<3;++row)for(UINT col=0;col<3;++col)m[row*4+col]=r[col][row];
        m[12]=.3f;m[13]=-.2f;m[14]=10;m[15]=1;
        static LitVertex v[4];correct_billboards(v,1,quad,m);
        float view[4][3];
        for(UINT c=0;c<4;++c)for(UINT axis=0;axis<3;++axis)
            view[c][axis]=v[c].x*m[axis]+v[c].y*m[4+axis]+v[c].z*m[8+axis]+m[12+axis];
        /* Corner 0 is top left, 1 top right, 2 bottom left (the anchor row). */
        float up[3],across[3];
        for(UINT axis=0;axis<3;++axis){up[axis]=view[0][axis]-view[2][axis];across[axis]=view[1][axis]-view[0][axis];}
        float want_up[2]={0,-height},want_across[2]={width,0};
        if(k<4){want_up[0]=height*sf;want_up[1]=-height*cf;want_across[0]=width*cf;want_across[1]=width*sf;}
        if(billboard_close(up[0],want_up[0])>1e-4f || billboard_close(up[1],want_up[1])>1e-4f || billboard_close(up[2],0)>1e-4f)ExitProcess(228);
        if(billboard_close(across[0],want_across[0])>1e-4f || billboard_close(across[1],want_across[1])>1e-4f || billboard_close(across[2],0)>1e-4f)ExitProcess(248);
    }
}
