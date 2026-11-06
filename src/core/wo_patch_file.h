/* Executable patching on disk: loading recipes, applying them with source
 * and target SHA-256 verification, choosing the patch for a game folder's
 * executable, and (for maintainers) creating a recipe from two files. */
#ifndef WO_PATCH_FILE_H
#define WO_PATCH_FILE_H

#include "core/wo_base.h"
#include "core/wo_json.h"
#include "core/wo_patch.h"

bool wo_patch_load(const char *recipe_path, WoPatchRecipe *out, WoError *error);

/* Patch source into target. The source must match the recipe's length and
 * SHA-256, and the result must match the target SHA-256, or nothing is
 * written. The target is replaced atomically. */
bool wo_patch_apply_file(const char *source, const char *recipe_path, const char *target,
                         WoError *error);

/* Install the game executable from source_dir as destination_dir/Wipeout2097.exe:
 * the first of Wipeout2.exe, Wipeout2_HighTest.exe, Wipeout2097.exe or
 * D3D.exe whose SHA-256 is either the patched output (copied) or a listed
 * source (patched with its recipe, a path relative to mod_root). manifest
 * is manifests/patches.json. */
bool wo_patch_install_executable(const char *source_dir, const char *destination_dir,
                                 const WoJson *manifest, const char *mod_root, WoError *error);

/* Maintainer helper: write a recipe that turns source into target. */
bool wo_patch_create(const char *source, const char *target, const char *recipe_path,
                     WoError *error);

#endif
