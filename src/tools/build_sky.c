/* The sky page of the authoring studio (wo4k-build sky, tools\sky.cmd).
 *
 * Each track's sky is one picture the game repeats round the sky (wo_sky.h).
 * The page chooses, for a track, how many times a picture drawn anew goes
 * round (once: one panorama all the way round) and where its seam is, and
 * and where the picture's top and bottom edges are (any shape of picture
 * fits: the sky beyond its edges shows its edge rows, or its parts beyond
 * the sky are unseen), and keeps that in manifests/sky-layout.json, which
 * Play applies while the picture is authored and HD artwork is on. It shows
 * the size to draw at (a panorama's proportions: the degrees round for the
 * degrees from bottom to top), puts a picture to start from into
 * assets/auth (the sky as the game shows it now, unwrapped into the
 * layout), and shows the sky from the camera's place, with the game's
 * picture or the one drawn.
 *
 * The geometry is read from the track's PlayStation archive as imported
 * (wobble/textures/ps1-source), whose sky is as the game made it, else from
 * the archive in place. */
#include <windows.h>
#include <shellapi.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "core/wo_fs.h"
#include "core/wo_json.h"
#include "core/wo_png.h"
#include "core/wo_sky.h"
#include "core/wo_texfmt.h"
#include "tools/build_tool.h"

#define SKY_LAYOUT "manifests/sky-layout.json"
#define CATALOG "manifests/hd-textures.json"
#define ORIGINALS "wobble/textures/originals"
#define AUTHORED "assets/auth"
#define UNUSED "assets/auth-unused"
#define UPSCALED "assets/hd"
#define GAME_PICTURES ".work/sky-pictures" /* the archives' own, where originals has none */
#define DRAW_HEIGHT 1024 /* the height to draw a sky at */
#define DRAW_WIDEST 8192 /* and the widest */

typedef struct SkyTrack {
    const char *track;
    const char *title;
    int sheet; /* skyNN.tga */
} SkyTrack;

static const SkyTrack SKY_TRACKS[] = {
    {"TRACK01", "Talon's Reach", 1}, {"TRACK08", "Sagarmatha", 2},    {"TRACK13", "Valparaiso", 3},
    {"TRACK20", "Phenitia Park", 4}, {"TRACK02", "Gare d'Europa", 5}, {"TRACK17", "Odessa Keys", 6},
    {"TRACK06", "Vostok Island", 7}, {"TRACK07", "Spilskinanke", 8}};

static const SkyTrack *find_track(const char *track) {
    for (size_t i = 0; i < WO_COUNT(SKY_TRACKS); i++) {
        if (wo_str_ieq(SKY_TRACKS[i].track, track)) {
            return &SKY_TRACKS[i];
        }
    }
    return NULL;
}

/* The catalogued picture shown over the track's sky sheet. */
static bool sky_picture(const WoJson *catalog, const SkyTrack *t, char *out, size_t size) {
    char native[16];
    snprintf(native, sizeof(native), "sky%02d.tga", t->sheet);
    for (size_t i = 0; i < wo_json_length(catalog); i++) {
        const WoJson *entry = wo_json_at(catalog, i);
        const WoJson *places = wo_json_get(entry, "regions");
        const WoJson *first = wo_json_at(places, 0);
        const char *own = wo_json_str(first ? first : entry, "native", "");
        if (wo_str_ieq(own, native)) {
            snprintf(out, size, "%s", wo_json_str(entry, "file", ""));
            return out[0] != 0;
        }
    }
    return false;
}

static bool read_archive(const SkyTrack *t, WoBuf *bytes, WoError *error) {
    char ps1[96], placed[64];
    snprintf(ps1, sizeof(ps1), "wobble/textures/ps1-source/%s/TRACK.WAD", t->track);
    snprintf(placed, sizeof(placed), "wobble/%s/TRACK.WAD", t->track);
    return wo_read_file(wo_is_file(ps1) ? ps1 : placed, 64u << 20, bytes, error);
}

