#include "core/wo_deband.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../tools/deband/filter.h"
#include "core/wo_fs.h"
#include "core/wo_hash.h"
#include "core/wo_hdregions.h"
#include "core/wo_hdtex.h"
#include "core/wo_log.h"
#include "core/wo_natives.h"
#include "core/wo_png.h"
#include "core/wo_texfmt.h"
#include "core/wo_texsrc.h"

/* Bump when the filter or anything else here changes what is written. */
#define DEBAND_REVISION 2
#define SELECTION_MAX_BYTES (32u * 1024u * 1024u)
#define RECEIPT_MAX_BYTES (4u * 1024u * 1024u)
#define ARCHIVE_MAX_BYTES (64u * 1024u * 1024u)
#define PAGE_SIDE 256
#define MARGIN 16 /* texels of a neighbour around a joined frame; more than the filter reaches */
#define ROAD_TILE 32

/* The two strengths of the comparison page, as tools/Deband-Ps1Textures.py
 * runs the tool for them: how far light and dark details are kept out of
 * the band ramps. */
typedef enum Level { LEVEL_ORIGINAL, LEVEL_V1, LEVEL_V2, LEVEL_COUNT } Level;
static const float LEVEL_KEEP[LEVEL_COUNT] = {0.0f, 0.25f, 1.0f};

/* --- The choices ----------------------------------------------------------------------- */

typedef struct Choice {
    const char *id;   /* "TRACK13/SCENE.CMP#0016" */
    const char *file; /* "track13-scene-cmp-16.png" */
    Level level;
} Choice;

typedef struct Choices {
    WoJson *json; /* owns the strings */
    Choice *items;
    size_t count;
} Choices;

static void choices_free(Choices *choices) {
    wo_free(choices->items);
    wo_json_free(choices->json);
    memset(choices, 0, sizeof(*choices));
}

static int compare_choices(const void *a, const void *b) {
    return strcmp(((const Choice *)a)->id, ((const Choice *)b)->id);
}

static bool choices_load(const char *path, Choices *choices, WoError *error) {
    memset(choices, 0, sizeof(*choices));
    if (!wo_read_json(path, SELECTION_MAX_BYTES, &choices->json, error)) {
        return false;
    }
    const WoJson *list = wo_json_get(choices->json, "choices");
    bool ok = wo_str_eq(wo_json_str(choices->json, "format", ""), "wo4k.deband-selection") &&
              wo_json_int(choices->json, "version", 0) == 1 && wo_json_is(list, WO_JSON_ARRAY);
    if (!ok) {
        choices_free(choices);
        return wo_fail(error, WO_ERR_FORMAT, "Unsupported deband selection: %s", path);
    }
    choices->items = wo_alloc_array(list->count ? list->count : 1, sizeof(Choice), error);
    if (!choices->items) {
        choices_free(choices);
        return false;
    }
    for (size_t i = 0; i < list->count; i++) {
        const WoJson *entry = list->items[i];
        const char *id = wo_json_str(entry, "id", NULL);
        const char *file = wo_json_str(entry, "file", NULL);
        const char *version = wo_json_str(entry, "version", "");
        Level level = wo_str_eq(version, "v1")   ? LEVEL_V1
                      : wo_str_eq(version, "v2") ? LEVEL_V2
                                                 : LEVEL_ORIGINAL;
        if (!id || !file || (level == LEVEL_ORIGINAL && !wo_str_eq(version, "original"))) {
            choices_free(choices);
            return wo_fail(error, WO_ERR_FORMAT, "Invalid deband choice %zu in %s", i + 1, path);
        }
        choices->items[choices->count++] = (Choice){id, file, level};
    }
    qsort(choices->items, choices->count, sizeof(Choice), compare_choices);
    return true;
}

static const Choice *choices_find(const Choices *choices, const char *archive, int64_t frame) {
    char id[160];
    if (snprintf(id, sizeof(id), "%s#%04lld", archive, (long long)frame) >= (int)sizeof(id)) {
        return NULL;
    }
    for (char *c = id; *c; c++) {
        *c = (char)((*c >= 'a' && *c <= 'z') ? *c - 32 : *c);
    }
    Choice key = {id, NULL, LEVEL_ORIGINAL};
    return bsearch(&key, choices->items, choices->count, sizeof(Choice), compare_choices);
}

/* --- Which frame borders meet ---------------------------------------------------------- */

/* The textured faces of a models file (scene.prm): per kind, its corners,
 * the index words after them, whether a picture follows and the bytes
 * after that; other kinds are skipped whole. */
typedef struct FaceKind {
    uint8_t corners;
    uint8_t extra;
    uint8_t textured;
    uint8_t tail;
} FaceKind;
static const FaceKind FACE_KINDS[24] = {
    [1] = {3, 0, 0, 6},   [2] = {3, 0, 1, 6},   [3] = {4, 0, 0, 4},   [4] = {4, 0, 1, 6},
    [5] = {3, 0, 0, 14},  [6] = {3, 0, 1, 14},  [7] = {4, 0, 0, 16},  [8] = {4, 0, 1, 18},
    [9] = {3, 1, 0, 4},   [10] = {3, 1, 1, 4},  [12] = {4, 1, 1, 4},  [13] = {3, 3, 0, 12},
    [14] = {3, 3, 1, 12}, [15] = {4, 4, 0, 16}, [16] = {4, 4, 1, 18}, [17] = {1, 3, 0, 4},
    [18] = {1, 3, 0, 4}};
static const uint8_t SKIPPED_KINDS[24] = {[11] = 12, [20] = 52, [21] = 24, [22] = 36, [23] = 12};

/* One edge of a textured face: its two ends in the world, lower first, and
 * where each lies on the face's picture. */
typedef struct Edge {
    int64_t a[3];
    int64_t b[3];
    int texture;
    uint8_t ua[2];
    uint8_t ub[2];
    size_t order;
} Edge;

