/* Patch recipe validation, application and JSON round trip. */
#include <string.h>

#include "core/wo_patch.h"
#include "fuzz.h"

#define APPLY_LIMIT (1u << 20)

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    WoError error = {0};
    WoPatchRecipe recipe;
    if (!wo_patch_parse((const char *)data, size, &recipe, &error)) {
        FUZZ_ASSERT(recipe.ranges == NULL && recipe.count == 0);
        return 0;
    }
    size_t end = 0;
    for (size_t i = 0; i < recipe.count; i++) {
        const WoPatchRange *range = &recipe.ranges[i];
        FUZZ_ASSERT(range->len > 0 && range->offset >= end);
        FUZZ_ASSERT(range->offset + range->len <= recipe.target_length);
        end = range->offset + range->len;
    }
    if (recipe.source_length <= APPLY_LIMIT && recipe.target_length <= APPLY_LIMIT) {
        uint8_t *source = wo_alloc(recipe.source_length, &error);
        uint8_t *target = NULL;
        if (source) {
            FUZZ_ASSERT(wo_patch_apply_bytes(&recipe, source, recipe.source_length, &target,
                                             &error));
            /* Diffing the result again reproduces an equivalent target. */
            WoPatchRecipe again;
            uint8_t *rebuilt = NULL;
            FUZZ_ASSERT(wo_patch_diff(source, recipe.source_length, target,
                                      recipe.target_length, &again, &error));
            FUZZ_ASSERT(wo_patch_apply_bytes(&again, source, recipe.source_length, &rebuilt,
                                             &error));
            FUZZ_ASSERT(recipe.target_length == 0 ||
                        memcmp(rebuilt, target, recipe.target_length) == 0);
            wo_free(rebuilt);
            wo_patch_free(&again);
        }
        wo_free(target);
        wo_free(source);
    }
    WoJson *json = NULL;
    FUZZ_ASSERT(wo_patch_to_json(&recipe, &json, &error));
    WoPatchRecipe copy;
    FUZZ_ASSERT(wo_patch_from_json(json, &copy, &error));
    FUZZ_ASSERT(copy.count == recipe.count && copy.target_length == recipe.target_length);
    wo_patch_free(&copy);
    wo_json_free(json);
    wo_patch_free(&recipe);
    return 0;
}
