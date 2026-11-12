/* Trackside boards with PlayStation scenery.
 *
 * The PlayStation's scenery has large boards (objects named "redb...") that
 * its game covers with Red Bull advertising at run time; in the archive each
 * is four panels sharing one tiny placeholder texture. The PC game has no
 * such code outside Talon's Reach. Its own scenery replaced some of those
 * boards with a single panel carrying a picture of its own (Sagarmatha's
 * "FEED ME", Vostok Island's cat) and dropped the others. With the
 * PlayStation's archive in place the boards would all stand as dark panels.
 *
 * wo_boards_keep makes the archive the game should load, the PlayStation's
 * with its boards filled in:
 *
 * - A board the PC's scenery also has, at the same place, is the PC's
 *   board: its model record, renumbered for this archive's textures.
 * - The other boards take, in turn, the pictures the caller names: one of
 *   the track's own scenery textures (its sponsors' signs), or the
 *   PlayStation's advert for the track (the first picture of its
 *   TEXTURES/...RED.CMP). A quarter of the picture goes on each panel, as
 *   the game's board routine lays a picture out.
 *
 * Pictures the PlayStation's scenery textures do not include are added
 * after them. The advert sits where its own header places it, which the
 * PlayStation's layout keeps clear. A PC picture sits where the PC has it
 * if that is clear, else where the advert would go on a track that does
 * not show it, else where a PC scenery texture large enough used to be. A
 * changed texture file is stored uncompressed, which the game reads as
 * well.
 *
 * Archives this code does not fully understand are left alone: *out is
 * then the PlayStation archive unchanged, as it is when there is nothing
 * to change. Only running out of memory fails. */
#ifndef WO_BOARDS_H
#define WO_BOARDS_H

#include "core/wo_base.h"

/* In `pictures`: the track's advert rather than a scenery texture. */
#define WO_BOARD_ADVERT (-1)

/* pc and adverts may be NULL (length 0). pictures lists what the boards
 * without a PC twin show, in the order the archive has them and over again
 * when there are more boards: a PlayStation scenery texture's number or
 * WO_BOARD_ADVERT. With none, those boards stay as they are. *kept
 * (optional) counts the boards that are not as the PlayStation archive has
 * them. */
bool wo_boards_keep(const uint8_t *ps1, size_t ps1_len, const uint8_t *pc, size_t pc_len,
                    const uint8_t *adverts, size_t adverts_len, const int *pictures,
                    size_t picture_count, WoBuf *out, size_t *kept, WoError *error);

#endif