static bool read_sky(const SkyTrack *t, WoSky *sky, WoError *error) {
    WoBuf bytes = {0};
    bool ok = read_archive(t, &bytes, error) && wo_sky_read(bytes.data, bytes.len, sky, error);
    wo_buf_free(&bytes);
    return ok;
}

/* The game's own picture of a sky: as Setup and Play keep it
 * (wobble/textures/originals), else the archive's (the PC sheets' skies are
 * not kept there), written once to GAME_PICTURES. */
static char *game_picture(const SkyTrack *t, const char *picture, WoError *error) {
    char *kept = wo_path_join(ORIGINALS, picture, error);
    if (!kept || wo_is_file(kept)) {
        return kept;
    }
    wo_free(kept);
    char *made = wo_path_join(GAME_PICTURES, picture, error);
    if (!made || wo_is_file(made)) {
        return made;
    }
    WoBuf bytes = {0};
    WoWad wad = {0};
    WoCmp frames = {0};
    WoImage image = {0};
    bool ok = read_archive(t, &bytes, error) && wo_wad_parse(bytes.data, bytes.len, &wad, error);
    const WoWadEntry *entry = ok ? wo_wad_find(&wad, "sky.cmp") : NULL;
    ok = ok && entry && wo_cmp_decode(bytes.data + entry->offset, entry->size, &frames, error) &&
         frames.count > 0 &&
         wo_tim_decode_bgra(frames.data + frames.offsets[0], frames.sizes[0], &image, error) &&
         wo_mkdirs(GAME_PICTURES, error) && wo_png_write(made, &image, WO_PIXELS_BGRA, error);
    if (!ok && !wo_error_is_set(error)) {
        wo_fail(error, WO_ERR_FORMAT, "The archive has no sky picture.");
    }
    wo_image_free(&image);
    wo_cmp_free(&frames);
    wo_wad_free(&wad);
    wo_buf_free(&bytes);
    if (!ok) {
        wo_free(made);
        return NULL;
    }
    return made;
}

bool sky_game_picture(const char *track, char **path, WoError *error) {
    *path = NULL;
    const SkyTrack *t = find_track(track);
    WoJson *catalog = NULL;
    char picture[128] = {0};
    bool ok = t && wo_read_json(CATALOG, 32u << 20, &catalog, error) &&
              sky_picture(catalog, t, picture, sizeof(picture));
    wo_json_free(catalog);
    if (!ok) {
        return wo_error_is_set(error) ? false : wo_fail(error, WO_ERR_NOT_FOUND, "No such sky.");
    }
    *path = game_picture(t, picture, error);
    return *path != NULL;
}

static char *in(const char *folder, const char *name) {
    return wo_path_join(folder, name, NULL);
}

static uint64_t mtime_in(const char *folder, const char *name) {
    char *path = in(folder, name);
    WoFileInfo info = {0};
    bool ok = path && wo_stat(path, &info, NULL) && info.exists;
    wo_free(path);
    return ok ? info.mtime : 0;
}

static WoJson *load_layouts(void) {
    WoJson *root = NULL;
    if (wo_is_file(SKY_LAYOUT)) {
        wo_read_json(SKY_LAYOUT, 4u << 20, &root, NULL);
    }
    if (!root) {
        root = wo_json_new_object();
    }
    if (root && !wo_json_get(root, "tracks")) {
        wo_json_put(root, "tracks", wo_json_new_object());
    }
    return root;
}

/* The size to draw a picture going `around` times round a sky at. */
static void draw_size(int around, double top, double bottom, uint32_t *width, uint32_t *height) {
    double w = DRAW_HEIGHT * (360.0 / around) / (top - bottom);
    double h = DRAW_HEIGHT;
    if (w > DRAW_WIDEST) {
        h = h * DRAW_WIDEST / w;
        w = DRAW_WIDEST;
    }
    *width = (uint32_t)lround(w);
    *height = (uint32_t)lround(h);
}

