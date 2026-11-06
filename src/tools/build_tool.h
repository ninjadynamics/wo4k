#ifndef WO_BUILD_TOOL_H
#define WO_BUILD_TOOL_H

#include "core/wo_base.h"

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

#endif
