#include "core/wo_hashcache.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/wo_fs.h"

typedef struct Entry {
    char *key; /* relative to the root ('/'), else absolute; ASCII lower case */
    WoFileIdentity identity;
    char digest[WO_SHA256_HEX_SIZE];
    bool valid; /* identity and digest belong together and may be reused */
    bool used;  /* looked up or hashed since the cache was opened */
} Entry;

struct WoHashCache {
    char *file;
    char *root;
    Entry *entries;
    size_t count;
    size_t cap;
    size_t *slots; /* open addressing; entry index + 1, 0 for an empty slot */
    size_t slot_count;
    WoJson *facts; /* {"<digest>": {"<kind>": facts}} */
    WoHashCacheStats stats;
};

/* --- Keys and the table ---------------------------------------------------------- */

static uint64_t key_hash(const char *key) {
    uint64_t hash = 1469598103934665603u; /* FNV-1a */
    for (const unsigned char *p = (const unsigned char *)key; *p; p++) {
        hash ^= *p;
        hash *= 1099511628211u;
    }
    return hash;
}

static void normalise_key(char *key) {
    for (char *p = key; *p; p++) {
        *p = *p == '\\' ? '/' : wo_ascii_lower(*p);
    }
}

static char *cache_key(const WoHashCache *cache, const char *path, WoError *error) {
    char *full = wo_path_full(path, error);
    if (!full) {
        return NULL;
    }
    char *key = cache->root ? wo_path_relative(cache->root, full, NULL) : NULL;
    if (key) {
        wo_free(full);
    } else {
        key = full;
    }
    normalise_key(key);
    return key;
}

static bool find(const WoHashCache *cache, const char *key, size_t *index) {
    if (cache->slot_count == 0) {
        return false;
    }
    size_t mask = cache->slot_count - 1;
    for (size_t i = (size_t)key_hash(key) & mask;; i = (i + 1) & mask) {
        size_t slot = cache->slots[i];
        if (slot == 0) {
            return false;
        }
        if (strcmp(cache->entries[slot - 1].key, key) == 0) {
            *index = slot - 1;
            return true;
        }
    }
}

static void place(WoHashCache *cache, size_t index) {
    size_t mask = cache->slot_count - 1;
    size_t i = (size_t)key_hash(cache->entries[index].key) & mask;
    while (cache->slots[i] != 0) {
        i = (i + 1) & mask;
    }
    cache->slots[i] = index + 1;
}

/* Keep the table at most half full. */
static bool reserve(WoHashCache *cache, size_t count, WoError *error) {
    size_t needed;
    if (!wo_mul_size(count, 2, &needed)) {
        return wo_fail(error, WO_ERR_LIMIT, "Too many cached files.");
    }
    if (needed > cache->slot_count) {
        size_t slot_count = cache->slot_count ? cache->slot_count : 64;
        while (slot_count < needed) {
            if (!wo_mul_size(slot_count, 2, &slot_count)) {
                return wo_fail(error, WO_ERR_LIMIT, "Too many cached files.");
            }
        }
        size_t *slots = wo_alloc_array(slot_count, sizeof(*slots), error);
        if (!slots) {
            return false;
        }
        wo_free(cache->slots);
        cache->slots = slots;
        cache->slot_count = slot_count;
        for (size_t i = 0; i < cache->count; i++) {
            place(cache, i);
        }
    }
    if (count > cache->cap) {
        size_t cap = cache->cap ? cache->cap : 64;
        while (cap < count) {
            if (!wo_mul_size(cap, 2, &cap)) {
                return wo_fail(error, WO_ERR_LIMIT, "Too many cached files.");
            }
        }
        Entry *grown = wo_realloc_array(cache->entries, cap, sizeof(*grown), error);
        if (!grown) {
            return false;
        }
        cache->entries = grown;
        cache->cap = cap;
    }
    return true;
}

