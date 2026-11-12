/* Valparaiso's waterfall falls by turning colours. Its picture has sixteen,
 * and at every simulation step the game moves the last twelve along by one
 * place. On the PlayStation it then writes them back over the palette the
 * picture is drawn with, and the bands of white water run down. The PC game
 * kept the routine and its timing but emptied the two video-memory calls
 * around it: it neither reads the palette when the track loads nor writes it
 * back, and its sheets hold the picture already coloured. The water stood
 * still.
 *
 * The palette and the picture's sixteen-colour form are still in the track's
 * archive. They are read from there when the game asks for the palette; the
 * game's own routine turns it, and after a turn the picture's place on its
 * texture page is painted again. That page is one of the HD loader's: where
 * it would have left the game's own page alone, it makes one of the game's
 * size. A replacement for the turning picture itself is not shown, for a
 * still picture has no colours to turn.
 *
 * The routine names the palette by its place in video memory, which is the
 * PlayStation's. The PC's own archive is an earlier build: there that place
 * holds the palette of a trackside sign and the waterfall's lies elsewhere.
 * In both the fall is the model named "wfall", so the picture is the one
 * that model is drawn with. */
typedef void (__cdecl *PaletteCycleFn)(void);
static PaletteCycleFn old_palette_read,old_palette_turn;
/* The colours of a picture, and where the game's routine keeps them. */
enum {PALETTE_CYCLE_COLOURS=16,PALETTE_CYCLE_STORE=0x665130};
/* waiting: read, for the scenery sheet about to load. live: on a page of ours. */
typedef struct PaletteCycle {BOOL waiting,live,turned;UINT slot,x,y,width,height;} PaletteCycle;
static PaletteCycle palette_cycle;
static BYTE palette_cycle_texels[256*256],palette_cycle_window[8192];
/* An archive: a count, then for each file a 16-byte name, its size twice and
 * a zero; the files follow in that order. */
static const BYTE *palette_cycle_file(const BYTE *archive,UINT length,const char *name,UINT *size) {
    if(length<2)return 0;
    UINT count=*(const WORD *)archive,at=2+count*25;
    if(!count || at>length)return 0;
    for(UINT i=0;i<count;++i) {
        const BYTE *record=archive+2+i*25;UINT bytes=*(const DWORD *)(record+16),n=0;
        if(bytes!=*(const DWORD *)(record+20) || record[24] || bytes>length-at)return 0;
        while(n<16 && name[n] && (record[n]|32)==(BYTE)name[n])++n;
        if(n<16 && !name[n] && !record[n]){*size=bytes;return archive+at;}
        at+=bytes;
    }
    return 0;
}
static BOOL palette_cycle_bits(const BYTE *data,UINT end,UINT *bit,UINT count,UINT *value) {
    UINT read=0;
    for(UINT i=0;i<count;++i,++*bit) {
        if(*bit>=end)return FALSE;
        read=read<<1|((data[*bit>>3]>>(7-(*bit&7)))&1);
    }
    *value=read;return TRUE;
}
/* A texture file: a count, each picture's size, then all of them packed as
 * one stream of bits. A set bit comes before a byte; a clear one before a
 * 13-bit place among the last 8192 bytes (none ends the stream) and four
 * bits that count the bytes to repeat from there, less three. */
