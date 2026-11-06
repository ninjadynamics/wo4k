/* HD artwork installation (manifests/hd-textures.json).
 *
 * PNGs are discovered in the shipped artwork folder and in an optional
 * extra folder (whose files win and are copied into the shipped folder).
 * Each PNG is either a catalog entry or a native texture name ("SKY01.png",
 * "HUD2.page1.png", optionally under Pics/Hi-res or Pics/Lo-res) whose page
 * count is inferred from the game's TGA sheets. Every image is decoded and
 * validated before anything changes. Catalog "region" entries are written
 * to textures/hd-regions.bin, which is only rewritten when its bytes change.
 * The PNG files themselves are never modified. */
#ifndef WO_HDTEX_H
#define WO_HDTEX_H

#include "core/wo_base.h"
#include "core/wo_hashcache.h"
#include "core/wo_hdregions.h"
#include "core/wo_json.h"

typedef struct WoHdOptions {
    const WoJson *catalog;     /* parsed manifests/hd-textures.json (an array) */
    const char *canonical_dir; /* <mod>/assets/hd */
    const char *source_dir;    /* optional extra artwork folder (may equal canonical_dir) */
    const char *game_root;
    WoHashCache *cache;        /* optional: skips decoding PNGs whose bytes were seen */
} WoHdOptions;

typedef struct WoHdResult {
    size_t checked;           /* PNGs validated */
    size_t updated;           /* PNGs copied plus 1 if the region index changed */
    size_t region_placements;
    bool region_index_updated;
    WoJson *files;            /* [{file, kind, width, height}], owned by the caller */
} WoHdResult;

bool wo_hd_install(const WoHdOptions *options, WoHdResult *result, WoError *error);
void wo_hd_result_free(WoHdResult *result);

/* Write the index to path unless the file already holds exactly these
 * bytes. *changed tells whether it was written. */
bool wo_hd_regions_save(const WoHdRegionIndex *index, const char *path, bool *changed,
                        WoError *error);

/* Build the region index from the catalog, the artwork folders and the
 * game's SD sheets without writing anything (used by wo_hd_install and to
 * verify an installation). */
bool wo_hd_build_index(const WoHdOptions *options, WoHdRegionIndex *index, WoHdResult *result,
                       WoError *error);

#endif
