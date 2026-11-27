/* wo4k-build auth [<port>]
 *
 * The authoring studio: a page for choosing which textures to draw anew,
 * served from this machine only (127.0.0.1), that does what it is asked at
 * once. Run from the mod folder (tools\auth.cmd).
 *
 * The same picture is in the game under several names, at one size or at
 * several (a wall of one track's scenery is in another's, a road texture is
 * on every track, a sign at 64 texels here and 128 there). Those are found
 * here and kept in manifests/auth-masters.json as groups: Play shows the one
 * authored file of a group in the places of all of it, each at its own size
 * (wo_hdtex.h). So a picture is drawn once, under any one of its names, and
 * nothing has to be matched by hand.
 *
 * The groups are found again at every start, from the pictures as the game
 * has them (wobble/textures/originals, which Setup and Play keep): names that
 * are the same picture exactly, or the same picture in the same proportions
 * at another size (drawn again for it or not; Play scales the one file to
 * each). Pictures of one size that are alike but not the same are kept
 * apart: those are an animation's frames.
 * A name taken out of its group on the page ("not the same") is recorded in
 * the manifest's "apart" and never grouped again until it is put back.
 *
 * What the page can do, at once:
 *   - author a group: a picture to start from goes into assets/auth under
 *     the name of the group's first (the upscaled one where there is one,
 *     else the picture as the game has it);
 *   - edit it (the PNG's own program: Photoshop where it opens PNGs) or show
 *     it in Explorer;
 *   - stop authoring it: its file moves to assets/auth-unused (never
 *     deleted);
 *   - where a group has more than one authored file, keep one;
 *   - take a name out of its group, or put it back.
 * The page asks for the state every few seconds, so a picture saved in
 * Photoshop shows there by itself. Play uses it on its next launch.
 *
 * Requests must carry the token the page was opened with, and name the
 * server by its address, so no other page can act through it. */
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <shellapi.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/wo_fs.h"
#include "core/wo_hash.h"
#include "core/wo_json.h"
#include "core/wo_png.h"
#include "tools/build_tool.h"

#define FIRST_PORT 8097
#define PORTS_TRIED 20
#define REQUEST_MAX 16384
#define MASTERS "manifests/auth-masters.json"
#define CATALOG "manifests/hd-textures.json"
#define ORIGINALS "wobble/textures/originals"
#define AUTHORED "assets/auth"
#define UNUSED "assets/auth-unused"
#define UPSCALED "assets/hd"
#define PAGE "tools/auth/index.html"
#define SKY_PAGE "tools/auth/sky.html"
/* Pictures of different sizes are the same picture when, both shrunk to half
 * the smaller one's size, they differ by at most MEAN_LIMIT levels on average
 * and their texels go together (correlation) by at least LIKENESS. Measured
 * over the game's pictures: copies differ by 6.4 at most and correlate by
 * 0.89 at least; different pictures that come as close correlate by under
 * 0.5, but for one (8.4 apart). */
#define MEAN_LIMIT 7.0
#define LIKENESS 0.85
#define FLAT_LIMIT 1.0 /* for a picture of one colour, which has no correlation */

typedef struct Picture {
    char name[128];
    uint32_t width, height;
    bool original; /* the game's own picture is in ORIGINALS */
    bool required; /* Play needs authored art for it (the HD fonts, HUD, menus) */
    WoImage pixels; /* while groups are found */
    int group;      /* index into Studio.groups, or -1 */
} Picture;

typedef struct Studio {
    Picture *pictures;
    size_t count;
    WoJson *apart;    /* names never grouped */
    WoJson *grouped;  /* the groups found: an array of arrays of names */
    char token[33];
    char host[64];
} Studio;

/* --- Names and files ----------------------------------------------------------------- */

static bool safe_name(const char *name) {
    size_t n = strlen(name);
    if (n < 5 || n >= 128 || !wo_str_iends_with(name, ".png")) {
        return false;
    }
    for (size_t i = 0; i < n; i++) {
        char c = name[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
              c == '-' || c == '_' || c == '.')) {
            return false;
        }
    }
    return strstr(name, "..") == NULL;
}

static char *in(const char *folder, const char *name) {
    return wo_path_join(folder, name, NULL);
}

static bool file_in(const char *folder, const char *name) {
    char *path = in(folder, name);
    bool there = path && wo_is_file(path);
    wo_free(path);
    return there;
}

