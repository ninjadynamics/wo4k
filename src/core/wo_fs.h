/* Filesystem operations (implemented for Windows in wo_fs_win.c).
 *
 * Paths are UTF-8; '/' and '\' are both accepted and results use '\'. Long
 * paths work regardless of the system's long-path setting. A "link" means a
 * name-surrogate reparse point (symbolic link or junction); OneDrive and
 * other cloud placeholders are ordinary files and folders here. */
#ifndef WO_FS_H
#define WO_FS_H

#include "core/wo_base.h"
#include "core/wo_io.h"
#include "core/wo_json.h"

/* --- Paths --------------------------------------------------------------------- */
char *wo_path_full(const char *path, WoError *error);
/* base + '\' + name, without normalisation; '/' in name becomes '\'. */
char *wo_path_join(const char *base, const char *name, WoError *error);
/* Resolve root\relative and require the result to stay strictly inside
 * root. Rejects absolute or drive-relative input, ':' (alternate data
 * streams), NUL and anything that normalises outside the root. */
char *wo_path_child(const char *root, const char *relative, WoError *error);
/* Both arguments must be full paths. True when path is root itself (if
 * allow_equal) or lies beneath it; case-insensitive. */
bool wo_path_is_within(const char *root, const char *path, bool allow_equal);
const char *wo_path_basename(const char *path);
/* Parent directory, or NULL for a root such as "C:\". */
char *wo_path_parent(const char *path, WoError *error);
/* ".zip" (including the dot) or "" when there is none. */
const char *wo_path_extension(const char *path);
char *wo_path_change_extension(const char *path, const char *extension, WoError *error);
bool wo_path_is_absolute(const char *path);
/* Path of 'path' relative to 'root' with '/' separators, or NULL when path
 * is not inside root. */
char *wo_path_relative(const char *root, const char *path, WoError *error);

/* --- Queries ------------------------------------------------------------------- */
typedef struct WoFileInfo {
    bool exists;
    bool is_dir;
    bool is_link;
    uint32_t attributes;
    uint64_t size;
    uint64_t mtime; /* FILETIME ticks (100 ns since 1601, UTC) */
} WoFileInfo;

bool wo_stat(const char *path, WoFileInfo *info, WoError *error); /* missing is not an error */
bool wo_exists(const char *path);
bool wo_is_file(const char *path);
bool wo_is_dir(const char *path);
bool wo_is_link(const char *path);
/* True if path or any existing parent up to (and including) root is a link.
 * path must lie within root. */
bool wo_chain_has_link(const char *root, const char *path);
bool wo_dir_is_empty(const char *path);

/* --- Whole-file helpers ------------------------------------------------------------ */
bool wo_read_file(const char *path, size_t max_bytes, WoBuf *out, WoError *error);
/* NUL-terminated contents; a UTF-8 BOM is removed. */
bool wo_read_text(const char *path, size_t max_bytes, char **out, WoError *error);
/* Create or overwrite, creating parent directories. */
bool wo_write_file(const char *path, const void *data, size_t len, WoError *error);
/* Write a temporary sibling, flush it, then atomically replace path. A
 * reader sees either the old or the new file, never a partial one. */
bool wo_write_file_atomic(const char *path, const void *data, size_t len, WoError *error);
bool wo_read_json(const char *path, size_t max_bytes, WoJson **out, WoError *error);
/* Pretty-printed, atomic. */
bool wo_write_json(const char *path, const WoJson *value, WoError *error);
/* As wo_write_json, but a file that already holds exactly these bytes is
 * left untouched (no write, no flush). *written (may be NULL) tells. */
bool wo_write_json_if_changed(const char *path, const WoJson *value, bool *written,
                              WoError *error);

/* --- Change detection -------------------------------------------------------------- */
/* What the file system records about a file's current contents. Equal
 * identities mean unchanged contents as far as it can tell: the change time
 * moves with every write and every metadata change (restoring an old
 * last-write time included), and the file id changes when a file is
 * replaced by another. */