typedef struct Edges {
    Edge *items;
    size_t count;
    size_t cap;
} Edges;

static int be16(const uint8_t *p) {
    return (int16_t)((uint16_t)p[0] << 8 | p[1]);
}

static int64_t be32(const uint8_t *p) {
    return (int32_t)((uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]);
}

static int compare_points(const int64_t *a, const int64_t *b) {
    for (int i = 0; i < 3; i++) {
        if (a[i] != b[i]) {
            return a[i] < b[i] ? -1 : 1;
        }
    }
    return 0;
}

static bool add_edge(Edges *edges, const int64_t *p, const int64_t *q, int texture,
                     const uint8_t *up, const uint8_t *uq, WoError *error) {
    if (edges->count == edges->cap) {
        size_t cap = edges->cap ? edges->cap * 2 : 1024;
        Edge *grown = wo_realloc_array(edges->items, cap, sizeof(*grown), error);
        if (!grown) {
            return false;
        }
        edges->items = grown;
        edges->cap = cap;
    }
    Edge *edge = &edges->items[edges->count];
    bool first = compare_points(p, q) < 0;
    memcpy(edge->a, first ? p : q, sizeof(edge->a));
    memcpy(edge->b, first ? q : p, sizeof(edge->b));
    memcpy(edge->ua, first ? up : uq, 2);
    memcpy(edge->ub, first ? uq : up, 2);
    edge->texture = texture;
    edge->order = edges->count++;
    return true;
}

/* Every edge of every textured face. A file that ends early, or has a face
 * of a kind unknown here, gives the edges read so far. */
static bool model_edges(const uint8_t *data, size_t len, Edges *edges, WoError *error) {
    size_t at = 0;
    while (len - at >= 144 && at < len) {
        int vertices = be16(data + at + 16);
        int normals = be16(data + at + 24);
        int faces = be16(data + at + 32);
        int64_t origin[3] = {be32(data + at + 84), be32(data + at + 88), be32(data + at + 92)};
        at += 144;
        if (vertices < 0 || normals < 0 || faces < 0 ||
            (size_t)(vertices + normals) * 8 > len - at) {
            return true;
        }
        const uint8_t *points = data + at;
        at += (size_t)(vertices + normals) * 8;
        for (int f = 0; f < faces; f++) {
            if (len - at < 4) {
                return true;
            }
            int kind = be16(data + at);
            at += 4;
            if (kind < 0 || kind >= 24) {
                return true;
            }
            if (SKIPPED_KINDS[kind]) {
                if (len - at < SKIPPED_KINDS[kind]) {
                    return true;
                }
                at += SKIPPED_KINDS[kind];
                continue;
            }
            FaceKind shape = FACE_KINDS[kind];
            size_t size = (size_t)shape.corners * 2 + (size_t)shape.extra * 2 +
                          (shape.textured ? 6 + (size_t)shape.corners * 2 : 0) + shape.tail;
            if (!shape.corners || len - at < size) {
                return true;
            }
            if (shape.textured) {
                const uint8_t *face = data + at;
                const uint8_t *uvs = face + shape.corners * 2 + shape.extra * 2 + 6;
                int texture = be16(face + shape.corners * 2 + shape.extra * 2);
                int64_t corner[4][3] = {{0}};
                bool whole = true;
                for (int c = 0; c < shape.corners; c++) {
                    int index = be16(face + c * 2);
                    whole = whole && index >= 0 && index < vertices;
                    for (int k = 0; whole && k < 3; k++) {
                        corner[c][k] = origin[k] + be16(points + (size_t)index * 8 + (size_t)k * 2);
                    }
                }
                static const int quad[4][2] = {{0, 1}, {1, 3}, {3, 2}, {2, 0}};
                static const int triangle[3][2] = {{0, 1}, {1, 2}, {2, 0}};
                for (int e = 0; whole && e < shape.corners && shape.corners >= 3; e++) {
                    int i = shape.corners == 4 ? quad[e][0] : triangle[e][0];
                    int j = shape.corners == 4 ? quad[e][1] : triangle[e][1];
                    if (!add_edge(edges, corner[i], corner[j], texture, uvs + i * 2, uvs + j * 2,
                                  error)) {
                        return false;
                    }
                }
            }
            at += size;
        }
    }
    return true;
}

static int compare_edges(const void *a, const void *b) {
    const Edge *left = a;
    const Edge *right = b;
    int order = compare_points(left->a, right->a);
    if (!order) {
        order = compare_points(left->b, right->b);
    }
    if (!order) {
        order = left->order < right->order ? -1 : left->order > right->order;
    }
    return order;
}

/* The border of a frame an edge lies along (0 left, 1 right, 2 top,
 * 3 bottom; -1: none), and where along that border its two ends are. */
static int edge_border(const uint8_t *p, const uint8_t *q, uint32_t width, uint32_t height, int *from,
                       int *to) {
    int u1 = p[0], v1 = p[1], u2 = q[0], v2 = q[1];
    if (abs(u1 - u2) <= 1 && abs(v1 - v2) * 5 >= (int)height * 3) {
        if (u1 <= 2 || u1 >= (int)width - 3) {
            *from = v1;
            *to = v2;
            return u1 <= 2 ? 0 : 1;
        }
    }
    if (abs(v1 - v2) <= 1 && abs(u1 - u2) * 5 >= (int)width * 3) {
        if (v1 <= 2 || v1 >= (int)height - 3) {
            *from = u1;
            *to = u2;
            return v1 <= 2 ? 2 : 3;
        }
    }
    return -1;
}

typedef struct Group {
    size_t start; /* its first edge, among the sorted edges */
    size_t order; /* when that edge was read */
} Group;

static int compare_groups(const void *a, const void *b) {
    const Group *left = a;
    const Group *right = b;
    return left->order < right->order ? -1 : left->order > right->order;
}

