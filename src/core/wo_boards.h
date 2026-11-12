/* The PC release's trackside boards, kept with PlayStation scenery.
 *
 * The PlayStation's scenery has large boards (objects named "redb...") that
 * its game covers with Red Bull advertising at run time; in the archive each
 * is four panels sharing one tiny placeholder texture. The PC game has no
 * such code outside Talon's Reach. Its own scenery replaced some of those
 * boards with a single panel carrying a picture of its own (Sagarmatha's
 * "FEED ME") and dropped the others. With the PlayStation's archive in
 * place the boards would all stand as dark panels.
 *
 * wo_boards_keep makes the archive the game should load: the PlayStation's,
 * in which every board the PC's scenery also has, at the same place, is the
 * PC's board. A picture the PlayStation's textures do not include takes
 * the place of the placeholder those boards no longer use, and is laid out
 * where a PC scenery texture used to be and no PlayStation one is; if there
 * is no such room, the track's boards stay as the PlayStation archive has
 * them, as do boards the PC's scenery does not have.
 *
 * Archives this code does not fully understand are left alone: *out is
 * then the PlayStation archive unchanged, as it is when there is nothing
 * to keep. Only running out of memory fails. */
#ifndef WO_BOARDS_H
#define WO_BOARDS_H

#include "core/wo_base.h"

/* pc may be NULL (pc_len 0). *kept (optional) counts the boards taken from
 * the PC archive. */
bool wo_boards_keep(const uint8_t *ps1, size_t ps1_len, const uint8_t *pc, size_t pc_len,
                    WoBuf *out, size_t *kept, WoError *error);

#endif