/* --- What the page sees ----------------------------------------------------------------- */

/* A sky's reach, the layouts that fit it and its rows of corners, into o. */
static void put_shape(WoJson *o, const WoSky *sky) {
    wo_json_put(o, "low", wo_json_new_number(sky->low));
    wo_json_put(o, "high", wo_json_new_number(sky->high));
    WoJson *options = wo_json_new_array();
    for (int around = 1; options && around <= WO_SKY_MAX_AROUND; around++) {
        double seams[256];
        size_t n = wo_sky_seams(sky, around, seams, WO_COUNT(seams));
        if (!n) {
            continue;
        }
        WoJson *option = wo_json_new_object();
        WoJson *list = wo_json_new_array();
        for (size_t s = 0; list && s < n; s++) {
            wo_json_push(list, wo_json_new_number(seams[s]));
        }
        uint32_t w, h;
        draw_size(around, sky->high, sky->low, &w, &h);
        wo_json_put_int(option, "around", around);
        wo_json_put(option, "seams", list);
        wo_json_put_int(option, "drawWidth", w);
        wo_json_put_int(option, "drawHeight", h);
        wo_json_push(options, option);
    }
    wo_json_put(o, "options", options);
    double rows[256];
    size_t row_count = wo_sky_rows(sky, rows, WO_COUNT(rows));
    WoJson *row_list = wo_json_new_array();
    for (size_t r = 0; row_list && r < row_count; r++) {
        wo_json_push(row_list, wo_json_new_number(rows[r]));
    }
    wo_json_put(o, "rows", row_list);
}

bool sky_state(WoJson **out, WoError *error) {
    *out = NULL;
    WoJson *catalog = NULL;
    if (!wo_read_json(CATALOG, 32u << 20, &catalog, error)) {
        return false;
    }
    WoJson *layouts = load_layouts();
    WoJson *root = wo_json_new_object();
    WoJson *tracks = wo_json_new_array();
    bool ok = layouts && root && tracks;
    for (size_t i = 0; ok && i < WO_COUNT(SKY_TRACKS); i++) {
        const SkyTrack *t = &SKY_TRACKS[i];
        WoSky sky;
        char picture[128] = {0};
        WoError own = {0};
        WoJson *o = wo_json_new_object();
        ok = o != NULL;
        wo_json_put_str(o, "track", t->track);
        wo_json_put_str(o, "title", t->title);
        if (!sky_picture(catalog, t, picture, sizeof(picture))) {
            wo_json_put_str(o, "problem", "No picture is catalogued for this sky.");
        } else if (!read_sky(t, &sky, &own)) {
            wo_json_put_str(o, "problem", own.message);
        } else {
            uint64_t authored = mtime_in(AUTHORED, picture);
            wo_json_put_str(o, "picture", picture);
            wo_json_put_bool(o, "authored", authored != 0);
            wo_json_put_int(o, "v", (int64_t)(authored % 1000000007ull));
            wo_json_put_int(o, "width", sky.width);
            wo_json_put_int(o, "height", sky.height);
            put_shape(o, &sky);
            WoJson *flat = wo_json_new_object();
            wo_sky_flatten(&sky);
            put_shape(flat, &sky);
            wo_json_put(o, "flat", flat);
            const WoJson *layout = wo_json_get(wo_json_get(layouts, "tracks"), t->track);
            if (layout) {
                wo_json_put(o, "layout", wo_json_clone(layout));
            }
            wo_sky_free(&sky);
        }
        ok = ok && wo_json_push(tracks, o);
    }
    if (ok) {
        ok = wo_json_put(root, "tracks", tracks); /* which takes it either way */
        tracks = NULL;
    }
    wo_json_free(tracks);
    if (!ok) {
        wo_json_free(root);
        root = NULL;
        wo_fail(error, WO_ERR_INTERNAL, "Out of memory.");
    }
    wo_json_free(layouts);
    wo_json_free(catalog);
    *out = root;
    return ok;
}