typedef struct Vote {
    int frame;
    int side;
    int other;
    int other_side;
    bool reversed;
    size_t count;
    size_t order;
} Vote;

static int compare_votes(const void *a, const void *b) {
    const Vote *left = a;
    const Vote *right = b;
    if (left->count != right->count) {
        return left->count > right->count ? -1 : 1;
    }
    return left->order < right->order ? -1 : left->order > right->order;
}

/* Two faces that share an edge in the world, each with that edge along a
 * border of its own picture, say those two borders meet. A pair of borders
 * needs two such edges: a single one is usually two unrelated faces. Where
 * a border has several candidates the one seen most often wins, the first
 * seen among equals. */
bool wo_deband_joins(const uint8_t *models, size_t len, const uint32_t *widths,
                     const uint32_t *heights, size_t count, WoDebandJoin *joins, WoError *error) {
    memset(joins, 0, count * WO_DEBAND_SIDES * sizeof(*joins));
    Edges edges = {0};
    Vote *votes = NULL;
    size_t vote_count = 0;
    size_t vote_cap = 0;
    Group *starts = NULL;
    bool ok = model_edges(models, len, &edges, error);
    if (ok && edges.count) {
        /* The edges between the same two points together, and those groups
         * in the order their first edge was read. */
        qsort(edges.items, edges.count, sizeof(Edge), compare_edges);
        starts = wo_alloc_array(edges.count, sizeof(*starts), error);
        ok = starts != NULL;
    }
    size_t groups = 0;
    for (size_t i = 0; ok && i < edges.count; i++) {
        if (!i || compare_points(edges.items[i].a, edges.items[i - 1].a) ||
            compare_points(edges.items[i].b, edges.items[i - 1].b)) {
            starts[groups++] = (Group){i, edges.items[i].order};
        }
    }
    if (ok && groups) {
        qsort(starts, groups, sizeof(Group), compare_groups);
    }
    for (size_t g = 0; ok && g < groups; g++) {
        size_t first = starts[g].start;
        size_t end = first;
        while (end < edges.count && !compare_points(edges.items[end].a, edges.items[first].a) &&
               !compare_points(edges.items[end].b, edges.items[first].b)) {
            end++;
        }
        for (size_t i = first; ok && i < end; i++) {
            for (size_t j = first; ok && j < end; j++) {
                const Edge *one = &edges.items[i];
                const Edge *two = &edges.items[j];
                if (one->texture == two->texture || one->texture < 0 || two->texture < 0 ||
                    (size_t)one->texture >= count || (size_t)two->texture >= count ||
                    !widths[one->texture] || !widths[two->texture]) {
                    continue;
                }
                int from_a = 0, to_a = 0, from_b = 0, to_b = 0;
                int side_a = edge_border(one->ua, one->ub, widths[one->texture],
                                         heights[one->texture], &from_a, &to_a);
                int side_b = edge_border(two->ua, two->ub, widths[two->texture],
                                         heights[two->texture], &from_b, &to_b);
                if (side_a < 0 || side_b < 0) {
                    continue;
                }
                bool reversed = (to_a > from_a) != (to_b > from_b);
                size_t v = 0;
                while (v < vote_count &&
                       !(votes[v].frame == one->texture && votes[v].side == side_a &&
                         votes[v].other == two->texture && votes[v].other_side == side_b &&
                         votes[v].reversed == reversed)) {
                    v++;
                }
                if (v == vote_count) {
                    if (vote_count == vote_cap) {
                        size_t cap = vote_cap ? vote_cap * 2 : 256;
                        Vote *grown = wo_realloc_array(votes, cap, sizeof(*grown), error);
                        ok = grown != NULL;
                        if (!ok) {
                            break;
                        }
                        votes = grown;
                        vote_cap = cap;
                    }
                    votes[vote_count] = (Vote){one->texture, side_a, two->texture, side_b,
                                               reversed,     0,      vote_count};
                    vote_count++;
                }
                votes[v].count++;
            }
        }
    }
    if (ok && vote_count) {
        qsort(votes, vote_count, sizeof(Vote), compare_votes);
    }
    for (size_t v = 0; ok && v < vote_count && votes[v].count >= 2; v++) {
        WoDebandJoin *join = &joins[(size_t)votes[v].frame * WO_DEBAND_SIDES + (size_t)votes[v].side];
        if (!join->set) {
            *join = (WoDebandJoin){true, votes[v].other, votes[v].other_side, votes[v].reversed};
        }
    }
    wo_free(starts);
    wo_free(votes);
    wo_free(edges.items);
    return ok;
}

/* --- A track's scenery frames ---------------------------------------------------------- */

typedef struct Track {
    char name[16]; /* "TRACK13" */
    WoNativeImage *frames; /* the PlayStation's SCENE.CMP; pixels NULL where there is none */
    size_t count;
    WoDebandJoin *joins;
    WoNativeImage advert; /* first frame of the track's advert file, if it has one */
    WoNativeImage slogan; /* its second, which the boards show in turn with the first */
} Track;

typedef struct Tracks {
    Track *items;
    size_t count;
    size_t cap;
} Tracks;

static void tracks_free(Tracks *tracks) {
    for (size_t i = 0; i < tracks->count; i++) {
        Track *track = &tracks->items[i];
        for (size_t f = 0; f < track->count; f++) {
            wo_native_image_free(&track->frames[f]);
        }
        wo_free(track->frames);
        wo_free(track->joins);
        wo_native_image_free(&track->advert);
        wo_native_image_free(&track->slogan);
    }
    wo_free(tracks->items);
    memset(tracks, 0, sizeof(*tracks));
}

/* The frames of a file of the imported disc that decode; a missing or
 * damaged file has none, which is not an error here. */
