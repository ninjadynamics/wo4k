/* Recursive folder listing and copying for the asset installers.
 *
 * The listing order matches the previous installer: a folder's own files
 * (sorted case-insensitively), then each subfolder (sorted), depth first.
 * Links (symbolic links and junctions) are reported but never followed. */
#ifndef WO_TREE_H
#define WO_TREE_H

#include "core/wo_base.h"

#define WO_TREE_MAX_DEPTH 64
#define WO_TREE_MAX_ENTRIES 1000000u

typedef struct WoTreeEntry {
    char *path;     /* full path */
    char *relative; /* relative to the listed root, '/'-separated */
    uint64_t size;
    bool is_dir;    /* only directory links are reported as entries */
    bool is_link;
} WoTreeEntry;

typedef struct WoTreeList {
    WoTreeEntry *items;
    size_t count;
    size_t cap;
} WoTreeList;

/* List every file below root. Directory links appear as entries with
 * is_dir and is_link set, and are not descended into. A missing root gives
 * an empty list. */
bool wo_tree_list(const char *root, WoTreeList *out, WoError *error);
void wo_tree_free(WoTreeList *list);

/* Copy every file below source to the same relative path below
 * destination, overwriting existing files. Linked files or folders are
 * refused before anything is copied. */
bool wo_copy_tree(const char *source, const char *destination, WoError *error);

#endif