static uint64_t mtime_in(const char *folder, const char *name) {
    char *path = in(folder, name);
    WoFileInfo info = {0};
    bool ok = path && wo_stat(path, &info, NULL) && info.exists;
    wo_free(path);
    return ok ? info.mtime : 0;
}

static Picture *find_picture(Studio *s, const char *name) {
    for (size_t i = 0; i < s->count; i++) {
        if (wo_str_ieq(s->pictures[i].name, name)) {
            return &s->pictures[i];
        }
    }
    return NULL;
}

static bool is_apart(const Studio *s, const char *name) {
    for (size_t i = 0; i < wo_json_length(s->apart); i++) {
        if (wo_str_ieq(wo_json_as_str(wo_json_at(s->apart, i), ""), name)) {
            return true;
        }
    }
    return false;
}

/* --- The pictures and their groups ----------------------------------------------------- */

/* Every catalogued picture the game has, or that is authored already. */
static bool load_pictures(Studio *s, WoError *error) {
    WoJson *catalog = NULL;
    if (!wo_read_json(CATALOG, 32u << 20, &catalog, error)) {
        return false;
    }
    size_t total = wo_json_length(catalog);
    s->pictures = wo_alloc_array(total ? total : 1, sizeof(Picture), error);
    bool ok = s->pictures != NULL;
    for (size_t i = 0; ok && i < total; i++) {
        const char *name = wo_json_str(wo_json_at(catalog, i), "file", "");
        if (!safe_name(name) || find_picture(s, name)) {
            continue;
        }
        bool original = file_in(ORIGINALS, name);
        if (!original && !file_in(AUTHORED, name)) {
            continue;
        }
        Picture *p = &s->pictures[s->count];
        memset(p, 0, sizeof(*p));
        snprintf(p->name, sizeof(p->name), "%s", name);
        p->original = original;
        p->required = wo_json_bool(wo_json_at(catalog, i), "required", false);
        p->group = -1;
        char *path = in(original ? ORIGINALS : AUTHORED, name);
        if (path && wo_png_read_size(path, &p->width, &p->height, NULL)) {
            s->count++;
        }
        wo_free(path);
    }
    wo_json_free(catalog);
    return ok;
}

/* `image` averaged down to w x h, as premultiplied RGB. */
static void shrink(const WoImage *image, uint32_t w, uint32_t h, double *out) {
    for (uint32_t y = 0; y < h; y++) {
        uint32_t y0 = (uint32_t)((uint64_t)y * image->height / h);
        uint32_t y1 = (uint32_t)((uint64_t)(y + 1) * image->height / h);
        y1 = y1 > y0 ? y1 : y0 + 1;
        for (uint32_t x = 0; x < w; x++) {
            uint32_t x0 = (uint32_t)((uint64_t)x * image->width / w);
            uint32_t x1 = (uint32_t)((uint64_t)(x + 1) * image->width / w);
            x1 = x1 > x0 ? x1 : x0 + 1;
            double sum[3] = {0, 0, 0};
            for (uint32_t sy = y0; sy < y1; sy++) {
                for (uint32_t sx = x0; sx < x1; sx++) {
                    const uint8_t *t = image->pixels + ((size_t)sy * image->width + sx) * 4;
                    for (int c = 0; c < 3; c++) {
                        sum[c] += t[c] * (t[3] / 255.0);
                    }
                }
            }
            double n = (double)(y1 - y0) * (x1 - x0);
            for (int c = 0; c < 3; c++) {
                out[((size_t)y * w + x) * 3 + c] = sum[c] / n;
            }
        }
    }
}

/* Whether `big` is the picture `small` is: the same exactly at one size, or
 * in the same proportions and alike at another (see MEAN_LIMIT). */
static bool same_picture(const WoImage *small, const WoImage *big) {
    if (!small->pixels || !big->pixels) {
        return false;
    }
    if (small->width == big->width && small->height == big->height) {
        return memcmp(small->pixels, big->pixels, (size_t)small->width * small->height * 4) == 0;
    }
    if ((uint64_t)small->width * big->height != (uint64_t)big->width * small->height) {
        return false;
    }
    uint32_t w = small->width >= 16 ? small->width / 2 : small->width;
    uint32_t h = small->height >= 16 ? small->height / 2 : small->height;
    size_t n = (size_t)w * h * 3;
    double *a = wo_alloc_array(n, sizeof(double), NULL);
    double *b = wo_alloc_array(n, sizeof(double), NULL);
    bool same = false;
    if (a && b) {
        shrink(small, w, h, a);
        shrink(big, w, h, b);
        double diff = 0, sa = 0, sb = 0;
        for (size_t i = 0; i < n; i++) {
            diff += fabs(a[i] - b[i]);
            sa += a[i];
            sb += b[i];
        }
        double mean = diff / (double)n;
        double ma = sa / (double)n, mb = sb / (double)n;
        double cov = 0, va = 0, vb = 0;
        for (size_t i = 0; i < n; i++) {
            cov += (a[i] - ma) * (b[i] - mb);
            va += (a[i] - ma) * (a[i] - ma);
            vb += (b[i] - mb) * (b[i] - mb);
        }
        if (va <= 0 || vb <= 0) {
            same = mean <= FLAT_LIMIT;
        } else {
            same = mean <= MEAN_LIMIT && cov / sqrt(va * vb) >= LIKENESS;
        }
    }
    wo_free(a);
    wo_free(b);
    return same;
}

