#include "core/wo_boards.h"

#include <string.h>

#include "core/wo_texfmt.h"

#define PRM_HEADER 144u
#define BOARD_PREFIX "redb"
#define BOARD_PANELS 4u
#define PANEL_KIND 8u       /* a shaded, textured quad */
#define PANEL_TEXTURE 8u    /* in a panel's body: its texture number */
#define PANEL_UVS 14u       /* and its four corners' texture coordinates */
#define SHEET_VRAM_X 320 /* the sheet's first page, in video memory words */
#define SHEET_PAGE 256
#define SHEET_HEIGHT 256
#define NEW_FRAMES 8u
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

/* A board as the PlayStation has it: four textured quads and nothing else. */
static bool four_panels(const uint8_t *data, const PrmObject *object) {
    size_t vertices = be16(data + object->offset + 16);
    size_t normals = be16(data + object->offset + 24);
    size_t p = object->offset + PRM_HEADER + (vertices + normals) * 8u;
    if (be16(data + object->offset + 32) != BOARD_PANELS) {
        return false;
    }
    for (size_t i = 0; i < BOARD_PANELS; i++) {
        size_t body = 0;
        size_t texture = 0;
        if (be16(data + p) != PANEL_KIND || !prm_primitive(PANEL_KIND, &body, &texture)) {
            return false;
        }
        p += 4 + body;
    }
    return true;
}

/* --- Texture numbers ------------------------------------------------------------ */

#define USE_FRAMES 1024u

typedef struct TextureUse {
    const uint8_t *data;
    bool used[USE_FRAMES];
    bool beyond; /* a number outside the table */
} TextureUse;