static bool disc_frames(const char *game_root, const char *relative, WoNativeImage **frames,
                        size_t *count, WoError *error) {
    *frames = NULL;
    *count = 0;
    char *inner = wo_format(error, "textures/ps1-source/%s", relative);
    char *path = inner ? wo_path_child(game_root, inner, error) : NULL;
    wo_free(inner);
    if (!path) {
        return false;
    }
    WoBuf bytes = {0};
    WoCmp cmp;
    memset(&cmp, 0, sizeof(cmp));
    bool ok = true;
    if (wo_is_file(path) && wo_read_file(path, ARCHIVE_MAX_BYTES, &bytes, NULL) &&
        wo_cmp_decode(bytes.data, bytes.len, &cmp, NULL)) {
        *frames = wo_alloc_array(cmp.count, sizeof(**frames), error);
        ok = *frames != NULL;
        for (size_t i = 0; ok && i < cmp.count; i++) {
            if (!cmp.sizes[i] ||
                !wo_tim_decode(cmp.data + cmp.offsets[i], cmp.sizes[i], &(*frames)[i], NULL)) {
                memset(&(*frames)[i], 0, sizeof(**frames));
            }
        }
        *count = ok ? cmp.count : 0;
    }
    wo_cmp_free(&cmp);
    wo_buf_free(&bytes);
    wo_free(path);
    return ok;
}

static bool track_joins(const char *game_root, Track *track, WoError *error) {
    track->joins = wo_alloc_array(track->count ? track->count * WO_DEBAND_SIDES : 1,
                                  sizeof(*track->joins), error);
    uint32_t *widths = wo_alloc_array(track->count ? track->count : 1, sizeof(*widths), error);
    uint32_t *heights = wo_alloc_array(track->count ? track->count : 1, sizeof(*heights), error);
    char *inner = wo_format(error, "textures/ps1-source/%s/TRACK.WAD", track->name);
    char *path = inner ? wo_path_child(game_root, inner, error) : NULL;
    bool ok = track->joins && widths && heights && path;
    if (ok) {
        memset(track->joins, 0, (track->count ? track->count * WO_DEBAND_SIDES : 1) * sizeof(*track->joins));
        for (size_t i = 0; i < track->count; i++) {
            widths[i] = track->frames[i].pixels ? track->frames[i].width : 0;
            heights[i] = track->frames[i].pixels ? track->frames[i].height : 0;
        }
        WoBuf bytes = {0};
        WoWad wad;
        memset(&wad, 0, sizeof(wad));
        if (wo_is_file(path) && wo_read_file(path, ARCHIVE_MAX_BYTES, &bytes, NULL) &&
            wo_wad_parse(bytes.data, bytes.len, &wad, NULL)) {
            const WoWadEntry *models = wo_wad_find(&wad, "scene.prm");
            if (models) {
                ok = wo_deband_joins(bytes.data + models->offset, models->size, widths, heights,
                                     track->count, track->joins, error);
            }
        }
        wo_wad_free(&wad);
        wo_buf_free(&bytes);
    }
    wo_free(path);
    wo_free(inner);
    wo_free(heights);
    wo_free(widths);
    return ok;
}

static bool tracks_get(Tracks *tracks, const char *game_root, const WoJson *plan, const char *name,
                       Track **out, WoError *error) {
    for (size_t i = 0; i < tracks->count; i++) {
        if (wo_str_ieq(tracks->items[i].name, name)) {
            *out = &tracks->items[i];
            return true;
        }
    }
    if (tracks->count == tracks->cap) {
        size_t cap = tracks->cap ? tracks->cap * 2 : 8;
        Track *grown = wo_realloc_array(tracks->items, cap, sizeof(*grown), error);
        if (!grown) {
            return false;
        }
        tracks->items = grown;
        tracks->cap = cap;
    }
    Track *track = &tracks->items[tracks->count];
    memset(track, 0, sizeof(*track));
    if (!wo_copy_str(track->name, sizeof(track->name), name)) {
        return wo_fail(error, WO_ERR_FORMAT, "Invalid track name: %s", name);
    }
    tracks->count++; /* owned from here, so a failure below is freed with the rest */
    char *scene = wo_format(error, "%s/SCENE.CMP", name);
    bool ok = scene && disc_frames(game_root, scene, &track->frames, &track->count, error) &&
              track_joins(game_root, track, error);
    wo_free(scene);
    const WoJson *scenery = wo_json_get(plan, "scenery");
    for (size_t i = 0; ok && i < wo_json_length(scenery); i++) {
        const WoJson *entry = wo_json_at(scenery, i);
        const char *adverts = wo_json_str(wo_json_get(entry, "boards"), "adverts", NULL);
        if (adverts && wo_str_ieq(wo_json_str(entry, "track", ""), name)) {
            WoNativeImage *frames = NULL;
            size_t count = 0;
            ok = disc_frames(game_root, adverts, &frames, &count, error);
            if (ok && count) {
                track->advert = frames[0];
                memset(&frames[0], 0, sizeof(frames[0]));
            }
            if (ok && count > 1) {
                track->slogan = frames[1];
                memset(&frames[1], 0, sizeof(frames[1]));
            }
            for (size_t f = 0; f < count; f++) {
                wo_native_image_free(&frames[f]);
            }
            wo_free(frames);
        }
    }
    *out = track;
    return ok;
}

/* --- What the sheets show ---------------------------------------------------------------- */

typedef struct Region {
    char native[32]; /* the sheet's file name, lower case: "nrace03.tga" */
    int64_t x;
    int64_t y;
    uint32_t width;
    uint32_t height;
    WoRegionSource source;
    char file[64]; /* see WoSheetRegion; empty for a picture only the PC has */
    int64_t number;
    int64_t level;
    uint16_t tiles[WO_TEXSRC_ROAD_TILES];
    size_t tile_count;
    uint16_t *pixels;
    bool whole; /* nothing pasted later covers any of it */
} Region;