static int by_area(const void *a, const void *b) {
    const Picture *const *pa = a;
    const Picture *const *pb = b;
    uint64_t x = (uint64_t)(*pa)->width * (*pa)->height;
    uint64_t y = (uint64_t)(*pb)->width * (*pb)->height;
    return x < y ? -1 : x > y ? 1 : strcmp((*pa)->name, (*pb)->name);
}

/* The groups: each picture joins the first group whose smallest picture it
 * is (same_picture); names set apart stay alone. */
static bool find_groups(Studio *s, WoError *error) {
    Picture **order = wo_alloc_array(s->count ? s->count : 1, sizeof(Picture *), error);
    if (!order) {
        return false;
    }
    for (size_t i = 0; i < s->count; i++) {
        Picture *p = &s->pictures[i];
        p->group = -1;
        order[i] = p;
        if (p->original && !p->pixels.pixels) {
            char *path = in(ORIGINALS, p->name);
            if (path) {
                wo_png_read(path, WO_PIXELS_RGBA, &p->pixels, NULL);
            }
            wo_free(path);
        }
    }
    qsort(order, s->count, sizeof(Picture *), by_area);
    Picture **heads = wo_alloc_array(s->count ? s->count : 1, sizeof(Picture *), error);
    size_t head_count = 0;
    bool ok = heads != NULL;
    for (size_t i = 0; ok && i < s->count; i++) {
        Picture *p = order[i];
        if (!p->pixels.pixels || is_apart(s, p->name)) {
            continue;
        }
        for (size_t h = 0; h < head_count && p->group < 0; h++) {
            if (same_picture(&heads[h]->pixels, &p->pixels)) {
                p->group = heads[h]->group;
            }
        }
        if (p->group < 0) {
            p->group = (int)head_count;
            heads[head_count++] = p;
        }
    }
    /* Into the manifest's form: groups of two or more, smallest first. */
    WoJson *grouped = ok ? wo_json_new_array() : NULL;
    ok = ok && grouped;
    for (size_t h = 0; ok && h < head_count; h++) {
        WoJson *names = wo_json_new_array();
        for (size_t i = 0; names && i < s->count; i++) {
            if (order[i]->group == (int)h) {
                wo_json_push_str(names, order[i]->name);
            }
        }
        if (names && wo_json_length(names) > 1) {
            ok = wo_json_push(grouped, names);
        } else {
            wo_json_free(names);
        }
    }
    /* Group numbers as the manifest has them; alone: -1. */
    for (size_t i = 0; ok && i < s->count; i++) {
        s->pictures[i].group = -1;
    }
    for (size_t g = 0; ok && g < wo_json_length(grouped); g++) {
        const WoJson *names = wo_json_at(grouped, g);
        for (size_t i = 0; i < wo_json_length(names); i++) {
            Picture *p = find_picture(s, wo_json_as_str(wo_json_at(names, i), ""));
            if (p) {
                p->group = (int)g;
            }
        }
    }
    if (ok) {
        wo_json_free(s->grouped);
        s->grouped = grouped;
    } else {
        wo_json_free(grouped);
        if (!wo_error_is_set(error)) {
            wo_fail(error, WO_ERR_INTERNAL, "Out of memory.");
        }
    }
    wo_free(heads);
    wo_free(order);
    return ok;
}

