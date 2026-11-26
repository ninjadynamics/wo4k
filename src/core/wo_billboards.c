#include "core/wo_billboards.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "core/wo_texfmt.h"

#define PRM_HEADER 144u
#define QUAD 4
#define KIND_TEXTURED_QUAD 4  /* flat */
#define KIND_SHADED_TEXTURED_QUAD 8
#define KIND_SHADED_QUAD 7
#define KIND_SPRITE 11
#define TEXTURE_AT 8u /* in a textured quad's body: after its four corners */
#define MAX_PIECES 64

static unsigned be16(const uint8_t *p) {
    return ((unsigned)p[0] << 8) | p[1];
}

static void put_be16(uint8_t *p, unsigned v) {
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

/* Bytes after a primitive's four-byte head and its corners (0: a sprite). */
static bool primitive_shape(unsigned kind, size_t *body, int *corners) {
    static const uint8_t count[9] = {0, 3, 3, 4, 4, 3, 3, 4, 4};
    static const uint8_t tail[9] = {0, 6, 6, 4, 6, 14, 14, 16, 18};
    *corners = 0;
    if (kind == KIND_SPRITE) {
        *body = 12;
        return true;
    }
    if (kind < 1 || kind > 8) {
        return false;
    }
    *corners = count[kind];
    *body = (size_t)count[kind] * 2u + tail[kind];
    if (!(kind & 1u)) {
        *body += 6u + (size_t)count[kind] * 2u;
    }
    return true;
}

/* --- One object ------------------------------------------------------------------------ */

typedef struct Primitive {
    size_t at;   /* into the model file */
    size_t size; /* head and body */
    unsigned kind;
    int corners;
    bool removed;
} Primitive;

typedef struct Object {
    size_t at, size;
    size_t vertices, normals;
    Primitive *primitives;
    size_t primitive_count;
    int *faces;   /* the primitives with corners, in order: what the manifest calls faces */
    size_t face_count;
    int *sprites; /* the sprites, in order */
    size_t sprite_count;
} Object;

static void object_free(Object *object) {
    free(object->primitives);
    free(object->faces);
    free(object->sprites);
    memset(object, 0, sizeof(*object));
}

/* Read the object at `at`; false when it is not one this code knows. */
static bool object_read(const uint8_t *data, size_t len, size_t at, Object *out) {
    memset(out, 0, sizeof(*out));
    if (at > len || len - at < PRM_HEADER) {
        return false;
    }
    out->at = at;
    out->vertices = be16(data + at + 16);
    out->normals = be16(data + at + 24);
    size_t count = be16(data + at + 32);
    size_t p = at + PRM_HEADER + (out->vertices + out->normals) * 8u;
    out->primitives = calloc(count ? count : 1, sizeof(Primitive));
    out->faces = calloc(count ? count : 1, sizeof(int));
    out->sprites = calloc(count ? count : 1, sizeof(int));
    if (!out->primitives || !out->faces || !out->sprites || p > len) {
        return false;
    }
    for (size_t i = 0; i < count; i++) {
        if (len - p < 4) {
            return false;
        }
        unsigned kind = be16(data + p);
        size_t body = 0;
        int corners = 0;
        if (!primitive_shape(kind, &body, &corners) || len - p - 4 < body) {
            return false;
        }
        out->primitives[i] = (Primitive){p, 4 + body, kind, corners, false};
        if (corners) {
            out->faces[out->face_count++] = (int)i;
        } else {
            out->sprites[out->sprite_count++] = (int)i;
        }
        p += 4 + body;
    }
    out->primitive_count = count;
    out->size = p - at;
    return true;
}

/* --- Geometry -------------------------------------------------------------------------- */

typedef struct Vec {
    double x, y, z;
} Vec;

static Vec add(Vec a, Vec b) {
    return (Vec){a.x + b.x, a.y + b.y, a.z + b.z};
}
static Vec scale(Vec a, double s) {
    return (Vec){a.x * s, a.y * s, a.z * s};
}
static double length(Vec a) {
    return sqrt(a.x * a.x + a.y * a.y + a.z * a.z);
}

static Vec vertex(const uint8_t *data, const Object *object, unsigned index) {
    const uint8_t *v = data + object->at + PRM_HEADER + (size_t)index * 8u;
    return (Vec){(int16_t)be16(v), (int16_t)be16(v + 2), (int16_t)be16(v + 4)};
}

/* world = origin + u * eu + v * ev, fitted to the faces' corners by least squares. */
typedef struct Mapping {
    Vec origin, eu, ev;
} Mapping;

static bool fit_mapping(const uint8_t *data, const Object *object, const int *prims, size_t count,
                        Mapping *out) {
    double s[3][3] = {{0}};
    Vec r[3] = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}};
    for (size_t f = 0; f < count; f++) {
        const Primitive *pr = &object->primitives[prims[f]];
        const uint8_t *b = data + pr->at + 4;
        const uint8_t *uv = b + (size_t)pr->corners * 2u + 6u;
        for (int c = 0; c < pr->corners; c++) {
            double a[3] = {1, uv[c * 2], uv[c * 2 + 1]};
            Vec p = vertex(data, object, be16(b + c * 2));
            for (int i = 0; i < 3; i++) {
                for (int j = 0; j < 3; j++) {
                    s[i][j] += a[i] * a[j];
                }
                r[i] = add(r[i], scale(p, a[i]));
            }
        }
    }
    double det = s[0][0] * (s[1][1] * s[2][2] - s[1][2] * s[2][1]) -
                 s[0][1] * (s[1][0] * s[2][2] - s[1][2] * s[2][0]) +
                 s[0][2] * (s[1][0] * s[2][1] - s[1][1] * s[2][0]);
    if (fabs(det) < 1e-9) {
        return false;
    }
    double inv[3][3] = {
        {(s[1][1] * s[2][2] - s[1][2] * s[2][1]) / det, (s[0][2] * s[2][1] - s[0][1] * s[2][2]) / det,
         (s[0][1] * s[1][2] - s[0][2] * s[1][1]) / det},
        {(s[1][2] * s[2][0] - s[1][0] * s[2][2]) / det, (s[0][0] * s[2][2] - s[0][2] * s[2][0]) / det,
         (s[0][2] * s[1][0] - s[0][0] * s[1][2]) / det},
        {(s[1][0] * s[2][1] - s[1][1] * s[2][0]) / det, (s[0][1] * s[2][0] - s[0][0] * s[2][1]) / det,
         (s[0][0] * s[1][1] - s[0][1] * s[1][0]) / det}};
    Vec k[3];
    for (int i = 0; i < 3; i++) {
        k[i] = add(add(scale(r[0], inv[i][0]), scale(r[1], inv[i][1])), scale(r[2], inv[i][2]));
    }
    *out = (Mapping){k[0], k[1], k[2]};
    return length(out->eu) > 1e-6 && length(out->ev) > 1e-6;
}

