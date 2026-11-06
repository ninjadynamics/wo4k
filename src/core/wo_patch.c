#include "core/wo_patch.h"

#include <string.h>

/* Differences closer together than this are stored as one range. */
#define DIFF_JOIN_GAP 16u
#define HEX_LINE 256u

void wo_patch_free(WoPatchRecipe *recipe) {
    if (!recipe) {
        return;
    }
    for (size_t i = 0; i < recipe->count; i++) {
        wo_free(recipe->ranges[i].bytes);
    }
    wo_free(recipe->ranges);
    memset(recipe, 0, sizeof(*recipe));
}

/* Exact-name member lookup: recipes are machine-written, so no case folding. */
static const WoJson *member(const WoJson *object, const char *key) {
    for (size_t i = 0; i < object->count; i++) {
        if (strcmp(object->keys[i], key) == 0) {
            return object->items[i];
        }
    }
    return NULL;
}

static bool only_known_members(const WoJson *object, const char *const *known, size_t count,
                               const char *where, WoError *error) {
    for (size_t i = 0; i < object->count; i++) {
        bool found = false;
        for (size_t k = 0; k < count && !found; k++) {
            found = strcmp(object->keys[i], known[k]) == 0;
        }
        if (!found) {
            return wo_fail(error, WO_ERR_FORMAT, "Invalid patch recipe: unknown member \"%s\" in %s.",
                           object->keys[i], where);
        }
    }
    return true;
}

static bool read_size(const WoJson *object, const char *key, size_t max, size_t *out,
                      WoError *error) {
    const WoJson *value = member(object, key);
    if (!value || value->type != WO_JSON_NUMBER || !value->is_integer || value->integer < 0 ||
        (uint64_t)value->integer > max) {
        return wo_fail(error, WO_ERR_FORMAT, "Invalid patch recipe: \"%s\" must be a whole number "
                       "from 0 to %zu.", key, max);
    }
    *out = (size_t)value->integer;
    return true;
}

static bool read_digest(const WoJson *object, const char *key, char out[65], WoError *error) {
    const char *text = wo_json_as_str(member(object, key), NULL);
    if (!text || !wo_is_hex_digest(text)) {
        return wo_fail(error, WO_ERR_FORMAT,
                       "Invalid patch recipe: \"%s\" must be a 64-digit SHA-256 value.", key);
    }
    for (size_t i = 0; i < 64; i++) {
        out[i] = wo_ascii_lower(text[i]);
    }
    out[64] = 0;
    return true;
}

static int hex_digit(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    c = wo_ascii_lower(c);
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    return -1;
}

/* Concatenate xor_hex (a string or an array of strings) and decode it. */
static bool decode_xor(const WoJson *value, size_t max_bytes, uint8_t **out, size_t *out_len,
                       WoError *error) {
    *out = NULL;
    *out_len = 0;
    const char *bad = "Invalid patch recipe: \"xor_hex\" must be non-empty hexadecimal byte pairs.";
    const WoJson *const *parts = NULL;
    const WoJson *single[1] = {value};
    size_t count = 0;
    if (value && value->type == WO_JSON_STRING) {
        parts = single;
        count = 1;
    } else if (value && value->type == WO_JSON_ARRAY) {
        parts = (const WoJson *const *)value->items;
        count = value->count;
    } else {
        return wo_fail(error, WO_ERR_FORMAT, "%s", bad);
    }
    size_t digits = 0;
    for (size_t i = 0; i < count; i++) {
        if (parts[i]->type != WO_JSON_STRING) {
            return wo_fail(error, WO_ERR_FORMAT, "%s", bad);
        }
        size_t n = strlen(parts[i]->string);
        if (!wo_add_size(digits, n, &digits)) {
            return wo_fail(error, WO_ERR_LIMIT, "%s", bad);
        }
    }
    if (digits == 0 || (digits & 1u) || digits / 2 > max_bytes) {
        return wo_fail(error, WO_ERR_FORMAT, "%s", bad);
    }
    uint8_t *bytes = wo_alloc(digits / 2, error);
    if (!bytes) {
        return false;
    }
    size_t at = 0;
    int high = -1;
    for (size_t i = 0; i < count; i++) {
        for (const char *p = parts[i]->string; *p; p++) {
            int digit = hex_digit(*p);
            if (digit < 0) {
                wo_free(bytes);
                return wo_fail(error, WO_ERR_FORMAT, "%s", bad);
            }
            if (high < 0) {
                high = digit;
            } else {
                bytes[at++] = (uint8_t)((high << 4) | digit);
                high = -1;
            }
        }
    }
    *out = bytes;
    *out_len = digits / 2;
    return true;
}