/* The sky's triangles for the page's view: per corner where it is (scaled to reach 1), where
 * it is on the game's picture and on the one laid out (0 to 1 of the
 * picture, as the game samples it). */
bool sky_faces(const char *track, bool flat, int around, double turn, double top, double bottom,
               WoJson **out, WoError *error) {
    *out = NULL;
    const SkyTrack *t = find_track(track);
    if (!t) {
        return wo_fail(error, WO_ERR_NOT_FOUND, "No such track.");
    }
    WoSky game, laid;
    if (!read_sky(t, &game, error)) {
        return false;
    }
    if (!read_sky(t, &laid, error)) {
        wo_sky_free(&game);
        return false;
    }
    if (flat) {
        wo_sky_flatten(&laid);
    }
    top = isnan(top) ? laid.high : top;
    bottom = isnan(bottom) ? laid.low : bottom;
    bool fits = wo_sky_fits(&laid, around, turn) && top - bottom >= 1;
    if (fits) {
        wo_sky_lay(&laid, around, turn, top, bottom);
    }
    WoJson *root = wo_json_new_object();
    WoJson *list = wo_json_new_array();
    bool ok = root && wo_json_put(root, "corners", list);
    static const int quad[2][3] = {{0, 1, 2}, {1, 3, 2}};
    /* The model as it is (its faces are flat between these corners), scaled to reach 1. */
    double reach = 1;
    for (size_t f = 0; f < game.count; f++) {
        for (int c = 0; c < game.faces[f].corners; c++) {
            const double *p = game.faces[f].c[c].pos;
            reach = fmax(reach, sqrt(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]));
        }
    }
    for (size_t f = 0; ok && f < game.count; f++) {
        int triangles = game.faces[f].corners == 4 ? 2 : 1;
        for (int k = 0; ok && k < triangles; k++) {
            for (int c = 0; c < 3; c++) {
                int i = quad[k][c];
                const WoSkyCorner *g = &game.faces[f].c[i];
                const WoSkyCorner *l = &laid.faces[f].c[i];
                double values[7] = {g->pos[0] / reach, g->pos[1] / reach, g->pos[2] / reach,
                                    g->u / (double)game.width,
                                    g->v / (double)game.height, l->u / (double)laid.width,
                                    l->v / (double)laid.height};
                for (int v = 0; ok && v < 7; v++) {
                    ok = wo_json_push(list, wo_json_new_number(round(values[v] * 1e5) / 1e5));
                }
            }
        }
    }
    ok = ok && wo_json_put_bool(root, "fits", fits);
    if (!ok) {
        wo_json_free(root);
        root = NULL;
        wo_fail(error, WO_ERR_INTERNAL, "Out of memory.");
    }
    wo_sky_free(&game);
    wo_sky_free(&laid);
    *out = root;
    return ok;
}

/* --- The picture to start from ---------------------------------------------------------- */

typedef struct Point {
    double x, y, u, v; /* on the panorama, and on the game's picture in texels */
} Point;

static void sample(const WoImage *src, uint32_t width, uint32_t height, double u, double v,
                   uint8_t *out) {
    double sx = u / width * src->width - 0.5;
    double sy = v / height * src->height - 0.5;
    sx = sx < 0 ? 0 : sx > src->width - 1 ? src->width - 1 : sx;
    sy = sy < 0 ? 0 : sy > src->height - 1 ? src->height - 1 : sy;
    uint32_t x0 = (uint32_t)sx, y0 = (uint32_t)sy;
    uint32_t x1 = x0 + 1 < src->width ? x0 + 1 : x0;
    uint32_t y1 = y0 + 1 < src->height ? y0 + 1 : y0;
    double fx = sx - x0, fy = sy - y0;
    for (int c = 0; c < 3; c++) {
        double a = src->pixels[((size_t)y0 * src->width + x0) * 4 + c];
        double b = src->pixels[((size_t)y0 * src->width + x1) * 4 + c];
        double d = src->pixels[((size_t)y1 * src->width + x0) * 4 + c];
        double e = src->pixels[((size_t)y1 * src->width + x1) * 4 + c];
        double top = a + (b - a) * fx, bottom = d + (e - d) * fx;
        out[c] = (uint8_t)lround(top + (bottom - top) * fy);
    }
    out[3] = 255;
}