/* --- Layout ---------------------------------------------------------------------------- */

/* A rectangle of the board in board units (the board is `aspect` wide and 1
 * high, x across the picture, y down it), showing part of the picture (in
 * texels) or, when not textured, the bars colour. */
typedef struct Piece {
    double x0, x1, y0, y1;
    bool textured;
    double u0, u1, v0, v1;
} Piece;

typedef struct Layout {
    Piece pieces[MAX_PIECES];
    int count;
} Layout;

static void piece(Layout *l, double x0, double x1, double y0, double y1, bool textured, double u0,
                  double u1, double v0, double v1) {
    if (x1 - x0 < 1e-4 || y1 - y0 < 1e-4 || l->count == MAX_PIECES) {
        return;
    }
    l->pieces[l->count++] = (Piece){x0, x1, y0, y1, textured, u0, u1, v0, v1};
}

/* A copy of the picture's part over [x0, x1) of the board, cut to the board. */
static void picture_piece(Layout *l, double aspect, double x0, double x1, double y0, double y1,
                          const double *source) {
    double a = x0 < 0 ? 0 : x0;
    double b = x1 > aspect ? aspect : x1;
    if (b <= a) {
        return;
    }
    double w = x1 - x0;
    piece(l, a, b, y0, y1, true, source[0] + (a - x0) / w * source[2],
          source[0] + (b - x0) / w * source[2], source[1], source[1] + source[3]);
}

/* The bars: above and below the picture across the board, and beside it
 * between x0 and x1 (each cut to the board). */
static void bars(Layout *l, double aspect, double x0, double x1, double y0, double y1) {
    piece(l, 0, aspect, 0, y0, false, 0, 0, 0, 0);
    piece(l, 0, aspect, y1, 1, false, 0, 0, 0, 0);
    if (x0 > 0) {
        piece(l, 0, x0 < aspect ? x0 : aspect, y0, y1, false, 0, 0, 0, 0);
    }
    if (x1 < aspect) {
        piece(l, x1 > 0 ? x1 : 0, aspect, y0, y1, false, 0, 0, 0, 0);
    }
}

/* The bars cut at every edge of a picture piece, so that no corner of one
 * face lies partway along another's edge: the renderer leaves a crack a
 * pixel wide there at some angles. The picture pieces are never cut. */