/* Takes ownership of key. */
static bool add(WoHashCache *cache, char *key, size_t *index, WoError *error) {
    size_t count;
    if (!wo_add_size(cache->count, 1, &count)) {
        wo_free(key);
        return wo_fail(error, WO_ERR_LIMIT, "Too many cached files.");
    }
    if (!reserve(cache, count, error)) {
        wo_free(key);
        return false;
    }
    Entry *entry = &cache->entries[cache->count];
    memset(entry, 0, sizeof(*entry));
    entry->key = key;
    *index = cache->count;
    cache->count = count;
    place(cache, *index);
    return true;
}

/* --- Hex fields ------------------------------------------------------------------- */

static void hex_encode(const uint8_t *bytes, size_t len, char *out) {
    static const char digits[] = "0123456789abcdef";
    for (size_t i = 0; i < len; i++) {
        out[i * 2] = digits[bytes[i] >> 4];
        out[i * 2 + 1] = digits[bytes[i] & 15];
    }
    out[len * 2] = 0;
}

static int hex_value(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    c = wo_ascii_lower(c);
    return c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}

static bool hex_decode(const char *text, uint8_t *bytes, size_t len) {
    if (!text || strlen(text) != len * 2) {
        return false;
    }
    for (size_t i = 0; i < len; i++) {
        int high = hex_value(text[i * 2]);
        int low = hex_value(text[i * 2 + 1]);
        if (high < 0 || low < 0) {
            return false;
        }
        bytes[i] = (uint8_t)(high * 16 + low);
    }
    return true;
}

static void u64_bytes(uint64_t value, uint8_t out[8]) {
    for (size_t i = 0; i < 8; i++) {
        out[i] = (uint8_t)(value >> (56 - 8 * i));
    }
}

static uint64_t bytes_u64(const uint8_t in[8]) {
    uint64_t value = 0;
    for (size_t i = 0; i < 8; i++) {
        value = (value << 8) | in[i];
    }
    return value;
}

/* --- Loading ---------------------------------------------------------------------- */

static bool read_u64(const WoJson *record, const char *key, uint64_t *out) {
    const WoJson *value = wo_json_get(record, key);
    if (!wo_json_is(value, WO_JSON_NUMBER) || !value->is_integer || value->integer < 0) {
        return false;
    }
    *out = (uint64_t)value->integer;
    return true;
}

static bool load_entry(WoHashCache *cache, const WoJson *record, WoError *error,
                       bool *valid) {
    *valid = false;
    const char *path = wo_json_str(record, "path", NULL);
    const char *digest = wo_json_str(record, "sha256", NULL);
    WoFileIdentity identity;
    memset(&identity, 0, sizeof(identity));
    uint8_t volume[8];
    if (!path || !path[0] || !digest || !wo_is_hex_digest(digest) ||
        !read_u64(record, "size", &identity.size) ||
        !read_u64(record, "mtime", &identity.mtime) || !read_u64(record, "ctime", &identity.ctime) ||
        !hex_decode(wo_json_str(record, "volume", NULL), volume, sizeof(volume)) ||
        !hex_decode(wo_json_str(record, "id", NULL), identity.id, sizeof(identity.id))) {
        return true;
    }
    identity.volume = bytes_u64(volume);
    char *key = wo_strdup(path, error);
    if (!key) {
        return false;
    }
    normalise_key(key);
    size_t index;
    if (find(cache, key, &index)) {
        wo_free(key);
        return true; /* a duplicate: the file is not what this program wrote */
    }
    if (!add(cache, key, &index, error)) {
        return false;
    }
    Entry *entry = &cache->entries[index];
    entry->identity = identity;
    memcpy(entry->digest, digest, WO_SHA256_HEX_SIZE);
    for (char *p = entry->digest; *p; p++) {
        *p = wo_ascii_lower(*p);
    }
    entry->valid = true;
    *valid = true;
    return true;
}