static void triangle(const Point *a, const Point *b, const Point *c, const WoImage *src,
                     uint32_t width, uint32_t height, WoImage *out, uint8_t *covered) {
    double area = (b->x - a->x) * (c->y - a->y) - (c->x - a->x) * (b->y - a->y);
    if (fabs(area) < 1e-9) {
        return;
    }
    double left = fmin(a->x, fmin(b->x, c->x)), right = fmax(a->x, fmax(b->x, c->x));
    double top = fmin(a->y, fmin(b->y, c->y)), bottom = fmax(a->y, fmax(b->y, c->y));
    int x0 = (int)floor(left), x1 = (int)ceil(right);
    int y0 = (int)floor(top), y1 = (int)ceil(bottom);
    x0 = x0 < 0 ? 0 : x0;
    y0 = y0 < 0 ? 0 : y0;
    x1 = x1 > (int)out->width ? (int)out->width : x1;
    y1 = y1 > (int)out->height ? (int)out->height : y1;
    for (int y = y0; y < y1; y++) {
        for (int x = x0; x < x1; x++) {
            double px = x + 0.5, py = y + 0.5;
            double wa = ((b->x - px) * (c->y - py) - (c->x - px) * (b->y - py)) / area;
            double wb = ((c->x - px) * (a->y - py) - (a->x - px) * (c->y - py)) / area;
            double wc = 1 - wa - wb;
            if (wa < -1e-6 || wb < -1e-6 || wc < -1e-6) {
                continue;
            }
            double u = wa * a->u + wb * b->u + wc * c->u;
            double v = wa * a->v + wb * b->v + wc * c->v;
            size_t at = (size_t)y * out->width + (size_t)x;
            sample(src, width, height, u, v, out->pixels + at * 4);
            covered[at] = 1;
        }
    }
}

/* The sky as the game shows it (`sky` with its own u and v, the picture
 * `src`), unwrapped into a panorama of one time round of `around` from
 * `turn`. */
