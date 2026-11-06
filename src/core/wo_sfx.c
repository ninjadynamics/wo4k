#include "core/wo_sfx.h"

#include <string.h>

#include "core/wo_audioclean.h"
#include "core/wo_fs.h"
#include "core/wo_hash.h"

#define STATE_MAX_BYTES (4u * 1024u * 1024u)

typedef struct Effect {
    const WoJson *entry;
    const char *name;
    char *input;
    char hash[WO_SHA256_HEX_SIZE];
} Effect;

static bool hash_listed(const WoJson *entry, const char *hash) {
    const WoJson *hashes = wo_json_get(entry, "source_hashes");
    for (size_t i = 0; i < wo_json_length(hashes); i++) {
        const char *listed = wo_json_as_str(wo_json_at(hashes, i), NULL);
        if (listed && wo_digest_eq(listed, hash)) {
            return true;
        }
    }
    return false;
}

static bool is_output(const WoJson *entry, const char *hash) {
    const char *output = wo_json_str(entry, "output_sha256", NULL);
    return output && wo_digest_eq(output, hash);
}

/* Previous records that carry every field an incremental refresh needs.
 * An unreadable state file simply means nothing can be reused. */
static WoJson *load_previous(const char *state_path) {
    WoJson *state = NULL;
    if (!wo_is_file(state_path) || !wo_read_json(state_path, STATE_MAX_BYTES, &state, NULL)) {
        return NULL;
    }
    return state;
}

static const WoJson *previous_record(const WoJson *state, const char *name) {
    const WoJson *files = wo_json_get(state, "files");
    for (size_t i = 0; i < wo_json_length(files); i++) {
        const WoJson *record = wo_json_at(files, i);
        const char *recorded = wo_json_str(record, "name", NULL);
        if (recorded && wo_str_ieq(recorded, name) && wo_json_has(record, "source_sha256") &&
            wo_json_has(record, "processor") && wo_json_has(record, "output_sha256")) {
            return record;
        }
    }
    return NULL;
}

static bool validate_bank(const WoJson *audio, const char *source, bool original, Effect *effects,
                          WoError *error) {
    for (size_t i = 0; i < audio->count; i++) {
        Effect *effect = &effects[i];
        effect->entry = audio->items[i];
        effect->name = wo_json_str(effect->entry, "name", NULL);
        if (!effect->name || !*effect->name) {
            return wo_fail(error, WO_ERR_FORMAT, "The sound-effect manifest has an entry without a name.");
        }
        effect->input = wo_path_child(source, effect->name, error);
        if (!effect->input) {
            return false;
        }
        if (!wo_is_file(effect->input)) {
            return wo_fail(error, WO_ERR_NOT_FOUND, "Missing sound effect: %s", effect->name);
        }
        if (!wo_sha256_file(effect->input, effect->hash, error)) {
            return false;
        }
        if (!original && !is_output(effect->entry, effect->hash) &&
            !hash_listed(effect->entry, effect->hash)) {
            return wo_fail(error, WO_ERR_CHECKSUM,
                           "Unsupported sound effect checksum: %s. Turn off sound-effect cleaning "
                           "in Setup to keep custom effects.",
                           effect->name);
        }
    }
    return true;
}

/* Keep a pristine copy of the input in the cache and use that from now on. */
static bool cache_input(Effect *effect, const char *cache, WoError *error) {
    char *pristine = wo_path_child(cache, effect->name, error);
    char *input_full = pristine ? wo_path_full(effect->input, error) : NULL;
    if (!input_full) {
        wo_free(pristine);
        return false;
    }
    bool ok = true;
    if (!wo_str_ieq(input_full, pristine)) {
        bool current = false;
        if (wo_is_file(pristine)) {
            char hash[WO_SHA256_HEX_SIZE];
            ok = wo_sha256_file(pristine, hash, error);
            current = ok && wo_digest_eq(hash, effect->hash);
        }
        ok = ok && (current || wo_copy_file(effect->input, pristine, error));
        if (ok) {
            wo_free(effect->input);
            effect->input = pristine;
            pristine = NULL;
        }
    }
    wo_free(input_full);
    wo_free(pristine);
    return ok;
}

static bool clean_effect(const Effect *effect, const char *target, char hash[WO_SHA256_HEX_SIZE],
                         WoError *error) {
    WoBuf input = {0};
    uint8_t *output = NULL;
    bool ok = wo_read_file(effect->input, WO_AUDIO_MAX_BYTES, &input, error) &&
              wo_audio_clean(input.data, input.len, effect->name, &output, error) &&
              wo_sha256_mem(output, input.len, hash, error) &&
              wo_write_file_atomic(target, output, input.len, error);
    if (!ok) {
        wo_error_context(error, "Audio processing failed for %s: ", effect->name);
    }
    wo_free(output);
    wo_buf_free(&input);
    return ok;
}