static void clear_entries(WoHashCache *cache) {
    for (size_t i = 0; i < cache->count; i++) {
        wo_free(cache->entries[i].key);
    }
    cache->count = 0;
    if (cache->slots) {
        memset(cache->slots, 0, cache->slot_count * sizeof(*cache->slots));
    }
}

/* Any malformed part discards the whole file: it was not written by this
 * program as it is now, so none of it is trusted. */
static bool load(WoHashCache *cache, const char **note, WoError *error) {
    WoFileInfo info;
    if (!wo_stat(cache->file, &info, NULL) || !info.exists) {
        return true;
    }
    WoJson *doc = NULL;
    if (info.is_dir || !wo_read_json(cache->file, WO_HASH_CACHE_MAX_BYTES, &doc, NULL)) {
        *note = "The integrity cache could not be read; every file is verified.";
        return true;
    }
    const WoJson *format = wo_json_get(doc, "format");
    const WoJson *files = wo_json_get(doc, "files");
    const WoJson *facts = wo_json_get(doc, "facts");
    bool well_formed = wo_json_is(format, WO_JSON_NUMBER) && format->is_integer &&
                       format->integer == WO_HASH_CACHE_FORMAT &&
                       wo_json_is(files, WO_JSON_ARRAY) &&
                       (!facts || wo_json_is(facts, WO_JSON_OBJECT));
    bool ok = true;
    for (size_t i = 0; ok && well_formed && i < wo_json_length(files); i++) {
        bool valid = false;
        ok = load_entry(cache, wo_json_at(files, i), error, &valid);
        well_formed = valid;
    }
    for (size_t i = 0; ok && well_formed && facts && i < facts->count; i++) {
        well_formed = wo_is_hex_digest(facts->keys[i]) &&
                      wo_json_is(facts->items[i], WO_JSON_OBJECT);
    }
    if (ok && well_formed && facts) {
        wo_json_free(cache->facts);
        cache->facts = wo_json_clone(facts);
        ok = cache->facts != NULL || wo_fail(error, WO_ERR_INTERNAL, "Out of memory.");
    }
    if (ok && !well_formed) {
        clear_entries(cache);
        *note = "The integrity cache is malformed; every file is verified.";
    }
    cache->stats.loaded = ok && well_formed && cache->count > 0;
    wo_json_free(doc);
    return ok;
}

WoHashCache *wo_hash_cache_open(const char *file, const char *root, bool fresh,
                                const char **note, WoError *error) {
    *note = NULL;
    WoHashCache *cache = wo_alloc(sizeof(*cache), error);
    if (!cache) {
        return NULL;
    }
    cache->facts = wo_json_new_object();
    bool ok = cache->facts != NULL || wo_fail(error, WO_ERR_INTERNAL, "Out of memory.");
    if (ok && file) {
        cache->file = wo_path_full(file, error);
        ok = cache->file != NULL;
    }
    if (ok && root) {
        cache->root = wo_path_full(root, error);
        ok = cache->root != NULL;
    }
    if (ok && cache->file && !fresh) {
        ok = load(cache, note, error);
    }
    if (!ok) {
        wo_hash_cache_free(cache);
        return NULL;
    }
    return cache;
}

void wo_hash_cache_free(WoHashCache *cache) {
    if (!cache) {
        return;
    }
    clear_entries(cache);
    wo_free(cache->entries);
    wo_free(cache->slots);
    wo_json_free(cache->facts);
    wo_free(cache->file);
    wo_free(cache->root);
    wo_free(cache);
}

/* --- Hashing ---------------------------------------------------------------------- */

/* Written so recently that a further write could share its time stamps (or
 * dated in the future): not safe to remember yet. */
static bool racy(const WoFileIdentity *identity, uint64_t now) {
    const uint64_t window = (uint64_t)WO_HASH_CACHE_RACY_SECONDS * 10000000u;
    return identity->mtime + window > now || identity->ctime + window > now;
}