typedef struct WoFileIdentity {
    uint64_t size;
    uint64_t mtime;  /* last write, FILETIME ticks (100 ns since 1601, UTC) */
    uint64_t ctime;  /* change time, same unit; 0 where the file system has none */
    uint64_t volume; /* volume serial number */
    uint8_t id[16];  /* file id on that volume */
} WoFileIdentity;

/* Identity of an existing file (a folder is WO_ERR_UNSUPPORTED). Opens it
 * for attributes only, so security software has nothing to scan. */
bool wo_file_identity(const char *path, WoFileIdentity *out, WoError *error);
bool wo_file_identity_eq(const WoFileIdentity *a, const WoFileIdentity *b);
/* Current time on the clock file times use (FILETIME ticks, UTC). */
uint64_t wo_fs_now(void);

/* --- Changes ------------------------------------------------------------------------- */
bool wo_mkdirs(const char *path, WoError *error);
bool wo_mkparent(const char *path, WoError *error);
/* Copy a file, overwriting the destination and creating its parents. */
bool wo_copy_file(const char *from, const char *to, WoError *error);
/* Rename a file or directory; fails if the destination exists. */
bool wo_move(const char *from, const char *to, WoError *error);
/* Rename a file over an existing one (same volume), write-through. */
bool wo_replace_file(const char *from, const char *to, WoError *error);
/* Delete a file; a missing file is fine. Read-only files are deleted too. */
bool wo_delete_file(const char *path, WoError *error);
bool wo_delete_empty_dir(const char *path, WoError *error);
/* Recursively delete path, which must lie strictly inside guard_root.
 * Links inside are removed without touching their targets. Missing is fine. */
bool wo_delete_tree(const char *guard_root, const char *path, WoError *error);

/* --- Directory listing ------------------------------------------------------------------ */
typedef struct WoDirEntry {
    char *name;
    bool is_dir;
    bool is_link;
    uint32_t attributes;
    uint64_t size;
    uint64_t mtime;
} WoDirEntry;

typedef struct WoDirList {
    WoDirEntry *items;
    size_t count;
    size_t cap;
} WoDirList;

/* Entries sorted case-insensitively, without "." and "..". */
bool wo_list_dir(const char *dir, WoDirList *out, WoError *error);
void wo_dirlist_free(WoDirList *list);

/* Depth-first walk below root (root itself is not reported). relative uses
 * '/' separators. Directories are reported before their contents; set
 * *descend = false to skip one. Links are reported but never followed.
 * max_depth limits recursion: 0 lists only root's own entries, -1 means no
 * limit. Returning false from the callback stops the walk with that error. */
typedef bool (*WoWalkFn)(void *context, const char *path, const char *relative,
                         const WoDirEntry *entry, bool *descend, WoError *error);
bool wo_walk(const char *root, int max_depth, WoWalkFn fn, void *context, WoError *error);

/* --- Streaming file access ---------------------------------------------------------------- */
typedef struct WoFile WoFile;

bool wo_file_open_read(const char *path, WoFile **out, WoError *error);
/* Create or truncate for writing, creating parent directories. */
bool wo_file_create(const char *path, WoFile **out, WoError *error);
/* Exact positional read; a short read is a WO_ERR_FORMAT failure. */
bool wo_file_read_at(WoFile *file, uint64_t offset, void *dst, size_t len, WoError *error);
/* Sequential read of up to len bytes; *got is 0 at end of file. */
bool wo_file_read(WoFile *file, void *dst, size_t len, size_t *got, WoError *error);
bool wo_file_write(WoFile *file, const void *data, size_t len, WoError *error);
bool wo_file_flush(WoFile *file, WoError *error);
uint64_t wo_file_size(const WoFile *file);
const char *wo_file_path(const WoFile *file);
/* Close; for written files check the result to catch delayed write errors. */
bool wo_file_close(WoFile *file, WoError *error);
/* Identity of an open file: taken before and after reading it, equal
 * results show nothing changed it meanwhile. */
bool wo_file_identity_of(WoFile *file, WoFileIdentity *out, WoError *error);

/* A WoSource reading a file; wo_source_close releases it. */
bool wo_source_open_file(WoSource *source, const char *path, WoError *error);

#endif
