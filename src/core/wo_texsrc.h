/* Decoding the native texture sources named by the fixed selection map
 * (portable). Files are fetched through a loader callback and cached, so a
 * WAD or CMP used by many regions is read and decoded once.
 *
 * A source is either a file (a TIM, or a CMP whose entries are TIM frames)
 * relative to the PC SD texture folder or the imported PlayStation folder,
 * or an entry of a WAD archive relative to the game folder. Roads are
 * composed from 32x32 tiles of a WAD's library CMP, laid out by its
 * 42-byte-per-material table (16 tiles at 128, 4 at 64, 1 at 32 pixels). */
#ifndef WO_TEXSRC_H
#define WO_TEXSRC_H

#include "core/wo_base.h"
#include "core/wo_texfmt.h"

#define WO_TEXSRC_MAX_FILE_BYTES (64u * 1024u * 1024u)

typedef enum WoTexOrigin {
    WO_TEX_FROM_GAME, /* WAD archives, relative to the game folder */
    WO_TEX_FROM_SD,   /* PC files, relative to textures/sd */
    WO_TEX_FROM_PS1   /* PlayStation files, relative to textures/ps1-source */
} WoTexOrigin;

/* Read a whole file (at most max_bytes) into out. */
typedef bool (*WoTexLoadFn)(void *context, WoTexOrigin origin, const char *relative,
                            size_t max_bytes, WoBuf *out, WoError *error);

typedef struct WoTexSources WoTexSources;

WoTexSources *wo_texsrc_new(WoTexLoadFn load, void *context, WoError *error);
void wo_texsrc_free(WoTexSources *sources);

/* Frame `index` of a source. With wad set, entry names the file inside it;
 * otherwise path is used, from the PlayStation folder when ps1 is set. Names
 * ending in .cmp are CMP sequences; anything else is a single TIM. */
bool wo_texsrc_frame(WoTexSources *sources, const char *path, const char *wad,
                     const char *entry, bool ps1, int64_t index, WoNativeImage *out,
                     WoError *error);

/* Compose road material `material` at size level (128, 64 or 32) from the
 * WAD's tile library and layout table. */
bool wo_texsrc_road(WoTexSources *sources, const char *wad, const char *library,
                    const char *table, int64_t material, int64_t level, WoNativeImage *out,
                    WoError *error);

#endif