/* Hash through one handle, taking the identity before and after reading. */
static bool hash_file(const char *path, char hex[WO_SHA256_HEX_SIZE], WoFileIdentity *before,
                      WoFileIdentity *after, uint64_t *bytes, WoError *error) {
    WoFile *file;
    if (!wo_file_open_read(path, &file, error)) {
        return false;
    }
    WoSha256 *hash = NULL;
    uint8_t *buffer = NULL;
    enum { CHUNK = 1 << 20 };
    bool ok = wo_file_identity_of(file, before, error);
    if (ok) {
        hash = wo_sha256_begin(error);
        buffer = hash ? wo_alloc(CHUNK, error) : NULL;
        ok = buffer != NULL;
    }
    *bytes = 0;
    while (ok) {
        size_t got = 0;
        ok = wo_file_read(file, buffer, CHUNK, &got, error);
        if (!ok || got == 0) {
            break;
        }
        ok = wo_sha256_update(hash, buffer, got, error);
        *bytes += got;
    }
    ok = ok && wo_file_identity_of(file, after, error);
    wo_free(buffer);
    wo_file_close(file, NULL);
    if (!ok) {
        wo_sha256_abort(hash);
        return false;
    }
    return wo_sha256_end(hash, hex, error);
}

bool wo_hash_cache_file(WoHashCache *cache, const char *path, char hex[WO_SHA256_HEX_SIZE],
                        WoError *error) {
    if (!cache) {
        return wo_sha256_file(path, hex, error);
    }
    char *key = cache_key(cache, path, error);
    if (!key) {
        return false;
    }
    size_t index = 0;
    bool known = find(cache, key, &index);
    if (known && cache->entries[index].valid) {
        WoFileIdentity now;
        if (!wo_file_identity(path, &now, error)) {
            wo_free(key);
            return false;
        }
        Entry *entry = &cache->entries[index];
        if (wo_file_identity_eq(&now, &entry->identity)) {
            memcpy(hex, entry->digest, WO_SHA256_HEX_SIZE);
            entry->used = true;
            cache->stats.hits++;
            wo_free(key);
            return true;
        }
    }
    WoFileIdentity before;
    WoFileIdentity after;
    uint64_t bytes = 0;
    if (!hash_file(path, hex, &before, &after, &bytes, error)) {
        if (known) {
            cache->entries[index].valid = false;
        }
        wo_free(key);
        return false;
    }
    cache->stats.hashed++;
    cache->stats.hashed_bytes += bytes;
    if (!known && !add(cache, key, &index, error)) {
        return false;
    }
    if (known) {
        wo_free(key);
    }
    Entry *entry = &cache->entries[index];
    entry->used = true;
    entry->valid = wo_file_identity_eq(&before, &after) && !racy(&after, wo_fs_now());
    if (!entry->valid) {
        cache->stats.racy++;
        return true;
    }
    entry->identity = after;
    memcpy(entry->digest, hex, WO_SHA256_HEX_SIZE);
    return true;
}

/* --- Facts ------------------------------------------------------------------------- */

const WoJson *wo_hash_cache_facts(const WoHashCache *cache, const char *digest,
                                  const char *kind) {
    if (!cache || !digest || !wo_is_hex_digest(digest)) {
        return NULL;
    }
    char lower[WO_SHA256_HEX_SIZE];
    wo_copy_str(lower, sizeof(lower), digest);
    normalise_key(lower);
    return wo_json_get(wo_json_get(cache->facts, lower), kind);
}

void wo_hash_cache_set_facts(WoHashCache *cache, const char *digest, const char *kind,
                             WoJson *facts) {
    if (!cache || !facts || !digest || !wo_is_hex_digest(digest)) {
        wo_json_free(facts);
        return;
    }
    char lower[WO_SHA256_HEX_SIZE];
    wo_copy_str(lower, sizeof(lower), digest);
    normalise_key(lower);
    WoJson *kinds = wo_json_get(cache->facts, lower);
    if (!kinds) {
        kinds = wo_json_new_object();
        if (!wo_json_put(cache->facts, lower, kinds)) {
            wo_json_free(facts);
            return;
        }
        kinds = wo_json_get(cache->facts, lower);
    }
    wo_json_put(kinds, kind, facts);
}