static bool save_masters(const Studio *s, WoError *error) {
    WoJson *root = wo_json_new_object();
    bool ok = root && wo_json_put_str(root, "format", "wo4k.auth-masters") &&
              wo_json_put_int(root, "version", 1) &&
              wo_json_put_str(root, "note",
                              "Names that are one picture. Play shows the authored file of any "
                              "of a group in the places of all of it. Made by tools\\auth.cmd.") &&
              wo_json_put(root, "groups", wo_json_clone(s->grouped)) &&
              wo_json_put(root, "apart", wo_json_clone(s->apart));
    ok = ok || wo_fail(error, WO_ERR_INTERNAL, "Out of memory.");
    bool written = false;
    ok = ok && wo_write_json_if_changed(MASTERS, root, &written, error);
    wo_json_free(root);
    return ok;
}

/* The names of p's group, p alone when it has none. */
static size_t members(Studio *s, Picture *p, Picture **out, size_t cap) {
    size_t n = 0;
    if (p->group < 0) {
        out[n++] = p;
        return n;
    }
    const WoJson *names = wo_json_at(s->grouped, (size_t)p->group);
    for (size_t i = 0; i < wo_json_length(names) && n < cap; i++) {
        Picture *m = find_picture(s, wo_json_as_str(wo_json_at(names, i), ""));
        if (m) {
            out[n++] = m;
        }
    }
    return n;
}

/* --- What the page sees ----------------------------------------------------------------- */

static const char *const TRACKS[][2] = {
    {"track01", "Talon's Reach"}, {"track08", "Sagarmatha"},   {"track13", "Valparaiso"},
    {"track20", "Phenitia Park"}, {"track02", "Gare d'Europa"}, {"track17", "Odessa Keys"},
    {"track06", "Vostok Island"}, {"track07", "Spilskinanke"}, {"track04", "Track 04"}};

static WoJson *picture_json(const Picture *p) {
    WoJson *o = wo_json_new_object();
    if (!o) {
        return NULL;
    }
    wo_json_put_str(o, "name", p->name);
    wo_json_put_int(o, "w", p->width);
    wo_json_put_int(o, "h", p->height);
    wo_json_put_bool(o, "original", p->original);
    /* Authored art that cannot be taken away: Play needs it, or the game
     * has no picture of its own to go back to. */
    wo_json_put_bool(o, "keep", p->required || !p->original);
    uint64_t authored = mtime_in(AUTHORED, p->name);
    wo_json_put_bool(o, "authored", authored != 0);
    wo_json_put_int(o, "v", (int64_t)(authored % 1000000007ull));
    wo_json_put_bool(o, "apart", false);
    return o;
}

static WoJson *state_json(Studio *s) {
    WoJson *root = wo_json_new_object();
    WoJson *groups = wo_json_new_array();
    WoJson *tracks = wo_json_new_array();
    if (!root || !groups || !tracks) {
        wo_json_free(root);
        wo_json_free(groups);
        wo_json_free(tracks);
        return NULL;
    }
    for (size_t t = 0; t < WO_COUNT(TRACKS); t++) {
        WoJson *track = wo_json_new_array();
        wo_json_push_str(track, TRACKS[t][0]);
        wo_json_push_str(track, TRACKS[t][1]);
        wo_json_push(tracks, track);
    }
    bool *done = wo_alloc_array(s->count ? s->count : 1, sizeof(bool), NULL);
    for (size_t i = 0; done && i < s->count; i++) {
        if (done[i]) {
            continue;
        }
        Picture *list[256];
        size_t n = members(s, &s->pictures[i], list, WO_COUNT(list));
        WoJson *group = wo_json_new_object();
        WoJson *names = wo_json_new_array();
        size_t authored = 0;
        for (size_t k = 0; k < n; k++) {
            done[list[k] - s->pictures] = true;
            WoJson *pj = picture_json(list[k]);
            if (pj && is_apart(s, list[k]->name)) {
                wo_json_put_bool(pj, "apart", true);
            }
            authored += wo_json_bool(pj, "authored", false);
            wo_json_push(names, pj);
        }
        wo_json_put(group, "names", names);
        wo_json_put_int(group, "authored", (int64_t)authored);
        wo_json_push(groups, group);
    }
    wo_free(done);
    wo_json_put(root, "groups", groups);
    wo_json_put(root, "tracks", tracks);
    return root;
}

/* --- Actions --------------------------------------------------------------------------- */

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

