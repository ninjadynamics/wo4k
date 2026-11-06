/* The game's standard-definition texture folder (game_root/textures/sd).
 *
 * wo_sd_extract copies, from a PC game folder:
 *   - an existing textures/sd tree (a previous WO4K install),
 *   - the Pics, Blitsurf and Textures folders (or their textures/sd copies),
 *   - the .cmp/.tim/.tex containers of every TrackNN and Common folder (they
 *     stay in the game data too; this exposes them beside the sheets),
 * and records every file with its SHA-256 in destination/sources.json. */
#ifndef WO_SDTEX_H
#define WO_SDTEX_H

#include "core/wo_base.h"

/* native_root must be a folder (disc images are imported to a folder first). */
bool wo_sd_extract(const char *native_root, const char *destination, WoError *error);

/* Copy the 2097-named title images to the XL names the game build loads,
 * where only the former exist (Hi-res and Lo-res Blitsurf/Pics folders). */
bool wo_sd_add_regional_aliases(const char *sd_root, WoError *error);

/* Maintainer-only: decode every .tga, .tim and .cmp below sd_root to PNG
 * files under sd_root/exports (CMP frames go to <name>.cmp.png.parts/NNNN.png). */
bool wo_sd_export_png(const char *sd_root, WoError *error);

#endif