static void split_bars(Layout *l) {
    double xs[2 * MAX_PIECES], ys[2 * MAX_PIECES];
    int nx = 0, ny = 0;
    for (int i = 0; i < l->count; i++) {
        if (l->pieces[i].textured) {
            xs[nx++] = l->pieces[i].x0;
            xs[nx++] = l->pieces[i].x1;
            ys[ny++] = l->pieces[i].y0;
            ys[ny++] = l->pieces[i].y1;
        }
    }
    Layout out;
    memset(&out, 0, sizeof(out));
    for (int i = 0; i < l->count; i++) {
        Piece p = l->pieces[i];
        if (p.textured) {
            piece(&out, p.x0, p.x1, p.y0, p.y1, true, p.u0, p.u1, p.v0, p.v1);
            continue;
        }
        /* The cuts inside the bar, in order, across and down. */
        double cx[2 * MAX_PIECES + 2], cy[2 * MAX_PIECES + 2];
        int ncx = 0, ncy = 0;
        cx[ncx++] = p.x0;
        cy[ncy++] = p.y0;
        for (int k = 0; k < nx; k++) {
            if (xs[k] > p.x0 + 1e-4 && xs[k] < p.x1 - 1e-4) {
                cx[ncx++] = xs[k];
            }
        }
        for (int k = 0; k < ny; k++) {
            if (ys[k] > p.y0 + 1e-4 && ys[k] < p.y1 - 1e-4) {
                cy[ncy++] = ys[k];
            }
        }
        cx[ncx++] = p.x1;
        cy[ncy++] = p.y1;
        for (int a = 1; a < ncx; a++) {
            for (int b = a; b > 0 && cx[b] < cx[b - 1]; b--) {
                double t = cx[b]; cx[b] = cx[b - 1]; cx[b - 1] = t;
            }
        }
        for (int a = 1; a < ncy; a++) {
            for (int b = a; b > 0 && cy[b] < cy[b - 1]; b--) {
                double t = cy[b]; cy[b] = cy[b - 1]; cy[b - 1] = t;
            }
        }
        for (int a = 0; a + 1 < ncy; a++) {
            for (int b = 0; b + 1 < ncx; b++) {
                piece(&out, cx[b], cx[b + 1], cy[a], cy[a + 1], false, 0, 0, 0, 0);
            }
        }
    }
    *l = out;
}

static void lay_out(Layout *l, const char *mode, double aspect, const double *source,
                    double stretch, double height, int tiles) {
    memset(l, 0, sizeof(*l));
    double y0 = (1 - height) / 2, y1 = y0 + height;
    double w = height * source[2] / source[3] * stretch;
    if (!strcmp(mode, "tile")) {
        if (tiles > 0) {
            double x0 = (aspect - tiles * w) / 2;
            for (int n = 0; n < tiles; n++) {
                picture_piece(l, aspect, x0 + n * w, x0 + (n + 1) * w, y0, y1, source);
            }
            bars(l, aspect, x0, x0 + tiles * w, y0, y1);
            split_bars(l);
        } else {
            double x0 = (aspect - w) / 2;
            int reach = (int)ceil(x0 / w) + 1;
            for (int n = -reach; n <= reach && l->count < MAX_PIECES - 2; n++) {
                picture_piece(l, aspect, x0 + n * w, x0 + (n + 1) * w, y0, y1, source);
            }
            bars(l, aspect, 0, aspect, y0, y1);
            split_bars(l);
        }
        return;
    }
    double x0 = (aspect - w) / 2;
    picture_piece(l, aspect, x0, x0 + w, y0, y1, source);
    bars(l, aspect, x0, x0 + w, y0, y1);
    split_bars(l);
}

/* --- Building --------------------------------------------------------------------------- */

typedef struct Built {
    WoBuf vertices;   /* new vertices, 8 bytes each */
    WoBuf primitives; /* new primitives */
    size_t vertex_count, primitive_count;
} Built;

/* The corner at p: one the object (`existing`, `first` corners) or an
 * earlier new face already has there, else a new one. Faces that meet share
 * their corners, so the renderer places them alike. */
static bool add_vertex(Built *built, Vec p, const uint8_t *existing, size_t first,
                       unsigned *index, WoError *error) {
    uint8_t v[8] = {0};
    double c[3] = {p.x, p.y, p.z};
    for (int i = 0; i < 3; i++) {
        double r = floor(c[i] + 0.5);
        int value = r < -32768 ? -32768 : r > 32767 ? 32767 : (int)r;
        put_be16(v + i * 2, (unsigned)(uint16_t)(int16_t)value);
    }
    for (size_t i = 0; existing && i < first; i++) {
        if (!memcmp(existing + i * 8u, v, 6)) {
            *index = (unsigned)i;
            return true;
        }
    }
    for (size_t i = 0; i < built->vertex_count; i++) {
        if (!memcmp(built->vertices.data + i * 8u, v, 6)) {
            *index = (unsigned)(first + i);
            return true;
        }
    }
    if (first + built->vertex_count > 0xffff) {
        return wo_fail(error, WO_ERR_FORMAT, "Too many corners for one object.");
    }
    *index = (unsigned)(first + built->vertex_count++);
    return wo_buf_append(&built->vertices, v, sizeof(v), error);
}

static int texel(double t) {
    int v = (int)floor(t + 0.5);
    return v < 0 ? 0 : v > 255 ? 255 : v;
}

