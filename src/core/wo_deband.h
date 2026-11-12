/* The debanded texture layer (the mod's assets/sd folder).
 *
 * The PlayStation's textures have 16 or 256 colours, and a gradient shows
 * as bands. manifests/deband-selection.json records, for every texture
 * frame of the disc, whether to keep it as it is or smooth it at one of two
 * strengths: the choices exported from the comparison page that
 * tools/Deband-Ps1Textures.py builds. This step debands, on the player's
 * own machine, every chosen picture that the game's sheets show, writes
 * each as a PNG into assets/sd, and records where each belongs in
 * textures/sd-regions.bin (the format of hd-regions.bin). The game's
 * texture loader lays those PNGs over the sheet's own texels. A sheet holds
 * 15-bit colour, too few levels for a smooth ramp, so the result cannot go
 * into the sheet itself.
 *
 * What a sheet shows is what its build pasted last (wo_natives_regions):
 *   - A frame takes the choice made for it by name,
 *     "TRACK13/SCENE.CMP#0016".
 *   - A frame of the scenery archive in place is its track's SCENE.CMP
 *     frame of the same number while the picture is the same; the advert
 *     added for the track's boards is the first frame of the advert file.
 *   - A road is its tiles put together. It is debanded as one picture,
 *     each tile at the strength chosen for it.
 * Scenery frames whose borders meet in the track's model are debanded with
 * a margin of their neighbours, so a gradient that crosses two frames gets
 * one ramp.
 *
 * The filter is the maintainer tool's own (tools/deband/filter.c), with the
 * tool's settings for the two strengths.
 *
 * The step runs again only when something it was made from has changed:
 * the choices, the filter, or the sheets. PNGs deleted in between stay
 * deleted, and the game shows the sheet's own texels in their place. */
#ifndef WO_DEBAND_H
#define WO_DEBAND_H

#include "core/wo_base.h"
#include "core/wo_hashcache.h"
#include "core/wo_json.h"

typedef struct WoDebandOptions {
    const char *game_root;
    const char *output_dir;       /* <mod>/assets/sd */
    const char *selection;        /* manifests/deband-selection.json; nothing is done without it */
    const WoJson *plan;           /* parsed manifests/texture-selection.json */
    const WoJson *native_receipt; /* what wo_natives_install returned for these sheets */
    bool force;                   /* build even when nothing has changed */
    WoHashCache *cache;           /* optional */
} WoDebandOptions;

typedef struct WoDebandResult {
    bool built;      /* false: the layer in place was made from the same things */
    size_t pictures; /* PNGs written */
    size_t regions;  /* places on the sheets they cover */
} WoDebandResult;

bool wo_deband_install(const WoDebandOptions *options, WoDebandResult *result, WoError *error);

/* --- Parts, for tests ---------------------------------------------------------------- */

#define WO_DEBAND_SIDES 4 /* left, right, top, bottom */

typedef struct WoDebandJoin {
    bool set;
    int frame;     /* the frame that meets this border */
    int side;      /* with which of its borders */
    bool reversed; /* running the other way along it */
} WoDebandJoin;

/* Which borders of a track's scenery frames meet in its model: joins has
 * count * WO_DEBAND_SIDES entries, for frames of the given sizes (a width
 * of 0: no such frame). */
bool wo_deband_joins(const uint8_t *models, size_t len, const uint32_t *widths,
                     const uint32_t *heights, size_t count, WoDebandJoin *joins, WoError *error);

#endif
