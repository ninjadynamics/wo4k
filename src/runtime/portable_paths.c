/* Game-local read redirection; the original EXE's short filename buffers and
 * all external/system files remain unchanged. Installed data is portable. */
typedef HANDLE (WINAPI *WoOpenFn)(LPCSTR,DWORD,DWORD,LPSECURITY_ATTRIBUTES,DWORD,DWORD,HANDLE);
static WoOpenFn wo_original_open;
static BOOL wo_prefix(const char *p,const char *prefix) {
    while(*prefix) {char a=*p++,b=*prefix++;if(a>='A' && a<='Z')a+=32;if(b>='A' && b<='Z')b+=32;if(a!=b)return FALSE;}return TRUE;
}
static const char *wo_path(const char *input,char *output,UINT capacity) {
    if(!input)return input;
    /* The game builds sound paths as .\\\\sound\\name (two separators).
     * Consume the complete relative prefix, never an absolute/UNC prefix. */
    const char *p=input;
    while(p[0]=='.' && (p[1]=='\\' || p[1]=='/')) {
        ++p;while(*p=='\\' || *p=='/')++p;
    }
    const char *prefix=0,*tail=p;
    if(wo_prefix(p,"sound\\") || wo_prefix(p,"sound/")) {prefix="audio\\";tail=p+6;}
    else if(wo_prefix(p,"pics\\") || wo_prefix(p,"pics/") || wo_prefix(p,"blitsurf\\") || wo_prefix(p,"blitsurf/"))prefix="textures\\sd\\";
    else if(wo_prefix(p,"textures\\") || wo_prefix(p,"textures/")) {
        if(wo_prefix(p+9,"sd\\") || wo_prefix(p+9,"hd\\") || wo_prefix(p+9,"sd/") || wo_prefix(p+9,"hd/"))return input;
        prefix="textures\\sd\\";
    } else if(wo_prefix(p,"common\\") || wo_prefix(p,"common/") || wo_prefix(p,"track")) {
        const char *extension=0;for(const char *s=p;*s;++s)if(*s=='.')extension=s;
        if(extension && (wo_prefix(extension,".cmp") || wo_prefix(extension,".tim") || wo_prefix(extension,".tex")))prefix="textures\\sd\\";
    } else if(wo_prefix(p,"debug.txt") && !p[9]) {prefix="..\\logs\\game-debug.txt";tail="";}
    if(!prefix)return input;
    UINT n=0;while(*prefix && n+1<capacity)output[n++]=*prefix++;
    if(*prefix)return input;while(*tail && n+1<capacity)output[n++]=*tail++;
    if(*tail)return input;output[n]=0;return output;
}
static HANDLE WINAPI wo_portable_open(LPCSTR name,DWORD access,DWORD share,LPSECURITY_ATTRIBUTES security,DWORD create,DWORD flags,HANDLE template_file) {
    char redirected[512];return wo_original_open(wo_path(name,redirected,sizeof(redirected)),access,share,security,create,flags,template_file);
}
static void wo_paths_initialize(BYTE *image) {
    IMAGE_NT_HEADERS32 *nt=(IMAGE_NT_HEADERS32 *)(image+((IMAGE_DOS_HEADER *)image)->e_lfanew);
    IMAGE_IMPORT_DESCRIPTOR *desc=(IMAGE_IMPORT_DESCRIPTOR *)(image+nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);
    for(;desc->Name;++desc) {
        if(!desc->OriginalFirstThunk)continue;
        IMAGE_THUNK_DATA32 *names=(IMAGE_THUNK_DATA32 *)(image+desc->OriginalFirstThunk),*slots=(IMAGE_THUNK_DATA32 *)(image+desc->FirstThunk);
        for(UINT i=0;names[i].u1.AddressOfData;++i) {
            if(names[i].u1.Ordinal&IMAGE_ORDINAL_FLAG32)continue;
            IMAGE_IMPORT_BY_NAME *name=(IMAGE_IMPORT_BY_NAME *)(image+names[i].u1.AddressOfData);
            if(wo_prefix((const char *)name->Name,"CreateFileA") && !name->Name[11]) {
                DWORD old,ignored;if(!VirtualProtect(&slots[i],sizeof(slots[i]),PAGE_READWRITE,&old))return;
                wo_original_open=(WoOpenFn)(UINT_PTR)slots[i].u1.Function;slots[i].u1.Function=(DWORD)(UINT_PTR)wo_portable_open;
                VirtualProtect(&slots[i],sizeof(slots[i]),old,&ignored);return;
            }
        }
    }
}
