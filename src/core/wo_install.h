/* Building blocks of Install / repair shared with reconcile and refresh.
 * The operations themselves are declared in wo_ops.h. */
#ifndef WO_INSTALL_H
#define WO_INSTALL_H

#include "core/wo_base.h"
#include "core/wo_hashcache.h"
#include "core/wo_json.h"
#include "core/wo_mod.h"

/* Read a manifest below the mod folder (e.g. "manifests/patches.json"). */
bool wo_load_manifest(const WoMod *mod, const char *relative, WoJson **out, WoError *error);

/* Hashes of the required Windows game files and original sound effects in
 * game_root, as recorded in the installation receipt ("pc_files"). */
bool wo_pc_file_receipt(const WoMod *mod, const char *game_root, WoJson **out, WoError *error);

/* Copy the mod runtime (DLLs, controller host, video player, DDrawCompat
 * settings template, shaders) from runtime/ into game_root. */
bool wo_install_runtime(const WoMod *mod, const char *game_root, WoError *error);

/* Deband the textures game_root's sheets show into the mod's assets/sd, as
 * manifests/deband-selection.json has them chosen (wo_deband.h).
 * native_receipt is what wo_natives_install returned for those sheets. The
 * game plays without the result, so a failure is logged, not returned. */
void wo_install_debanded(const WoMod *mod, const char *game_root, const WoJson *plan,
                         const WoJson *native_receipt, WoHashCache *cache);

#endif