static bool act(Studio *s, const char *action, const char *name, WoError *error) {
    Picture *p = find_picture(s, name);
    if (!p) {
        return wo_fail(error, WO_ERR_NOT_FOUND, "No texture is called %s.", name);
    }
    Picture *list[256];
    size_t n = members(s, p, list, WO_COUNT(list));
    if (wo_str_eq(action, "author")) {
        for (size_t k = 0; k < n; k++) {
            if (file_in(AUTHORED, list[k]->name)) {
                return true; /* authored already */
            }
        }
        /* The best picture to start from: upscaled, else the game's own. */
        char *from = NULL;
        for (size_t k = 0; !from && k < n; k++) {
            if (file_in(UPSCALED, list[k]->name)) {
                from = in(UPSCALED, list[k]->name);
            }
        }
        for (size_t k = 0; !from && k < n; k++) {
            if (list[k]->original) {
                from = in(ORIGINALS, list[k]->name);
            }
        }
        char *to = in(AUTHORED, p->name);
        bool ok = from && to && wo_mkdirs(AUTHORED, error) && wo_copy_file(from, to, error);
        if (!from) {
            ok = wo_fail(error, WO_ERR_NOT_FOUND, "There is no picture of %s to start from.", name);
        }
        wo_free(from);
        wo_free(to);
        return ok;
    }
    if (wo_str_eq(action, "unauthor")) {
        for (size_t k = 0; k < n; k++) {
            if (list[k]->required || !list[k]->original) {
                return wo_fail(error, WO_ERR_FORMAT,
                               "%s stays authored: the game has no picture of its own for it.",
                               list[k]->name);
            }
        }
        bool ok = true;
        for (size_t k = 0; ok && k < n; k++) {
            if (file_in(AUTHORED, list[k]->name)) {
                ok = move_unused(list[k]->name, error);
            }
        }
        return ok;
    }
    if (wo_str_eq(action, "keep")) { /* this one for the whole group */
        bool ok = file_in(AUTHORED, name);
        for (size_t k = 0; ok && k < n; k++) {
            if (list[k] != p && file_in(AUTHORED, list[k]->name)) {
                ok = move_unused(list[k]->name, error);
            }
        }
        return ok;
    }
    if (wo_str_eq(action, "edit") || wo_str_eq(action, "reveal")) {
        const char *own = NULL;
        for (size_t k = 0; !own && k < n; k++) {
            own = file_in(AUTHORED, list[k]->name) ? list[k]->name : NULL;
        }
        char *path = own ? in(AUTHORED, own) : NULL;
        if (path) {
            open_file(path, wo_str_eq(action, "reveal"));
        }
        wo_free(path);
        return true;
    }
    if (wo_str_eq(action, "split") || wo_str_eq(action, "rejoin")) {
        bool split = wo_str_eq(action, "split");
        if (split && !is_apart(s, name)) {
            wo_json_push_str(s->apart, name);
        }
        for (size_t i = 0; !split && i < wo_json_length(s->apart); i++) {
            if (wo_str_ieq(wo_json_as_str(wo_json_at(s->apart, i), ""), name)) {
                WoJson *kept = wo_json_new_array();
                for (size_t j = 0; kept && j < wo_json_length(s->apart); j++) {
                    if (j != i) {
                        wo_json_push(kept, wo_json_clone(wo_json_at(s->apart, j)));
                    }
                }
                wo_json_free(s->apart);
                s->apart = kept ? kept : wo_json_new_array();
                break;
            }
        }
        return find_groups(s, error) && save_masters(s, error);
    }
    return wo_fail(error, WO_ERR_FORMAT, "Unknown action %s.", action);
}

/* --- HTTP ------------------------------------------------------------------------------ */

static void send_all(SOCKET c, const void *data, size_t len) {
    const char *p = data;
    while (len) {
        int n = send(c, p, len > 65536 ? 65536 : (int)len, 0);
        if (n <= 0) {
            return;
        }
        p += n;
        len -= (size_t)n;
    }
}

static void respond(SOCKET c, int status, const char *type, const void *body, size_t len,
                    bool cache) {
    char head[512];
    int n = snprintf(head, sizeof(head),
                     "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %u\r\n"
                     "Cache-Control: %s\r\nX-Content-Type-Options: nosniff\r\n"
                     "Connection: close\r\n\r\n",
                     status, status == 200 ? "OK" : status == 403 ? "Forbidden" : "Not Found", type,
                     (unsigned)len, cache ? "max-age=86400" : "no-store");
    send_all(c, head, (size_t)n);
    send_all(c, body, len);
}

static void respond_json(SOCKET c, const WoJson *json) {
    WoBuf out = {0};
    if (json && wo_json_write(json, false, &out, NULL)) {
        respond(c, 200, "application/json", out.data, out.len, false);
    } else {
        respond(c, 404, "text/plain", "no", 2, false);
    }
    wo_buf_free(&out);
}