static WoJson *new_record(const char *name, const char *source_hash, const char *processor,
                          const char *output_hash) {
    WoJson *record = wo_json_new_object();
    if (record && wo_json_put_str(record, "name", name) &&
        wo_json_put_str(record, "source_sha256", source_hash) &&
        wo_json_put_str(record, "processor", processor) &&
        wo_json_put_str(record, "output_sha256", output_hash)) {
        return record;
    }
    wo_json_free(record);
    return NULL;
}

/* Install one effect and append its state record. */
static bool install_effect(const WoSfxOptions *options, Effect *effect, const char *cache,
                           const WoJson *previous, const char *processor, WoJson *records,
                           bool *updated, WoError *error) {
    *updated = false;
    char *relative = wo_format(error, "audio/%s", effect->name);
    char *target = relative ? wo_path_child(options->game_root, relative, error) : NULL;
    wo_free(relative);
    if (!target || !cache_input(effect, cache, error)) {
        wo_free(target);
        return false;
    }
    bool ok = true;
    const WoJson *old = options->incremental ? previous_record(previous, effect->name) : NULL;
    if (old && wo_str_ieq(wo_json_str(old, "source_sha256", ""), effect->hash) &&
        wo_str_ieq(wo_json_str(old, "processor", ""), processor) && wo_is_file(target)) {
        char current[WO_SHA256_HEX_SIZE];
        ok = wo_sha256_file(target, current, error);
        if (ok && wo_str_ieq(wo_json_str(old, "output_sha256", ""), current)) {
            WoJson *kept = wo_json_clone(old);
            ok = kept && wo_json_push(records, kept);
            wo_free(target);
            return ok || wo_fail(error, WO_ERR_INTERNAL, "Out of memory.");
        }
    }
    char output[WO_SHA256_HEX_SIZE];
    memcpy(output, effect->hash, sizeof(output));
    if (ok && (options->original || is_output(effect->entry, effect->hash))) {
        ok = wo_copy_file(effect->input, target, error);
    } else if (ok) {
        ok = clean_effect(effect, target, output, error);
    }
    WoJson *record = ok ? new_record(effect->name, effect->hash, processor, output) : NULL;
    if (ok && !(record && wo_json_push(records, record))) {
        ok = wo_fail(error, WO_ERR_INTERNAL, "Out of memory.");
    }
    *updated = ok;
    wo_free(target);
    return ok;
}

bool wo_sfx_install(const WoSfxOptions *options, WoSfxResult *result, WoError *error) {
    memset(result, 0, sizeof(*result));
    const WoJson *audio = wo_json_get(options->manifest, "audio");
    if (!wo_json_is(audio, WO_JSON_ARRAY)) {
        return wo_fail(error, WO_ERR_FORMAT, "The patch manifest has no sound-effect list.");
    }
    char *source = wo_path_full(options->source_dir, error);
    char *cache = source ? wo_path_child(options->game_root, "audio/source", error) : NULL;
    char *state_path = cache ? wo_path_child(options->game_root, "audio/install-state.json", error)
                             : NULL;
    Effect *effects = state_path ? wo_alloc_array(audio->count ? audio->count : 1, sizeof(Effect), error)
                                 : NULL;
    WoJson *previous = NULL;
    WoJson *state = NULL;
    WoJson *records = NULL;
    bool ok = effects != NULL;
    if (ok && options->incremental) {
        previous = load_previous(state_path);
    }
    const char *processor = options->original ? "original" : WO_AUDIO_PROCESSOR_ID;
    ok = ok && validate_bank(audio, source, options->original, effects, error);
    if (ok) {
        state = wo_json_new_object();
        records = wo_json_new_array();
        ok = state && records;
        if (!ok) {
            wo_fail(error, WO_ERR_INTERNAL, "Out of memory.");
        }
    }
    for (size_t i = 0; ok && i < audio->count; i++) {
        bool updated = false;
        ok = install_effect(options, &effects[i], cache, previous, processor, records, &updated,
                            error);
        result->updated += updated ? 1 : 0;
    }
    if (ok) {
        ok = wo_json_put(state, "files", records);
        records = NULL;
        if (!ok) {
            wo_fail(error, WO_ERR_INTERNAL, "Out of memory.");
        }
    }
    ok = ok && wo_write_json(state_path, state, error);
    if (ok) {
        result->checked = audio->count;
    }
    for (size_t i = 0; effects && i < audio->count; i++) {
        wo_free(effects[i].input);
    }
    wo_json_free(records);
    wo_json_free(state);
    wo_json_free(previous);
    wo_free(effects);
    wo_free(state_path);
    wo_free(cache);
    wo_free(source);
    return ok;
}
