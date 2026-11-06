#include "core/wo_texsrc.h"

#include <string.h>

#define ROAD_RECORD 42u
#define ROAD_TILE 32u

typedef struct WadSlot {
    char *name;
    WoBuf data;
    WoWad wad;
} WadSlot;

typedef struct FileSlot {
    char *key;
    WoBuf data;
} FileSlot;

typedef struct SequenceSlot {
    char *key;
    bool is_cmp;
    WoCmp cmp;
    const uint8_t *single; /* non-CMP sources: the whole file is frame 0 */
    size_t single_len;
} SequenceSlot;

struct WoTexSources {
    WoTexLoadFn load;
    void *context;
    WadSlot *wads;
    size_t wad_count;
    size_t wad_cap;
    FileSlot *files;
    size_t file_count;
    size_t file_cap;
    SequenceSlot *sequences;
    size_t sequence_count;
    size_t sequence_cap;
};

WoTexSources *wo_texsrc_new(WoTexLoadFn load, void *context, WoError *error) {
    WoTexSources *sources = wo_alloc(sizeof(*sources), error);
    if (sources) {
        sources->load = load;
        sources->context = context;
    }
    return sources;
}

void wo_texsrc_free(WoTexSources *sources) {
    if (!sources) {
        return;
    }
    for (size_t i = 0; i < sources->wad_count; i++) {
        wo_free(sources->wads[i].name);
        wo_buf_free(&sources->wads[i].data);
        wo_wad_free(&sources->wads[i].wad);
    }
    for (size_t i = 0; i < sources->file_count; i++) {
        wo_free(sources->files[i].key);
        wo_buf_free(&sources->files[i].data);
    }
    for (size_t i = 0; i < sources->sequence_count; i++) {
        wo_free(sources->sequences[i].key);
        wo_cmp_free(&sources->sequences[i].cmp);
    }
    wo_free(sources->wads);
    wo_free(sources->files);
    wo_free(sources->sequences);
    wo_free(sources);
}

/* Grow an array of slots by one; returns the new zeroed slot or NULL. */
static void *grow(void **items, size_t *count, size_t *cap, size_t size, WoError *error) {
    if (*count == *cap) {
        size_t next = *cap ? *cap * 2 : 8;
        void *grown = wo_realloc_array(*items, next, size, error);
        if (!grown) {
            return NULL;
        }
        *items = grown;
        *cap = next;
    }
    uint8_t *slot = (uint8_t *)*items + *count * size;
    memset(slot, 0, size);
    (*count)++;
    return slot;
}

static const char *text(const char *value) {
    return value ? value : "";
}

static char *source_key(const char *path, const char *wad, const char *entry, bool ps1,
                        WoError *error) {
    return wo_format(error, "%s%s|%s|%s", ps1 ? "ps1/" : "pc/", text(path), text(wad),
                     text(entry));
}

static bool load_wad(WoTexSources *sources, const char *name, const WadSlot **out,
                     WoError *error) {
    for (size_t i = 0; i < sources->wad_count; i++) {
        if (wo_str_ieq(sources->wads[i].name, name)) {
            *out = &sources->wads[i];
            return true;
        }
    }
    WoBuf data = {0};
    WoWad wad = {0};
    char *copy = wo_strdup(name, error);
    bool ok = copy && sources->load(sources->context, WO_TEX_FROM_GAME, name,
                                    WO_TEXSRC_MAX_FILE_BYTES, &data, error) &&
              wo_wad_parse(data.data, data.len, &wad, error);
    WadSlot *slot = ok ? grow((void **)&sources->wads, &sources->wad_count, &sources->wad_cap,
                              sizeof(WadSlot), error)
                       : NULL;
    if (!slot) {
        wo_error_context(error, "%s: ", name);
        wo_wad_free(&wad);
        wo_buf_free(&data);
        wo_free(copy);
        return false;
    }
    slot->name = copy;
    slot->data = data;
    slot->wad = wad;
    *out = slot;
    return true;
}

/* The bytes of a source file or WAD entry; valid while sources lives. */
static bool read_source(WoTexSources *sources, const char *path, const char *wad,
                        const char *entry, bool ps1, const uint8_t **data, size_t *len,
                        WoError *error) {
    if (wad && *wad) {
        const WadSlot *slot = NULL;
        if (!load_wad(sources, wad, &slot, error)) {
            return false;
        }
        const WoWadEntry *found = wo_wad_find(&slot->wad, text(entry));
        if (!found) {
            return wo_fail(error, WO_ERR_NOT_FOUND, "Missing WAD texture entry: %s (in %s)",
                           text(entry), wad);
        }
        *data = slot->data.data + found->offset;
        *len = found->size;
        return true;
    }
    char *key = source_key(path, wad, entry, ps1, error);
    if (!key) {
        return false;
    }
    for (size_t i = 0; i < sources->file_count; i++) {
        if (wo_str_ieq(sources->files[i].key, key)) {
            wo_free(key);
            *data = sources->files[i].data.data;
            *len = sources->files[i].data.len;
            return true;
        }
    }
    WoBuf bytes = {0};
    WoTexOrigin origin = ps1 ? WO_TEX_FROM_PS1 : WO_TEX_FROM_SD;
    bool ok = sources->load(sources->context, origin, text(path), WO_TEXSRC_MAX_FILE_BYTES,
                            &bytes, error);
    FileSlot *slot = ok ? grow((void **)&sources->files, &sources->file_count,
                               &sources->file_cap, sizeof(FileSlot), error)
                        : NULL;
    if (!slot) {
        wo_buf_free(&bytes);
        wo_free(key);
        return false;
    }
    slot->key = key;
    slot->data = bytes;
    *data = slot->data.data;
    *len = slot->data.len;
    return true;
}

