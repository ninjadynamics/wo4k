#include "core/wo_boards.h"

#include <string.h>

#include "core/wo_texfmt.h"

#define PRM_HEADER 144u
#define BOARD_PREFIX "redb"
#define SHEET_VRAM_X 320 /* the sheet's first page, in video memory words */
#define SHEET_PAGE 256
#define SHEET_HEIGHT 256
#define NO_FRAME SIZE_MAX

/* --- Scenery models -------------------------------------------------------------
 * A model file is a run of objects: a 144-byte header (name, vertex, normal
 * and primitive counts, origin), the vertices and normals at eight bytes
 * each, then the primitives. All numbers are big-endian. */

typedef struct PrmObject {
    size_t offset;
    size_t size;
    int32_t origin[3];
    bool board;
    size_t twin; /* the PC archive's board at the same place, or NO_FRAME */
} PrmObject;

typedef struct PrmList {
    PrmObject *items;
    size_t count;
} PrmList;

typedef void PrmTexture(void *context, size_t field);

static unsigned be16(const uint8_t *p) {
    return ((unsigned)p[0] << 8) | p[1];
}

static int32_t be32(const uint8_t *p) {
    return (int32_t)(((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) |
                     p[3]);
}

/* Bytes of a primitive after its four-byte head, and where in them its
 * texture number is (0: untextured). Scenery uses flat and shaded triangles
 * and quads, plain or textured (kinds 1 to 8), and sprites (11). */
static bool prm_primitive(unsigned kind, size_t *body, size_t *texture) {
    static const uint8_t corners[9] = {0, 3, 3, 4, 4, 3, 3, 4, 4};
    static const uint8_t tail[9] = {0, 6, 6, 4, 6, 14, 14, 16, 18};
    *texture = 0;
    if (kind == 11) {
        *body = 12;
        *texture = 6;
        return true;
    }
    if (kind < 1 || kind > 8) {
        return false;
    }
    *body = corners[kind] * 2u + tail[kind];
    if (!(kind & 1u)) {
        *texture = corners[kind] * 2u;
        *body += 6u + corners[kind] * 2u;
    }
    return true;
}

/* Read the object at data[at]; false when it is not one this code knows.
 * each (optional) is told where every texture number is. */
static bool prm_object(const uint8_t *data, size_t len, size_t at, PrmObject *out,
                       PrmTexture *each, void *context) {
    if (at > len || len - at < PRM_HEADER) {
        return false;
    }
    size_t vertices = be16(data + at + 16);
    size_t normals = be16(data + at + 24);
    size_t primitives = be16(data + at + 32);
    size_t p = at + PRM_HEADER + (vertices + normals) * 8u;
    for (size_t i = 0; i < primitives; i++) {
        size_t body = 0;
        size_t texture = 0;
        if (p > len || len - p < 4 || !prm_primitive(be16(data + p), &body, &texture) ||
            len - p - 4 < body) {
            return false;
        }
        if (texture && each) {
            each(context, p + 4 + texture);
        }
        p += 4 + body;
    }
    if (p > len) {
        return false;
    }
    out->offset = at;
    out->size = p - at;
    for (int axis = 0; axis < 3; axis++) {
        out->origin[axis] = be32(data + at + 84 + axis * 4);
    }
    out->board = memcmp(data + at, BOARD_PREFIX, sizeof(BOARD_PREFIX) - 1) == 0;
    out->twin = NO_FRAME;
    return true;
}

/* List a model file's objects. *known is false when the file is not a run
 * of objects this code knows; false is returned only for lack of memory. */
static bool prm_list(const uint8_t *data, size_t len, PrmList *out, bool *known,
                     WoError *error) {
    memset(out, 0, sizeof(*out));
    *known = false;
    size_t count = 0;
    PrmObject object;
    for (size_t at = 0; at < len; at += object.size) {
        if (!prm_object(data, len, at, &object, NULL, NULL)) {
            return true;
        }
        count++;
    }
    out->items = wo_alloc_array(count ? count : 1, sizeof(PrmObject), error);
    if (!out->items) {
        return false;
    }
    size_t at = 0;
    for (size_t i = 0; i < count; i++) {
        prm_object(data, len, at, &out->items[i], NULL, NULL);
        at += out->items[i].size;
    }
    out->count = count;
    *known = true;
    return true;
}

/* --- Texture use ---------------------------------------------------------------- */

#define USE_FRAMES 1024u

typedef struct TextureUse {
    const uint8_t *data;
    size_t counts[USE_FRAMES];
    bool beyond; /* a number outside the table */
} TextureUse;

static void count_texture(void *context, size_t field) {
    TextureUse *use = context;
    unsigned frame = be16(use->data + field);
    if (frame < USE_FRAMES) {
        use->counts[frame]++;
    } else {
        use->beyond = true;
    }
}

typedef struct TextureMap {
    uint8_t *data;
    const size_t *to; /* by PC texture number */
    size_t count;
    bool unmapped;
} TextureMap;

static void map_texture(void *context, size_t field) {
    TextureMap *map = context;
    unsigned frame = be16(map->data + field);
    if (frame >= map->count || map->to[frame] == NO_FRAME) {
        map->unmapped = true;
        return;
    }
    map->data[field] = (uint8_t)(map->to[frame] >> 8);
    map->data[field + 1] = (uint8_t)map->to[frame];
}

/* --- Pictures and their places -------------------------------------------------- */

typedef struct Rect {
    int64_t left;
    int64_t top;
    int64_t width;
    int64_t height;
} Rect;

static bool rects_meet(const Rect *a, const Rect *b) {
    return a->left < b->left + b->width && b->left < a->left + a->width &&
           a->top < b->top + b->height && b->top < a->top + a->height;
}

/* A texture's place in the sheet; false when it is not a texture. */
static bool frame_rect(const WoCmp *frames, size_t index, Rect *out, uint32_t *per_word) {
    const uint8_t *tim = frames->data + frames->offsets[index];
    size_t size = frames->sizes[index];
    WoNativeImage image;
    uint32_t x = 0;
    uint32_t y = 0;
    if (!size || !wo_tim_origin(tim, size, &x, &y, per_word, NULL) ||
        !wo_tim_decode(tim, size, &image, NULL)) {
        return false;
    }
    out->left = ((int64_t)x - SHEET_VRAM_X) * *per_word;
    out->top = y;
    out->width = image.width;
    out->height = image.height;
    wo_native_image_free(&image);
    return true;
}

static bool same_picture(const WoCmp *a, size_t i, const WoCmp *b, size_t j) {
    if (!a->sizes[i] || a->sizes[i] != b->sizes[j]) {
        return false;
    }
    WoNativeImage one;
    WoNativeImage other;
    if (!wo_tim_decode(a->data + a->offsets[i], a->sizes[i], &one, NULL)) {
        return false;
    }
    bool same = wo_tim_decode(b->data + b->offsets[j], b->sizes[j], &other, NULL);
    if (same) {
        same = one.width == other.width && one.height == other.height &&
               memcmp(one.pixels, other.pixels,
                      (size_t)one.width * one.height * sizeof(uint16_t)) == 0;
        wo_native_image_free(&other);
    }
    wo_native_image_free(&one);
    return same;
}

/* A place in the sheet for the PC texture `wanted`, clear of the PlayStation
 * textures in `taken`: its own place if none of them is there, or else the
 * corner of a PC scenery texture large enough to hold it. Those places
 * belong to scenery the PlayStation models no longer use. */
static bool free_place(const WoCmp *pc, size_t wanted, const Rect *taken, size_t taken_count,
                       Rect *place, uint32_t *per_word) {
    Rect picture;
    if (!frame_rect(pc, wanted, &picture, per_word)) {
        return false;
    }
    for (size_t candidate = 0; candidate <= pc->count; candidate++) {
        /* First the picture's own place, then each PC texture's. */
        size_t at = candidate ? candidate - 1 : wanted;
        Rect host;
        uint32_t host_words = 0;
        if (!frame_rect(pc, at, &host, &host_words) || host.width < picture.width ||
            host.height < picture.height || host.left < 0 || host.left % *per_word) {
            continue;
        }
        Rect spot = {host.left, host.top, picture.width, picture.height};
        bool fits = spot.left % SHEET_PAGE + spot.width <= SHEET_PAGE &&
                    spot.top + spot.height <= SHEET_HEIGHT;
        for (size_t i = 0; fits && i < taken_count; i++) {
            fits = !taken[i].width || !rects_meet(&spot, &taken[i]);
        }
        if (fits) {
            *place = spot;
            return true;
        }
    }
    return false;
}

/* --- The archive ---------------------------------------------------------------- */

typedef struct Scene {
    WoWad wad;
    const WoWadEntry *models;
    const WoWadEntry *textures;
    const uint8_t *prm; /* the model file's bytes */
    PrmList objects;
    WoCmp frames;
    bool known;
} Scene;

static void scene_free(Scene *scene) {
    wo_free(scene->objects.items);
    wo_cmp_free(&scene->frames);
    wo_wad_free(&scene->wad);
    memset(scene, 0, sizeof(*scene));
}

/* Open an archive's scenery. scene->known is false when it has none this
 * code understands; false is returned only for lack of memory. */
static bool scene_open(const uint8_t *data, size_t len, Scene *scene, WoError *error) {
    memset(scene, 0, sizeof(*scene));
    if (!data || !len || !wo_wad_parse(data, len, &scene->wad, NULL)) {
        return true;
    }
    scene->models = wo_wad_find(&scene->wad, "scene.prm");
    scene->textures = wo_wad_find(&scene->wad, "scene.cmp");
    if (!scene->models || !scene->textures ||
        !wo_cmp_decode(data + scene->textures->offset, scene->textures->size, &scene->frames,
                       NULL)) {
        return true;
    }
    scene->prm = data + scene->models->offset;
    return prm_list(scene->prm, scene->models->size, &scene->objects, &scene->known, error);
}

/* Count the texture numbers one object uses. */
static void object_use(const Scene *scene, size_t index, TextureUse *use) {
    PrmObject scratch;
    use->data = scene->prm;
    prm_object(scene->prm, scene->models->size, scene->objects.items[index].offset, &scratch,
               count_texture, use);
}

/* The PlayStation archive with new models and, if textures is not NULL, new
 * textures. */
static bool write_archive(const uint8_t *ps1, const Scene *scene, const WoBuf *models,
                          const WoBuf *textures, WoBuf *out, WoError *error) {
    const WoWad *wad = &scene->wad;
    out->len = 0;
    bool ok = wo_buf_append(out, ps1, wad->entries[0].offset, error);
    for (size_t i = 0; ok && i < wad->count; i++) {
        const WoWadEntry *entry = &wad->entries[i];
        const uint8_t *data = ps1 + entry->offset;
        size_t size = entry->size;
        if (entry == scene->models) {
            data = models->data;
            size = models->len;
        } else if (entry == scene->textures && textures) {
            data = textures->data;
            size = textures->len;
        }
        ok = wo_buf_append(out, data, size, error);
        if (ok) {
            wo_put_le32(out->data + entry->record, (uint32_t)size);
            wo_put_le32(out->data + entry->record + 4, (uint32_t)size);
        }
    }
    return ok;
}

/* Working memory for one archive. */
typedef struct Boards {
    Scene mine;        /* the PlayStation archive's scenery */
    Scene theirs;      /* the PC archive's */
    TextureUse all;    /* by every PlayStation object */
    TextureUse paired; /* by the boards that have a PC twin */
    TextureUse shown;  /* by one PC board */
    size_t *map;       /* PC texture number to the new archive's */
    Rect *taken;       /* the PlayStation textures' places (width 0: none) */
    size_t spare;      /* the placeholder only paired boards use */
    size_t added;      /* the PC texture that takes its place */
    WoBuf picture;     /* that texture, moved */
    WoBuf models;
    WoBuf textures;
} Boards;

/* Pair each board with the PC's board at the same place; how many. */
static size_t pair_boards(Boards *b) {
    size_t boards = 0;
    for (size_t i = 0; i < b->mine.objects.count; i++) {
        PrmObject *board = &b->mine.objects.items[i];
        for (size_t j = 0; board->board && board->twin == NO_FRAME && j < b->theirs.objects.count;
             j++) {
            const PrmObject *twin = &b->theirs.objects.items[j];
            bool taken = false;
            for (size_t k = 0; k < i; k++) {
                taken = taken || b->mine.objects.items[k].twin == j;
            }
            if (twin->board && !taken &&
                memcmp(twin->origin, board->origin, sizeof(board->origin)) == 0) {
                board->twin = j;
                boards++;
            }
        }
    }
    return boards;
}

/* The one texture the paired boards use, if nothing else uses it. */
static void find_spare(Boards *b) {
    size_t used = 0;
    b->spare = NO_FRAME;
    for (size_t i = 0; i < b->mine.objects.count; i++) {
        if (b->mine.objects.items[i].twin != NO_FRAME) {
            object_use(&b->mine, i, &b->paired);
        }
    }
    for (size_t frame = 0; frame < USE_FRAMES && frame < b->mine.frames.count; frame++) {
        if (b->paired.counts[frame]) {
            used++;
            b->spare = b->paired.counts[frame] == b->all.counts[frame] ? frame : NO_FRAME;
        }
    }
    if (used != 1) {
        b->spare = NO_FRAME;
    }
}

/* Find the PC texture `frame` among the new archive's textures, giving it
 * the placeholder if the PlayStation has no such picture. *found is false
 * when it has no place; false is returned only for lack of memory. */
static bool map_picture(Boards *b, size_t frame, bool *found, WoError *error) {
    const WoCmp *pc = &b->theirs.frames;
    const WoCmp *ps1 = &b->mine.frames;
    *found = true;
    for (size_t i = 0; i < ps1->count; i++) {
        if (i != b->spare && same_picture(pc, frame, ps1, i)) {
            b->map[frame] = i;
            return true;
        }
    }
    if (b->added != NO_FRAME) {
        /* The placeholder is taken: only the same picture again will do. */
        *found = same_picture(pc, frame, pc, b->added);
        b->map[frame] = *found ? b->spare : NO_FRAME;
        return true;
    }
    Rect place;
    uint32_t per_word = 0;
    *found = b->spare != NO_FRAME &&
             free_place(pc, frame, b->taken, ps1->count, &place, &per_word);
    if (!*found) {
        return true;
    }
    b->picture.len = 0;
    if (!wo_buf_append(&b->picture, pc->data + pc->offsets[frame], pc->sizes[frame], error)) {
        return false;
    }
    *found = wo_tim_move(b->picture.data, b->picture.len,
                         (uint32_t)(place.left / per_word + SHEET_VRAM_X), (uint32_t)place.top,
                         NULL);
    b->added = frame;
    b->map[frame] = b->spare;
    return true;
}

/* Map every texture the PC's paired boards show. */
static bool map_pictures(Boards *b, bool *found, WoError *error) {
    *found = true;
    for (size_t i = 0; *found && i < b->mine.objects.count; i++) {
        size_t twin = b->mine.objects.items[i].twin;
        if (twin == NO_FRAME) {
            continue;
        }
        memset(&b->shown, 0, sizeof(b->shown));
        object_use(&b->theirs, twin, &b->shown);
        *found = !b->shown.beyond;
        for (size_t frame = 0; *found && frame < USE_FRAMES; frame++) {
            if (!b->shown.counts[frame]) {
                continue;
            }
            if (frame >= b->theirs.frames.count) {
                *found = false;
            } else if (b->map[frame] == NO_FRAME && !map_picture(b, frame, found, error)) {
                return false;
            }
        }
    }
    return true;
}

/* The models: the PlayStation's, with the paired boards the PC's. */
static bool write_models(Boards *b, bool *mapped, WoError *error) {
    *mapped = true;
    for (size_t i = 0; *mapped && i < b->mine.objects.count; i++) {
        const PrmObject *object = &b->mine.objects.items[i];
        if (object->twin == NO_FRAME) {
            if (!wo_buf_append(&b->models, b->mine.prm + object->offset, object->size, error)) {
                return false;
            }
            continue;
        }
        const PrmObject *twin = &b->theirs.objects.items[object->twin];
        size_t at = b->models.len;
        if (!wo_buf_append(&b->models, b->theirs.prm + twin->offset, twin->size, error)) {
            return false;
        }
        TextureMap renumber = {b->models.data, b->map, b->theirs.frames.count, false};
        PrmObject scratch;
        prm_object(b->models.data, b->models.len, at, &scratch, map_texture, &renumber);
        *mapped = !renumber.unmapped;
    }
    return true;
}

/* The textures: the PlayStation's, the placeholder now the PC's picture. */
static bool write_textures(Boards *b, WoError *error) {
    const WoCmp *ps1 = &b->mine.frames;
    const uint8_t **entries = wo_alloc_array(ps1->count, sizeof(*entries), error);
    size_t *sizes = entries ? wo_alloc_array(ps1->count, sizeof(size_t), error) : NULL;
    bool ok = sizes != NULL;
    for (size_t i = 0; ok && i < ps1->count; i++) {
        entries[i] = i == b->spare ? b->picture.data : ps1->data + ps1->offsets[i];
        sizes[i] = i == b->spare ? b->picture.len : ps1->sizes[i];
    }
    ok = ok && wo_cmp_encode(entries, sizes, ps1->count, &b->textures, error);
    wo_free(sizes);
    wo_free((void *)entries);
    return ok;
}

/* *kept is the number of boards taken, 0 when the archive stays as it is. */
static bool keep_boards(Boards *b, const uint8_t *ps1, size_t ps1_len, const uint8_t *pc,
                        size_t pc_len, WoBuf *out, size_t *kept, WoError *error) {
    *kept = 0;
    if (!scene_open(ps1, ps1_len, &b->mine, error) || !scene_open(pc, pc_len, &b->theirs, error)) {
        return false;
    }
    if (!b->mine.known || !b->theirs.known) {
        return true;
    }
    for (size_t i = 0; i < b->mine.objects.count; i++) {
        object_use(&b->mine, i, &b->all);
    }
    size_t boards = pair_boards(b);
    if (!boards || b->all.beyond) {
        return true;
    }
    find_spare(b);
    b->added = NO_FRAME;
    b->map = wo_alloc_array(b->theirs.frames.count, sizeof(size_t), error);
    b->taken = b->map ? wo_alloc_array(b->mine.frames.count, sizeof(Rect), error) : NULL;
    if (!b->taken) {
        return false;
    }
    for (size_t i = 0; i < b->theirs.frames.count; i++) {
        b->map[i] = NO_FRAME;
    }
    for (size_t i = 0; i < b->mine.frames.count; i++) {
        uint32_t per_word = 0;
        if (i == b->spare || !frame_rect(&b->mine.frames, i, &b->taken[i], &per_word)) {
            memset(&b->taken[i], 0, sizeof(b->taken[i]));
        }
    }
    bool whole = true;
    if (!map_pictures(b, &whole, error) || (whole && !write_models(b, &whole, error)) ||
        (whole && b->added != NO_FRAME && !write_textures(b, error))) {
        return false;
    }
    if (!whole) {
        return true;
    }
    if (!write_archive(ps1, &b->mine, &b->models, b->added != NO_FRAME ? &b->textures : NULL, out,
                       error)) {
        return false;
    }
    *kept = boards;
    return true;
}

bool wo_boards_keep(const uint8_t *ps1, size_t ps1_len, const uint8_t *pc, size_t pc_len,
                    WoBuf *out, size_t *kept, WoError *error) {
    Boards *b = wo_alloc(sizeof(*b), error);
    if (!b) {
        return false;
    }
    memset(b, 0, sizeof(*b));
    size_t boards = 0;
    bool ok = keep_boards(b, ps1, ps1_len, pc, pc_len, out, &boards, error);
    if (ok && !boards) {
        out->len = 0;
        ok = wo_buf_append(out, ps1, ps1_len, error);
    }
    if (kept) {
        *kept = ok ? boards : 0;
    }
    wo_buf_free(&b->picture);
    wo_buf_free(&b->textures);
    wo_buf_free(&b->models);
    wo_free(b->taken);
    wo_free(b->map);
    scene_free(&b->theirs);
    scene_free(&b->mine);
    wo_free(b);
    return ok;
}