typedef struct Regions {
    Region *items;
    size_t count;
    size_t cap;
} Regions;

static void regions_free(Regions *regions) {
    for (size_t i = 0; i < regions->count; i++) {
        wo_free(regions->items[i].pixels);
    }
    wo_free(regions->items);
    memset(regions, 0, sizeof(*regions));
}

static bool note_region(void *context, const WoSheetRegion *seen, WoError *error) {
    Regions *regions = context;
    if (regions->count == regions->cap) {
        size_t cap = regions->cap ? regions->cap * 2 : 256;
        Region *grown = wo_realloc_array(regions->items, cap, sizeof(*grown), error);
        if (!grown) {
            return false;
        }
        regions->items = grown;
        regions->cap = cap;
    }
    Region *region = &regions->items[regions->count];
    memset(region, 0, sizeof(*region));
    const char *name = wo_path_basename(seen->atlas);
    size_t texels = (size_t)seen->picture->width * seen->picture->height;
    if (!wo_copy_str(region->native, sizeof(region->native), name) ||
        (seen->file && !wo_copy_str(region->file, sizeof(region->file), seen->file)) ||
        seen->tile_count > WO_COUNT(region->tiles) || seen->x < 0 || seen->y < 0 || !texels) {
        return true; /* nothing this layer can place */
    }
    for (char *c = region->native; *c; c++) {
        *c = wo_ascii_lower(*c);
    }
    region->pixels = wo_alloc_array(texels, sizeof(uint16_t), error);
    if (!region->pixels) {
        return false;
    }
    memcpy(region->pixels, seen->picture->pixels, texels * sizeof(uint16_t));
    region->x = seen->x;
    region->y = seen->y;
    region->width = seen->picture->width;
    region->height = seen->picture->height;
    region->source = seen->source;
    region->number = seen->number;
    region->level = seen->level;
    region->tile_count = seen->tile_count;
    if (seen->tile_count) {
        memcpy(region->tiles, seen->tiles, seen->tile_count * sizeof(uint16_t));
    }
    regions->count++;
    return true;
}

/* A region counts while every texel of it is still the one it pasted, and
 * it lies within one of the sheet's 256-texel pages. */
static bool mark_whole(Regions *regions, WoError *error) {
    for (size_t first = 0; first < regions->count; first++) {
        const char *native = regions->items[first].native;
        bool seen = false;
        for (size_t i = 0; i < first; i++) {
            seen = seen || wo_str_eq(regions->items[i].native, native);
        }
        if (seen) {
            continue;
        }
        int64_t width = 0;
        int64_t height = 0;
        for (size_t i = first; i < regions->count; i++) {
            const Region *region = &regions->items[i];
            if (wo_str_eq(region->native, native)) {
                width = region->x + region->width > width ? region->x + region->width : width;
                height = region->y + region->height > height ? region->y + region->height : height;
            }
        }
        if (width > 65536 || height > 65536) {
            return wo_fail(error, WO_ERR_FORMAT, "Texture placements too large on %s", native);
        }
        int32_t *owner = wo_alloc_array((size_t)(width * height), sizeof(*owner), error);
        if (!owner) {
            return false;
        }
        for (int pass = 0; pass < 2; pass++) {
            for (size_t i = first; i < regions->count; i++) {
                Region *region = &regions->items[i];
                if (!wo_str_eq(region->native, native)) {
                    continue;
                }
                bool whole = region->x % PAGE_SIDE + region->width <= PAGE_SIDE &&
                             region->y + region->height <= PAGE_SIDE;
                for (uint32_t y = 0; y < region->height; y++) {
                    int32_t *row = owner + (region->y + y) * width + region->x;
                    for (uint32_t x = 0; x < region->width; x++) {
                        if (!pass) {
                            row[x] = (int32_t)i;
                        } else if (row[x] != (int32_t)i) {
                            whole = false;
                        }
                    }
                }
                region->whole = pass && whole;
            }
        }
        wo_free(owner);
    }
    return true;
}

/* --- Debanding --------------------------------------------------------------------------- */

typedef struct Work {
    const WoDebandOptions *options;
    Choices choices;
    Tracks tracks;
    Regions regions;
    float *noise;
    WoStrList written; /* file names, each once */
    WoHdRegionIndex index;
} Work;

static uint8_t expand5(unsigned value) {
    return (uint8_t)((value & 31u) * 255u / 31u);
}

/* PlayStation colours as the comparison page's pictures have them: colour 0
 * see-through, everything else solid, flagged black included. */
static void to_rgba(const uint16_t *pixels, size_t count, uint8_t *out) {
    for (size_t i = 0; i < count; i++) {
        uint16_t c = pixels[i];
        out[i * 4] = expand5(c);
        out[i * 4 + 1] = expand5(c >> 5);
        out[i * 4 + 2] = expand5(c >> 10);
        out[i * 4 + 3] = c ? 255 : 0;
    }
}

static uint8_t *filtered(const Work *work, const uint8_t *rgba, uint32_t width, uint32_t height,
                         Level level, const char *name, WoError *error) {
    DebandOptions settings;
    deband_defaults(&settings);
    settings.keep = LEVEL_KEEP[level];
    uint8_t *out = deband_picture(rgba, (int)width, (int)height, &settings, work->noise,
                                  deband_seed(name));
    if (!out) {
        wo_fail(error, WO_ERR_INTERNAL, "Out of memory.");
    }
    return out; /* for free(), not wo_free() */
}

/* A frame with MARGIN texels of each neighbour whose border meets one of
 * its own, laid along that border and stretched to its length: row `depth`
 * of the strip is the neighbour's border line at depth 0, the next line in
 * at depth 1. */
