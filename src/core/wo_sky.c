#include "core/wo_sky.h"

#include <math.h>
#include <string.h>

#include "core/wo_texfmt.h"

#define PRM_HEADER 144u
#define APEX_SHARE 0.05 /* a corner this close to the axis, for the widest, is at the top */
#define SEAM_SLACK 0.6  /* degrees a seam may pass inside a face's edge (the columns wander) */

/* Faces of a models file: per kind, its corners, the index words after
 * them, whether a picture follows and the bytes after that (as wo_deband.c
 * reads scene.prm); kinds without corners are skipped whole. */
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

static int be16s(const uint8_t *p) {
    return (int16_t)((uint16_t)p[0] << 8 | p[1]);
}

static double wrap360(double a) {
    a = fmod(a, 360.0);
    return a < 0 ? a + 360.0 : a;
}

static double wrap180(double a) {
    return wrap360(a + 180.0) - 180.0;
}

static const double DEGREES = 57.29577951308232;

void wo_sky_free(WoSky *sky) {
    if (sky) {
        wo_free(sky->faces);
        memset(sky, 0, sizeof(*sky));
    }
}

/* The picture's size: the first frame of sky.cmp. */
static bool picture_size(const uint8_t *data, const WoWadEntry *entry, WoSky *out, WoError *error) {
    WoCmp frames = {0};
    WoImage image = {0};
    bool ok = wo_cmp_decode(data + entry->offset, entry->size, &frames, error);
    if (ok && frames.count < 1) {
        ok = wo_fail(error, WO_ERR_FORMAT, "The sky has no picture.");
    }
    ok = ok && wo_tim_decode_bgra(frames.data + frames.offsets[0], frames.sizes[0], &image, error);
    if (ok) {
        out->width = image.width;
        out->height = image.height;
    }
    wo_image_free(&image);
    wo_cmp_free(&frames);
    return ok;
}

/* The textured faces of the sky's models, `base` being where they start in
 * the archive. */
static bool read_faces(const uint8_t *data, size_t len, size_t base, WoSky *out, WoError *error) {
    size_t cap = 0;
    double widest = 0;
    for (int pass = 0; pass < 2; pass++) {
        size_t at = 0;
        size_t count = 0;
        while (len - at >= PRM_HEADER) {
            int vertices = be16s(data + at + 16);
            int normals = be16s(data + at + 24);
            int faces = be16s(data + at + 32);
            at += PRM_HEADER;
            if (vertices < 0 || normals < 0 || faces < 0 ||
                (size_t)(vertices + normals) * 8 > len - at) {
                break;
            }
            const uint8_t *points = data + at;
            at += (size_t)(vertices + normals) * 8;
            for (int v = 0; pass == 0 && v < vertices; v++) {
                const uint8_t *p = points + (size_t)v * 8;
                double r = hypot(be16s(p), be16s(p + 4));
                widest = r > widest ? r : widest;
            }
            bool whole = true;
            for (int f = 0; whole && f < faces; f++) {
                whole = len - at >= 4;
                int kind = whole ? be16s(data + at) : -1;
                at += 4;
                whole = whole && kind >= 0 && kind < 24;
                if (whole && SKIPPED_KINDS[kind]) {
                    whole = len - at >= SKIPPED_KINDS[kind];
                    at += SKIPPED_KINDS[kind];
                    continue;
                }
                FaceKind shape = whole ? FACE_KINDS[kind] : (FaceKind){0, 0, 0, 0};
                size_t size = (size_t)shape.corners * 2 + (size_t)shape.extra * 2 +
                              (shape.textured ? 6 + (size_t)shape.corners * 2 : 0) + shape.tail;
                whole = whole && shape.corners && len - at >= size;
                if (whole && shape.textured && shape.corners >= 3) {
                    const uint8_t *face = data + at;
                    size_t uv = (size_t)shape.corners * 2 + (size_t)shape.extra * 2 + 6;
                    bool inside = true;
                    for (int c = 0; c < shape.corners; c++) {
                        int index = be16s(face + c * 2);
                        inside = inside && index >= 0 && index < vertices;
                    }
                    if (inside && pass == 1 && count < cap) {
                        WoSkyFace *out_face = &out->faces[count];
                        out_face->corners = shape.corners;
                        for (int c = 0; c < shape.corners; c++) {
                            const uint8_t *p = points + (size_t)be16s(face + c * 2) * 8;
                            double x = be16s(p), y = -(double)be16s(p + 2), z = be16s(p + 4);
                            double length = sqrt(x * x + y * y + z * z);
                            WoSkyCorner *corner = &out_face->c[c];
                            length = length > 0 ? length : 1;
                            corner->pos[0] = x;
                            corner->pos[1] = y;
                            corner->pos[2] = z;
                            corner->dir[0] = x / length;
                            corner->dir[1] = y / length;
                            corner->dir[2] = z / length;
                            corner->apex = hypot(x, z) < APEX_SHARE * widest;
                            corner->az = wrap360(atan2(z, x) * DEGREES);
                            corner->el = atan2(y, hypot(x, z)) * DEGREES;
                            corner->u = face[uv + (size_t)c * 2];
                            corner->v = face[uv + (size_t)c * 2 + 1];
                            corner->at = base + at + uv + (size_t)c * 2;
                        }
                    }
                    count += inside;
                }
                at += size;
            }
            if (!whole) {
                break;
            }
        }
        if (pass == 0) {
            if (!count) {
                return wo_fail(error, WO_ERR_FORMAT, "The sky has no textured faces.");
            }
            out->faces = wo_alloc_array(count, sizeof(WoSkyFace), error);
            if (!out->faces) {
                return false;
            }
            cap = count;
        } else {
            out->count = count < cap ? count : cap;
        }
    }
    return true;
}

