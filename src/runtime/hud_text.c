/* Native text quads use half-width atlas pixels and integer advances.
 * Center the visible run, excluding leading/trailing spacing and the last
 * glyph's unused advance. Keep the fractional start for the vertex hook. */
static BOOL hud_text_origin(const BYTE *lookup,const BYTE *font,const char *text,float *origin) {
    if(!text)return FALSE;
    int pen=0;float left=0,right=0;BOOL visible=FALSE;
    for(UINT i=0;i<1024;++i) {
        BYTE character=(BYTE)text[i];
        if(!character) {
            if(visible)*origin=160.0f-(left+right)*0.5f;
            return visible;
        }
        UINT glyph=character>=32 && character<96?lookup[character]:50;
        if(glyph==50) {pen+=8;continue;}
        const BYTE *metric=font+glyph*8;
        UINT width=(BYTE)*(const WORD *)(metric+4);
        if(width) {
            float edge=(float)pen+width*0.5f;
            if(!visible)left=(float)pen;
            if(!visible || edge>right)right=edge;
            visible=TRUE;
        }
        pen+=*(const SHORT *)(metric+6)>>1;
    }
    return FALSE;
}