static uint8_t *with_margins(const Track *track, size_t index, const uint8_t *own, WoError *error) {
    uint32_t width = track->frames[index].width;
    uint32_t height = track->frames[index].height;
    size_t stride = ((size_t)width + 2 * MARGIN) * 4;
    uint8_t *canvas = wo_alloc_array(((size_t)height + 2 * MARGIN) * stride, 1, error);
    if (!canvas) {
        return NULL;
    }
    memset(canvas, 0, ((size_t)height + 2 * MARGIN) * stride);
    for (uint32_t y = 0; y < height; y++) {
        memcpy(canvas + (y + MARGIN) * stride + MARGIN * 4, own + (size_t)y * width * 4,
               (size_t)width * 4);
    }
    for (int side = 0; side < WO_DEBAND_SIDES; side++) {
        const WoDebandJoin *join = &track->joins[index * WO_DEBAND_SIDES + (size_t)side];
        if (!join->set || !track->frames[join->frame].pixels) {
            continue;
        }
        const WoNativeImage *other = &track->frames[join->frame];
        uint32_t along_other = join->side < 2 ? other->height : other->width;
        uint32_t deep_other = join->side < 2 ? other->width : other->height;
        uint32_t length = side < 2 ? height : width;
        for (uint32_t depth = 0; depth < MARGIN && depth < deep_other; depth++) {
            for (uint32_t j = 0; j < length; j++) {
                uint32_t along = (uint32_t)((uint64_t)j * along_other / length);
                if (join->reversed) {
                    along = along_other - 1 - along;
                }
                uint32_t ox = join->side == 0   ? depth
                              : join->side == 1 ? other->width - 1 - depth
                                                : along;
                uint32_t oy = join->side == 2   ? depth
                              : join->side == 3 ? other->height - 1 - depth
                                                : along;
                size_t cx = side == 0   ? MARGIN - 1 - depth
                            : side == 1 ? MARGIN + width + depth
                                        : MARGIN + j;
                size_t cy = side == 2   ? MARGIN - 1 - depth
                            : side == 3 ? MARGIN + height + depth
                                        : MARGIN + j;
                to_rgba(other->pixels + (size_t)oy * other->width + ox, 1,
                        canvas + cy * stride + cx * 4);
            }
        }
    }
    return canvas;
}

static bool write_picture(Work *work, const char *name, uint32_t width, uint32_t height,
                          uint8_t *rgba, WoError *error) {
    char *path = wo_path_child(work->options->output_dir, name, error);
    if (!path) {
        return false;
    }
    WoImage image = {width, height, rgba};
    bool ok = wo_png_write(path, &image, WO_PIXELS_RGBA, error) &&
              wo_strlist_push(&work->written, name, error);
    wo_free(path);
    return ok;
}

/* One frame, at the strength chosen for it. */
static bool deband_frame(Work *work, const Region *region, const Choice *choice, Track *track,
                         int64_t frame, WoError *error) {
    size_t texels = (size_t)region->width * region->height;
    uint8_t *rgba = wo_alloc_array(texels, 4, error);
    if (!rgba) {
        return false;
    }
    to_rgba(region->pixels, texels, rgba);
    bool joined = false;
    for (int side = 0; track && side < WO_DEBAND_SIDES; side++) {
        joined = joined || track->joins[(size_t)frame * WO_DEBAND_SIDES + (size_t)side].set;
    }
    bool ok = true;
    if (joined) {
        uint8_t *canvas = with_margins(track, (size_t)frame, rgba, error);
        uint8_t *smooth = canvas ? filtered(work, canvas, region->width + 2 * MARGIN,
                                            region->height + 2 * MARGIN, choice->level,
                                            choice->file, error)
                                 : NULL;
        ok = smooth != NULL;
        for (uint32_t y = 0; ok && y < region->height; y++) {
            memcpy(rgba + (size_t)y * region->width * 4,
                   smooth + ((size_t)(y + MARGIN) * (region->width + 2 * MARGIN) + MARGIN) * 4,
                   (size_t)region->width * 4);
        }
        free(smooth);
        wo_free(canvas);
    } else {
        uint8_t *smooth = filtered(work, rgba, region->width, region->height, choice->level,
                                   choice->file, error);
        ok = smooth != NULL;
        if (ok) {
            memcpy(rgba, smooth, texels * 4);
        }
        free(smooth);
    }
    ok = ok && write_picture(work, choice->file, region->width, region->height, rgba, error);
    wo_free(rgba);
    return ok;
}

static bool valid_name(const char *name) {
    size_t length = strlen(name);
    if (length < 5 || length >= 128 || !wo_str_iends_with(name, ".png")) {
        return false;
    }
    for (size_t i = 0; i < length - 4; i++) {
        char c = name[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) {
            return false;
        }
    }
    return true;
}

static bool was_written(const Work *work, const char *name) {
    for (size_t i = 0; i < work->written.count; i++) {
        if (wo_str_eq(work->written.items[i], name)) {
            return true;
        }
    }
    return false;
}

/* A road: the whole picture debanded once for each strength its tiles were
 * given, and every tile taken from the one chosen for it. *name is empty
 * when no tile is to be debanded. */
