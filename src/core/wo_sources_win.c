#include "core/wo_sources.h"
#include "core/wo_win.h"

#include <knownfolders.h>
#include <shlobj.h>

#include "core/wo_fs.h"

static void push_known_folder(WoStrList *roots, REFKNOWNFOLDERID id) {
    wchar_t *path = NULL;
    if (SUCCEEDED(SHGetKnownFolderPath(id, 0, NULL, &path)) && path) {
        char *utf8 = wo_utf8(path, NULL);
        if (utf8 && wo_is_dir(utf8)) {
            wo_strlist_push_owned(roots, utf8, NULL);
        } else {
            wo_free(utf8);
        }
    }
    CoTaskMemFree(path);
}

bool wo_discovery_roots(const WoMod *mod, WoStrList *roots, WoError *error) {
    (void)error;
    push_known_folder(roots, &FOLDERID_Desktop);
    push_known_folder(roots, &FOLDERID_Downloads);
    push_known_folder(roots, &FOLDERID_Documents);
    char *parent = wo_path_parent(mod->root, NULL);
    if (parent && wo_is_dir(parent)) {
        wo_strlist_push_owned(roots, parent, NULL);
    } else {
        wo_free(parent);
    }
    return true;
}