static bool unwrap(const WoSky *sky, int around, double turn, double top, double bottom,
                   const WoImage *src, WoImage *out, WoError *error) {
    uint8_t *covered = wo_alloc_array((size_t)out->width * out->height, 1, error);
    if (!covered) {
        return false;
    }
    memset(covered, 0, (size_t)out->width * out->height);
    memset(out->pixels, 0, (size_t)out->width * out->height * 4);
    double period = 360.0 / around;
    double range = top - bottom;
    double full = out->width * (double)around; /* the panorama's width all the way round */
    for (size_t f = 0; f < sky->count; f++) {
        const WoSkyFace *face = &sky->faces[f];
        static const int quad[4] = {0, 1, 3, 2};
        static const int tri[3] = {0, 1, 2};
        const int *order = face->corners == 4 ? quad : tri;
        /* Round the face, from its first corner that has a direction. */
        double base = -1;
        for (int c = 0; c < face->corners && base < 0; c++) {
            base = face->c[c].apex ? -1 : face->c[c].az;
        }
        if (base < 0) {
            continue;
        }
        Point points[8];
        int n = 0;
        for (int k = 0; k < face->corners; k++) {
            const WoSkyCorner *corner = &face->c[order[k]];
            if (!corner->apex) {
                double az = base + fmod(fmod(corner->az - base + 540.0, 360.0), 360.0) - 180.0;
                points[n++] = (Point){(az - turn) / period * out->width,
                                      (top - corner->el) / range * out->height, corner->u,
                                      corner->v};
                continue;
            }
            /* The top of a dome: a line along the top, as wide as the face. */
            const WoSkyCorner *before = &face->c[order[(k + face->corners - 1) % face->corners]];
            const WoSkyCorner *after = &face->c[order[(k + 1) % face->corners]];
            const WoSkyCorner *ends[2] = {before, after};
            for (int e = 0; e < 2; e++) {
                double az = ends[e]->apex
                                ? base
                                : base + fmod(fmod(ends[e]->az - base + 540.0, 360.0), 360.0) -
                                      180.0;
                points[n++] = (Point){(az - turn) / period * out->width,
                                      (top - corner->el) / range * out->height, corner->u,
                                      corner->v};
            }
        }
        /* Within the first time round, or across its seam. */
        double middle = 0;
        for (int k = 0; k < n; k++) {
            middle += points[k].x / n;
        }
        double shift = -floor(middle / full) * full;
        for (int pass = 0; pass < 2; pass++) {
            Point moved[8];
            for (int k = 0; k < n; k++) {
                moved[k] = points[k];
                moved[k].x += shift - pass * full;
            }
            for (int k = 1; k + 1 < n; k++) {
                triangle(&moved[0], &moved[k], &moved[k + 1], src, sky->width, sky->height, out,
                         covered);
            }
        }
    }
    /* Anything left bare takes the nearest row covered above or below. */
    for (uint32_t x = 0; x < out->width; x++) {
        for (uint32_t y = 0; y < out->height; y++) {
            size_t at = (size_t)y * out->width + x;
            if (covered[at]) {
                continue;
            }
            for (uint32_t d = 1; d < out->height; d++) {
                long up = (long)y - (long)d, down = (long)y + (long)d;
                long from = up >= 0 && covered[(size_t)up * out->width + x] ? up
                            : down < (long)out->height && covered[(size_t)down * out->width + x]
                                ? down
                                : -1;
                if (from >= 0) {
                    memcpy(out->pixels + at * 4, out->pixels + ((size_t)from * out->width + x) * 4,
                           4);
                    break;
                }
            }
            out->pixels[at * 4 + 3] = 255;
        }
    }
    wo_free(covered);
    return true;
}

static bool move_unused(const char *name, WoError *error) {
    char *from = in(AUTHORED, name);
    char *to = NULL;
    bool ok = from && wo_mkdirs(UNUSED, error);
    for (int n = 1; ok && n < 1000; n++) {
        char variant[160];
        if (n == 1) {
            snprintf(variant, sizeof(variant), "%s", name);
        } else {
            snprintf(variant, sizeof(variant), "%.*s-%d.png", (int)strlen(name) - 4, name, n);
        }
        wo_free(to);
        to = in(UNUSED, variant);
        if (to && !wo_exists(to)) {
            break;
        }
    }
    ok = ok && to && wo_move(from, to, error);
    wo_free(from);
    wo_free(to);
    return ok;
}

static bool start_picture(const SkyTrack *t, const char *picture, int around, double turn,
                          double top, double bottom, WoError *error) {
    WoSky sky;
    if (!read_sky(t, &sky, error)) {
        return false;
    }
    bool ok = wo_sky_fits(&sky, around, turn);
    if (!ok) {
        wo_fail(error, WO_ERR_FORMAT, "That seam would cut the sky's faces.");
    }
    /* The best picture of the sky as it is: upscaled, else the game's own. */
    char *from = NULL;
    if (ok) {
        from = mtime_in(UPSCALED, picture) ? in(UPSCALED, picture) : game_picture(t, picture, error);
    }
    WoImage src = {0};
    ok = ok && from && wo_png_read(from, WO_PIXELS_RGBA, &src, error);
    WoImage out = {0};
    if (ok) {
        draw_size(around, top, bottom, &out.width, &out.height);
        out.pixels = wo_alloc_array((size_t)out.width * out.height, 4, error);
        ok = out.pixels != NULL;
    }
    ok = ok && unwrap(&sky, around, turn, top, bottom, &src, &out, error);
    char *to = ok ? in(AUTHORED, picture) : NULL;
    ok = ok && to && wo_mkdirs(AUTHORED, error) &&
         (!wo_is_file(to) || move_unused(picture, error)) &&
         wo_png_write(to, &out, WO_PIXELS_RGBA, error);
    wo_free(to);
    wo_free(out.pixels);
    wo_image_free(&src);
    wo_free(from);
    wo_sky_free(&sky);
    return ok;
}