static BYTE *palette_cycle_unpack(const BYTE *packed,UINT length) {
    if(length<4)return 0;
    UINT count=*(const DWORD *)packed,total=0;
    if(!count || count>4096 || 4+count*4>length || length>0x1000000)return 0;
    for(UINT i=0;i<count;++i) {
        UINT size=*(const DWORD *)(packed+4+i*4);
        if(size>0x1000000 || total+size>0x1000000)return 0;
        total+=size;
    }
    BYTE *out=VirtualAlloc(0,total?total:1,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);if(!out)return 0;
    for(UINT i=0;i<sizeof(palette_cycle_window);++i)palette_cycle_window[i]=0;
    UINT bit=(4+count*4)*8,end=length*8,at=0,place=1,flag,value,repeat;
    for(;;) {
        if(!palette_cycle_bits(packed,end,&bit,1,&flag))break;
        if(flag) {
            if(at>=total || !palette_cycle_bits(packed,end,&bit,8,&value))break;
            out[at++]=palette_cycle_window[place]=(BYTE)value;place=(place+1)&8191;
            continue;
        }
        if(!palette_cycle_bits(packed,end,&bit,13,&value))break;
        if(!value) {if(at==total)return out;break;}
        if(!palette_cycle_bits(packed,end,&bit,4,&repeat) || repeat+3>total-at)break;
        for(UINT i=0;i<repeat+3;++i) {
            out[at++]=palette_cycle_window[place]=palette_cycle_window[(value+i)&8191];place=(place+1)&8191;
        }
    }
    VirtualFree(out,0,MEM_RELEASE);return 0;
}
static UINT palette_cycle_be16(const BYTE *p) {return (UINT)p[0]<<8|p[1];}
/* The number of the picture the scenery's waterfall is drawn with, or -1.
 * The models file is one object after another: a 144-byte heading (the name,
 * then counts of corners, normals and faces at 16, 24 and 32, high byte
 * first), eight bytes for every corner and normal, and the faces. A face is
 * four bytes that begin with its kind, then as many as its kind has; the
 * kinds with a picture name it after their corners. */
static int palette_cycle_picture(const BYTE *models,UINT length) {
    static const BYTE bytes[24]={0,12,24,12,28,20,32,24,40,12,24,12,28,24,36,32,48,12,12,0,52,24,36,12};
    static const BYTE picture[24]={0,0,6,0,8,0,6,0,8,0,8,0,10,0,12,0,16};
    for(UINT at=0;length-at>=144 && at<length;) {
        const BYTE *object=models+at;
        BOOL waterfall=object[0]=='w' && object[1]=='f' && object[2]=='a' && object[3]=='l' && object[4]=='l' && !object[5];
        UINT faces=palette_cycle_be16(object+32);
        at+=144+(palette_cycle_be16(object+16)+palette_cycle_be16(object+24))*8;
        for(UINT i=0;i<faces;++i) {
            if(at>length || length-at<4)return -1;
            UINT kind=palette_cycle_be16(models+at);at+=4;
            if(kind>=24 || !bytes[kind] || length-at<bytes[kind])return -1;
            if(waterfall && picture[kind])return (int)palette_cycle_be16(models+at+picture[kind]);
            at+=bytes[kind];
        }
    }
    return -1;
}
/* That picture: its place among the texture pages (page 0 starts at word
 * 320 of video memory, 64 words to a page, four texels to a word), its
 * texels and its colours as the archive has them. */