typedef struct Board {
    const uint8_t *data;
    const Object *object;
    const WoImage *picture; /* the board's texture, BGRA; NULL when it cannot be read */
    int bar_u, bar_v;       /* a texel of the bars colour in it, or -1 */
    const Primitive *model; /* the board's own textured quad: what new faces copy */
    Mapping map;
    double umin, umax, vmin, vmax, aspect;
    bool reversed; /* the model's corners go round the other way */
} Board;

/* The board's point (x, y) in board units, in the object's coordinates. */
static Vec board_point(const Board *b, double x, double y) {
    double u = b->umin + x / b->aspect * (b->umax - b->umin);
    double v = b->vmin + y * (b->vmax - b->vmin);
    return add(b->map.origin, add(scale(b->map.eu, u), scale(b->map.ev, v)));
}

/* A texel of the picture in the bars colour with its neighbours alike (the
 * picture is filtered when drawn), in the part of it the board uses; false
 * when there is none near enough. A bar drawn with it is lit as the picture
 * is, whatever the game does with a face's colours. */
#define BAR_MATCH 30 /* the most the colour of a texel and its neighbours may differ, summed */
static bool find_bar_texel(Board *b, const double *source, const uint8_t bar[3]) {
    b->bar_u = b->bar_v = -1;
    const WoImage *img = b->picture;
    if (!img) {
        return false;
    }
    int x0 = (int)fmax(1, floor(fmin(source[0], b->umin))), y0 = (int)fmax(1, floor(fmin(source[1], b->vmin)));
    int x1 = (int)fmin((double)img->width - 2, ceil(fmax(source[0] + source[2] - 1, b->umax)));
    int y1 = (int)fmin((double)img->height - 2, ceil(fmax(source[1] + source[3] - 1, b->vmax)));
    /* Of the texels alike enough, the one farthest from the picture's edges:
     * near one, the filtering reads the art beside the picture too. */
    int best = BAR_MATCH + 1, best_room = -1;
    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            int room = x - x0;
            room = x1 - x < room ? x1 - x : room;
            room = y - y0 < room ? y - y0 : room;
            room = y1 - y < room ? y1 - y : room;
            room = room > 8 ? 8 : room;
            int worst = 0;
            for (int dy = -1; dy <= 1 && worst < best; dy++) {
                for (int dx = -1; dx <= 1; dx++) {
                    const uint8_t *px = img->pixels + ((size_t)(y + dy) * img->width + (size_t)(x + dx)) * 4;
                    int d = px[3] ? abs(px[2] - bar[0]) + abs(px[1] - bar[1]) + abs(px[0] - bar[2]) : 999;
                    worst = d > worst ? d : worst;
                }
            }
            if (worst <= BAR_MATCH && (room > best_room || (room == best_room && worst < best))) {
                best = worst;
                best_room = room;
                b->bar_u = x;
                b->bar_v = y;
            }
        }
    }
    return b->bar_u >= 0;
}

static bool add_piece(const Board *b, const Piece *p, const uint8_t bar[3], Built *built,
                      WoError *error) {
    /* Corners in the board's order: top left, top right, bottom left, bottom
     * right, or with the middle two swapped where the board's go round the
     * other way. */
    double xs[QUAD] = {p->x0, p->x1, p->x0, p->x1};
    double ys[QUAD] = {p->y0, p->y0, p->y1, p->y1};
    if (b->reversed) {
        xs[1] = p->x0; ys[1] = p->y1;
        xs[2] = p->x1; ys[2] = p->y0;
    }
    unsigned index[QUAD];
    size_t first = b->object->vertices;
    for (int c = 0; c < QUAD; c++) {
        if (!add_vertex(built, board_point(b, xs[c], ys[c]),
                        b->data + b->object->at + PRM_HEADER, first, &index[c], error)) {
            return false;
        }
    }
    const uint8_t *model = b->data + b->model->at;
    size_t body = b->model->size - 4;
    const uint8_t *colours = model + 4 + TEXTURE_AT + 6 + QUAD * 2 + 2; /* after the pad */
    bool shaded = b->model->kind == KIND_SHADED_TEXTURED_QUAD;
    if (p->textured || b->bar_u >= 0) {
        /* A face like the board's: the picture's part, or for a bar one
         * texel of the picture in the bars colour. */
        uint8_t face[64];
        memcpy(face, model, b->model->size);
        for (int c = 0; c < QUAD; c++) {
            put_be16(face + 4 + c * 2, index[c]);
            bool right = xs[c] == p->x1, bottom = ys[c] == p->y1;
            int u0 = texel(p->u0), u1 = texel(p->u1) - 1, v0 = texel(p->v0), v1 = texel(p->v1) - 1;
            /* Where the picture's edge meets a bar or another tile rather
             * than the board's edge, a texel in: the game's filtering reads
             * past the edge, into the art beside the picture on its page,
             * and draws a line of it there at a glancing angle. */
            u0 += p->x0 > 1e-4 && u1 - u0 > 2;
            u1 -= p->x1 < b->aspect - 1e-4 && u1 - u0 > 2;
            v0 += p->y0 > 1e-4 && v1 - v0 > 2;
            v1 -= p->y1 < 1 - 1e-4 && v1 - v0 > 2;
            u1 = u1 < u0 ? u0 : u1;
            v1 = v1 < v0 ? v0 : v1;
            if (!p->textured) {
                u0 = u1 = b->bar_u;
                v0 = v1 = b->bar_v;
            }
            face[4 + TEXTURE_AT + 6 + c * 2] = (uint8_t)(right ? u1 : u0);
            face[4 + TEXTURE_AT + 6 + c * 2 + 1] = (uint8_t)(bottom ? v1 : v0);
        }
        built->primitive_count++;
        return wo_buf_append(&built->primitives, face, 4 + body, error);
    }
    /* A bar: a plain shaded quad in the bars colour, lit as the board is (a
     * textured face's colour of 128 leaves the picture as it is). */
    uint8_t face[4 + 24];
    put_be16(face, KIND_SHADED_QUAD);
    face[2] = model[2];
    face[3] = model[3];
    for (int c = 0; c < QUAD; c++) {
        put_be16(face + 4 + c * 2, index[c]);
        const uint8_t *light = colours + (shaded ? c * 4 : 0);
        for (int k = 0; k < 3; k++) {
            unsigned value = (unsigned)bar[k] * light[k] / 128u;
            face[4 + 8 + c * 4 + k] = (uint8_t)(value > 255 ? 255 : value);
        }
        face[4 + 8 + c * 4 + 3] = light[3];
    }
    built->primitive_count++;
    return wo_buf_append(&built->primitives, face, sizeof(face), error);
}

