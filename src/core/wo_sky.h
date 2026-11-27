/* Skies laid out by choice (manifests/sky-layout.json).
 *
 * A track's sky is a ring or a dome of faces around the camera (sky.prm of
 * TRACKnn/TRACK.WAD) showing one picture (sky.cmp), which the 1996 models
 * repeat many times round the sky (eight or twelve) and, on Talon's Reach,
 * twice up it. So a sky could only be drawn as that one narrow picture. To
 * draw one in other proportions (a single picture all the way round, say),
 * the sky's texture coordinates are laid out anew from where each corner is
 * seen from the camera:
 *   - across: the picture goes `around` times round the sky, its left edge
 *     at `turn` degrees (round from +x towards +z, as the models have it);
 *   - up: the picture's top edge is at `top` degrees above the horizon and
 *     its bottom edge at `bottom` (by default the sky's highest and lowest
 *     corners, so that it covers the sky once). Sky above the top shows the
 *     picture's top row, sky below the bottom its bottom row; a picture
 *     reaching beyond the sky has those parts unseen. An edge that is not at
 *     a row of the sky's corners bends the picture within the faces it
 *     crosses (wo_sky_rows lists the rows).
 * So a picture is drawn in the proportions of an equirectangular panorama:
 * 360 / around degrees wide for (top - bottom) degrees high.
 *
 * The faces and their corners stay as they are, so a seam (where the
 * picture's right edge meets its left) must fall on the edges between
 * faces: wo_sky_seams says where it can for each number of times round. A
 * corner at the top of a dome, which has no direction round, takes the
 * middle of its face's. Texture coordinates are whole texels of the picture
 * (128 across), at most one short of its size (the game adds the picture's
 * place to them in a byte): where a column is not a whole number of texels
 * its edges are rounded, which bends the picture by half a texel at most.
 *
 * The layout is applied after the boards' (wo_billboards.h), only while the
 * sky's picture is authored and HD artwork is on (wo_load_plan,
 * wo_plan_drop_authored): the game's own picture is made for its own layout. */
#ifndef WO_SKY_H
#define WO_SKY_H

#include "core/wo_base.h"
#include "core/wo_json.h"

#define WO_SKY_MAX_AROUND 16

typedef struct WoSkyCorner {
    double dir[3]; /* from the camera, unit length, y up */
    double pos[3]; /* where it is in the model, y up */
    double az, el; /* degrees: round from +x towards +z, and above the horizon */
    bool apex;     /* the top of a dome: no direction round */
    uint8_t u, v;  /* as the archive has them, or as wo_sky_lay made them */
    size_t at;     /* where u is in the archive; v follows */
} WoSkyCorner;

typedef struct WoSkyFace {
    int corners; /* 3 or 4, in the archive's order (a quad's are 0 1 2, 1 3 2) */
    WoSkyCorner c[4];
} WoSkyFace;

typedef struct WoSky {
    uint32_t width, height; /* the picture's, in texels */
    WoSkyFace *faces;
    size_t count;
    double low, high; /* the lowest and highest corner, degrees */
} WoSky;

/* The sky of a track archive. Fails when the archive has none this code
 * can read. */
bool wo_sky_read(const uint8_t *archive, size_t len, WoSky *out, WoError *error);
void wo_sky_free(WoSky *sky);

/* Whether a picture going `around` times round with its left edge at `turn`
 * keeps every face within one picture. */
bool wo_sky_fits(const WoSky *sky, int around, double turn);

/* The turns that fit for `around` (each once, below 360 / around), into out
 * in ascending order; the count found, at most cap. */
size_t wo_sky_seams(const WoSky *sky, int around, double *out, size_t cap);

/* The elevations of the sky's rows of corners (each once: corners within
 * 0.6 degrees of one another are one row), ascending, into out; the count
 * found, at most cap. */
size_t wo_sky_rows(const WoSky *sky, double *out, size_t cap);

/* Lay pictures flat on the sky's faces rather than by direction from the
 * camera: a photo or painting (made through a flat lens: straight lines
 * straight) rather than a panorama (made by direction: straight lines bow).
 * Its corners' directions round become how far along the sky's outline
 * round the horizon they are, in degrees of the whole outline, and their
 * heights above the horizon become how high they are, in degrees of a
 * circle that long (low and high follow): a picture is then laid evenly
 * across each flat face, as on a wall, and only the faces' corners are
 * where they would be seen as a panorama. For the 1996 box skies (four flat
 * walls), a picture four times round shows on each wall as a photo of 90
 * degrees would. Layouts ask for it with "flat": true. */
void wo_sky_flatten(WoSky *sky);

/* Give the faces of `sky` (not the archive) the layout's u and v. */
void wo_sky_lay(WoSky *sky, int around, double turn, double top, double bottom);

/* Lay out the sky of the track archive `archive` (changed in place: its
 * size stays) by `layout` ({"around", "turn"} and optionally {"top",
 * "bottom", "flat"}, or NULL for none). A sky
 * this code cannot read, or a layout that does not fit it, is left as it
 * is; *changed (optional) says whether it was laid out. Never fails but on
 * a layout that is not one. */
bool wo_sky_apply(const WoJson *layout, WoBuf *archive, bool *changed, WoError *error);

#endif
