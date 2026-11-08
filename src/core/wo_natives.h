/* Native texture restoration driven by manifests/texture-selection.json:
 * PC regions recovered from the game's own files and, when the European
 * PlayStation disc (SLES-00327) is supplied, regions from its textures.
 *
 * Layout below the game folder:
 *   textures/sd/...            the packed RGB555 sheets the game loads
 *   textures/native-source/... pristine copies of those sheets (taken once)
 *   textures/ps1-source/...    files imported from the PlayStation disc,
 *                              with source.json describing the import
 *   textures/selection.json    receipt of the last build
 * Sheets are always rebuilt from the pristine copies, so a rebuild never
 * patches an already patched sheet. */
#ifndef WO_NATIVES_H
#define WO_NATIVES_H

#include "core/wo_base.h"
#include "core/wo_hashcache.h"
#include "core/wo_json.h"

#define WO_PS1_EDITION "WipEout 2097 Europe SLES-00327"

typedef struct WoPs1Disc {
    char *image;     /* file holding the data track */
    uint64_t offset; /* byte offset of the data track */
} WoPs1Disc;

void wo_ps1_disc_free(WoPs1Disc *disc);

/* Find the European disc in source (a folder searched recursively, CUE and
 * MDS descriptors first, or a single .cue/.mds/.mdf/.bin/.iso file).
 * *found is false when there is none; images that cannot be read are
 * skipped, but a damaged CUE/MDS descriptor is an error. */
bool wo_ps1_find_disc(const char *source, WoPs1Disc *out, bool *found, WoError *error);

/* Extract the disc's texture archives (COMMON, TEXTURES and TRACKnn .CMP,
 * .TIM, .TTF and .TEX files below WIPEOUT2) into destination, except those
 * the plan marks "pc_byte_identical", and write destination/source.json.
 * source must already be unpacked (no ZIP); configured_source is what the
 * user chose and is recorded in source.json (NULL: source). *imported is
 * false, without an error, when source holds only the PlayStation
 * soundtrack (a music folder or an audio-only disc descriptor). */
bool wo_ps1_import_textures(const char *source, const char *destination, const WoJson *plan,
                            const char *configured_source, bool *imported, WoError *error);

/* True in *healthy when textures/selection.json matches the plan's
 * revision and every patched sheet still has its recorded SHA-256. */
bool wo_natives_test(const char *game_root, const WoJson *plan, bool require_ps1, bool *healthy,
                     WoError *error);
/* The same, taking unchanged sheets' digests from cache (may be NULL). */
bool wo_natives_test_cached(const char *game_root, const WoJson *plan, bool require_ps1,
                            WoHashCache *cache, bool *healthy, WoError *error);

typedef struct WoNativeCounts {
    size_t pc_regions;
    size_t ps1_regions;
    size_t pending_ps1_regions; /* PlayStation regions whose source is not imported */
} WoNativeCounts;

/* Applies plan operations to sheets, reading sources from a game folder. */
typedef struct WoSheetBuilder WoSheetBuilder;

/* sd_root: the PC SD texture folder (game_root/textures/sd);
 * ps1_root: the imported PlayStation folder (may not exist). */
WoSheetBuilder *wo_sheet_builder_new(const char *game_root, const char *sd_root,
                                     const char *ps1_root, WoError *error);
void wo_sheet_builder_free(WoSheetBuilder *builder);
/* Patch every operation for atlas (matched ignoring case) into sheet, a
 * pristine RGB555 TGA held in memory. Counts are added to counts. */
bool wo_sheet_builder_apply(WoSheetBuilder *builder, const WoJson *plan, const char *atlas,
                            uint8_t *sheet, size_t len, WoNativeCounts *counts, WoError *error);

typedef struct WoNativeOptions {
    const char *game_root;
    const WoJson *plan;            /* parsed manifests/texture-selection.json */
    const char *ps1_source;        /* optional, already unpacked */
    const char *configured_source; /* optional, recorded in source.json */
    const char *retain_from;       /* optional previous game folder to keep caches from */
    bool incremental;              /* keep a healthy build when nothing new is supplied */
    bool require_ps1;              /* the PlayStation artwork must be present and complete */
    WoHashCache *cache;            /* optional: digests of unchanged sheets */
    /* The game folder's sheets were just extracted from original game data,
     * so they are the pristine copies: the previous folder's are not kept,
     * and a damaged one is never carried into a repair. */
    bool sheets_pristine;
} WoNativeOptions;

/* Build every sheet in the plan and write textures/selection.json. Each
 * sheet is built in memory and then replaces the old one atomically.
 * *receipt receives the receipt (format 2) as written or, for an
 * unchanged incremental run, as found. */
bool wo_natives_install(const WoNativeOptions *options, WoJson **receipt, WoError *error);

#endif