static bool parse_colour(const char *text, uint8_t out[3]) {
    if (!text || text[0] != '#' || strlen(text) != 7) {
        return false;
    }
    for (int i = 0; i < 3; i++) {
        unsigned value = 0;
        for (int j = 1; j <= 2; j++) {
            char c = text[i * 2 + j];
            value = value * 16 + (unsigned)(c >= '0' && c <= '9'   ? c - '0'
                                           : c >= 'a' && c <= 'f' ? c - 'a' + 10
                                           : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                                                  : 0);
        }
        out[i] = (uint8_t)value;
    }
    return true;
}

static double number_at(const WoJson *array, size_t i) {
    return wo_json_as_number(wo_json_at(array, i), 0);
}

/* A sprite takes the picture's proportions (times the stretch) at its scale. */
static void lay_out_sprite(uint8_t *model_bytes, const Object *object, const WoJson *entry,
                           size_t *changed) {
    const WoJson *sprites = wo_json_get(entry, "sprites");
    const WoJson *source = wo_json_get(entry, "source");
    double sw = number_at(source, 2), sh = number_at(source, 3);
    if (sw <= 0 || sh <= 0) {
        return;
    }
    double stretch = strcmp(wo_json_str(entry, "mode", ""), "stretch")
                         ? 1
                         : (double)wo_json_int(entry, "stretch", 100) / 100;
    double height_scale = (double)wo_json_int(entry, "scale", 100) / 100;
    for (size_t i = 0; i < wo_json_length(sprites); i++) {
        int64_t n = wo_json_as_int(wo_json_at(sprites, i), -1);
        if (n < 0 || (size_t)n >= object->sprite_count) {
            continue;
        }
        uint8_t *b = model_bytes + object->primitives[object->sprites[n]].at + 4;
        double height = be16(b + 4) * height_scale;
        double width = height * sw / sh * stretch;
        put_be16(b + 2, (unsigned)fmin(65535, floor(width + 0.5)));
        put_be16(b + 4, (unsigned)fmin(65535, floor(height + 0.5)));
        ++*changed;
    }
}

/* A board the game's own code fills (Talon's Reach's: its routine writes a
 * quarter of one picture into each of the four panels' texture coordinates
 * at every step, so the panels must stay the object's first faces, four
 * corners each). Instead its corners move: the board narrows about its
 * middle to the picture's proportions (`aspect`, width over height), and
 * plain faces in the bars colour, lit as the panels are, fill the two sides
 * after the panels. The corners' vertical place is kept. */