bool wo_sky_read(const uint8_t *archive, size_t len, WoSky *out, WoError *error) {
    memset(out, 0, sizeof(*out));
    WoWad wad;
    if (!wo_wad_parse(archive, len, &wad, error)) {
        return false;
    }
    const WoWadEntry *models = wo_wad_find(&wad, "sky.prm");
    const WoWadEntry *pictures = wo_wad_find(&wad, "sky.cmp");
    bool ok = models && pictures;
    if (!ok) {
        wo_fail(error, WO_ERR_NOT_FOUND, "The track archive has no sky.");
    }
    ok = ok && picture_size(archive, pictures, out, error) &&
         read_faces(archive + models->offset, models->size, models->offset, out, error);
    wo_wad_free(&wad);
    if (ok && (!out->width || !out->height || out->width > 256 || out->height > 256)) {
        ok = wo_fail(error, WO_ERR_FORMAT, "The sky's picture is %ux%u.", out->width, out->height);
    }
    if (ok) {
        out->low = 90;
        out->high = -90;
        for (size_t f = 0; f < out->count; f++) {
            for (int c = 0; c < out->faces[f].corners; c++) {
                double el = out->faces[f].c[c].el;
                out->low = el < out->low ? el : out->low;
                out->high = el > out->high ? el : out->high;
            }
        }
        if (out->high - out->low < 1) {
            ok = wo_fail(error, WO_ERR_FORMAT, "The sky has no height.");
        }
    }
    if (!ok) {
        wo_sky_free(out);
    }
    return ok;
}

/* The middle of a face round the sky, and how far its corners reach from it
 * either way; false for a face of apex corners only. */
static bool face_span(const WoSkyFace *face, double *middle, double *reach) {
    double cx = 0, cy = 0;
    int n = 0;
    for (int c = 0; c < face->corners; c++) {
        if (!face->c[c].apex) {
            cx += cos(face->c[c].az / DEGREES);
            cy += sin(face->c[c].az / DEGREES);
            n++;
        }
    }
    if (!n) {
        return false;
    }
    *middle = wrap360(atan2(cy, cx) * DEGREES);
    *reach = 0;
    for (int c = 0; c < face->corners; c++) {
        if (!face->c[c].apex) {
            double d = fabs(wrap180(face->c[c].az - *middle));
            *reach = d > *reach ? d : *reach;
        }
    }
    return true;
}

