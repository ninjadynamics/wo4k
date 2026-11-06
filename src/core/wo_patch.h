/* Executable patch recipes (portable part).
 *
 * A recipe is reviewable JSON text describing the target executable as XOR
 * differences from one exact source executable:
 *
 *   {"format": 1, "source_length": N, "target_length": M,
 *    "source_sha256": "<64 hex>", "target_sha256": "<64 hex>",
 *    "ranges": [{"offset": O, "xor_hex": ["0a1b...", ...]}, ...]}
 *
 * The target starts as the source, truncated or zero-extended to M bytes;
 * each range XORs its bytes in at its offset. Ranges are non-empty, in
 * ascending order, never overlap and stay inside the target. xor_hex is a
 * string or an array of strings that are concatenated. Unknown members are
 * rejected. Hash verification and file access live in wo_patch_file.h. */
#ifndef WO_PATCH_H
#define WO_PATCH_H

#include "core/wo_base.h"
#include "core/wo_json.h"

#define WO_PATCH_MAX_BYTES (128u * 1024u * 1024u)
/* Largest recipe text accepted. */
#define WO_PATCH_RECIPE_MAX_BYTES (64u * 1024u * 1024u)

typedef struct WoPatchRange {
    size_t offset;
    size_t len;
    uint8_t *bytes;
} WoPatchRange;

typedef struct WoPatchRecipe {
    size_t source_length;
    size_t target_length;
    char source_sha256[65]; /* lowercase */
    char target_sha256[65];
    WoPatchRange *ranges;
    size_t count;
    size_t cap;
} WoPatchRecipe;

bool wo_patch_parse(const char *text, size_t len, WoPatchRecipe *out, WoError *error);
bool wo_patch_from_json(const WoJson *json, WoPatchRecipe *out, WoError *error);
void wo_patch_free(WoPatchRecipe *recipe);

/* Build the target from source bytes (which must have source_length
 * bytes). Hashes are not checked here. *out has target_length bytes. */
bool wo_patch_apply_bytes(const WoPatchRecipe *recipe, const uint8_t *source, size_t source_len,
                          uint8_t **out, WoError *error);

/* Maintainer helper: describe target as XOR ranges against source (the
 * original Delta algorithm: runs of differences, joined across gaps of up
 * to 16 equal bytes). Lengths are set; the hashes are left empty. */
bool wo_patch_diff(const uint8_t *source, size_t source_len, const uint8_t *target,
                   size_t target_len, WoPatchRecipe *out, WoError *error);

/* Recipe as JSON, xor_hex split into 256-character lines for review. */
bool wo_patch_to_json(const WoPatchRecipe *recipe, WoJson **out, WoError *error);

#endif