static bool lay_out_routine_board(uint8_t *copy, const Object *object, const WoJson *entry,
                                  Built *built, size_t *changed, WoError *error) {
    if (object->face_count < 4 || object->vertices < 4) {
        return true;
    }
    /* The middle: the corner all four panels share. */
    int middle = -1;
    for (size_t v = 0; v < object->vertices && middle < 0; v++) {
        int shared = 0;
        for (int f = 0; f < 4; f++) {
            const Primitive *p = &object->primitives[object->faces[f]];
            const uint8_t *b = copy + p->at + 4;
            for (int c = 0; c < p->corners; c++) {
                shared += be16(b + c * 2) == v;
            }
        }
        middle = shared == 4 ? (int)v : -1;
    }
    const Primitive *panel = &object->primitives[object->faces[0]];
    if (middle < 0 || panel->kind != KIND_SHADED_TEXTURED_QUAD) {
        return true;
    }
    Vec centre = vertex(copy, object, (unsigned)middle);
    double reach = 0, top = 1e9, bottom = -1e9;
    for (size_t v = 0; v < object->vertices; v++) {
        Vec p = vertex(copy, object, (unsigned)v);
        reach = fmax(reach, hypot(p.x - centre.x, p.z - centre.z));
        top = fmin(top, p.y);
        bottom = fmax(bottom, p.y);
    }
    double aspect = wo_json_as_number(wo_json_get(entry, "aspect"), 1);
    double factor = reach > 0 ? (bottom - top) * aspect / (2 * reach) : 1;
    if (factor >= 1 || factor <= 0) {
        return true;
    }
    /* The corners on each side (as far out as the board reaches), before
     * they move: a bar runs between their old and new places. */
    enum { MOST = 16 };
    unsigned side_index[2][MOST];
    Vec side_old[2][MOST];
    int side_count[2] = {0, 0};
    Vec axis = {0, 0, 0};
    for (size_t v = 0; v < object->vertices; v++) {
        Vec p = vertex(copy, object, (unsigned)v);
        if (hypot(p.x - centre.x, p.z - centre.z) < reach * 0.9) {
            continue;
        }
        if (axis.x == 0 && axis.z == 0) {
            axis = (Vec){p.x - centre.x, 0, p.z - centre.z};
        }
        int side = (p.x - centre.x) * axis.x + (p.z - centre.z) * axis.z < 0;
        if (side_count[side] == MOST) {
            return true;
        }
        /* Kept in order down the side. */
        int at = side_count[side]++;
        while (at > 0 && side_old[side][at - 1].y > p.y) {
            side_old[side][at] = side_old[side][at - 1];
            side_index[side][at] = side_index[side][at - 1];
            at--;
        }
        side_old[side][at] = p;
        side_index[side][at] = (unsigned)v;
    }
    if (side_count[0] < 2 || side_count[1] < 2) {
        return true;
    }
    /* Narrow: every corner's distance across from the middle, scaled. */
    for (size_t v = 0; v < object->vertices; v++) {
        uint8_t *corner = copy + object->at + PRM_HEADER + v * 8u;
        Vec p = vertex(copy, object, (unsigned)v);
        double x = centre.x + (p.x - centre.x) * factor, z = centre.z + (p.z - centre.z) * factor;
        put_be16(corner, (unsigned)(uint16_t)(int16_t)floor(x + 0.5));
        put_be16(corner + 4, (unsigned)(uint16_t)(int16_t)floor(z + 0.5));
    }
    uint8_t bar[3];
    if (!parse_colour(wo_json_str(entry, "bars", NULL), bar)) {
        ++*changed;
        return true; /* see-through bars: the board is narrower, nothing more */
    }
    /* The panels' way round: a bar's corners go round as they do. */
    const uint8_t *pb = copy + panel->at + 4;
    Vec a = vertex(copy, object, be16(pb)), b = vertex(copy, object, be16(pb + 2)),
        c = vertex(copy, object, be16(pb + 4));
    Vec n = {(b.y - a.y) * (c.z - a.z) - (b.z - a.z) * (c.y - a.y),
             (b.z - a.z) * (c.x - a.x) - (b.x - a.x) * (c.z - a.z),
             (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x)};
    const uint8_t *light = pb + TEXTURE_AT + 6 + QUAD * 2 + 2;
    const uint8_t *existing = copy + object->at + PRM_HEADER;
    for (int side = 0; side < 2; side++) {
        for (int k = 0; k + 1 < side_count[side]; k++) {
            /* Outer top, inner top, outer bottom, inner bottom: the inner
             * corners are the panels' own. */
            unsigned index[QUAD];
            if (!add_vertex(built, side_old[side][k], existing, object->vertices, &index[0], error) ||
                !add_vertex(built, side_old[side][k + 1], existing, object->vertices, &index[2],
                            error)) {
                return false;
            }
            index[1] = side_index[side][k];
            index[3] = side_index[side][k + 1];
            Vec ot = side_old[side][k], ob = side_old[side][k + 1];
            Vec it = vertex(copy, object, index[1]);
            Vec e1 = {it.x - ot.x, it.y - ot.y, it.z - ot.z};
            Vec e2 = {ob.x - ot.x, ob.y - ot.y, ob.z - ot.z};
            Vec m = {e1.y * e2.z - e1.z * e2.y, e1.z * e2.x - e1.x * e2.z,
                     e1.x * e2.y - e1.y * e2.x};
            if (m.x * n.x + m.y * n.y + m.z * n.z < 0) {
                unsigned t = index[0]; index[0] = index[1]; index[1] = t;
                t = index[2]; index[2] = index[3]; index[3] = t;
            }
            uint8_t face[4 + 24];
            put_be16(face, KIND_SHADED_QUAD);
            face[2] = copy[panel->at + 2];
            face[3] = copy[panel->at + 3];
            for (int q = 0; q < QUAD; q++) {
                put_be16(face + 4 + q * 2, index[q]);
                for (int ch = 0; ch < 3; ch++) {
                    unsigned value = (unsigned)bar[ch] * light[q * 4 + ch] / 128u;
                    face[4 + 8 + q * 4 + ch] = (uint8_t)(value > 255 ? 255 : value);
                }
                face[4 + 8 + q * 4 + 3] = light[q * 4 + 3];
            }
            built->primitive_count++;
            if (!wo_buf_append(&built->primitives, face, sizeof(face), error)) {
                return false;
            }
        }
    }
    ++*changed;
    return true;
}