static bool push_range(WoPatchRecipe *recipe, size_t offset, uint8_t *bytes, size_t len,
                       WoError *error) {
    if (recipe->count == recipe->cap) {
        size_t cap = recipe->cap ? recipe->cap * 2 : 64;
        WoPatchRange *grown = wo_realloc_array(recipe->ranges, cap, sizeof(*grown), error);
        if (!grown) {
            wo_free(bytes);
            return false;
        }
        recipe->ranges = grown;
        recipe->cap = cap;
    }
    WoPatchRange *range = &recipe->ranges[recipe->count++];
    range->offset = offset;
    range->len = len;
    range->bytes = bytes;
    return true;
}

static bool parse_ranges(const WoJson *ranges, WoPatchRecipe *recipe, WoError *error) {
    static const char *const keys[] = {"offset", "xor_hex"};
    if (!ranges || ranges->type != WO_JSON_ARRAY) {
        return wo_fail(error, WO_ERR_FORMAT, "Invalid patch recipe: \"ranges\" must be a list.");
    }
    size_t end = 0;
    for (size_t i = 0; i < ranges->count; i++) {
        const WoJson *range = ranges->items[i];
        if (range->type != WO_JSON_OBJECT) {
            return wo_fail(error, WO_ERR_FORMAT, "Invalid patch recipe: range %zu is not an object.",
                           i + 1);
        }
        if (!only_known_members(range, keys, WO_COUNT(keys), "a range", error)) {
            return false;
        }
        size_t offset = 0;
        if (!read_size(range, "offset", recipe->target_length, &offset, error)) {
            return false;
        }
        uint8_t *bytes = NULL;
        size_t len = 0;
        if (!decode_xor(member(range, "xor_hex"), recipe->target_length, &bytes, &len, error)) {
            return false;
        }
        if (offset < end || len > recipe->target_length - offset) {
            wo_free(bytes);
            return wo_fail(error, WO_ERR_FORMAT,
                           "Invalid patch recipe: range %zu at offset %zu overlaps another range "
                           "or leaves the target.",
                           i + 1, offset);
        }
        end = offset + len;
        if (!push_range(recipe, offset, bytes, len, error)) {
            return false;
        }
    }
    return true;
}

bool wo_patch_from_json(const WoJson *json, WoPatchRecipe *out, WoError *error) {
    static const char *const keys[] = {"format",        "source_length", "target_length",
                                       "source_sha256", "target_sha256", "ranges"};
    memset(out, 0, sizeof(*out));
    if (!json || json->type != WO_JSON_OBJECT) {
        return wo_fail(error, WO_ERR_FORMAT, "Invalid patch recipe: expected a JSON object.");
    }
    const WoJson *format = member(json, "format");
    if (!format || format->type != WO_JSON_NUMBER || !format->is_integer || format->integer != 1) {
        return wo_fail(error, WO_ERR_UNSUPPORTED, "Unsupported patch recipe format.");
    }
    bool ok = only_known_members(json, keys, WO_COUNT(keys), "the recipe", error) &&
              read_size(json, "source_length", WO_PATCH_MAX_BYTES, &out->source_length, error) &&
              read_size(json, "target_length", WO_PATCH_MAX_BYTES, &out->target_length, error) &&
              read_digest(json, "source_sha256", out->source_sha256, error) &&
              read_digest(json, "target_sha256", out->target_sha256, error) &&
              parse_ranges(member(json, "ranges"), out, error);
    if (!ok) {
        wo_patch_free(out);
    }
    return ok;
}

bool wo_patch_parse(const char *text, size_t len, WoPatchRecipe *out, WoError *error) {
    memset(out, 0, sizeof(*out));
    if (len > WO_PATCH_RECIPE_MAX_BYTES) {
        return wo_fail(error, WO_ERR_LIMIT, "The patch recipe is larger than %u bytes.",
                       WO_PATCH_RECIPE_MAX_BYTES);
    }
    WoJson *json = NULL;
    if (!wo_json_parse(text, len, &json, error)) {
        wo_error_context(error, "Invalid patch recipe: ");
        return false;
    }
    bool ok = wo_patch_from_json(json, out, error);
    wo_json_free(json);
    return ok;
}