bool wo_sky_fits(const WoSky *sky, int around, double turn) {
    if (around < 1 || around > WO_SKY_MAX_AROUND || !isfinite(turn)) {
        return false;
    }
    double period = 360.0 / around;
    for (size_t f = 0; f < sky->count; f++) {
        double middle, reach;
        if (!face_span(&sky->faces[f], &middle, &reach)) {
            continue;
        }
        if (2 * reach > period + SEAM_SLACK) {
            return false;
        }
        for (int k = 0; k < around; k++) {
            if (fabs(wrap180(turn + k * period - middle)) < reach - SEAM_SLACK) {
                return false;
            }
        }
    }
    return true;
}

size_t wo_sky_seams(const WoSky *sky, int around, double *out, size_t cap) {
    if (around < 1 || around > WO_SKY_MAX_AROUND) {
        return 0;
    }
    double period = 360.0 / around;
    size_t n = 0;
    for (size_t f = 0; f < sky->count; f++) {
        for (int c = 0; c < sky->faces[f].corners; c++) {
            const WoSkyCorner *corner = &sky->faces[f].c[c];
            if (corner->apex) {
                continue;
            }
            /* A tenth of a degree is as fine as the manifest keeps it. */
            double turn = round(fmod(corner->az, period) * 10.0) / 10.0;
            turn = turn >= period ? 0 : turn;
            bool known = false;
            for (size_t i = 0; i < n && !known; i++) {
                known = fabs(out[i] - turn) < 0.25;
            }
            if (!known && n < cap && wo_sky_fits(sky, around, turn)) {
                size_t i = n++;
                for (; i > 0 && out[i - 1] > turn; i--) {
                    out[i] = out[i - 1];
                }
                out[i] = turn;
            }
        }
    }
    return n;
}

static uint8_t texel(double share, uint32_t size) {
    share = share < 0 ? 0 : share > 1 ? 1 : share;
    return (uint8_t)lround(share * (double)(size - 1));
}

size_t wo_sky_rows(const WoSky *sky, double *out, size_t cap) {
    size_t n = 0;
    for (size_t f = 0; f < sky->count; f++) {
        for (int c = 0; c < sky->faces[f].corners; c++) {
            double el = round(sky->faces[f].c[c].el * 10.0) / 10.0;
            bool known = false;
            for (size_t i = 0; i < n && !known; i++) {
                known = fabs(out[i] - el) < SEAM_SLACK; /* the rows wander too */
            }
            if (!known && n < cap) {
                size_t i = n++;
                for (; i > 0 && out[i - 1] > el; i--) {
                    out[i] = out[i - 1];
                }
                out[i] = el;
            }
        }
    }
    return n;
}

static void find_range(WoSky *sky) {
    sky->low = 1e9;
    sky->high = -1e9;
    for (size_t f = 0; f < sky->count; f++) {
        for (int c = 0; c < sky->faces[f].corners; c++) {
            double el = sky->faces[f].c[c].el;
            sky->low = el < sky->low ? el : sky->low;
            sky->high = el > sky->high ? el : sky->high;
        }
    }
}

#define OUTLINE_MAX 256
#define FLAT_FURTHEST 89.0 /* how high or low a corner may be when laid flat */

typedef struct Outline {
    size_t n;
    double az[OUTLINE_MAX], x[OUTLINE_MAX], z[OUTLINE_MAX], along[OUTLINE_MAX];
    double total; /* its length round, in the model's units */
} Outline;

/* Where the ray from the camera towards `az` (degrees round) crosses the
 * outline: how far along it (in the model's units), and how far out. */