/* The value of `key` in the query, URL-decoded, into out. */
static bool query(const char *q, const char *key, char *out, size_t size) {
    size_t klen = strlen(key);
    for (const char *p = q; p && *p;) {
        if (!strncmp(p, key, klen) && p[klen] == '=') {
            const char *v = p + klen + 1;
            size_t n = 0;
            while (*v && *v != '&' && n + 1 < size) {
                if (*v == '%' && v[1] && v[2]) {
                    char hex[3] = {v[1], v[2], 0};
                    out[n++] = (char)strtol(hex, NULL, 16);
                    v += 3;
                } else {
                    out[n++] = *v == '+' ? ' ' : *v;
                    v++;
                }
            }
            out[n] = 0;
            return true;
        }
        p = strchr(p, '&');
        p = p ? p + 1 : NULL;
    }
    return false;
}

static void serve_file(SOCKET c, const char *folder, const char *name) {
    WoBuf data = {0};
    char *path = safe_name(name) ? in(folder, name) : NULL;
    if (path && wo_read_file(path, 64u << 20, &data, NULL)) {
        respond(c, 200, "image/png", data.data, data.len, true);
    } else {
        respond(c, 404, "text/plain", "missing", 7, false);
    }
    wo_buf_free(&data);
    wo_free(path);
}

/* A page of the studio, with the token for its own requests. */
static void serve_page(SOCKET c, const char *file, const char *token) {
    char *page = NULL;
    if (!wo_read_text(file, 4u << 20, &page, NULL)) {
        respond(c, 404, "text/plain", "page missing", 12, false);
        return;
    }
    WoBuf out = {0};
    const char *mark = strstr(page, "__TOKEN__");
    if (mark) {
        wo_buf_append(&out, page, (size_t)(mark - page), NULL);
        wo_buf_append_str(&out, token, NULL);
        wo_buf_append_str(&out, mark + 9, NULL);
    } else {
        wo_buf_append_str(&out, page, NULL);
    }
    respond(c, 200, "text/html; charset=utf-8", out.data, out.len, false);
    wo_buf_free(&out);
    wo_free(page);
}

/* The sky page's requests: its state, a sky's faces laid out, and actions,
 * which answer with the state (and what went wrong). */
static void handle_sky(SOCKET c, const char *method, const char *target, const char *q) {
    char track[16] = {0}, around[8] = {0}, turn[32] = {0}, top[32] = {0}, bottom[32] = {0};
    query(q, "track", track, sizeof(track));
    query(q, "around", around, sizeof(around));
    query(q, "turn", turn, sizeof(turn));
    query(q, "top", top, sizeof(top));
    query(q, "bottom", bottom, sizeof(bottom));
    char flat[4] = {0};
    query(q, "flat", flat, sizeof(flat));
    bool flat_laid = flat[0] == '1';
    int times = atoi(around);
    double degrees = turn[0] ? strtod(turn, NULL) : 0;
    double high = top[0] ? strtod(top, NULL) : NAN;
    double low = bottom[0] ? strtod(bottom, NULL) : NAN;
    WoError error = {0};
    WoJson *json = NULL;
    if (!strcmp(target, "/skypic")) { /* the game's own picture of a sky */
        char *path = NULL;
        WoBuf data = {0};
        if (sky_game_picture(track, &path, &error) && wo_read_file(path, 64u << 20, &data, NULL)) {
            respond(c, 200, "image/png", data.data, data.len, false);
        } else {
            respond(c, 404, "text/plain", "missing", 7, false);
        }
        wo_buf_free(&data);
        wo_free(path);
        return;
    }
    if (!strcmp(target, "/skystate")) {
        sky_state(&json, &error);
    } else if (!strcmp(target, "/skyfaces")) {
        sky_faces(track, flat_laid, times, degrees, high, low, &json, &error);
    } else if (!strcmp(target, "/skyact") && !strcmp(method, "POST")) {
        char action[16] = {0};
        bool ok = query(q, "a", action, sizeof(action)) &&
                  sky_act(action, track, flat_laid, times, degrees, high, low, &error);
        sky_state(&json, NULL);
        if (json && !ok) {
            wo_json_put_str(json, "error",
                            wo_error_is_set(&error) ? error.message : "That cannot be done.");
        }
        if (!ok) {
            printf("  sky %s %s: %s\n", action, track, error.message);
        } else if (strcmp(action, "edit") && strcmp(action, "reveal")) {
            printf("  sky %s %s (%d round, from %.1f)\n", action, track, times, degrees);
        }
        fflush(stdout);
    }
    respond_json(c, json);
    wo_json_free(json);
}

