#include "core/wo_tree.h"

#include <string.h>

#include "core/wo_fs.h"

void wo_tree_free(WoTreeList *list) {
    if (!list) {
        return;
    }
    for (size_t i = 0; i < list->count; i++) {
        wo_free(list->items[i].path);
        wo_free(list->items[i].relative);
    }
    wo_free(list->items);
    memset(list, 0, sizeof(*list));
}

static bool push_entry(WoTreeList *list, const char *dir, const char *relative,
                       const WoDirEntry *entry, WoError *error) {
    if (list->count >= WO_TREE_MAX_ENTRIES) {
        return wo_fail(error, WO_ERR_LIMIT, "The folder contains more than %u files.",
                       WO_TREE_MAX_ENTRIES);
    }
    if (list->count == list->cap) {
        size_t cap = list->cap ? list->cap * 2 : 64;
        WoTreeEntry *grown = wo_realloc_array(list->items, cap, sizeof(*grown), error);
        if (!grown) {
            return false;
        }
        list->items = grown;
        list->cap = cap;
    }
    char *path = wo_path_join(dir, entry->name, error);
    char *rel = path ? (relative[0] ? wo_format(error, "%s/%s", relative, entry->name)
                                    : wo_strdup(entry->name, error))
                     : NULL;
    if (!rel) {
        wo_free(path);
        return false;
    }
    WoTreeEntry *item = &list->items[list->count++];
    item->path = path;
    item->relative = rel;
    item->size = entry->size;
    item->is_dir = entry->is_dir;
    item->is_link = entry->is_link;
    return true;
}

static bool list_inner(const char *dir, const char *relative, int depth, WoTreeList *out,
                       WoError *error) {
    if (depth > WO_TREE_MAX_DEPTH) {
        return wo_fail(error, WO_ERR_LIMIT, "Folders are nested too deeply below \"%s\".", dir);
    }
    WoDirList list = {0};
    if (!wo_list_dir(dir, &list, error)) {
        return false;
    }
    bool ok = true;
    for (size_t i = 0; ok && i < list.count; i++) {
        const WoDirEntry *entry = &list.items[i];
        if (!entry->is_dir || entry->is_link) {
            ok = push_entry(out, dir, relative, entry, error);
        }
    }
    for (size_t i = 0; ok && i < list.count; i++) {
        const WoDirEntry *entry = &list.items[i];
        if (!entry->is_dir || entry->is_link) {
            continue;
        }
        char *path = wo_path_join(dir, entry->name, error);
        char *rel = path ? (relative[0] ? wo_format(error, "%s/%s", relative, entry->name)
                                        : wo_strdup(entry->name, error))
                         : NULL;
        ok = rel && list_inner(path, rel, depth + 1, out, error);
        wo_free(rel);
        wo_free(path);
    }
    wo_dirlist_free(&list);
    return ok;
}

bool wo_tree_list(const char *root, WoTreeList *out, WoError *error) {
    memset(out, 0, sizeof(*out));
    char *full = wo_path_full(root, error);
    if (!full) {
        return false;
    }
    bool ok = !wo_is_dir(full) || list_inner(full, "", 0, out, error);
    wo_free(full);
    if (!ok) {
        wo_tree_free(out);
    }
    return ok;
}

bool wo_copy_tree(const char *source, const char *destination, WoError *error) {
    WoTreeList list;
    if (!wo_tree_list(source, &list, error)) {
        return false;
    }
    bool ok = true;
    for (size_t i = 0; ok && i < list.count; i++) {
        if (list.items[i].is_link) {
            ok = wo_fail(error, WO_ERR_UNSAFE,
                         "Linked files are not supported as game sources: %s",
                         list.items[i].path);
        }
    }
    for (size_t i = 0; ok && i < list.count; i++) {
        char *target = wo_path_child(destination, list.items[i].relative, error);
        ok = target && wo_copy_file(list.items[i].path, target, error);
        wo_free(target);
    }
    wo_tree_free(&list);
    return ok;
}
