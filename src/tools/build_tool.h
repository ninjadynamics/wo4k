#ifndef WO_BUILD_TOOL_H
#define WO_BUILD_TOOL_H

#include "core/wo_base.h"

/* ICO file with PNG frames at 16, 24, 32, 48, 64, 128 and 256 pixels,
 * resampled from a square PNG with WIC's high-quality scaler. */
bool build_icon(const char *png_path, WoBuf *ico, WoError *error);

#endif