/* A picture dropped on the page becomes the sky's authored picture (any
 * format the system decodes; written as PNG), the one there before moved to
 * assets/auth-unused. */
#define DROP_FILE ".work/sky-drop.bin"
#define DROP_SMALLEST 16

bool sky_drop(const char *track, const uint8_t *data, size_t len, WoError *error) {
    const SkyTrack *t = find_track(track);
    WoJson *catalog = NULL;
    char picture[128] = {0};
    bool ok = t && wo_read_json(CATALOG, 32u << 20, &catalog, error) &&
              sky_picture(catalog, t, picture, sizeof(picture));
    wo_json_free(catalog);
    if (!ok) {
        return wo_error_is_set(error) ? false : wo_fail(error, WO_ERR_NOT_FOUND, "No such sky.");
    }
    WoImage image = {0};
    ok = wo_mkdirs(".work", error) && wo_write_file(DROP_FILE, data, len, error);
    if (ok && !wo_png_read(DROP_FILE, WO_PIXELS_RGBA, &image, NULL)) {
        ok = wo_fail(error, WO_ERR_FORMAT, "That is not a picture this can read.");
    }
    wo_delete_file(DROP_FILE, NULL);
    if (ok && (image.width < DROP_SMALLEST || image.height < DROP_SMALLEST ||
               image.width > DRAW_WIDEST || image.height > DRAW_WIDEST)) {
        ok = wo_fail(error, WO_ERR_FORMAT, "A sky picture must be %d to %d pixels each way.",
                     DROP_SMALLEST, DRAW_WIDEST);
    }
    char *to = ok ? in(AUTHORED, picture) : NULL;
    ok = ok && to && wo_mkdirs(AUTHORED, error) &&
         (!wo_is_file(to) || move_unused(picture, error)) &&
         wo_png_write(to, &image, WO_PIXELS_RGBA, error);
    wo_free(to);
    wo_image_free(&image);
    return ok;
}

/* --- Actions --------------------------------------------------------------------------- */

static bool save_layout(const char *track, const char *picture, bool flat, int around,
                        double turn, double top, double bottom, bool remove, WoError *error) {
    WoJson *root = load_layouts();
    WoJson *tracks = wo_json_get(root, "tracks");
    bool ok = root && tracks && wo_json_put_str(root, "format", "wo4k.sky-layout") &&
              wo_json_put_int(root, "version", 1) &&
              wo_json_put_str(root, "note",
                              "How each track's sky picture is laid round its sky: \"around\" "
                              "times, its left edge at \"turn\" degrees, its top and bottom "
                              "edges at \"top\" and \"bottom\" degrees above the horizon (with \"flat\", "
                              "laid flat on the sky's faces, as a photo: wo_sky.h). Play "
                              "applies it while the picture is authored and HD artwork is on. "
                              "Made by tools\\sky.cmd.");
    if (ok && remove) {
        wo_json_remove(tracks, track);
    } else if (ok) {
        WoJson *entry = wo_json_new_object();
        ok = entry && wo_json_put_str(entry, "picture", picture) &&
             wo_json_put_int(entry, "around", around) &&
             wo_json_put(entry, "turn", wo_json_new_number(turn)) &&
             wo_json_put(entry, "top", wo_json_new_number(round(top * 10) / 10)) &&
             wo_json_put(entry, "bottom", wo_json_new_number(round(bottom * 10) / 10)) &&
             (!flat || wo_json_put_bool(entry, "flat", true)) &&
             wo_json_put(tracks, track, entry);
    }
    ok = ok || wo_fail(error, WO_ERR_INTERNAL, "Out of memory.");
    bool written = false;
    ok = ok && wo_write_json_if_changed(SKY_LAYOUT, root, &written, error);
    wo_json_free(root);
    return ok;
}

