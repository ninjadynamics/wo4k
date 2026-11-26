/* Billboards laid out by choice (manifests/billboard-layout.json).
 *
 * The 1996 artwork was laid over the trackside boards with little regard for
 * its proportions: a square picture can be stretched over a board three
 * times as wide. The maintainer's billboard page (wo4k-build billboards)
 * records for each board how its picture should fill it instead, and
 * wo4k-build billboard-layout turns that into the manifest.
 *
 * Nothing of the pictures changes: the boards themselves do. A board's faces
 * in the track's scenery models are replaced by new ones over the same
 * rectangle of the same plane:
 *   - fit: one face showing the picture at its own proportions, the board's
 *     full height, centred;
 *   - stretch: the same at a percentage of those proportions, and at a scale
 *     of the board's height;
 *   - tile: faces side by side from the centre, filling the board or a set
 *     number of them, at the stretch and scale chosen; those cut by the
 *     board's edges show the part of the picture that falls inside.
 * Where the picture leaves part of the board bare, faces in the bars colour
 * fill it: faces like the picture's that all show one texel of it in that
 * colour (with its neighbours alike), so they are lit as the picture is;
 * where the picture has no such texel, plain faces in that colour, lit as
 * the board was. With no colour (see-through bars) the board is bare there
 * and whatever is behind it shows. The part of the
 * picture used is the one chosen on the page (all of it, its visible part,
 * or an area). Every new face takes the drawing settings of the board's own
 * (its texture, palette and page, flags and colours), so it is drawn as the
 * board was. A sprite (a picture that always faces the camera) takes a new
 * width and height instead.
 *
 * A board the game's own code fills ("routine": Talon's Reach's four-panel
 * boards, which show the lucky cat) cannot lose its panels: the game writes
 * their texture coordinates at every step. Its corners move instead,
 * narrowing it about its middle to the picture's proportions ("aspect"),
 * with faces in the bars colour beside it.
 *
 * A board is named by its object and faces in the installed archive, which
 * the boards and moves of wo_boards.h make: the layout comes after them. */
#ifndef WO_BILLBOARDS_H
#define WO_BILLBOARDS_H

#include "core/wo_base.h"
#include "core/wo_json.h"

/* Lay out the boards `entries` (a track's list of the manifest) in the
 * track archive `archive`, which is replaced. *changed (optional) counts the
 * boards laid out; a board this code cannot read is left as it is. Only
 * running out of memory fails. */
bool wo_billboards_apply(const WoJson *entries, WoBuf *archive, size_t *changed, WoError *error);

#endif