/* A picture dropped on the sky page: the request's body, read whole. */
#define DROP_MAX (128u << 20)

static void drop_sky(SOCKET c, const char *request, size_t got, const char *q) {
    char track[16] = {0};
    query(q, "track", track, sizeof(track));
    const char *end = strstr(request, "\r\n\r\n");
    const char *length = strstr(request, "\r\nContent-Length: ");
    size_t want = length && end && length < end ? strtoul(length + 18, NULL, 10) : 0;
    WoError error = {0};
    uint8_t *body = end && want > 0 && want <= DROP_MAX ? wo_alloc(want, &error) : NULL;
    size_t have = 0;
    if (body) {
        size_t start = (size_t)(end + 4 - request);
        have = got > start ? got - start : 0;
        have = have > want ? want : have;
        memcpy(body, request + start, have);
        while (have < want) {
            size_t chunk = want - have > 65536 ? 65536 : want - have;
            int n = recv(c, (char *)body + have, (int)chunk, 0);
            if (n <= 0) {
                break;
            }
            have += (size_t)n;
        }
    }
    bool ok = body && have == want && sky_drop(track, body, want, &error);
    if (!ok && !wo_error_is_set(&error)) {
        wo_fail(&error, WO_ERR_FORMAT, "The picture did not arrive whole (at most 128 MB).");
    }
    wo_free(body);
    WoJson *json = NULL;
    sky_state(&json, NULL);
    if (json && !ok) {
        wo_json_put_str(json, "error", error.message);
    }
    if (ok) {
        printf("  sky picture dropped on %s\n", track);
    } else {
        printf("  sky drop %s: %s\n", track, error.message);
    }
    fflush(stdout);
    respond_json(c, json);
    wo_json_free(json);
}

static void handle(Studio *s, SOCKET c) {
    char request[REQUEST_MAX + 1];
    size_t got = 0;
    while (got < REQUEST_MAX) {
        int n = recv(c, request + got, (int)(REQUEST_MAX - got), 0);
        if (n <= 0) {
            break;
        }
        got += (size_t)n;
        request[got] = 0;
        if (strstr(request, "\r\n\r\n")) {
            break;
        }
    }
    request[got] = 0;
    char method[8] = {0}, target[2048] = {0};
    if (sscanf(request, "%7s %2047s", method, target) != 2) {
        return;
    }
    /* Only this server, by its own address. */
    const char *host = strstr(request, "\r\nHost: ");
    if (!host || strncmp(host + 8, s->host, strlen(s->host)) ||
        (host[8 + strlen(s->host)] != '\r')) {
        respond(c, 403, "text/plain", "host", 4, false);
        return;
    }
    char *q = strchr(target, '?');
    if (q) {
        *q++ = 0;
    }
    char token[64] = {0};
    bool trusted = q && query(q, "t", token, sizeof(token)) && !strcmp(token, s->token);
    if ((!strcmp(target, "/") || !strcmp(target, "/sky")) && !strcmp(method, "GET")) {
        serve_page(c, strcmp(target, "/") ? SKY_PAGE : PAGE, s->token);
        return;
    }
    if (!trusted) {
        respond(c, 403, "text/plain", "token", 5, false);
        return;
    }
    char name[160] = {0};
    if (!strncmp(target, "/o/", 3)) {
        serve_file(c, ORIGINALS, target + 3);
    } else if (!strncmp(target, "/a/", 3)) {
        serve_file(c, AUTHORED, target + 3);
    } else if (!strcmp(target, "/skydrop") && !strcmp(method, "POST")) {
        drop_sky(c, request, got, q);
    } else if (!strncmp(target, "/sky", 4)) {
        handle_sky(c, method, target, q);
    } else if (!strcmp(target, "/state")) {
        WoJson *state = state_json(s);
        respond_json(c, state);
        wo_json_free(state);
    } else if (!strcmp(target, "/act") && !strcmp(method, "POST")) {
        char action[32] = {0};
        WoError error = {0};
        bool ok = query(q, "a", action, sizeof(action)) && query(q, "n", name, sizeof(name)) &&
                  safe_name(name) && act(s, action, name, &error);
        WoJson *state = state_json(s);
        if (state && !ok) {
            wo_json_put_str(state, "error",
                            wo_error_is_set(&error) ? error.message : "That cannot be done.");
        }
        respond_json(c, state);
        wo_json_free(state);
        if (!ok) {
            printf("  %s %s: %s\n", action, name, error.message);
        } else if (strcmp(action, "edit") && strcmp(action, "reveal")) {
            printf("  %s %s\n", action, name);
        }
        fflush(stdout);
    } else {
        respond(c, 404, "text/plain", "not here", 8, false);
    }
}