static void open_file(const char *path, bool reveal) {
    wchar_t wide[MAX_PATH * 2];
    char *full = wo_path_full(path, NULL);
    if (!full || !MultiByteToWideChar(CP_UTF8, 0, full, -1, wide, MAX_PATH * 2)) {
        wo_free(full);
        return;
    }
    wo_free(full);
    if (reveal) {
        wchar_t args[MAX_PATH * 2 + 16];
        _snwprintf(args, WO_COUNT(args), L"/select,\"%ls\"", wide);
        args[WO_COUNT(args) - 1] = 0;
        ShellExecuteW(NULL, L"open", L"explorer.exe", args, NULL, SW_SHOWNORMAL);
    } else {
        ShellExecuteW(NULL, L"open", wide, NULL, NULL, SW_SHOWNORMAL);
    }
}

bool sky_act(const char *action, const char *track, bool flat, int around, double turn,
             double top, double bottom, WoError *error) {
    const SkyTrack *t = find_track(track);
    WoJson *catalog = NULL;
    char picture[128] = {0};
    bool ok = t && wo_read_json(CATALOG, 32u << 20, &catalog, error) &&
              sky_picture(catalog, t, picture, sizeof(picture));
    wo_json_free(catalog);
    if (!ok) {
        return wo_error_is_set(error) ? false : wo_fail(error, WO_ERR_NOT_FOUND, "No such sky.");
    }
    bool sets = wo_str_eq(action, "lay") || wo_str_eq(action, "start");
    flat = flat && !wo_str_eq(action, "start"); /* the start picture is unwrapped as a panorama */
    if (sets) {
        WoSky sky;
        if (!read_sky(t, &sky, error)) {
            return false;
        }
        if (flat) {
            wo_sky_flatten(&sky);
        }
        top = isnan(top) ? sky.high : top;
        bottom = isnan(bottom) ? sky.low : bottom;
        ok = turn >= 0 && turn < 360 && wo_sky_fits(&sky, around, turn) && top <= 180 &&
             bottom >= -180 && top - bottom >= 1;
        wo_sky_free(&sky);
        if (!ok) {
            return wo_fail(error, WO_ERR_FORMAT, "That layout does not fit the sky.");
        }
    }
    if (wo_str_eq(action, "lay")) {
        return save_layout(t->track, picture, flat, around, turn, top, bottom, false, error);
    }
    if (wo_str_eq(action, "unlay")) {
        return save_layout(t->track, picture, false, 0, 0, 0, 0, true, error);
    }
    if (wo_str_eq(action, "start")) {
        return start_picture(t, picture, around, turn, top, bottom, error) &&
               save_layout(t->track, picture, false, around, turn, top, bottom, false, error);
    }
    if (wo_str_eq(action, "edit") || wo_str_eq(action, "reveal")) {
        char *path = in(AUTHORED, picture);
        if (path && wo_is_file(path)) {
            open_file(path, wo_str_eq(action, "reveal"));
        }
        wo_free(path);
        return true;
    }
    if (wo_str_eq(action, "unauthor")) {
        return !mtime_in(AUTHORED, picture) || move_unused(picture, error);
    }
    return wo_fail(error, WO_ERR_FORMAT, "Unknown action %s.", action);
}