static bool deband_road(Work *work, const Region *region, char *name, size_t name_size,
                        WoError *error) {
    name[0] = 0;
    uint32_t side = region->width / ROAD_TILE;
    if (!region->file[0] || region->width != region->height || !side ||
        side * ROAD_TILE != region->width || (size_t)side * side != region->tile_count) {
        return true;
    }
    Level levels[WO_TEXSRC_ROAD_TILES];
    bool wanted[LEVEL_COUNT] = {false};
    for (size_t i = 0; i < region->tile_count; i++) {
        const Choice *choice = choices_find(&work->choices, region->file, region->tiles[i]);
        levels[i] = choice ? choice->level : LEVEL_ORIGINAL;
        wanted[levels[i]] = true;
    }
    if (!wanted[LEVEL_V1] && !wanted[LEVEL_V2]) {
        return true;
    }
    /* "TRACK13/LIBRARY.CMP" material 0 at 128: "track13-road-0-128.png". */
    char road[128];
    if (snprintf(road, sizeof(road), "%.*s-road-%lld-%lld.png", (int)strcspn(region->file, "/"),
                 region->file, (long long)region->number,
                 (long long)region->level) >= (int)sizeof(road)) {
        return true;
    }
    for (char *c = road; *c; c++) {
        *c = wo_ascii_lower(*c);
    }
    if (!valid_name(road) || !wo_copy_str(name, name_size, road)) {
        name[0] = 0;
        return true;
    }
    if (was_written(work, name)) {
        return true; /* the same material at the same size, placed again */
    }
    size_t texels = (size_t)region->width * region->height;
    uint8_t *rgba = wo_alloc_array(texels, 4, error);
    uint8_t *out = rgba ? wo_alloc_array(texels, 4, error) : NULL;
    bool ok = out != NULL;
    if (ok) {
        to_rgba(region->pixels, texels, rgba);
        memcpy(out, rgba, texels * 4);
    }
    for (int level = LEVEL_V1; ok && level < LEVEL_COUNT; level++) {
        if (!wanted[level]) {
            continue;
        }
        uint8_t *smooth = filtered(work, rgba, region->width, region->height, (Level)level, name,
                                   error);
        ok = smooth != NULL;
        for (size_t i = 0; ok && i < region->tile_count; i++) {
            if (levels[i] != (Level)level) {
                continue;
            }
            size_t left = i % side * ROAD_TILE;
            size_t top = i / side * ROAD_TILE;
            for (size_t y = 0; y < ROAD_TILE; y++) {
                size_t at = ((top + y) * region->width + left) * 4;
                memcpy(out + at, smooth + at, ROAD_TILE * 4);
            }
        }
        free(smooth);
    }
    ok = ok && write_picture(work, name, region->width, region->height, out, error);
    wo_free(out);
    wo_free(rgba);
    return ok;
}

static bool same_picture(const Region *region, const WoNativeImage *image) {
    return image->pixels && image->width == region->width && image->height == region->height &&
           !memcmp(image->pixels, region->pixels,
                   (size_t)region->width * region->height * sizeof(uint16_t));
}

/* The second picture of a track's advert file. It is on no sheet: the game
 * paints it over the first by turns, and looks for it by its name. */
static bool deband_slogan(Work *work, const Track *track, const char *archive, WoError *error) {
    const Choice *choice =
        track->slogan.pixels ? choices_find(&work->choices, archive, 1) : NULL;
    if (!choice || choice->level == LEVEL_ORIGINAL || !valid_name(choice->file) ||
        was_written(work, choice->file)) {
        return true;
    }
    Region picture;
    memset(&picture, 0, sizeof(picture));
    picture.width = track->slogan.width;
    picture.height = track->slogan.height;
    picture.pixels = track->slogan.pixels;
    return deband_frame(work, &picture, choice, NULL, 1, error);
}

/* The PNG that belongs on a region, written if it has not been yet; *name
 * is empty when the region keeps the sheet's texels. */
static bool region_picture(Work *work, const Region *region, char *name, size_t name_size,
                           WoError *error) {
    name[0] = 0;
    if (region->source == WO_REGION_ROAD) {
        return deband_road(work, region, name, name_size, error);
    }
    const char *archive = region->file;
    int64_t frame = region->number;
    char scene[64];
    Track *track = NULL;
    if (region->source == WO_REGION_SCENERY) {
        /* The archive in place: the PlayStation's frame of that number, or
         * the advert added for the track's boards. */
        if (!tracks_get(&work->tracks, work->options->game_root, work->options->plan,
                        region->file, &track, error)) {
            return false;
        }
        if (frame >= 0 && (uint64_t)frame < track->count &&
            same_picture(region, &track->frames[frame])) {
            snprintf(scene, sizeof(scene), "%s/SCENE.CMP", track->name);
            archive = scene;
        } else if (same_picture(region, &track->advert)) {
            const WoJson *scenery = wo_json_get(work->options->plan, "scenery");
            archive = "";
            for (size_t i = 0; i < wo_json_length(scenery); i++) {
                const WoJson *entry = wo_json_at(scenery, i);
                if (wo_str_ieq(wo_json_str(entry, "track", ""), track->name)) {
                    archive = wo_json_str(wo_json_get(entry, "boards"), "adverts", "");
                }
            }
            if (archive[0] && !deband_slogan(work, track, archive, error)) {
                return false;
            }
            frame = 0;
            track = NULL;
        } else {
            return true;
        }
    } else if (archive[0] && wo_str_iends_with(archive, "/SCENE.CMP")) {
        char folder[16];
        size_t length = strcspn(archive, "/");
        if (length < sizeof(folder)) {
            memcpy(folder, archive, length);
            folder[length] = 0;
            if (!tracks_get(&work->tracks, work->options->game_root, work->options->plan, folder,
                            &track, error)) {
                return false;
            }
            if (frame < 0 || (uint64_t)frame >= track->count ||
                !same_picture(region, &track->frames[frame])) {
                track = NULL; /* not the frame its neighbours were found for */
            }
        }
    }
    if (!archive[0]) {
        return true;
    }
    const Choice *choice = choices_find(&work->choices, archive, frame);
    if (!choice || choice->level == LEVEL_ORIGINAL || !valid_name(choice->file) ||
        !wo_copy_str(name, name_size, choice->file)) {
        name[0] = 0;
        return true;
    }
    /* A picture shown in several places is debanded once. */
    return was_written(work, name) || deband_frame(work, region, choice, track, frame, error);
}

/* --- The step ---------------------------------------------------------------------------- */