static void note_texture(void *context, size_t field) {
    TextureUse *use = context;
    unsigned frame = be16(use->data + field);
    if (frame < USE_FRAMES) {
        use->used[frame] = true;
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

static void put_be16(uint8_t *p, size_t value) {
    p[0] = (uint8_t)(value >> 8);
    p[1] = (uint8_t)value;
}

static void map_texture(void *context, size_t field) {
    TextureMap *map = context;
    unsigned frame = be16(map->data + field);
    if (frame >= map->count || map->to[frame] == NO_FRAME) {
        map->unmapped = true;
        return;
    }
    put_be16(map->data + field, map->to[frame]);
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
static bool tim_rect(const uint8_t *tim, size_t size, Rect *out, uint32_t *per_word) {
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

static bool frame_rect(const WoCmp *frames, size_t index, Rect *out, uint32_t *per_word) {
    return tim_rect(frames->data + frames->offsets[index], frames->sizes[index], out, per_word);
}

static bool in_sheet(const Rect *spot) {
    return spot->left >= 0 && spot->left % SHEET_PAGE + spot->width <= SHEET_PAGE &&
           spot->top + spot->height <= SHEET_HEIGHT;
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

/* A texture added after the PlayStation's own. */
typedef struct NewFrame {
    WoBuf tim;
    Rect place;
    size_t pc_frame; /* the PC texture it is, or NO_FRAME: the advert */
} NewFrame;

/* Working memory for one archive. */
typedef struct Boards {
    Scene mine;       /* the PlayStation archive's scenery */
    Scene theirs;     /* the PC archive's */
    WoCmp adverts;    /* the PlayStation's board pictures for this track */
    TextureUse shown; /* by one PC board */
    size_t *map;      /* PC texture number to the new archive's */
    Rect *taken;      /* the PlayStation textures' places (width 0: none) */
    NewFrame added[NEW_FRAMES];
    size_t added_count;
    size_t advert; /* the advert's texture number, or NO_FRAME */
    const int *pictures; /* what the boards without a PC twin show, in turn */
    size_t picture_count;
    Rect advert_place;
    bool advert_fits;
    WoBuf models;
    WoBuf textures;
} Boards;

static void drop_added(Boards *b) {
    for (size_t i = 0; i < b->added_count; i++) {
        wo_buf_free(&b->added[i].tim);
    }
    b->added_count = 0;
}

/* Nothing the archive will hold is at `spot`. */
static bool spot_clear(const Boards *b, const Rect *spot) {
    for (size_t i = 0; i < b->mine.frames.count; i++) {
        if (b->taken[i].width && rects_meet(spot, &b->taken[i])) {
            return false;
        }
    }
    for (size_t i = 0; i < b->added_count; i++) {
        if (rects_meet(spot, &b->added[i].place)) {
            return false;
        }
    }
    return in_sheet(spot);
}

/* Add a texture at `place`: `size` bytes of TIM, moved there. *added is false
 * when there is no room for another. */
static bool add_frame(Boards *b, const uint8_t *tim, size_t size, const Rect *place,
                      uint32_t per_word, size_t pc_frame, bool *added, WoError *error) {
    *added = false;
    if (b->added_count == NEW_FRAMES) {
        return true;
    }
    NewFrame *frame = &b->added[b->added_count];
    memset(frame, 0, sizeof(*frame));
    if (!wo_buf_append(&frame->tim, tim, size, error)) {
        return false;
    }
    if (!wo_tim_move(frame->tim.data, frame->tim.len,
                     (uint32_t)(place->left / per_word + SHEET_VRAM_X), (uint32_t)place->top,
                     NULL)) {
        wo_buf_free(&frame->tim);
        return true;
    }
    frame->place = *place;
    frame->pc_frame = pc_frame;
    b->added_count++;
    *added = true;
    return true;
}

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

/* A board of four panels that has no PC twin. */
static bool free_board(const Boards *b, size_t index) {
    const PrmObject *object = &b->mine.objects.items[index];
    return object->board && object->twin == NO_FRAME && four_panels(b->mine.prm, object);
}

/* What the n-th such board is to show: a PlayStation scenery texture's
 * number, WO_BOARD_ADVERT, or nothing new (NO_PICTURE). */
#define NO_PICTURE (-2)
static int board_picture(const Boards *b, size_t n) {
    return b->picture_count ? b->pictures[n % b->picture_count] : NO_PICTURE;
}

static bool advert_wanted(const Boards *b) {
    size_t n = 0;
    for (size_t i = 0; i < b->mine.objects.count; i++) {
        if (free_board(b, i) && board_picture(b, n++) == WO_BOARD_ADVERT) {
            return true;
        }
    }
    return false;
}

/* A place for the PC texture `frame`, which the PlayStation does not have:
 * where the PC has it if nothing is there now, else where this track's
 * advert would go, else the corner of a PC scenery texture large enough
 * to hold it. All of these belong to scenery the PlayStation models do not
 * use. */
static bool place_picture(const Boards *b, size_t frame, Rect *place, uint32_t *per_word) {
    const WoCmp *pc = &b->theirs.frames;
    Rect picture;
    if (!frame_rect(pc, frame, &picture, per_word)) {
        return false;
    }
    for (size_t candidate = 0; candidate < pc->count + 2; candidate++) {
        Rect host;
        uint32_t host_words = 0;
        if (candidate == 1) {
            if (!b->advert_fits) {
                continue;
            }
            host = b->advert_place;
        } else if (!frame_rect(pc, candidate ? candidate - 2 : frame, &host, &host_words)) {
            continue;
        }
        if (host.width < picture.width || host.height < picture.height || host.left < 0 ||
            host.left % *per_word) {
            continue;
        }
        Rect spot = {host.left, host.top, picture.width, picture.height};
        if (spot_clear(b, &spot)) {
            *place = spot;
            return true;
        }
    }
    return false;
}

/* Find the PC texture `frame` among the new archive's textures, adding it
 * if the PlayStation has no such picture. *found is false when it has no
 * place; false is returned only for lack of memory. */
static bool map_picture(Boards *b, size_t frame, bool *found, WoError *error) {
    const WoCmp *pc = &b->theirs.frames;
    const WoCmp *ps1 = &b->mine.frames;
    *found = true;
    for (size_t i = 0; i < ps1->count; i++) {
        if (same_picture(pc, frame, ps1, i)) {
            b->map[frame] = i;
            return true;
        }
    }
    for (size_t i = 0; i < b->added_count; i++) {
        if (same_picture(pc, frame, pc, b->added[i].pc_frame)) {
            b->map[frame] = ps1->count + i;
            return true;
        }
    }
    Rect place;
    uint32_t per_word = 0;
    *found = place_picture(b, frame, &place, &per_word);
    if (!*found) {
        return true;
    }
    if (!add_frame(b, pc->data + pc->offsets[frame], pc->sizes[frame], &place, per_word, frame,
                   found, error)) {
        return false;
    }
    if (*found) {
        b->map[frame] = ps1->count + b->added_count - 1;
    }
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
        PrmObject scratch;
        memset(&b->shown, 0, sizeof(b->shown));
        b->shown.data = b->theirs.prm;
        prm_object(b->theirs.prm, b->theirs.models->size, b->theirs.objects.items[twin].offset,
                   &scratch, note_texture, &b->shown);
        *found = !b->shown.beyond;
        for (size_t frame = 0; *found && frame < USE_FRAMES; frame++) {
            if (!b->shown.used[frame]) {
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

/* A picture on a board of four panels: each panel shows a quarter of it,
 * as the game's own board routine lays one out. */
static void advertise(uint8_t *board, size_t texture, const Rect *picture) {
    size_t vertices = be16(board + 16);
    size_t normals = be16(board + 24);
    uint8_t *panel = board + PRM_HEADER + (vertices + normals) * 8u;
    unsigned half_w = (unsigned)picture->width / 2u;
    unsigned half_h = (unsigned)picture->height / 2u;
    for (unsigned q = 0; q < BOARD_PANELS; q++) {
        size_t body = 0;
        size_t field = 0;
        prm_primitive(PANEL_KIND, &body, &field);
        put_be16(panel + 4 + PANEL_TEXTURE, texture);
        for (unsigned corner = 0; corner < 4; corner++) {
            uint8_t *uv = panel + 4 + PANEL_UVS + corner * 2u;
            uv[0] = (uint8_t)((q % 2u) * half_w + (corner % 2u) * (half_w - 1u));
            uv[1] = (uint8_t)((q / 2u) * half_h + (corner / 2u) * (half_h - 1u));
        }
        panel += 4 + body;
    }
}

/* The models: the PlayStation's, with the paired boards the PC's and the
 * others showing their pictures. *changed counts the boards that are not
 * as they were. */
static bool write_models(Boards *b, bool *mapped, size_t *changed, WoError *error) {
    *mapped = true;
    *changed = 0;
    size_t free_boards = 0;
    for (size_t i = 0; *mapped && i < b->mine.objects.count; i++) {
        const PrmObject *object = &b->mine.objects.items[i];
        size_t at = b->models.len;
        if (object->twin == NO_FRAME) {
            if (!wo_buf_append(&b->models, b->mine.prm + object->offset, object->size, error)) {
                return false;
            }
            int picture = free_board(b, i) ? board_picture(b, free_boards++) : NO_PICTURE;
            if (picture == WO_BOARD_ADVERT && b->advert != NO_FRAME) {
                advertise(b->models.data + at, b->advert,
                          &b->added[b->advert - b->mine.frames.count].place);
                ++*changed;
            } else if (picture >= 0 && (size_t)picture < b->mine.frames.count &&
                       b->taken[picture].width) {
                advertise(b->models.data + at, (size_t)picture, &b->taken[picture]);
                ++*changed;
            }
            continue;
        }
        const PrmObject *twin = &b->theirs.objects.items[object->twin];
        if (!wo_buf_append(&b->models, b->theirs.prm + twin->offset, twin->size, error)) {
            return false;
        }
        TextureMap renumber = {b->models.data, b->map, b->theirs.frames.count, false};
        PrmObject scratch;
        prm_object(b->models.data, b->models.len, at, &scratch, map_texture, &renumber);
        *mapped = !renumber.unmapped;
        ++*changed;
    }
    return true;
}

/* The textures: the PlayStation's, then the added ones. */
static bool write_textures(Boards *b, WoError *error) {
    const WoCmp *ps1 = &b->mine.frames;
    size_t count = ps1->count + b->added_count;
    const uint8_t **entries = wo_alloc_array(count, sizeof(*entries), error);
    size_t *sizes = entries ? wo_alloc_array(count, sizeof(size_t), error) : NULL;
    bool ok = sizes != NULL;
    for (size_t i = 0; ok && i < count; i++) {
        bool own = i < ps1->count;
        entries[i] = own ? ps1->data + ps1->offsets[i] : b->added[i - ps1->count].tim.data;
        sizes[i] = own ? ps1->sizes[i] : b->added[i - ps1->count].tim.len;
    }
    ok = ok && wo_cmp_encode(entries, sizes, count, &b->textures, error);
    wo_free(sizes);
    wo_free((void *)entries);
    return ok;
}

/* *kept is the number of boards changed, 0 when the archive stays as it is. */
static bool keep_boards(Boards *b, const uint8_t *ps1, size_t ps1_len, const uint8_t *pc,
                        size_t pc_len, const uint8_t *adverts, size_t adverts_len, WoBuf *out,
                        size_t *kept, WoError *error) {
    /* b->pictures and b->picture_count are set by the caller. */
    *kept = 0;
    if (!scene_open(ps1, ps1_len, &b->mine, error) || !scene_open(pc, pc_len, &b->theirs, error)) {
        return false;
    }
    if (!b->mine.known) {
        return true;
    }
    size_t boards = 0;
    for (size_t i = 0; i < b->mine.objects.count; i++) {
        boards += b->mine.objects.items[i].board;
    }
    if (!boards) {
        return true;
    }
    b->advert = NO_FRAME;
    b->taken = wo_alloc_array(b->mine.frames.count, sizeof(Rect), error);
    b->map = b->taken ? wo_alloc_array(b->theirs.frames.count + 1, sizeof(size_t), error) : NULL;
    if (!b->map) {
        return false;
    }
    for (size_t i = 0; i < b->theirs.frames.count; i++) {
        b->map[i] = NO_FRAME;
    }
    for (size_t i = 0; i < b->mine.frames.count; i++) {
        uint32_t per_word = 0;
        if (!frame_rect(&b->mine.frames, i, &b->taken[i], &per_word)) {
            memset(&b->taken[i], 0, sizeof(b->taken[i]));
        }
    }
    /* Where this track's advert goes, which the PlayStation keeps clear. */
    uint32_t advert_words = 0;
    bool advert = adverts && adverts_len &&
                  wo_cmp_decode(adverts, adverts_len, &b->adverts, NULL) && b->adverts.count &&
                  frame_rect(&b->adverts, 0, &b->advert_place, &advert_words);

    /* The PC's boards first. A picture of theirs may go where the advert
     * would, on a track where no board shows the advert. If one of their
     * pictures has no place, the PC's boards are not kept and those boards
     * take their turn with the others. */
    bool whole = b->theirs.known && pair_boards(b) > 0;
    b->advert_fits = advert && !advert_wanted(b) && spot_clear(b, &b->advert_place);
    if (whole && !map_pictures(b, &whole, error)) {
        return false;
    }
    if (!whole) {
        for (size_t i = 0; i < b->mine.objects.count; i++) {
            b->mine.objects.items[i].twin = NO_FRAME;
        }
        drop_added(b);
    }
    if (advert_wanted(b) && advert && spot_clear(b, &b->advert_place)) {
        bool added = false;
        if (!add_frame(b, b->adverts.data + b->adverts.offsets[0], b->adverts.sizes[0],
                       &b->advert_place, advert_words, NO_FRAME, &added, error)) {
            return false;
        }
        if (added) {
            b->advert = b->mine.frames.count + b->added_count - 1;
        }
    }
    size_t changed = 0;
    if (!write_models(b, &whole, &changed, error)) {
        return false;
    }
    if (!whole || !changed) {
        return true;
    }
    if (b->added_count && !write_textures(b, error)) {
        return false;
    }
    if (!write_archive(ps1, &b->mine, &b->models, b->added_count ? &b->textures : NULL, out,
                       error)) {
        return false;
    }
    *kept = changed;
    return true;
}

bool wo_boards_keep(const uint8_t *ps1, size_t ps1_len, const uint8_t *pc, size_t pc_len,
                    const uint8_t *adverts, size_t adverts_len, const int *pictures,
                    size_t picture_count, WoBuf *out, size_t *kept, WoError *error) {
    Boards *b = wo_alloc(sizeof(*b), error);
    if (!b) {
        return false;
    }
    memset(b, 0, sizeof(*b));
    b->pictures = pictures;
    b->picture_count = pictures ? picture_count : 0;
    size_t boards = 0;
    bool ok = keep_boards(b, ps1, ps1_len, pc, pc_len, adverts, adverts_len, out, &boards, error);
    if (ok && !boards) {
        out->len = 0;
        ok = wo_buf_append(out, ps1, ps1_len, error);
    }
    if (kept) {
        *kept = ok ? boards : 0;
    }
    drop_added(b);
    wo_buf_free(&b->textures);
    wo_buf_free(&b->models);
    wo_free(b->taken);
    wo_free(b->map);
    wo_cmp_free(&b->adverts);
    scene_free(&b->theirs);
    scene_free(&b->mine);
    wo_free(b);
    return ok;
}
