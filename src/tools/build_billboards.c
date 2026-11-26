/* wo4k-build billboards <game folder> <output folder> [<assets folder>]
 *
 * A contact sheet of the trackside billboards, for deciding how each one's
 * picture should fill it: writes <output folder>/data.js and copies the page
 * (tools/billboards/index.html) beside it. Maintainer tool; it reads the
 * installed track archives and never changes them.
 *
 * A billboard is a textured surface that shows one whole picture once. The
 * 1996 artwork was laid over boards with little regard for proportions: a
 * square picture can be stretched over a board three times as wide. For each
 * board this works out the stretch: the world distance a texel covers along
 * the picture's width, over the distance it covers along its height.
 *
 * Found in each track's scenery models (scene.prm of TRACKnn/TRACK.WAD):
 * per object, the faces drawn with one picture. When one affine mapping from
 * picture to world fits all of them (the PlayStation's four-panel boards
 * show a quarter each) and together they show at least most of the picture,
 * they are one board. Faces that each show the whole picture side by side
 * are a tiled surface (a wall) and are left out; a picture shown on both
 * sides of a board gives a board for each side. Identical boards of a track
 * (the same picture at the same size) are one entry with several instances.
 *
 * Sprites are boards too: pictures that always face the camera (the
 * orange cat on Odessa Keys is one), drawn whole at a width and height of
 * their own.
 *
 * With each board goes the rest of its object as seen face on: the faces
 * near the board's plane and facing the same way, projected onto it, so the
 * page can show a board's frame or backing behind its picture.
 *
 * Each picture is the debanded one (assets/sd/<name>.png), else the
 * archive's own; where authored artwork replaces it (assets/auth: a PNG of
 * the picture's name, or one the HD index places exactly over the picture on
 * the track's sheet, the index being worked out afresh from assets/auth as
 * Play would, so new artwork shows before the game has run), that goes with
 * it too, and
 * the page has a section of its own for boards that show authored artwork.
 * All are put into data.js as PNG data, so that the page can sample their
 * colours. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "core/wo_fs.h"
#include "core/wo_hdtex.h"
#include "core/wo_install.h"
#include "core/wo_json.h"
#include "core/wo_natives.h"
#include "core/wo_png.h"
#include "core/wo_texfmt.h"
#include "tools/build_tool.h"

#define PRM_HEADER 144u
#define MAX_CORNERS 4
#define COVER_MIN 0.80  /* a board shows at least this much of its picture each way */
#define FIT_TOLERANCE 0.04 /* of the board's size, for one mapping to fit all its faces */
#define PLANE_SLACK 0.35 /* backing faces lie within this much of the board's height of its plane */
#define MIN_PICTURE 8   /* smaller pictures are lights and dots, not boards */

static const char *const TRACK_NAMES[][2] = {
    {"TRACK01", "Talon's Reach"}, {"TRACK08", "Sagarmatha"},   {"TRACK13", "Valparaiso"},
    {"TRACK20", "Phenitia Park"}, {"TRACK02", "Gare d'Europa"}, {"TRACK17", "Odessa Keys"},
    {"TRACK06", "Vostok Island"}, {"TRACK07", "Spilskinanke"}};

/* --- Models --------------------------------------------------------------------------- */

typedef struct Face {
    int corners;
    int vertex[MAX_CORNERS];
    int texture; /* -1: none */
    int u[MAX_CORNERS];
    int v[MAX_CORNERS];
    uint8_t colour[3];
} Face;

typedef struct Sprite {
    int width, height; /* in world units */
    int texture;
} Sprite;

typedef struct Object {
    char name[17];
    int vertex_count;
    double (*vertices)[3];
    Face *faces;
    int face_count;
    Sprite *sprites;
    int sprite_count;
} Object;

typedef struct Scene {
    Object *objects;
    int count;
} Scene;

static unsigned be16(const uint8_t *p) {
    return ((unsigned)p[0] << 8) | p[1];
}

static int sbe16(const uint8_t *p) {
    return (int16_t)be16(p);
}

/* A primitive's bytes after its four-byte head, its corners and where its
 * texture number is (0: none): flat and shaded triangles and quads, plain or
 * textured (kinds 1 to 8), and sprites (11). */
static bool primitive_shape(unsigned kind, size_t *body, int *corners, size_t *texture) {
    static const uint8_t count[9] = {0, 3, 3, 4, 4, 3, 3, 4, 4};
    static const uint8_t tail[9] = {0, 6, 6, 4, 6, 14, 14, 16, 18};
    *texture = 0;
    *corners = 0;
    if (kind == 11) {
        *body = 12;
        return true;
    }
    if (kind < 1 || kind > 8) {
        return false;
    }
    *corners = count[kind];
    *body = (size_t)count[kind] * 2u + tail[kind];
    if (!(kind & 1u)) {
        *texture = (size_t)count[kind] * 2u;
        *body += 6u + (size_t)count[kind] * 2u;
    }
    return true;
}

static void scene_free(Scene *scene) {
    for (int i = 0; i < scene->count; i++) {
        free(scene->objects[i].vertices);
        free(scene->objects[i].faces);
        free(scene->objects[i].sprites);
    }
    free(scene->objects);
    memset(scene, 0, sizeof(*scene));
}