bool build_auth(const char *port_text, bool sky, WoError *error) {
    if (!wo_is_file(CATALOG) || !wo_is_file(PAGE) || !wo_is_file(SKY_PAGE)) {
        return wo_fail(error, WO_ERR_NOT_FOUND, "Run this from the mod folder.");
    }
    if (!wo_is_dir(ORIGINALS)) {
        return wo_fail(error, WO_ERR_NOT_FOUND,
                       "There are no textures in %s yet. Run Setup or Play once.", ORIGINALS);
    }
    Studio s;
    memset(&s, 0, sizeof(s));
    uint8_t random[16];
    if (!wo_random_bytes(random, sizeof(random), error)) {
        return false;
    }
    for (int i = 0; i < 16; i++) {
        snprintf(s.token + i * 2, 3, "%02x", random[i]);
    }
    WoJson *old = NULL;
    if (wo_is_file(MASTERS)) {
        wo_read_json(MASTERS, 16u << 20, &old, NULL);
    }
    s.apart = wo_json_clone(wo_json_get(old, "apart"));
    wo_json_free(old);
    if (!s.apart) {
        s.apart = wo_json_new_array();
    }
    printf("Finding the textures that are one picture...\n");
    fflush(stdout);
    bool ok = load_pictures(&s, error) && find_groups(&s, error) && save_masters(&s, error);
    for (size_t i = 0; i < s.count; i++) {
        wo_image_free(&s.pictures[i].pixels);
    }
    if (!ok) {
        wo_json_free(s.apart);
        wo_json_free(s.grouped);
        wo_free(s.pictures);
        return false;
    }
    size_t copies = 0;
    for (size_t g = 0; g < wo_json_length(s.grouped); g++) {
        copies += wo_json_length(wo_json_at(s.grouped, g)) - 1;
    }
    printf("%zu textures; %zu groups of copies (%zu copies). Groups: %s\n", s.count,
           wo_json_length(s.grouped), copies, MASTERS);

    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa)) {
        ok = wo_fail(error, WO_ERR_IO, "Windows sockets are not available.");
    }
    SOCKET listener = ok ? socket(AF_INET, SOCK_STREAM, IPPROTO_TCP) : INVALID_SOCKET;
    int port = port_text ? atoi(port_text) : FIRST_PORT;
    bool bound = false;
    if (listener != INVALID_SOCKET) {
        BOOL exclusive = TRUE;
        setsockopt(listener, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (const char *)&exclusive,
                   sizeof(exclusive));
        for (int tries = 0; !bound && tries < PORTS_TRIED; tries++, port++) {
            struct sockaddr_in addr;
            memset(&addr, 0, sizeof(addr));
            addr.sin_family = AF_INET;
            addr.sin_port = htons((u_short)port);
            addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            bound = bind(listener, (struct sockaddr *)&addr, sizeof(addr)) == 0;
            if (bound) {
                break;
            }
        }
    }
    if (ok && (!bound || listen(listener, 64))) {
        ok = wo_fail(error, WO_ERR_IO, "No port from %d was free.", FIRST_PORT);
    }
    if (ok) {
        snprintf(s.host, sizeof(s.host), "127.0.0.1:%d", port);
        char url[160];
        snprintf(url, sizeof(url), "http://%s/%s?t=%s", s.host, sky ? "sky" : "", s.token);
        printf("\nThe %s page is at %s\nClose this window to stop.\n\n",
               sky ? "sky authoring" : "authoring", url);
        fflush(stdout);
        wchar_t wide[160];
        MultiByteToWideChar(CP_UTF8, 0, url, -1, wide, 160);
        if (!getenv("WO4K_AUTH_NO_BROWSER")) { /* for tests */
            ShellExecuteW(NULL, L"open", wide, NULL, NULL, SW_SHOWNORMAL);
        }
        for (;;) {
            SOCKET c = accept(listener, NULL, NULL);
            if (c == INVALID_SOCKET) {
                break;
            }
            handle(&s, c);
            shutdown(c, SD_SEND);
            closesocket(c);
        }
    }
    if (listener != INVALID_SOCKET) {
        closesocket(listener);
    }
    WSACleanup();
    wo_json_free(s.apart);
    wo_json_free(s.grouped);
    wo_free(s.pictures);
    return ok;
}