/* Lay out one board of `object`: its faces are marked removed and the new
 * ones go into `built`. False only for lack of memory. */
static bool lay_out_board(const uint8_t *data, Object *object, const WoJson *entry,
                          const WoCmp *frames, Built *built, size_t *changed, WoError *error) {
    const WoJson *faces = wo_json_get(entry, "faces");
    const WoJson *source_json = wo_json_get(entry, "source");
    int prims[64];
    size_t count = 0;
    const Primitive *model = NULL;
    for (size_t i = 0; i < wo_json_length(faces) && count < 64; i++) {
        int64_t n = wo_json_as_int(wo_json_at(faces, i), -1);
        if (n < 0 || (size_t)n >= object->face_count) {
            return true; /* not this archive's board */
        }
        const Primitive *p = &object->primitives[object->faces[n]];
        if (p->kind & 1u) {
            return true; /* untextured */
        }
        if (!model && (p->kind == KIND_TEXTURED_QUAD || p->kind == KIND_SHADED_TEXTURED_QUAD)) {
            model = p;
        }
        prims[count++] = object->faces[n];
    }
    double source[4];
    for (int i = 0; i < 4; i++) {
        source[i] = number_at(source_json, (size_t)i);
    }
    Board b = {data, object, NULL, -1, -1, model, {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}}, 255, 0, 255, 0, 1,
               false};
    if (!count || !model || source[2] <= 0 || source[3] <= 0 ||
        !fit_mapping(data, object, prims, count, &b.map)) {
        return true;
    }
    for (size_t f = 0; f < count; f++) {
        const Primitive *p = &object->primitives[prims[f]];
        const uint8_t *uv = data + p->at + 4 + (size_t)p->corners * 2u + 6u;
        for (int c = 0; c < p->corners; c++) {
            b.umin = fmin(b.umin, uv[c * 2]);
            b.umax = fmax(b.umax, uv[c * 2]);
            b.vmin = fmin(b.vmin, uv[c * 2 + 1]);
            b.vmax = fmax(b.vmax, uv[c * 2 + 1]);
        }
    }
    if (b.umax <= b.umin || b.vmax <= b.vmin) {
        return true;
    }
    b.aspect = length(b.map.eu) * (b.umax - b.umin) / (length(b.map.ev) * (b.vmax - b.vmin));
    const uint8_t *muv = data + model->at + 4 + TEXTURE_AT + 6;
    double turn = (double)(muv[2] - muv[0]) * (muv[5] - muv[1]) -
                  (double)(muv[3] - muv[1]) * (muv[4] - muv[0]);
    b.reversed = turn < 0;

    Layout layout;
    lay_out(&layout, wo_json_str(entry, "mode", "fit"), b.aspect, source,
            (double)wo_json_int(entry, "stretch", 100) / 100,
            (double)wo_json_int(entry, "scale", 100) / 100, (int)wo_json_int(entry, "tiles", 0));
    uint8_t bar[3];
    bool coloured = parse_colour(wo_json_str(entry, "bars", NULL), bar);
    WoImage picture = {0};
    unsigned number = be16(data + model->at + 4 + TEXTURE_AT);
    if (coloured && frames && number < frames->count &&
        wo_tim_decode_bgra(frames->data + frames->offsets[number], frames->sizes[number], &picture,
                           NULL)) {
        b.picture = &picture;
        find_bar_texel(&b, source, bar);
    }
    bool ok = true;
    for (int i = 0; ok && i < layout.count; i++) {
        if (layout.pieces[i].textured || coloured) {
            ok = add_piece(&b, &layout.pieces[i], bar, built, error);
        }
    }
    wo_image_free(&picture);
    if (!ok) {
        return false;
    }
    for (size_t f = 0; f < count; f++) {
        object->primitives[prims[f]].removed = true;
    }
    ++*changed;
    return true;
}