static BOOL palette_cycle_find(const BYTE *archive,UINT length,WORD *palette) {
    UINT size=0;const BYTE *models=palette_cycle_file(archive,length,"scene.prm",&size);
    int wanted=models?palette_cycle_picture(models,size):-1;
    const BYTE *packed=wanted>=0?palette_cycle_file(archive,length,"scene.cmp",&size):0;
    BYTE *pictures=packed?palette_cycle_unpack(packed,size):0;if(!pictures)return FALSE;
    UINT at=0;BOOL found=FALSE;
    if((UINT)wanted<*(const DWORD *)packed) {
        for(int i=0;i<wanted;++i)at+=*(const DWORD *)(packed+4+i*4);
        UINT bytes=*(const DWORD *)(packed+4+wanted*4);const BYTE *picture=pictures+at;
        /* Sixteen colours and a palette of its own. */
        UINT colours=bytes>=8+12+PALETTE_CYCLE_COLOURS*2+12 && *(const DWORD *)picture==0x10 &&
            (*(const DWORD *)(picture+4)&15)==8?*(const DWORD *)(picture+8):0;
        if(colours>=12+PALETTE_CYCLE_COLOURS*2 && colours<=bytes-8-12) {
            const BYTE *image=picture+8+colours;
            UINT block=*(const DWORD *)image,x=*(const WORD *)(image+4),y=*(const WORD *)(image+6);
            UINT words=*(const WORD *)(image+8),height=*(const WORD *)(image+10);
            if(x>=320 && x<1024 && words && height && (x-320)%64+words<=64 && y+height<=256 &&
               block>=12 && block<=bytes-8-colours && words*2*height<=block-12) {
                palette_cycle.slot=(x-320)/64;palette_cycle.x=(x-320)%64*4;palette_cycle.y=y;
                palette_cycle.width=words*4;palette_cycle.height=height;
                for(UINT j=0;j<words*2*height;++j) {
                    palette_cycle_texels[j*2]=image[12+j]&15;palette_cycle_texels[j*2+1]=image[12+j]>>4;
                }
                for(UINT j=0;j<PALETTE_CYCLE_COLOURS;++j)palette[j]=*(const WORD *)(picture+20+j*2);
                found=TRUE;
            }
        }
    }
    VirtualFree(pictures,0,MEM_RELEASE);return found;
}
static BYTE *palette_cycle_archive(UINT track,UINT *length) {
    WCHAR path[MAX_PATH],name[]=L"TRACK00\\TRACK.WAD";path[0]=0;
    name[5]=(WCHAR)(L'0'+track/10%10);name[6]=(WCHAR)(L'0'+track%10);
    if(!hd_append(path,MAX_PATH,ui_hd_game_root) || !hd_append(path,MAX_PATH,name))return 0;
    HANDLE file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,0,OPEN_EXISTING,0,0);if(file==INVALID_HANDLE_VALUE)return 0;
    LARGE_INTEGER size;BYTE *data=0;DWORD read=0;
    if(GetFileSizeEx(file,&size) && size.QuadPart>=2 && size.QuadPart<=0x2000000)
        data=VirtualAlloc(0,(UINT)size.QuadPart,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    if(data && (!ReadFile(file,data,(DWORD)size.QuadPart,&read,0) || read!=(DWORD)size.QuadPart)) {
        VirtualFree(data,0,MEM_RELEASE);data=0;
    }
    CloseHandle(file);*length=read;return data;
}
/* The game reads the palette here, on the one track that has it, before it
 * loads the track's sheet. */
static void __cdecl palette_cycle_read(void) {
    old_palette_read();
    palette_cycle.waiting=palette_cycle.live=FALSE;
    const BYTE *config=*(const BYTE *const *)(game+0x517958);if(!config)return;
    UINT length=0;BYTE *archive=palette_cycle_archive(config[1],&length);
    if(archive) {
        palette_cycle.waiting=palette_cycle_find(archive,length,(WORD *)(game+PALETTE_CYCLE_STORE));
        VirtualFree(archive,0,MEM_RELEASE);
    }
    wo_render_event("palette_cycle_read",palette_cycle.waiting);
}
static void __cdecl palette_cycle_turn(void) {
    old_palette_turn();palette_cycle.turned=TRUE;
}
/* A sheet loading over the picture's page ends what was painted there. The
 * picture last read for begins on the scenery sheet that does: the page of
 * that sheet to paint, or none. */
static int palette_cycle_claim(const char *filename,UINT first,UINT count) {
    if(palette_cycle.slot<first || palette_cycle.slot-first>=count)return -1;
    BOOL wanted=palette_cycle.waiting && hd_world_name(filename);
    palette_cycle.waiting=palette_cycle.live=FALSE;
    return wanted?(int)(palette_cycle.slot-first):-1;
}
/* The game's palette as page colours; like the sheets, colour 0 is see-through. */
static void palette_cycle_colours(DWORD *colours) {
    const WORD *palette=(const WORD *)(game+PALETTE_CYCLE_STORE);
    for(UINT i=0;i<PALETTE_CYCLE_COLOURS;++i) {
        UINT c=palette[i];
        colours[i]=((c&0x7fff)?0xff000000u:0)|((c&31)*255/31)<<16|((c>>5&31)*255/31)<<8|((c>>10&31)*255/31);
    }
}
static void palette_cycle_paint(BYTE *corner,UINT pitch,UINT scale,const DWORD *colours,DWORD solid) {
    for(UINT y=0;y<palette_cycle.height*scale;++y) {
        DWORD *out=(DWORD *)(corner+y*pitch);const BYTE *texels=palette_cycle_texels+y/scale*palette_cycle.width;
        for(UINT x=0;x<palette_cycle.width;++x) {
            DWORD colour=colours[texels[x]]|solid;
            for(UINT i=0;i<scale;++i)*out++=colour;
        }
    }
}
/* The page of pixels the loader is about to upload, with the picture as the
 * palette now has it. */
