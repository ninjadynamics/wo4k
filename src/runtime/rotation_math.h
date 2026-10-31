/* Float equivalents of the four native local rotations. The original integer
 * routines remain authoritative for simulation; these are for rendering only.
 * Angle units and axis/order/signs match the supplied PC reference source.
 */
static float render_sin(int angle) {
    double radians=(angle%4096)*(6.283185307179586476925286766559/4096.0),value;
    __asm__ volatile("fsin" : "=t"(value) : "0"(radians));
    return (float)value;
}
static float render_cos(int angle) {return render_sin(angle+1024);}
static void rotation_float(float *m,UINT order,SHORT a,SHORT b,SHORT c) {
    float sx,sy,sz,cx,cy,cz;
    int x,y,z;
    if(order==0) {x=a;y=b;z=c;}
    else if(order==1) {y=a;z=b;x=c;}
    else if(order==2) {y=a;x=b;z=c;}
    else {z=a;x=b;y=c;}
    sx=render_sin(x);sy=render_sin(y);sz=render_sin(z);
    cx=render_cos(x);cy=render_cos(y);cz=render_cos(z);
    if(order==0) {
        m[0]=cy*cz;m[1]=-cy*sz;m[2]=-sy;
        m[3]=cx*sz-sx*sy*cz;m[4]=cx*cz+sx*sy*sz;m[5]=-sx*cy;
        m[6]=sx*sz+cx*sy*cz;m[7]=sx*cz-cx*sy*sz;m[8]=cx*cy;
    } else if(order==1) {
        m[0]=cz*cy;m[1]=-sy*sx+cy*sz*cx;m[2]=-sy*cx-cy*sz*sx;
        m[3]=-sz;m[4]=cz*cx;m[5]=-cz*sx;
        m[6]=cz*sy;m[7]=cy*sx+sy*sz*cx;m[8]=cy*cx-sy*sz*sx;
    } else if(order==2) {
        m[0]=cy*cz+sy*sz*sx;m[1]=cy*sz-sy*sx*cz;m[2]=-sy*cx;
        m[3]=-sz*cx;m[4]=cx*cz;m[5]=-sx;
        m[6]=sy*cz-cy*sx*sz;m[7]=sy*sz+cy*sx*cz;m[8]=cx*cy;
    } else {
        m[0]=cy*cz-sz*sx*sy;m[1]=sz*cx;m[2]=-sy*cz-sz*sx*cy;
        m[3]=-sz*cy-sx*cz*sy;m[4]=cx*cz;m[5]=sz*sy-sx*cz*cy;
        m[6]=cx*sy;m[7]=sx;m[8]=cx*cy;
    }
}