static bool load_sequence(WoTexSources *sources, const char *path, const char *wad,
                          const char *entry, bool ps1, const SequenceSlot **out, WoError *error) {
    char *key = source_key(path, wad, entry, ps1, error);
    if (!key) {
        return false;
    }
    for (size_t i = 0; i < sources->sequence_count; i++) {
        if (wo_str_ieq(sources->sequences[i].key, key)) {
            wo_free(key);
            *out = &sources->sequences[i];
            return true;
        }
    }
    const uint8_t *data = NULL;
    size_t len = 0;
    if (!read_source(sources, path, wad, entry, ps1, &data, &len, error)) {
        wo_free(key);
        return false;
    }
    const char *name = (wad && *wad) ? text(entry) : text(path);
    SequenceSlot local = {0};
    local.is_cmp = wo_str_iends_with(name, ".cmp");
    if (local.is_cmp) {
        if (!wo_cmp_decode(data, len, &local.cmp, error)) {
            wo_error_context(error, "%s: ", name);
            wo_free(key);
            return false;
        }
    } else {
        local.single = data;
        local.single_len = len;
    }
    SequenceSlot *slot = grow((void **)&sources->sequences, &sources->sequence_count,
                              &sources->sequence_cap, sizeof(SequenceSlot), error);
    if (!slot) {
        wo_cmp_free(&local.cmp);
        wo_free(key);
        return false;
    }
    *slot = local;
    slot->key = key;
    *out = slot;
    return true;
}

bool wo_texsrc_frame(WoTexSources *sources, const char *path, const char *wad,
                     const char *entry, bool ps1, int64_t index, WoNativeImage *out,
                     WoError *error) {
    memset(out, 0, sizeof(*out));
    const SequenceSlot *sequence = NULL;
    if (!load_sequence(sources, path, wad, entry, ps1, &sequence, error)) {
        return false;
    }
    const uint8_t *data = NULL;
    size_t len = 0;
    size_t frames = sequence->is_cmp ? sequence->cmp.count : 1;
    if (index >= 0 && (uint64_t)index < frames) {
        if (sequence->is_cmp) {
            data = sequence->cmp.data + sequence->cmp.offsets[index];
            len = sequence->cmp.sizes[index];
        } else {
            data = sequence->single;
            len = sequence->single_len;
        }
    }
    const char *name = (wad && *wad) ? text(entry) : text(path);
    if (len == 0) {
        return wo_fail(error, WO_ERR_FORMAT, "Missing native texture frame %lld in %s.",
                       (long long)index, name);
    }
    if (!wo_tim_decode(data, len, out, error)) {
        wo_error_context(error, "%s frame %lld: ", name, (long long)index);
        return false;
    }
    return true;
}

bool wo_texsrc_road(WoTexSources *sources, const char *wad, const char *library,
                    const char *table, int64_t material, int64_t level, WoNativeImage *out,
                    WoError *error) {
    memset(out, 0, sizeof(*out));
    if (level != 128 && level != 64 && level != 32) {
        return wo_fail(error, WO_ERR_FORMAT, "Invalid road texture size %lld.", (long long)level);
    }
    const uint8_t *refs = NULL;
    size_t len = 0;
    if (!read_source(sources, NULL, wad, table, false, &refs, &len, error)) {
        return false;
    }
    if (material < 0 || len % ROAD_RECORD != 0 || (uint64_t)material >= len / ROAD_RECORD) {
        return wo_fail(error, WO_ERR_FORMAT, "Invalid road layout entry %lld in %s.",
                       (long long)material, text(table));
    }
    size_t size = (size_t)level;
    size_t side = size / ROAD_TILE;
    size_t start = (size_t)material * ROAD_RECORD + (size == 128 ? 0 : size == 64 ? 32 : 40);
    uint16_t *pixels = wo_alloc_array(size * size, sizeof(uint16_t), error);
    if (!pixels) {
        return false;
    }
    for (size_t i = 0; i < side * side; i++) {
        unsigned tile = ((unsigned)refs[start + i * 2] << 8) | refs[start + i * 2 + 1];
        WoNativeImage piece;
        if (!wo_texsrc_frame(sources, NULL, wad, library, false, tile, &piece, error)) {
            wo_free(pixels);
            return false;
        }
        if (piece.width != ROAD_TILE || piece.height != ROAD_TILE) {
            wo_native_image_free(&piece);
            wo_free(pixels);
            return wo_fail(error, WO_ERR_FORMAT, "Road tile %u in %s is not 32x32.", tile,
                           text(library));
        }
        size_t row0 = i / side * ROAD_TILE;
        size_t col0 = i % side * ROAD_TILE;
        for (size_t y = 0; y < ROAD_TILE; y++) {
            memcpy(pixels + (row0 + y) * size + col0, piece.pixels + y * ROAD_TILE,
                   ROAD_TILE * sizeof(uint16_t));
        }
        wo_native_image_free(&piece);
    }
    out->width = (uint32_t)size;
    out->height = (uint32_t)size;
    out->pixels = pixels;
    return true;
}
