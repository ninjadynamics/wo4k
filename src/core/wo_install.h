/* Building blocks of Install / repair shared with reconcile and refresh.
 * The operations themselves are declared in wo_ops.h. */
#ifndef WO_INSTALL_H
#define WO_INSTALL_H

#include "core/wo_base.h"
#include "core/wo_hashcache.h"
#include "core/wo_json.h"
#include "core/wo_mod.h"
#include "core/wo_ops.h"

/* Read a manifest below the mod folder (e.g. "manifests/patches.json"). */
bool wo_load_manifest(const WoMod *mod, const char *relative, WoJson **out, WoError *error);
/* manifests/texture-selection.json, with each scenery track's boards of
 * manifests/billboard-layout.json (when there is one) as its "billboards",
 * its sky of manifests/sky-layout.json as its "sky" while the sky's
 * picture is authored (assets/auth), and, for a track with boards, the
 * authored pictures its scenery textures show as its "shown" (by texture
 * number, each its own or its group's of manifests/auth-masters.json: the
 * boards' bars take their colour from them). */
bool wo_load_plan(const WoMod *mod, WoJson **out, WoError *error);
/* The plan without what follows authored artwork (its skies' layouts and
 * the pictures its boards show): for HD artwork turned off, when the game
 * shows its own pictures. */
void wo_plan_drop_authored(WoJson *plan);
/* manifests/auth-masters.json (groups of names that are one picture), or
 * NULL without error when there is none. */
bool wo_load_masters(const WoMod *mod, WoJson **out, WoError *error);

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

/* Where wo_install_debanded keeps every named picture as the sheets have it,
 * below the game folder: what the upscaler starts from. */
#define WO_ORIGINALS_DIR "textures/originals"

/* With `wanted`, make the upscaled textures of assets/hd from the pictures
 * of game_root's WO_ORIGINALS_DIR where they are missing or out of date
 * (core/wo_upscaled.h): every picture, debanded or not, debanded once
 * upscaled. Without, take away the ones made before. */
bool wo_install_upscaled(const WoMod *mod, const char *game_root, bool wanted,
                         const WoJson *catalog, WoHashCache *cache, WoProgress *progress,
                         WoError *error);

#endif