static BYTE *palette_cycle_page(const char *filename,UINT page,UINT pages,BYTE *pixels,UINT *width) {
    if(!pixels){pixels=hd_native_page(filename,page,pages,256);*width=256;}
    if(!pixels)return 0;
    UINT scale=*width/256;DWORD colours[PALETTE_CYCLE_COLOURS];palette_cycle_colours(colours);
    palette_cycle_paint(pixels+(palette_cycle.y*scale**width+palette_cycle.x*scale)*4,*width*4,scale,colours,0);
    palette_cycle.live=TRUE;palette_cycle.turned=FALSE;
    wo_render_event("palette_cycle_page",palette_cycle.slot);
    return pixels;
}
static BOOL palette_cycle_write(IDirectDrawSurface *surface,UINT scale,const DWORD *colours,DWORD solid) {
    RECT area={(LONG)(palette_cycle.x*scale),(LONG)(palette_cycle.y*scale),
        (LONG)((palette_cycle.x+palette_cycle.width)*scale),(LONG)((palette_cycle.y+palette_cycle.height)*scale)};
    DDSURFACEDESC lock={0};lock.dwSize=sizeof(lock);
    HRESULT hr=surface->lpVtbl->Lock(surface,&area,&lock,DDLOCK_WAIT|DDLOCK_WRITEONLY,0);
    if(hr>=0) {
        if(lock.lpSurface && lock.lPitch>=(LONG)(palette_cycle.width*scale*4))
            palette_cycle_paint(lock.lpSurface,(UINT)lock.lPitch,scale,colours,solid);
        else hr=E_FAIL;
        HRESULT unlock=surface->lpVtbl->Unlock(surface,lock.lpSurface);
        if(hr>=0)hr=unlock;
    }
    if(hr<0)wo_render_event("palette_cycle_error",(DWORD)hr);
    return hr>=0;
}
/* Once a frame at most, after it is presented: several steps may have turned
 * the palette since the last one. */
static void palette_cycle_present(void) {
    if(!palette_cycle.live || !palette_cycle.turned)return;
    palette_cycle.turned=FALSE;
    UiHdPage *page=ui_hd_pages+palette_cycle.slot,*opaque=ui_hd_opaque_pages+palette_cycle.slot;
    DWORD colours[PALETTE_CYCLE_COLOURS];palette_cycle_colours(colours);
    if(!page->surface || !palette_cycle_write(page->surface,page->width/256,colours,0) ||
       (opaque->surface && !palette_cycle_write(opaque->surface,opaque->width/256,colours,0xff000000u)))
        palette_cycle.live=FALSE;
}
static void initialize_palette_cycle(void) {
    static const BYTE read_entry[]={0x83,0xec,0x08,0x66,0xc7,0x44,0x24,0x00,0x70,0x03};
    static const BYTE turn_entry[]={0x66,0xa1,0x4e,0x51,0xa6,0x00};
    if(equal(game+0x114d0,read_entry,sizeof(read_entry)) && equal(game+0x11470,turn_entry,sizeof(turn_entry))) {
        old_palette_read=(PaletteCycleFn)hook(0x114d0,read_entry,sizeof(read_entry),palette_cycle_read);
        old_palette_turn=(PaletteCycleFn)hook(0x11470,turn_entry,sizeof(turn_entry),palette_cycle_turn);
    }
    wo_render_event("palette_cycle_hooks",old_palette_read && old_palette_turn);
}