/* --- Saving ------------------------------------------------------------------------ */

static int compare_entries(const void *a, const void *b) {
    const Entry *left = *(const Entry *const *)a;
    const Entry *right = *(const Entry *const *)b;
    return strcmp(left->key, right->key);
}

static bool kept(const Entry *entry, bool prune) {
    return entry->valid && (entry->used || !prune);
}

static WoJson *entry_record(const Entry *entry) {
    char volume[17];
    char id[33];
    uint8_t volume_bytes[8];
    u64_bytes(entry->identity.volume, volume_bytes);
    hex_encode(volume_bytes, sizeof(volume_bytes), volume);
    hex_encode(entry->identity.id, sizeof(entry->identity.id), id);
    WoJson *record = wo_json_new_object();
    bool ok = record && wo_json_put_str(record, "path", entry->key) &&
              wo_json_put_int(record, "size", (int64_t)entry->identity.size) &&
              wo_json_put_int(record, "mtime", (int64_t)entry->identity.mtime) &&
              wo_json_put_int(record, "ctime", (int64_t)entry->identity.ctime) &&
              wo_json_put_str(record, "volume", volume) && wo_json_put_str(record, "id", id) &&
              wo_json_put_str(record, "sha256", entry->digest);
    if (!ok) {
        wo_json_free(record);
        return NULL;
    }
    return record;
}

static bool digest_kept(const Entry *const *order, size_t count, const char *digest) {
    for (size_t i = 0; i < count; i++) {
        if (strcmp(order[i]->digest, digest) == 0) {
            return true;
        }
    }
    return false;
}

bool wo_hash_cache_save(WoHashCache *cache, bool prune, WoError *error) {
    if (!cache || !cache->file) {
        return true;
    }
    const Entry **order = wo_alloc_array(cache->count ? cache->count : 1, sizeof(*order), error);
    if (!order) {
        return false;
    }
    size_t count = 0;
    for (size_t i = 0; i < cache->count; i++) {
        if (kept(&cache->entries[i], prune)) {
            order[count++] = &cache->entries[i];
        }
    }
    if (count > 1) {
        qsort(order, count, sizeof(*order), compare_entries);
    }
    WoJson *doc = wo_json_new_object();
    WoJson *files = wo_json_new_array();
    WoJson *facts = wo_json_new_object();
    bool ok = doc && files && facts && wo_json_put_int(doc, "format", WO_HASH_CACHE_FORMAT);
    for (size_t i = 0; ok && i < count; i++) {
        ok = wo_json_push(files, entry_record(order[i]));
    }
    for (size_t i = 0; ok && i < cache->facts->count; i++) {
        if (digest_kept(order, count, cache->facts->keys[i])) {
            ok = wo_json_put(facts, cache->facts->keys[i], wo_json_clone(cache->facts->items[i]));
        }
    }
    ok = ok && wo_json_put(doc, "files", files);
    files = NULL;
    ok = ok && wo_json_put(doc, "facts", facts);
    facts = NULL;
    if (!ok) {
        wo_fail(error, WO_ERR_INTERNAL, "Out of memory.");
    }
    ok = ok && wo_write_json_if_changed(cache->file, doc, NULL, error);
    wo_json_free(files);
    wo_json_free(facts);
    wo_json_free(doc);
    wo_free(order);
    return ok;
}

void wo_hash_cache_stats(const WoHashCache *cache, WoHashCacheStats *out) {
    memset(out, 0, sizeof(*out));
    if (!cache) {
        return;
    }
    *out = cache->stats;
    for (size_t i = 0; i < cache->count; i++) {
        out->entries += cache->entries[i].valid ? 1 : 0;
    }
}