/* What the layer is made from, as one digest. */
static bool make_stamp(const WoDebandOptions *options, char stamp[WO_SHA256_HEX_SIZE],
                       WoError *error) {
    char selection[WO_SHA256_HEX_SIZE];
    if (!wo_hash_cache_file(options->cache, options->selection, selection, error)) {
        return false;
    }
    WoBuf text = {0};
    char *head = wo_format(error, "deband %d\n%s\n%lld\n", DEBAND_REVISION, selection,
                           (long long)wo_json_int(options->native_receipt, "revision", -1));
    bool ok = head && wo_buf_append_str(&text, head, error);
    wo_free(head);
    const WoJson *files = wo_json_get(options->native_receipt, "files");
    for (size_t i = 0; ok && i < wo_json_length(files); i++) {
        const WoJson *record = wo_json_at(files, i);
        ok = wo_buf_append_str(&text, wo_json_str(record, "file", ""), error) &&
             wo_buf_append_str(&text, "=", error) &&
             wo_buf_append_str(&text, wo_json_str(record, "sha256", ""), error) &&
             wo_buf_append_str(&text, "\n", error);
    }
    ok = ok && wo_sha256_mem(text.data, text.len, stamp, error);
    wo_buf_free(&text);
    return ok;
}

static bool build(Work *work, WoDebandResult *result, WoError *error) {
    const WoDebandOptions *options = work->options;
    work->noise = wo_alloc_array(DEBAND_NOISE_COUNT, sizeof(float), error);
    if (!work->noise) {
        return false;
    }
    if (!deband_noise(work->noise)) {
        return wo_fail(error, WO_ERR_INTERNAL, "Out of memory.");
    }
    if (!choices_load(options->selection, &work->choices, error) ||
        !wo_natives_regions(options->game_root, options->plan, note_region, &work->regions,
                            error) ||
        !mark_whole(&work->regions, error) || !wo_mkdirs(options->output_dir, error)) {
        return false;
    }
    for (size_t i = 0; i < work->regions.count; i++) {
        const Region *region = &work->regions.items[i];
        if (!region->whole) {
            continue;
        }
        char name[128];
        if (!region_picture(work, region, name, sizeof(name), error)) {
            return false;
        }
        if (name[0] &&
            !wo_hd_regions_add(&work->index, name, region->native, region->x / PAGE_SIDE,
                               region->x % PAGE_SIDE, region->y, region->width, region->height,
                               false, error)) {
            return false;
        }
    }
    result->pictures = work->written.count;
    result->regions = work->index.count;
    return true;
}

bool wo_deband_install(const WoDebandOptions *options, WoDebandResult *result, WoError *error) {
    memset(result, 0, sizeof(*result));
    if (!options->selection || !wo_is_file(options->selection)) {
        return true;
    }
    char stamp[WO_SHA256_HEX_SIZE];
    char *receipt_path = wo_path_child(options->game_root, "textures/deband.json", error);
    char *index_path = receipt_path
                           ? wo_path_child(options->game_root, "textures/sd-regions.bin", error)
                           : NULL;
    bool ok = index_path && make_stamp(options, stamp, error);
    WoJson *previous = NULL;
    if (ok && wo_is_file(receipt_path)) {
        wo_read_json(receipt_path, RECEIPT_MAX_BYTES, &previous, NULL); /* unreadable: build */
    }
    if (ok && !options->force && wo_is_file(index_path) &&
        wo_str_eq(wo_json_str(previous, "stamp", ""), stamp)) {
        wo_json_free(previous);
        wo_free(index_path);
        wo_free(receipt_path);
        return true;
    }
    Work work;
    memset(&work, 0, sizeof(work));
    work.options = options;
    ok = ok && build(&work, result, error);
    bool changed = false;
    ok = ok && wo_hd_regions_save(&work.index, index_path, &changed, error);
    if (ok) {
        /* Pictures of the last build that this one no longer makes. */
        const WoJson *old = wo_json_get(previous, "files");
        for (size_t i = 0; i < wo_json_length(old); i++) {
            const char *name = wo_json_as_str(wo_json_at(old, i), "");
            bool kept = !valid_name(name);
            for (size_t j = 0; !kept && j < work.written.count; j++) {
                kept = wo_str_eq(work.written.items[j], name);
            }
            char *path = kept ? NULL : wo_path_child(options->output_dir, name, NULL);
            if (path && wo_is_file(path)) {
                wo_delete_file(path, NULL);
            }
            wo_free(path);
        }
        WoJson *receipt = wo_json_new_object();
        WoJson *files = wo_json_new_array();
        ok = receipt && files && wo_json_put_int(receipt, "format", 1) &&
             wo_json_put_str(receipt, "stamp", stamp) &&
             wo_json_put_int(receipt, "pictures", (int64_t)result->pictures) &&
             wo_json_put_int(receipt, "regions", (int64_t)result->regions);
        for (size_t i = 0; ok && i < work.written.count; i++) {
            ok = wo_json_push_str(files, work.written.items[i]);
        }
        if (ok) {
            ok = wo_json_put(receipt, "files", files);
            files = NULL;
        }
        if (!ok) {
            wo_fail(error, WO_ERR_INTERNAL, "Out of memory.");
        }
        ok = ok && wo_write_json(receipt_path, receipt, error);
        wo_json_free(files);
        wo_json_free(receipt);
    }
    result->built = ok;
    if (ok) {
        WoJson *data = wo_json_new_object();
        if (data) {
            wo_json_put_int(data, "pictures", (int64_t)result->pictures);
            wo_json_put_int(data, "regions", (int64_t)result->regions);
            wo_log_event("textures_debanded", data);
        }
    }
    wo_hd_regions_free(&work.index);
    wo_strlist_free(&work.written);
    wo_free(work.noise);
    regions_free(&work.regions);
    tracks_free(&work.tracks);
    choices_free(&work.choices);
    wo_json_free(previous);
    wo_free(index_path);
    wo_free(receipt_path);
    return ok;
}