/* The objects of a model file; false when it is not one this code reads. */
static bool scene_read(const uint8_t *data, size_t len, Scene *scene) {
    memset(scene, 0, sizeof(*scene));
    int cap = 0;
    for (size_t at = 0; at + PRM_HEADER <= len;) {
        const uint8_t *head = data + at;
        size_t vertices = be16(head + 16);
        size_t normals = be16(head + 24);
        size_t primitives = be16(head + 32);
        size_t p = at + PRM_HEADER + (vertices + normals) * 8u;
        if (p > len) {
            return false;
        }
        if (scene->count == cap) {
            cap = cap ? cap * 2 : 64;
            Object *grown = realloc(scene->objects, (size_t)cap * sizeof(Object));
            if (!grown) {
                return false;
            }
            scene->objects = grown;
        }
        Object *object = &scene->objects[scene->count++];
        memset(object, 0, sizeof(*object));
        memcpy(object->name, head, 16);
        for (int i = 0; i < 16; i++) {
            char c = object->name[i];
            if (c && (c < 32 || c > 126)) {
                object->name[i] = '?';
            }
        }
        object->vertex_count = (int)vertices;
        object->vertices = calloc(vertices ? vertices : 1, sizeof(*object->vertices));
        object->faces = calloc(primitives ? primitives : 1, sizeof(Face));
        object->sprites = calloc(primitives ? primitives : 1, sizeof(Sprite));
        if (!object->vertices || !object->faces || !object->sprites) {
            return false;
        }
        for (size_t i = 0; i < vertices; i++) {
            const uint8_t *v = head + PRM_HEADER + i * 8u;
            object->vertices[i][0] = sbe16(v);
            object->vertices[i][1] = sbe16(v + 2);
            object->vertices[i][2] = sbe16(v + 4);
        }
        for (size_t i = 0; i < primitives; i++) {
            if (p + 4 > len) {
                return false;
            }
            unsigned kind = be16(data + p);
            size_t body = 0;
            size_t texture = 0;
            int corners = 0;
            if (!primitive_shape(kind, &body, &corners, &texture) || p + 4 + body > len) {
                return false;
            }
            const uint8_t *b = data + p + 4;
            if (kind == 11) {
                /* A sprite: its corner, width, height and picture. */
                Sprite *sprite = &object->sprites[object->sprite_count++];
                sprite->width = (int)be16(b + 2);
                sprite->height = (int)be16(b + 4);
                sprite->texture = (int)be16(b + 6);
            }
            if (corners) {
                Face *face = &object->faces[object->face_count];
                bool valid = true;
                face->corners = corners;
                for (int c = 0; c < corners; c++) {
                    face->vertex[c] = (int)be16(b + c * 2);
                    valid = valid && face->vertex[c] < (int)vertices;
                }
                face->texture = texture ? (int)be16(b + texture) : -1;
                if (texture) {
                    const uint8_t *uv = b + texture + 6;
                    for (int c = 0; c < corners; c++) {
                        face->u[c] = uv[c * 2];
                        face->v[c] = uv[c * 2 + 1];
                    }
                }
                /* The first colour: after the corners for a plain face, after
                 * the coordinates and two bytes of padding for a textured one. */
                const uint8_t *colour = b + (size_t)corners * 2u + (texture ? 6u + (size_t)corners * 2u + 2u : 0u);
                if (corners == 3 && !texture && kind == 1) {
                    colour += 2; /* a flat triangle pads its three corners first */
                }
                memcpy(face->colour, colour, 3);
                if (valid) {
                    object->face_count++;
                }
            }
            p += 4 + body;
        }
        at = p;
    }
    return scene->count > 0;
}

/* --- Geometry ------------------------------------------------------------------------- */

typedef struct Vec {
    double x, y, z;
} Vec;

