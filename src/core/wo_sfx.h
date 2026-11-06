/* Sound-effect installation (manifests/patches.json "audio" list).
 *
 * The whole bank is validated before anything changes. Inputs are kept as
 * pristine copies in game_root/audio/source, so a later refresh never
 * cleans its own output. game_root/audio/install-state.json records, per
 * effect, the source hash, the processor and the output hash, which lets
 * an incremental refresh skip effects that are already up to date. */
#ifndef WO_SFX_H
#define WO_SFX_H

#include "core/wo_base.h"
#include "core/wo_json.h"

typedef struct WoSfxOptions {
    const WoJson *manifest; /* parsed manifests/patches.json */
    const char *source_dir; /* folder holding the original effects (may be audio/source) */
    const char *game_root;
    bool original;          /* install the effects unchanged (and accept custom ones) */
    bool incremental;       /* reuse install-state.json records that still match */
} WoSfxOptions;

typedef struct WoSfxResult {
    size_t checked;
    size_t updated;
} WoSfxResult;

bool wo_sfx_install(const WoSfxOptions *options, WoSfxResult *result, WoError *error);

#endif