static void cross_outline(const Outline *o, double az, double *along, double *out) {
    double dx = cos(az / DEGREES), dz = sin(az / DEGREES);
    size_t i = o->n - 1; /* the side from the last corner round to the first */
    for (size_t k = 0; k + 1 < o->n; k++) {
        if (az >= o->az[k] && az < o->az[k + 1]) {
            i = k;
            break;
        }
    }
    size_t j = (i + 1) % o->n;
    double ax = o->x[i], az0 = o->z[i], bx = o->x[j], bz = o->z[j];
    /* A + s (B - A) on the ray: cross(d, A + s (B - A)) = 0. */
    double ca = dx * az0 - dz * ax, cd = dx * (bz - az0) - dz * (bx - ax);
    double s = fabs(cd) > 1e-12 ? -ca / cd : 0;
    s = s < 0 ? 0 : (s > 1 ? 1 : s);
    double px = ax + s * (bx - ax), pz = az0 + s * (bz - az0);
    *along = o->along[i] + s * hypot(bx - ax, bz - az0);
    *out = hypot(px, pz);
}

void wo_sky_flatten(WoSky *sky) {
    /* The outline: the widest row of corners round the horizon. */
    double rows[OUTLINE_MAX];
    size_t row_count = wo_sky_rows(sky, rows, OUTLINE_MAX);
    double widest = -1, row = 0;
    for (size_t r = 0; r < row_count; r++) {
        double sum = 0;
        int count = 0;
        for (size_t f = 0; f < sky->count; f++) {
            for (int c = 0; c < sky->faces[f].corners; c++) {
                const WoSkyCorner *corner = &sky->faces[f].c[c];
                if (!corner->apex && fabs(corner->el - rows[r]) < SEAM_SLACK) {
                    sum += hypot(corner->pos[0], corner->pos[2]);
                    count++;
                }
            }
        }
        if (count && sum / count > widest) {
            widest = sum / count;
            row = rows[r];
        }
    }
    Outline o;
    o.n = 0;
    for (size_t f = 0; f < sky->count; f++) {
        for (int c = 0; c < sky->faces[f].corners; c++) {
            const WoSkyCorner *corner = &sky->faces[f].c[c];
            if (corner->apex || fabs(corner->el - row) >= SEAM_SLACK || o.n == OUTLINE_MAX) {
                continue;
            }
            size_t i = 0;
            while (i < o.n && fabs(wrap180(o.az[i] - corner->az)) >= SEAM_SLACK) {
                i++;
            }
            if (i == o.n) {
                o.az[o.n] = corner->az;
                o.x[o.n] = corner->pos[0];
                o.z[o.n] = corner->pos[2];
                o.n++;
            }
        }
    }
    if (o.n < 3) {
        return;
    }
    /* In order round (few enough to sort by insertion). */
    for (size_t i = 1; i < o.n; i++) {
        for (size_t j = i; j > 0 && o.az[j - 1] > o.az[j]; j--) {
            double t;
            t = o.az[j]; o.az[j] = o.az[j - 1]; o.az[j - 1] = t;
            t = o.x[j]; o.x[j] = o.x[j - 1]; o.x[j - 1] = t;
            t = o.z[j]; o.z[j] = o.z[j - 1]; o.z[j - 1] = t;
        }
    }
    o.total = 0;
    for (size_t i = 0; i < o.n; i++) {
        o.along[i] = o.total;
        size_t next = (i + 1) % o.n;
        o.total += hypot(o.x[next] - o.x[i], o.z[next] - o.z[i]);
    }
    if (o.total <= 0) {
        return;
    }
    /* Each corner, seen from the camera, on the upright walls over the
     * outline: how far along them (from straight ahead, +x, so that the
     * numbers stay near the directions they were) and how high. */
    double scale = 360.0 / o.total, radius = o.total / (2 * 3.14159265358979323846);
    double zero = 0, unused = 0;
    cross_outline(&o, 0, &zero, &unused);
    for (size_t f = 0; f < sky->count; f++) {
        for (int c = 0; c < sky->faces[f].corners; c++) {
            WoSkyCorner *corner = &sky->faces[f].c[c];
            double flat_el;
            if (corner->apex) {
                flat_el = corner->pos[1] > 0 ? FLAT_FURTHEST : -FLAT_FURTHEST;
            } else {
                double along = 0, out = 0;
                cross_outline(&o, corner->az, &along, &out);
                double near = hypot(corner->pos[0], corner->pos[2]);
                corner->az = wrap360((along - zero) * scale);
                flat_el = corner->pos[1] * (out / near) / radius * DEGREES;
                flat_el = flat_el > FLAT_FURTHEST ? FLAT_FURTHEST
                        : (flat_el < -FLAT_FURTHEST ? -FLAT_FURTHEST : flat_el);
            }
            corner->el = flat_el;
        }
    }
    find_range(sky);
}

