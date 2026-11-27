#ifndef WO_BUILD_TOOL_H
#define WO_BUILD_TOOL_H

#include "core/wo_base.h"
#include "core/wo_json.h"

/* ICO file with PNG frames at 16, 24, 32, 48, 64, 128 and 256 pixels,
 * resampled from a square PNG with WIC's high-quality scaler. */
bool build_icon(const char *png_path, WoBuf *ico, WoError *error);

/* Download, verify and unpack manifests/dependencies.json into deps. */
bool build_fetch(const char *manifest_path, const char *deps, WoError *error);
/* Refresh manifests/artwork.json and manifests/third-party.json. */
bool build_manifests(WoError *error);
/* Copy the binaries to be signed into folder, keeping relative paths. */
bool build_sign_input(const char *folder, WoError *error);
/* Assemble dist/wo4k-<version>.zip, its .sha256 and release-files.json;
 * signed_dir (optional) supplies the signed binaries. */
bool build_package(const char *version, const char *dist, const char *signed_dir, WoError *error);
/* Upscale every PNG of source into output, 2x or 4x (scale is "2" or "4"),
 * leaving out those the authored folder (optional) has: build_upscale.c. */
/* The contact sheet of the trackside billboards (data.js and the page) in
 * output, from the installed game (assets: the mod's assets folder, optional). */
bool build_billboards(const char *game, const char *output, const char *assets, WoError *error);
/* The billboard page's export as manifests/billboard-layout.json. */
bool build_billboard_layout(const char *export_path, const char *output, WoError *error);
/* The authoring studio, served on this machine, opened at its textures
 * page or (sky) its sky page: build_auth.c. */
bool build_auth(const char *port, bool sky, WoError *error);
/* The sky page's data and actions: build_sky.c. */
bool sky_state(WoJson **out, WoError *error);
/* top and bottom: NAN for the sky's own. */
bool sky_faces(const char *track, bool flat, int around, double turn, double top, double bottom,
               WoJson **out, WoError *error);
bool sky_act(const char *action, const char *track, bool flat, int around, double turn, double top,
             double bottom, WoError *error);
bool sky_game_picture(const char *track, char **path, WoError *error);
bool sky_drop(const char *track, const uint8_t *data, size_t len, WoError *error);
bool build_upscale(const char *source, const char *output, const char *scale, const char *authored,
                   WoError *error);

#endif