/* The object, laid out, appended to out. */
static bool write_object(const uint8_t *data, Object *object, const WoJson *entries, int index,
                         const WoCmp *frames, WoBuf *out, size_t *changed, WoError *error) {
    Built built = {{0}, {0}, 0, 0};
    uint8_t *copy = malloc(object->at + object->size);
    if (!copy) {
        return wo_fail(error, WO_ERR_INTERNAL, "Out of memory.");
    }
    memcpy(copy, data, object->at + object->size);
    bool ok = true;
    for (size_t i = 0; ok && i < wo_json_length(entries); i++) {
        const WoJson *entry = wo_json_at(entries, i);
        if (wo_json_int(entry, "object", -1) != index) {
            continue;
        }
        if (wo_json_get(entry, "sprites")) {
            lay_out_sprite(copy, object, entry, changed);
        } else if (wo_json_bool(entry, "routine", false)) {
            ok = lay_out_routine_board(copy, object, entry, &built, changed, error);
        } else {
            ok = lay_out_board(data, object, entry, frames, &built, changed, error);
        }
    }
    size_t removed = 0;
    for (size_t i = 0; i < object->primitive_count; i++) {
        removed += object->primitives[i].removed;
    }
    uint8_t header[PRM_HEADER];
    memcpy(header, copy + object->at, PRM_HEADER);
    size_t vertices = object->vertices + built.vertex_count;
    size_t primitives = object->primitive_count - removed + built.primitive_count;
    if (ok && (vertices > 0xffff || primitives > 0xffff)) {
        ok = wo_fail(error, WO_ERR_FORMAT, "An object grew too large.");
    }
    if (ok) {
        put_be16(header + 16, (unsigned)vertices);
        put_be16(header + 32, (unsigned)primitives);
        const uint8_t *points = copy + object->at + PRM_HEADER;
        ok = wo_buf_append(out, header, PRM_HEADER, error) &&
             wo_buf_append(out, points, object->vertices * 8u, error) &&
             wo_buf_append(out, built.vertices.data, built.vertices.len, error) &&
             wo_buf_append(out, points + object->vertices * 8u, object->normals * 8u, error);
    }
    for (size_t i = 0; ok && i < object->primitive_count; i++) {
        const Primitive *p = &object->primitives[i];
        if (!p->removed) {
            ok = wo_buf_append(out, copy + p->at, p->size, error);
        }
    }
    ok = ok && wo_buf_append(out, built.primitives.data, built.primitives.len, error);
    wo_buf_free(&built.vertices);
    wo_buf_free(&built.primitives);
    free(copy);
    return ok;
}

bool wo_billboards_apply(const WoJson *entries, WoBuf *archive, size_t *changed, WoError *error) {
    size_t laid = 0;
    if (changed) {
        *changed = 0;
    }
    if (!wo_json_length(entries)) {
        return true;
    }
    WoWad wad = {0};
    if (!wo_wad_parse(archive->data, archive->len, &wad, NULL)) {
        return true;
    }
    const WoWadEntry *models = wo_wad_find(&wad, "scene.prm");
    if (!models) {
        wo_wad_free(&wad);
        return true;
    }
    const uint8_t *prm = archive->data + models->offset;
    const WoWadEntry *textures = wo_wad_find(&wad, "scene.cmp");
    WoCmp frames = {0};
    bool have_frames = textures && wo_cmp_decode(archive->data + textures->offset, textures->size,
                                                 &frames, NULL);
    WoBuf rebuilt = {0};
    bool ok = true;
    int index = 0;
    for (size_t at = 0; ok && at < models->size; index++) {
        Object object;
        if (!object_read(prm, models->size, at, &object)) {
            object_free(&object);
            /* Not a model file this code reads: leave the archive alone. */
            wo_buf_free(&rebuilt);
            wo_cmp_free(&frames);
            wo_wad_free(&wad);
            return true;
        }
        bool edited = false;
        for (size_t i = 0; !edited && i < wo_json_length(entries); i++) {
            edited = wo_json_int(wo_json_at(entries, i), "object", -1) == index;
        }
        ok = edited ? write_object(prm, &object, entries, index, have_frames ? &frames : NULL,
                                   &rebuilt, &laid, error)
                    : wo_buf_append(&rebuilt, prm + at, object.size, error);
        at += object.size;
        object_free(&object);
    }
    /* The archive again, with the new model file. */
    WoBuf out = {0};
    ok = ok && wo_buf_append(&out, archive->data, wad.entries[0].offset, error);
    for (size_t i = 0; ok && i < wad.count; i++) {
        const WoWadEntry *entry = &wad.entries[i];
        bool mine = entry == models;
        const uint8_t *bytes = mine ? rebuilt.data : archive->data + entry->offset;
        size_t size = mine ? rebuilt.len : entry->size;
        ok = wo_buf_append(&out, bytes, size, error);
        if (ok) {
            wo_put_le32(out.data + entry->record, (uint32_t)size);
            wo_put_le32(out.data + entry->record + 4, (uint32_t)size);
        }
    }
    if (ok) {
        wo_buf_free(archive);
        *archive = out;
        memset(&out, 0, sizeof(out));
        if (changed) {
            *changed = laid;
        }
    }
    wo_buf_free(&out);
    wo_buf_free(&rebuilt);
    wo_cmp_free(&frames);
    wo_wad_free(&wad);
    return ok;
}