bool wo_patch_apply_bytes(const WoPatchRecipe *recipe, const uint8_t *source, size_t source_len,
                          uint8_t **out, WoError *error) {
    *out = NULL;
    if (source_len != recipe->source_length) {
        return wo_fail(error, WO_ERR_CHECKSUM,
                       "The executable is %zu bytes; the patch expects %zu bytes.", source_len,
                       recipe->source_length);
    }
    if (recipe->target_length > WO_PATCH_MAX_BYTES) {
        return wo_fail(error, WO_ERR_LIMIT, "The patched executable would be too large.");
    }
    uint8_t *target = wo_alloc(recipe->target_length, error);
    if (!target) {
        return false;
    }
    size_t keep = source_len < recipe->target_length ? source_len : recipe->target_length;
    if (keep) {
        memcpy(target, source, keep);
    }
    size_t end = 0;
    for (size_t i = 0; i < recipe->count; i++) {
        const WoPatchRange *range = &recipe->ranges[i];
        /* Recipes from wo_patch_parse are already validated; recheck so a
         * hand-built recipe can never write out of bounds. */
        if (range->offset < end || range->len == 0 || range->offset > recipe->target_length ||
            range->len > recipe->target_length - range->offset) {
            wo_free(target);
            return wo_fail(error, WO_ERR_FORMAT, "Invalid patch range.");
        }
        for (size_t j = 0; j < range->len; j++) {
            target[range->offset + j] ^= range->bytes[j];
        }
        end = range->offset + range->len;
    }
    *out = target;
    return true;
}

static uint8_t source_byte(const uint8_t *source, size_t source_len, size_t at) {
    return at < source_len ? source[at] : 0;
}

bool wo_patch_diff(const uint8_t *source, size_t source_len, const uint8_t *target,
                   size_t target_len, WoPatchRecipe *out, WoError *error) {
    memset(out, 0, sizeof(*out));
    if (source_len > WO_PATCH_MAX_BYTES || target_len > WO_PATCH_MAX_BYTES) {
        return wo_fail(error, WO_ERR_LIMIT, "Executables larger than %u bytes are not supported.",
                       WO_PATCH_MAX_BYTES);
    }
    out->source_length = source_len;
    out->target_length = target_len;
    size_t at = 0;
    while (at < target_len) {
        while (at < target_len && target[at] == source_byte(source, source_len, at)) {
            at++;
        }
        if (at == target_len) {
            break;
        }
        size_t start = at;
        size_t last = at;
        while (at < target_len) {
            if (target[at] != source_byte(source, source_len, at)) {
                last = at;
            }
            if (at - last > DIFF_JOIN_GAP) {
                break;
            }
            at++;
        }
        size_t len = last - start + 1;
        uint8_t *bytes = wo_alloc(len, error);
        if (!bytes) {
            wo_patch_free(out);
            return false;
        }
        for (size_t i = 0; i < len; i++) {
            bytes[i] = (uint8_t)(target[start + i] ^ source_byte(source, source_len, start + i));
        }
        if (!push_range(out, start, bytes, len, error)) {
            wo_patch_free(out);
            return false;
        }
        at = last + 1;
    }
    return true;
}

static WoJson *hex_lines(const WoPatchRange *range) {
    static const char digits[] = "0123456789abcdef";
    WoJson *lines = wo_json_new_array();
    char line[HEX_LINE + 1];
    size_t used = 0;
    for (size_t i = 0; lines && i < range->len; i++) {
        line[used++] = digits[range->bytes[i] >> 4];
        line[used++] = digits[range->bytes[i] & 15];
        if (used == HEX_LINE || i + 1 == range->len) {
            line[used] = 0;
            if (!wo_json_push_str(lines, line)) {
                wo_json_free(lines);
                return NULL;
            }
            used = 0;
        }
    }
    return lines;
}

bool wo_patch_to_json(const WoPatchRecipe *recipe, WoJson **out, WoError *error) {
    *out = NULL;
    WoJson *json = wo_json_new_object();
    WoJson *ranges = wo_json_new_array();
    bool ok = json && ranges && wo_json_put_int(json, "format", 1) &&
              wo_json_put_int(json, "source_length", (int64_t)recipe->source_length) &&
              wo_json_put_int(json, "target_length", (int64_t)recipe->target_length) &&
              wo_json_put_str(json, "source_sha256", recipe->source_sha256) &&
              wo_json_put_str(json, "target_sha256", recipe->target_sha256);
    for (size_t i = 0; ok && i < recipe->count; i++) {
        WoJson *range = wo_json_new_object();
        ok = range && wo_json_put_int(range, "offset", (int64_t)recipe->ranges[i].offset) &&
             wo_json_put(range, "xor_hex", hex_lines(&recipe->ranges[i]));
        if (ok) {
            ok = wo_json_push(ranges, range);
        } else {
            wo_json_free(range);
        }
    }
    if (ok) {
        ok = wo_json_put(json, "ranges", ranges);
        ranges = NULL;
    }
    wo_json_free(ranges);
    if (!ok) {
        wo_json_free(json);
        return wo_fail(error, WO_ERR_INTERNAL, "Out of memory while writing a patch recipe.");
    }
    *out = json;
    return true;
}