void wo_sky_lay(WoSky *sky, int around, double turn, double top, double bottom) {
    double period = 360.0 / (around < 1 ? 1 : around);
    double height = top - bottom > 0.1 ? top - bottom : 0.1;
    for (size_t f = 0; f < sky->count; f++) {
        WoSkyFace *face = &sky->faces[f];
        double middle = 0, reach = 0;
        bool round_it = face_span(face, &middle, &reach);
        /* Which time round the face is in, and where its middle is in it. */
        double from = wrap360(middle - turn);
        double start = floor(from / period) * period;
        for (int c = 0; c < face->corners; c++) {
            WoSkyCorner *corner = &face->c[c];
            double across = from - start;
            if (round_it && !corner->apex) {
                across += wrap180(corner->az - middle);
            }
            corner->u = texel(round_it ? across / period : 0.5, sky->width);
            corner->v = texel((top - corner->el) / height, sky->height);
        }
    }
}

bool wo_sky_apply(const WoJson *layout, WoBuf *archive, bool *changed, WoError *error) {
    if (changed) {
        *changed = false;
    }
    if (!layout) {
        return true;
    }
    int64_t around = wo_json_int(layout, "around", 0);
    double turn = wo_json_as_number(wo_json_get(layout, "turn"), NAN);
    if (around < 1 || around > WO_SKY_MAX_AROUND || !isfinite(turn) || turn < 0 || turn >= 360) {
        return wo_fail(error, WO_ERR_FORMAT, "Invalid sky layout.");
    }
    const WoJson *top_value = wo_json_get(layout, "top");
    const WoJson *bottom_value = wo_json_get(layout, "bottom");
    double top = wo_json_as_number(top_value, 90);
    double bottom = wo_json_as_number(bottom_value, -90);
    if ((top_value && (!isfinite(top) || top < -90 || top > 180)) ||
        (bottom_value && (!isfinite(bottom) || bottom < -180 || bottom > 90)) ||
        (top_value && bottom_value && top - bottom < 1)) {
        return wo_fail(error, WO_ERR_FORMAT, "Invalid sky layout.");
    }
    WoSky sky;
    if (!wo_sky_read(archive->data, archive->len, &sky, NULL)) {
        return true;
    }
    if (wo_json_bool(layout, "flat", false)) {
        wo_sky_flatten(&sky);
    }
    top = top_value ? top : sky.high;
    bottom = bottom_value ? bottom : sky.low;
    bool fits = wo_sky_fits(&sky, (int)around, turn) && top - bottom >= 1;
    if (fits) {
        wo_sky_lay(&sky, (int)around, turn, top, bottom);
        for (size_t f = 0; f < sky.count; f++) {
            for (int c = 0; c < sky.faces[f].corners; c++) {
                const WoSkyCorner *corner = &sky.faces[f].c[c];
                archive->data[corner->at] = corner->u;
                archive->data[corner->at + 1] = corner->v;
            }
        }
    }
    wo_sky_free(&sky);
    if (changed) {
        *changed = fits;
    }
    return true;
}