static Vec vec(const double *p) {
    return (Vec){p[0], p[1], p[2]};
}
static Vec sub(Vec a, Vec b) {
    return (Vec){a.x - b.x, a.y - b.y, a.z - b.z};
}
static Vec add(Vec a, Vec b) {
    return (Vec){a.x + b.x, a.y + b.y, a.z + b.z};
}
static Vec scale(Vec a, double s) {
    return (Vec){a.x * s, a.y * s, a.z * s};
}
static double dot(Vec a, Vec b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
static Vec cross(Vec a, Vec b) {
    return (Vec){a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
static double length(Vec a) {
    return sqrt(dot(a, a));
}

/* world = origin + u * eu + v * ev, fitted by least squares to the corners
 * of the faces given; false when the corners do not span a plane of picture. */
typedef struct Mapping {
    Vec origin, eu, ev;
    double residual; /* the largest distance of a corner from where the mapping puts it */
} Mapping;

static bool fit_mapping(const Object *object, const int *faces, int count, Mapping *out) {
    double s[3][3] = {{0}};
    Vec r[3] = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}};
    for (int f = 0; f < count; f++) {
        const Face *face = &object->faces[faces[f]];
        for (int c = 0; c < face->corners; c++) {
            double a[3] = {1, face->u[c], face->v[c]};
            Vec p = vec(object->vertices[face->vertex[c]]);
            for (int i = 0; i < 3; i++) {
                for (int j = 0; j < 3; j++) {
                    s[i][j] += a[i] * a[j];
                }
                r[i] = add(r[i], scale(p, a[i]));
            }
        }
    }
    /* Solve the 3x3 normal equations by Cramer's rule. */
    double det = s[0][0] * (s[1][1] * s[2][2] - s[1][2] * s[2][1]) -
                 s[0][1] * (s[1][0] * s[2][2] - s[1][2] * s[2][0]) +
                 s[0][2] * (s[1][0] * s[2][1] - s[1][1] * s[2][0]);
    if (fabs(det) < 1e-9) {
        return false;
    }
    double inv[3][3];
    inv[0][0] = (s[1][1] * s[2][2] - s[1][2] * s[2][1]) / det;
    inv[0][1] = (s[0][2] * s[2][1] - s[0][1] * s[2][2]) / det;
    inv[0][2] = (s[0][1] * s[1][2] - s[0][2] * s[1][1]) / det;
    inv[1][0] = (s[1][2] * s[2][0] - s[1][0] * s[2][2]) / det;
    inv[1][1] = (s[0][0] * s[2][2] - s[0][2] * s[2][0]) / det;
    inv[1][2] = (s[0][2] * s[1][0] - s[0][0] * s[1][2]) / det;
    inv[2][0] = (s[1][0] * s[2][1] - s[1][1] * s[2][0]) / det;
    inv[2][1] = (s[0][1] * s[2][0] - s[0][0] * s[2][1]) / det;
    inv[2][2] = (s[0][0] * s[1][1] - s[0][1] * s[1][0]) / det;
    Vec k[3];
    for (int i = 0; i < 3; i++) {
        k[i] = add(add(scale(r[0], inv[i][0]), scale(r[1], inv[i][1])), scale(r[2], inv[i][2]));
    }
    out->origin = k[0];
    out->eu = k[1];
    out->ev = k[2];
    out->residual = 0;
    for (int f = 0; f < count; f++) {
        const Face *face = &object->faces[faces[f]];
        for (int c = 0; c < face->corners; c++) {
            Vec want = add(out->origin, add(scale(out->eu, face->u[c]), scale(out->ev, face->v[c])));
            double d = length(sub(want, vec(object->vertices[face->vertex[c]])));
            out->residual = d > out->residual ? d : out->residual;
        }
    }
    return length(out->eu) > 1e-6 && length(out->ev) > 1e-6;
}

/* --- Pictures ------------------------------------------------------------------------- */

typedef struct Picture {
    char key[48];  /* "TRACK13:5" */
    char name[64]; /* "track13-scene-cmp-5.png", or "" for a picture only the archive has */
    char source[8]; /* "auth", "sd" or "archive" */
    uint32_t width, height; /* in texels */
    char *data_uri;
    char auth_name[128]; /* the authored artwork shown instead, or "" */
    char *auth_uri;
} Picture;

typedef struct Pictures {
    Picture *items;
    size_t count, cap;
    WoBuf regions; /* textures/hd-regions.bin */
} Pictures;

static char *base64(const uint8_t *data, size_t len) {
    static const char digits[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    char *out = malloc((len + 2) / 3 * 4 + 1);
    if (!out) {
        return NULL;
    }
    size_t o = 0;
    for (size_t i = 0; i < len; i += 3) {
        uint32_t n = (uint32_t)data[i] << 16;
        if (i + 1 < len) n |= (uint32_t)data[i + 1] << 8;
        if (i + 2 < len) n |= data[i + 2];
        out[o++] = digits[(n >> 18) & 63];
        out[o++] = digits[(n >> 12) & 63];
        out[o++] = i + 1 < len ? digits[(n >> 6) & 63] : '=';
        out[o++] = i + 2 < len ? digits[n & 63] : '=';
    }
    out[o] = 0;
    return out;
}

static char *png_data_uri(const char *path, WoError *error) {
    WoBuf bytes = {0};
    if (!wo_read_file(path, WO_PNG_MAX_FILE_BYTES, &bytes, error)) {
        return NULL;
    }
    char *encoded = base64(bytes.data, bytes.len);
    char *uri = encoded ? wo_format(error, "data:image/png;base64,%s", encoded) : NULL;
    if (!encoded) {
        wo_fail(error, WO_ERR_INTERNAL, "Out of memory.");
    }
    free(encoded);
    wo_buf_free(&bytes);
    return uri;
}

typedef struct Track {
    const char *folder; /* "TRACK13" */
    char sheet[16];     /* its scenery sheet, "nrace03.tga" */
    WoBuf wad_bytes;
    WoCmp frames;
    size_t ps1_frames; /* the PlayStation's own; later ones were added for boards */
    Scene scene;
} Track;

/* The authored PNG the HD index places exactly where frame `index` lies on
 * the track's sheet, written into name; false when there is none. */
static bool authored_over(const Pictures *pictures, const Track *track, int index,
                          const char *assets, char *name, size_t size) {
    uint32_t x = 0, y = 0, per_word = 1;
    WoImage image = {0};
    const uint8_t *tim = track->frames.data + track->frames.offsets[index];
    size_t len = track->frames.sizes[index];
    const WoBuf *r = &pictures->regions;
    if (r->len < 12 || memcmp(r->data, "WOHDREG1", 8) != 0 ||
        !wo_tim_origin(tim, len, &x, &y, &per_word, NULL) ||
        !wo_tim_decode_bgra(tim, len, &image, NULL)) {
        return false;
    }
    int64_t left = ((int64_t)x - 320) * per_word;
    uint32_t want[5] = {(uint32_t)(left / 256), (uint32_t)(left % 256), y, image.width, image.height};
    wo_image_free(&image);
    size_t count = wo_le32(r->data + 8);
    for (size_t i = 0; left >= 0 && i < count && 12 + (i + 1) * 312 <= r->len; i++) {
        const uint8_t *record = r->data + 12 + i * 312;
        bool same = !strncmp((const char *)record + 256, track->sheet, 32);
        for (int k = 0; same && k < 5; k++) {
            same = wo_le32(record + 288 + k * 4) == want[k];
        }
        if (!same) {
            continue;
        }
        char png[128];
        size_t n = 0;
        for (; n < sizeof(png) - 1 && record[n * 2]; n++) {
            png[n] = (char)record[n * 2];
        }
        png[n] = 0;
        char *relative = wo_format(NULL, "auth/%s", png);
        char *path = relative ? wo_path_join(assets, relative, NULL) : NULL;
        bool found = path && wo_is_file(path);
        wo_free(path);
        wo_free(relative);
        if (found) {
            snprintf(name, size, "%s", png);
            return true;
        }
    }
    return false;
}

/* The picture of frame `index` of a track, made once. */
static Picture *picture_get(Pictures *pictures, const Track *track, int index, const char *assets,
                            const char *output, WoError *error) {
    char key[48];
    snprintf(key, sizeof(key), "%s:%d", track->folder, index);
    for (size_t i = 0; i < pictures->count; i++) {
        if (!strcmp(pictures->items[i].key, key)) {
            return &pictures->items[i];
        }
    }
    if (index < 0 || (size_t)index >= track->frames.count) {
        return NULL;
    }
    WoImage image = {0};
    if (!wo_tim_decode_bgra(track->frames.data + track->frames.offsets[index],
                            track->frames.sizes[index], &image, NULL)) {
        return NULL;
    }
    if (pictures->count == pictures->cap) {
        size_t cap = pictures->cap ? pictures->cap * 2 : 128;
        Picture *grown = realloc(pictures->items, cap * sizeof(Picture));
        if (!grown) {
            wo_image_free(&image);
            wo_fail(error, WO_ERR_INTERNAL, "Out of memory.");
            return NULL;
        }
        pictures->items = grown;
        pictures->cap = cap;
    }
    Picture *picture = &pictures->items[pictures->count];
    memset(picture, 0, sizeof(*picture));
    snprintf(picture->key, sizeof(picture->key), "%s", key);
    picture->width = image.width;
    picture->height = image.height;
    char lower[16];
    snprintf(lower, sizeof(lower), "%s", track->folder);
    for (char *c = lower; *c; c++) {
        *c = (char)(*c >= 'A' && *c <= 'Z' ? *c + 32 : *c);
    }
    char *path = NULL;
    if ((size_t)index < track->ps1_frames) {
        snprintf(picture->name, sizeof(picture->name), "%s-scene-cmp-%d.png", lower, index);
        static const char *const folders[] = {"sd"};
        for (size_t i = 0; i < 1 && !path && assets; i++) {
            char *relative = wo_format(error, "%s/%s", folders[i], picture->name);
            char *candidate = relative ? wo_path_join(assets, relative, error) : NULL;
            if (candidate && wo_is_file(candidate)) {
                path = candidate;
                snprintf(picture->source, sizeof(picture->source), "%s", folders[i]);
            } else {
                wo_free(candidate);
            }
            wo_free(relative);
        }
    }
    if (!path) {
        char *file = wo_format(error, "tex/%s-%d.png", lower, index);
        path = file ? wo_path_join(output, file, error) : NULL;
        wo_free(file);
        char *folder = wo_path_join(output, "tex", error);
        bool ok = path && folder && wo_mkdirs(folder, error) &&
                  wo_png_write(path, &image, WO_PIXELS_BGRA, error);
        wo_free(folder);
        if (!ok) {
            wo_free(path);
            wo_image_free(&image);
            return NULL;
        }
        snprintf(picture->source, sizeof(picture->source), "archive");
    }
    wo_image_free(&image);
    picture->data_uri = png_data_uri(path, error);
    wo_free(path);
    if (!picture->data_uri) {
        return NULL;
    }
    /* Authored artwork in its place: by the picture's name, else by where it lies. */
    if (assets) {
        char *relative = picture->name[0] ? wo_format(error, "auth/%s", picture->name) : NULL;
        char *candidate = relative ? wo_path_join(assets, relative, error) : NULL;
        if (candidate && wo_is_file(candidate)) {
            snprintf(picture->auth_name, sizeof(picture->auth_name), "%s", picture->name);
        } else {
            authored_over(pictures, track, index, assets, picture->auth_name,
                          sizeof(picture->auth_name));
        }
        wo_free(candidate);
        wo_free(relative);
        if (picture->auth_name[0]) {
            char *auth_relative = wo_format(error, "auth/%s", picture->auth_name);
            char *auth_path = auth_relative ? wo_path_join(assets, auth_relative, error) : NULL;
            picture->auth_uri = auth_path ? png_data_uri(auth_path, error) : NULL;
            wo_free(auth_path);
            wo_free(auth_relative);
            if (!picture->auth_uri) {
                free(picture->data_uri);
                return NULL;
            }
        }
    }
    pictures->count++;
    return picture;
}

/* --- Boards ---------------------------------------------------------------------------- */

static double round_to(double value, double step) {
    return floor(value / step + 0.5) * step;
}

static WoJson *number_array(const double *values, int count) {
    WoJson *array = wo_json_new_array();
    for (int i = 0; array && i < count; i++) {
        wo_json_push(array, wo_json_new_number(round_to(values[i], 0.001)));
    }
    return array;
}

/* The board's picture rectangle in texels, and the faces of its object seen
 * face on, in board units: s along the picture's width, t down it, 0..1
 * over the board. */
static WoJson *board_record(const Track *track, const Object *object, int object_index,
                            const int *faces, int count, const Mapping *map, const Picture *picture,
                            Pictures *pictures, const char *assets, const char *output,
                            WoError *error) {
    int umin = 255, vmin = 255, umax = 0, vmax = 0;
    for (int f = 0; f < count; f++) {
        const Face *face = &object->faces[faces[f]];
        for (int c = 0; c < face->corners; c++) {
            umin = face->u[c] < umin ? face->u[c] : umin;
            umax = face->u[c] > umax ? face->u[c] : umax;
            vmin = face->v[c] < vmin ? face->v[c] : vmin;
            vmax = face->v[c] > vmax ? face->v[c] : vmax;
        }
    }
    /* The texels shown: the corners address the first and last texel. */
    double du = umax - umin + 1;
    double dv = vmax - vmin + 1;
    double width = length(map->eu) * du;
    double height = length(map->ev) * dv;
    Vec s_axis = scale(map->eu, 1 / length(map->eu));
    Vec t_axis = scale(map->ev, 1 / length(map->ev));
    Vec normal = cross(s_axis, t_axis);
    Vec corner = add(map->origin, add(scale(map->eu, umin), scale(map->ev, vmin)));

    WoJson *board = wo_json_new_object();
    wo_json_put_str(board, "track", track->folder);
    WoJson *instance = wo_json_new_object();
    wo_json_put_str(instance, "object", object->name);
    wo_json_put_int(instance, "objectIndex", object_index);
    WoJson *face_list = wo_json_new_array();
    for (int f = 0; f < count; f++) {
        wo_json_push(face_list, wo_json_new_int(faces[f]));
    }
    wo_json_put(instance, "faces", face_list);
    WoJson *instances = wo_json_new_array();
    wo_json_push(instances, instance);
    wo_json_put(board, "instances", instances);
    wo_json_put_str(board, "picture", picture->key);
    double rect[4] = {umin, vmin, du, dv};
    wo_json_put(board, "uv", number_array(rect, 4));
    double size[2] = {width, height};
    wo_json_put(board, "size", number_array(size, 2));
    double stretch = (width / du) / (height / dv);
    wo_json_put(board, "stretch", wo_json_new_number(round_to(stretch, 0.0001)));
    /* Which way is down on the board: the world's y grows downwards. */
    double down[2] = {s_axis.y, t_axis.y};
    wo_json_put(board, "down", number_array(down, 2));
    double skew = fabs(dot(s_axis, t_axis));
    wo_json_put(board, "skew", wo_json_new_number(round_to(skew, 0.001)));

    /* The rest of the object as seen from the board's front. */
    WoJson *backing = wo_json_new_array();
    for (int f = 0; f < object->face_count; f++) {
        const Face *face = &object->faces[f];
        bool own = false;
        for (int i = 0; i < count; i++) {
            own = own || faces[i] == f;
        }
        Vec p[MAX_CORNERS];
        double depth = 0;
        bool near = true;
        for (int c = 0; c < face->corners; c++) {
            p[c] = sub(vec(object->vertices[face->vertex[c]]), corner);
            double d = dot(p[c], normal);
            near = near && fabs(d) <= height * PLANE_SLACK;
            depth += d / face->corners;
        }
        Vec fn = cross(sub(p[1], p[0]), sub(p[2], p[0]));
        double fl = length(fn);
        if (!near || fl < 1e-9 || fabs(dot(fn, normal)) / fl < 0.5) {
            continue; /* away from the board, or seen edge on */
        }
        WoJson *shape = wo_json_new_object();
        double points[MAX_CORNERS * 2];
        for (int c = 0; c < face->corners; c++) {
            points[c * 2] = dot(p[c], s_axis) / width;
            points[c * 2 + 1] = dot(p[c], t_axis) / height;
        }
        wo_json_put(shape, "points", number_array(points, face->corners * 2));
        wo_json_put(shape, "depth", wo_json_new_number(round_to(depth / height, 0.001)));
        if (own) {
            wo_json_put_bool(shape, "board", true);
        }
        const Picture *seen = face->texture >= 0 && !own
                                  ? picture_get(pictures, track, face->texture, assets, output, error)
                                  : NULL;
        if (seen) {
            wo_json_put_str(shape, "picture", seen->key);
            double uv[MAX_CORNERS * 2];
            for (int c = 0; c < face->corners; c++) {
                uv[c * 2] = face->u[c];
                uv[c * 2 + 1] = face->v[c];
            }
            wo_json_put(shape, "uv", number_array(uv, face->corners * 2));
        }
        char colour[8];
        snprintf(colour, sizeof(colour), "#%02x%02x%02x", face->colour[0], face->colour[1],
                 face->colour[2]);
        wo_json_put_str(shape, "colour", colour);
        wo_json_push(backing, shape);
    }
    wo_json_put(board, "backing", backing);
    return board;
}

/* The boards among one object's faces drawn with one picture. */
static bool object_boards(const Track *track, const Object *object, int object_index, int texture,
                          Pictures *pictures, const char *assets, const char *output,
                          WoJson *boards, WoError *error) {
    int faces[64];
    int count = 0;
    for (int f = 0; f < object->face_count && count < 64; f++) {
        if (object->faces[f].texture == texture) {
            faces[count++] = f;
        }
    }
    if (!count || count >= 64) {
        return true;
    }
    Picture *picture = picture_get(pictures, track, texture, assets, output, error);
    if (!picture) {
        return !wo_error_is_set(error);
    }
    if (picture->width < MIN_PICTURE || picture->height < MIN_PICTURE) {
        return true;
    }
    /* All faces one board, else each face one (a picture on both sides of a
     * board), unless there are many: a tiled surface. */
    int groups[64][64];
    int sizes[64];
    int group_count = 0;
    Mapping all;
    if (fit_mapping(object, faces, count, &all) &&
        all.residual <= FIT_TOLERANCE * fmax(length(all.eu) * picture->width,
                                             length(all.ev) * picture->height)) {
        memcpy(groups[0], faces, (size_t)count * sizeof(int));
        sizes[0] = count;
        group_count = 1;
    } else if (count <= 2) {
        for (int i = 0; i < count; i++) {
            groups[i][0] = faces[i];
            sizes[i] = 1;
        }
        group_count = count;
    }
    for (int g = 0; g < group_count; g++) {
        Mapping map;
        if (!fit_mapping(object, groups[g], sizes[g], &map)) {
            continue;
        }
        int umin = 255, vmin = 255, umax = 0, vmax = 0;
        for (int f = 0; f < sizes[g]; f++) {
            const Face *face = &object->faces[groups[g][f]];
            for (int c = 0; c < face->corners; c++) {
                umin = face->u[c] < umin ? face->u[c] : umin;
                umax = face->u[c] > umax ? face->u[c] : umax;
                vmin = face->v[c] < vmin ? face->v[c] : vmin;
                vmax = face->v[c] > vmax ? face->v[c] : vmax;
            }
        }
        if (umax - umin + 1 < COVER_MIN * picture->width ||
            vmax - vmin + 1 < COVER_MIN * picture->height) {
            continue;
        }
        WoJson *board = board_record(track, object, object_index, groups[g], sizes[g], &map,
                                     picture, pictures, assets, output, error);
        if (!board || wo_error_is_set(error)) {
            wo_json_free(board);
            return false;
        }
        wo_json_push(boards, board);
    }
    return true;
}

/* A sprite as a board: the whole picture over its width and height, upright. */
static bool sprite_board(const Track *track, const Object *object, int object_index, int index,
                         Pictures *pictures, const char *assets, const char *output,
                         WoJson *boards, WoError *error) {
    const Sprite *sprite = &object->sprites[index];
    Picture *picture = picture_get(pictures, track, sprite->texture, assets, output, error);
    if (!picture) {
        return !wo_error_is_set(error);
    }
    if (picture->width < MIN_PICTURE || picture->height < MIN_PICTURE || sprite->width <= 0 ||
        sprite->height <= 0) {
        return true;
    }
    WoJson *board = wo_json_new_object();
    wo_json_put_str(board, "track", track->folder);
    wo_json_put_bool(board, "sprite", true);
    WoJson *instance = wo_json_new_object();
    wo_json_put_str(instance, "object", object->name);
    wo_json_put_int(instance, "objectIndex", object_index);
    WoJson *sprite_list = wo_json_new_array();
    wo_json_push(sprite_list, wo_json_new_int(index));
    wo_json_put(instance, "sprites", sprite_list);
    wo_json_put(instance, "faces", wo_json_new_array());
    WoJson *instances = wo_json_new_array();
    wo_json_push(instances, instance);
    wo_json_put(board, "instances", instances);
    wo_json_put_str(board, "picture", picture->key);
    double rect[4] = {0, 0, picture->width, picture->height};
    wo_json_put(board, "uv", number_array(rect, 4));
    double size[2] = {sprite->width, sprite->height};
    wo_json_put(board, "size", number_array(size, 2));
    double stretch = ((double)sprite->width / picture->width) / ((double)sprite->height / picture->height);
    wo_json_put(board, "stretch", wo_json_new_number(round_to(stretch, 0.0001)));
    wo_json_put(board, "skew", wo_json_new_number(0));
    double down[2] = {0, 1};
    wo_json_put(board, "down", number_array(down, 2));
    WoJson *backing = wo_json_new_array();
    WoJson *shape = wo_json_new_object();
    double points[8] = {0, 0, 1, 0, 0, 1, 1, 1};
    wo_json_put(shape, "points", number_array(points, 8));
    wo_json_put(shape, "depth", wo_json_new_number(0));
    wo_json_put_bool(shape, "board", true);
    wo_json_put_str(shape, "colour", "#000000");
    wo_json_push(backing, shape);
    wo_json_put(board, "backing", backing);
    wo_json_push(boards, board);
    return true;
}

/* Boards of one track with the same picture, part of it and size are one
 * entry with several instances: `found` (one track's) are added to `boards`. */
static bool same_board(const WoJson *a, const WoJson *b) {
    if (strcmp(wo_json_str(a, "picture", ""), wo_json_str(b, "picture", ""))) {
        return false;
    }
    for (size_t k = 0; k < 4; k++) {
        if (wo_json_as_number(wo_json_at(wo_json_get(a, "uv"), k), -1) !=
            wo_json_as_number(wo_json_at(wo_json_get(b, "uv"), k), -2)) {
            return false;
        }
    }
    for (size_t k = 0; k < 2; k++) {
        double x = wo_json_as_number(wo_json_at(wo_json_get(a, "size"), k), 0);
        double y = wo_json_as_number(wo_json_at(wo_json_get(b, "size"), k), 0);
        if (fabs(x - y) > 0.02 * fabs(x)) {
            return false;
        }
    }
    return true;
}

static void merge_into(WoJson *boards, const WoJson *found) {
    size_t first = wo_json_length(boards);
    for (size_t i = 0; i < wo_json_length(found); i++) {
        const WoJson *board = wo_json_at(found, i);
        WoJson *into = NULL;
        for (size_t j = first; !into && j < wo_json_length(boards); j++) {
            if (same_board(wo_json_at(boards, j), board)) {
                into = wo_json_at(boards, j);
            }
        }
        if (into) {
            wo_json_push(wo_json_get(into, "instances"),
                         wo_json_clone(wo_json_at(wo_json_get(board, "instances"), 0)));
        } else {
            wo_json_push(boards, wo_json_clone(board));
        }
    }
}

/* The track archive as Setup and Play make it before its boards are laid
 * out (wo_billboards.h), so that the page shows the boards as they were made
 * and its face numbers are the ones the layout manifest refers to; else the
 * installed archive. */
static bool archive_unlaid(const char *game, const WoJson *plan, const char *folder,
                           const char *path, WoBuf *out, WoError *error) {
    const WoJson *scenery = wo_json_get(plan, "scenery");
    for (size_t i = 0; i < wo_json_length(scenery); i++) {
        const WoJson *entry = wo_json_at(scenery, i);
        if (wo_str_ieq(wo_json_str(entry, "track", ""), folder)) {
            return wo_natives_scenery_unlaid(game, entry, out, error);
        }
    }
    return wo_is_file(path) && wo_read_file(path, 64u << 20, out, error);
}

static bool track_load(const char *game, const WoJson *plan, const char *folder, Track *track,
                       WoError *error) {
    memset(track, 0, sizeof(*track));
    track->folder = folder;
    char *relative = wo_format(error, "%s/TRACK.WAD", folder);
    char *path = relative ? wo_path_join(game, relative, error) : NULL;
    char *ps1_relative = relative ? wo_format(error, "textures/ps1-source/%s", relative) : NULL;
    char *ps1_path = ps1_relative ? wo_path_join(game, ps1_relative, error) : NULL;
    WoWad wad = {0};
    bool ok = path && wo_is_file(path) &&
              archive_unlaid(game, plan, folder, path, &track->wad_bytes, error) &&
              wo_wad_parse(track->wad_bytes.data, track->wad_bytes.len, &wad, error);
    const WoWadEntry *models = ok ? wo_wad_find(&wad, "scene.prm") : NULL;
    const WoWadEntry *textures = ok ? wo_wad_find(&wad, "scene.cmp") : NULL;
    ok = ok && models && textures &&
         wo_cmp_decode(track->wad_bytes.data + textures->offset, textures->size, &track->frames,
                       error) &&
         scene_read(track->wad_bytes.data + models->offset, models->size, &track->scene);
    track->ps1_frames = track->frames.count;
    if (ok && ps1_path && wo_is_file(ps1_path)) {
        /* Frames beyond the PlayStation archive's own were added for boards. */
        WoBuf ps1 = {0};
        WoWad ps1_wad = {0};
        WoCmp ps1_frames = {0};
        if (wo_read_file(ps1_path, 64u << 20, &ps1, NULL) &&
            wo_wad_parse(ps1.data, ps1.len, &ps1_wad, NULL)) {
            const WoWadEntry *entry = wo_wad_find(&ps1_wad, "scene.cmp");
            if (entry && wo_cmp_decode(ps1.data + entry->offset, entry->size, &ps1_frames, NULL)) {
                track->ps1_frames = ps1_frames.count;
            }
        }
        wo_cmp_free(&ps1_frames);
        wo_wad_free(&ps1_wad);
        wo_buf_free(&ps1);
    }
    wo_wad_free(&wad);
    wo_free(ps1_path);
    wo_free(ps1_relative);
    wo_free(path);
    wo_free(relative);
    return ok;
}

/* Where the authored artwork of `assets`/auth goes, worked out now as Play
 * would, so a picture added there shows on the page before the game has run
 * again. When that cannot be done (a PNG the HD catalogue does not know, say,
 * which Play would refuse too), the installed index is used, and why is
 * printed. */
static void authored_places(const char *game, const char *assets, WoBuf *out) {
    WoError error = {0};
    WoJson *catalog = NULL;
    WoJson *plan = NULL;
    WoMod mod;
    bool have_mod = wo_mod_init(&mod, ".", &error);
    /* The plan as Play has it, the boards' layout included: the index finds
     * the installed scenery by it. */
    char *auth = assets && have_mod ? wo_path_join(assets, "auth", &error) : NULL;
    bool ok = auth && wo_load_plan(&mod, &plan, &error) &&
              wo_read_json("manifests/hd-textures.json", 16u << 20, &catalog, &error);
    if (ok) {
        WoHdOptions options = {catalog, auth, auth, game, NULL, plan, NULL};
        WoHdRegionIndex index;
        WoHdResult result;
        ok = wo_hd_build_index(&options, &index, &result, &error);
        if (ok) {
            ok = wo_hd_regions_serialize(&index, out, &error);
            wo_hd_regions_free(&index);
            wo_hd_result_free(&result);
        }
    }
    wo_json_free(catalog);
    wo_json_free(plan);
    wo_free(auth);
    if (have_mod) {
        wo_mod_free(&mod);
    }
    if (ok) {
        return;
    }
    if (assets) {
        printf("Authored artwork placed as the game last placed it: %s\n", error.message);
    }
    wo_buf_free(out);
    char *regions = wo_path_join(game, "textures/hd-regions.bin", NULL);
    if (regions && wo_is_file(regions)) {
        wo_read_file(regions, 16u << 20, out, NULL); /* unreadable: no authored art */
    }
    wo_free(regions);
}

static void track_free(Track *track) {
    scene_free(&track->scene);
    wo_cmp_free(&track->frames);
    wo_buf_free(&track->wad_bytes);
}

bool build_billboards(const char *game, const char *output, const char *assets, WoError *error) {
    if (!wo_mkdirs(output, error)) {
        return false;
    }
    WoJson *boards = wo_json_new_array();
    WoJson *tracks = wo_json_new_object();
    Pictures pictures = {0};
    WoJson *plan = NULL;
    bool ok = boards && tracks &&
              wo_read_json("manifests/texture-selection.json", 32u << 20, &plan, error);
    if (ok) {
        authored_places(game, assets, &pictures.regions);
    }
    for (size_t t = 0; ok && t < WO_COUNT(TRACK_NAMES); t++) {
        Track track;
        if (!track_load(game, plan, TRACK_NAMES[t][0], &track, error)) {
            track_free(&track);
            if (wo_error_is_set(error)) {
                ok = false;
            }
            continue;
        }
        wo_json_put_str(tracks, TRACK_NAMES[t][0], TRACK_NAMES[t][1]);
        snprintf(track.sheet, sizeof(track.sheet), "nrace%02u.tga", (unsigned)(t + 1));
        WoJson *found = wo_json_new_array();
        ok = found != NULL;
        for (int o = 0; ok && o < track.scene.count; o++) {
            const Object *object = &track.scene.objects[o];
            for (int i = 0; ok && i < object->sprite_count; i++) {
                ok = sprite_board(&track, object, o, i, &pictures, assets, output, found, error);
            }
            int seen[512];
            int seen_count = 0;
            for (int f = 0; ok && f < object->face_count; f++) {
                int texture = object->faces[f].texture;
                bool done = texture < 0;
                for (int i = 0; !done && i < seen_count; i++) {
                    done = seen[i] == texture;
                }
                if (done || seen_count == 512) {
                    continue;
                }
                seen[seen_count++] = texture;
                ok = object_boards(&track, object, o, texture, &pictures, assets, output, found,
                                   error);
            }
        }
        merge_into(boards, found);
        wo_json_free(found);
        printf("%s: %zu boards so far\n", TRACK_NAMES[t][1], wo_json_length(boards));
        track_free(&track);
    }
    WoJson *picture_map = wo_json_new_object();
    for (size_t i = 0; ok && i < pictures.count; i++) {
        const Picture *p = &pictures.items[i];
        WoJson *record = wo_json_new_object();
        wo_json_put_str(record, "name", p->name);
        wo_json_put_str(record, "source", p->source);
        wo_json_put_int(record, "width", p->width);
        wo_json_put_int(record, "height", p->height);
        wo_json_put_str(record, "src", p->data_uri);
        if (p->auth_uri) {
            wo_json_put_str(record, "authName", p->auth_name);
            wo_json_put_str(record, "authSrc", p->auth_uri);
        }
        wo_json_put(picture_map, p->key, record);
    }
    WoJson *root = wo_json_new_object();
    wo_json_put_int(root, "format", 1);
    char made[32];
    time_t now = time(NULL);
    strftime(made, sizeof(made), "%Y-%m-%d %H:%M", localtime(&now));
    wo_json_put_str(root, "made", made);
    wo_json_put(root, "tracks", tracks);
    wo_json_put(root, "pictures", picture_map);
    wo_json_put(root, "boards", boards);
    WoBuf text = {0};
    ok = ok && wo_buf_append_str(&text, "window.BILLBOARDS = ", error) &&
         wo_json_write(root, false, &text, error) && wo_buf_append_str(&text, ";\n", error);
    char *data_path = ok ? wo_path_join(output, "data.js", error) : NULL;
    ok = ok && data_path && wo_write_file(data_path, text.data, text.len, error);
    char *page = ok ? wo_path_join(output, "index.html", error) : NULL;
    ok = ok && page && wo_copy_file("tools/billboards/index.html", page, error);
    if (ok) {
        printf("%zu boards, %zu pictures: %s\n", wo_json_length(boards), pictures.count, page);
    }
    wo_free(page);
    wo_free(data_path);
    wo_buf_free(&text);
    wo_json_free(root);
    wo_json_free(plan);
    for (size_t i = 0; i < pictures.count; i++) {
        free(pictures.items[i].data_uri);
        wo_free(pictures.items[i].auth_uri);
    }
    free(pictures.items);
    wo_buf_free(&pictures.regions);
    return ok;
}

/* --- The layout manifest --------------------------------------------------------------- */

/* A choice of the page as numbers; false when the board keeps its picture as it is. */
static bool layout_entry(const WoJson *record, const WoJson *instance, WoJson **out) {
    const char *mode = wo_json_str(record, "mode", "keep");
    if (strcmp(mode, "fit") && strcmp(mode, "stretch") && strcmp(mode, "tile")) {
        return false;
    }
    /* The part of the picture laid out: chosen by hand, else without its
     * see-through margins, else what the board shows. */
    const WoJson *source = wo_json_get(record, "area");
    if (!source && wo_json_bool(record, "trim", false)) {
        source = wo_json_get(record, "visibleUv");
    }
    if (!source) {
        source = wo_json_get(record, "uv");
    }
    if (wo_json_length(source) != 4) {
        return false;
    }
    WoJson *entry = wo_json_new_object();
    wo_json_put_int(entry, "object", wo_json_int(instance, "objectIndex", -1));
    wo_json_put(entry, "faces", wo_json_clone(wo_json_get(instance, "faces")));
    if (wo_json_get(instance, "sprites")) {
        wo_json_put(entry, "sprites", wo_json_clone(wo_json_get(instance, "sprites")));
    }
    wo_json_put_str(entry, "mode", mode);
    wo_json_put(entry, "source", wo_json_clone(source));
    int64_t stretch = !strcmp(mode, "fit") ? 100 : wo_json_int(record, "stretchPercent", 100);
    wo_json_put_int(entry, "stretch", stretch);
    wo_json_put_int(entry, "scale", !strcmp(mode, "fit") ? 100 : wo_json_int(record, "scalePercent", 100));
    wo_json_put_int(entry, "tiles", wo_json_int(record, "tiles", 0));
    const char *bars = wo_json_str(record, "bars", "transparent");
    if (bars[0] == '#' && strlen(bars) == 7) {
        wo_json_put_str(entry, "bars", bars);
    }
    *out = entry;
    return true;
}

bool build_billboard_layout(const char *export_path, const char *output, WoError *error) {
    WoJson *exported = NULL;
    if (!wo_read_json(export_path, 64u << 20, &exported, error)) {
        return false;
    }
    const WoJson *records = wo_json_get(exported, "boards");
    WoJson *tracks = wo_json_new_object();
    size_t boards = 0, from_auth = 0;
    for (size_t i = 0; i < wo_json_length(records); i++) {
        const WoJson *record = wo_json_at(records, i);
        if (!strcmp(wo_json_str(record, "source", ""), "auth")) {
            continue;
        }
        /* With HD artwork on, players see the authored picture: its choice
         * comes first where one was made. */
        const char *key = wo_json_str(record, "key", "");
        const WoJson *chosen = record;
        for (size_t j = 0; j < wo_json_length(records); j++) {
            const WoJson *other = wo_json_at(records, j);
            const char *other_key = wo_json_str(other, "key", "");
            const char *mode = wo_json_str(other, "mode", "keep");
            if (!strncmp(other_key, "auth:", 5) && !strcmp(other_key + 5, key) &&
                strcmp(mode, "keep") && strcmp(mode, "ignore")) {
                chosen = other;
                from_auth++;
            }
        }
        const char *track = wo_json_str(record, "track", "");
        const WoJson *instances = wo_json_get(record, "instances");
        WoJson *entry = NULL;
        if (!layout_entry(chosen, wo_json_at(instances, 0), &entry)) {
            continue;
        }
        wo_json_free(entry);
        WoJson *list = wo_json_get(tracks, track);
        if (!list) {
            wo_json_put(tracks, track, wo_json_new_array());
            list = wo_json_get(tracks, track);
        }
        for (size_t k = 0; k < wo_json_length(instances); k++) {
            if (layout_entry(chosen, wo_json_at(instances, k), &entry)) {
                wo_json_push(list, entry);
            }
        }
        boards++;
    }
    /* Boards the game's own code fills are not on the page: their entries
     * ("routine") are kept from the manifest as it was. */
    WoJson *previous = NULL;
    if (wo_is_file(output) && wo_read_json(output, 16u << 20, &previous, NULL)) {
        const WoJson *old_tracks = wo_json_get(previous, "tracks");
        for (size_t t = 0; t < WO_COUNT(TRACK_NAMES); t++) {
            const WoJson *old = wo_json_get(old_tracks, TRACK_NAMES[t][0]);
            for (size_t i = 0; i < wo_json_length(old); i++) {
                if (!wo_json_bool(wo_json_at(old, i), "routine", false)) {
                    continue;
                }
                WoJson *list = wo_json_get(tracks, TRACK_NAMES[t][0]);
                if (!list) {
                    wo_json_put(tracks, TRACK_NAMES[t][0], wo_json_new_array());
                    list = wo_json_get(tracks, TRACK_NAMES[t][0]);
                }
                wo_json_push(list, wo_json_clone(wo_json_at(old, i)));
            }
        }
        wo_json_free(previous);
    }
    WoJson *root = wo_json_new_object();
    wo_json_put_str(root, "format", "wo4k.billboard-layout");
    wo_json_put_int(root, "version", 1);
    wo_json_put_str(root, "note",
                    "Made by wo4k-build billboard-layout from the billboard page's export. Per "
                    "track: the faces (or sprites) of an object of the installed scenery that "
                    "make one board, and how its picture fills it.");
    wo_json_put(root, "tracks", tracks);
    WoBuf text = {0};
    bool ok = wo_json_write(root, true, &text, error) && wo_buf_append_str(&text, "\n", error) &&
              wo_write_file_atomic(output, text.data, text.len, error);
    if (ok) {
        printf("%zu boards laid out (%zu by their authored picture's choice): %s\n", boards,
               from_auth, output);
    }
    wo_buf_free(&text);
    wo_json_free(root);
    wo_json_free(exported);
    return ok;
}
