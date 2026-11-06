#include "core/wo_release.h"

#include <string.h>

#define URL_MAX 2048
#define RELEASE_FILE_MAX_BYTES (256ull << 20)
#define RELEASE_MAX_FILES 2000

/* --- URLs ---------------------------------------------------------------------- */

static bool url_char_ok(unsigned char c) {
    return c > 0x20 && c < 0x7F && c != '"' && c != '<' && c != '>' && c != '\\' && c != '#';
}

static bool host_char_ok(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
           c == '.' || c == '-';
}

void wo_url_free(WoUrl *url) {
    wo_free(url->host);
    wo_free(url->path);
    wo_free(url->query);
    memset(url, 0, sizeof(*url));
}

bool wo_url_parse(const char *text, WoUrl *url) {
    memset(url, 0, sizeof(*url));
    size_t len = strlen(text);
    if (len == 0 || len > URL_MAX || !wo_str_istarts_with(text, "https://")) {
        return false;
    }
    for (size_t i = 0; i < len; i++) {
        if (!url_char_ok((unsigned char)text[i])) {
            return false;
        }
    }
    const char *authority = text + 8;
    size_t authority_len = strcspn(authority, "/?");
    if (authority_len == 0 || memchr(authority, '@', authority_len)) {
        return false;
    }
    const char *colon = memchr(authority, ':', authority_len);
    size_t host_len = colon ? (size_t)(colon - authority) : authority_len;
    if (colon) {
        size_t port_len = authority_len - host_len - 1;
        if (port_len != 3 || memcmp(colon + 1, "443", 3) != 0) {
            return false;
        }
    }
    if (host_len == 0 || authority[0] == '.' || authority[host_len - 1] == '.') {
        return false;
    }
    for (size_t i = 0; i < host_len; i++) {
        if (!host_char_ok(authority[i])) {
            return false;
        }
    }
    const char *rest = authority + authority_len;
    size_t path_len = strcspn(rest, "?");
    url->host = wo_strndup(authority, host_len, NULL);
    url->path = path_len ? wo_strndup(rest, path_len, NULL) : wo_strdup("/", NULL);
    url->query = wo_strdup(rest[path_len] == '?' ? rest + path_len + 1 : "", NULL);
    if (!url->host || !url->path || !url->query) {
        wo_url_free(url);
        return false;
    }
    for (char *p = url->host; *p; p++) {
        *p = wo_ascii_lower(*p);
    }
    return true;
}

static bool segment_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
           c == '_' || c == '.' || c == '-';
}

/* Split "/a/b/c/" into up to max segments of [A-Za-z0-9_.-]+; a single
 * trailing slash is allowed. Returns the count, or -1 when malformed. */
static int path_segments(const char *path, const char *segments[], size_t lengths[], int max) {
    if (path[0] != '/') {
        return -1;
    }
    int count = 0;
    const char *at = path + 1;
    while (*at) {
        const char *start = at;
        while (*at && *at != '/') {
            if (!segment_char(*at)) {
                return -1;
            }
            at++;
        }
        if (at == start) {
            return -1;
        }
        if (count == max) {
            return -1;
        }
        segments[count] = start;
        lengths[count] = (size_t)(at - start);
        count++;
        if (*at == '/') {
            at++;
        }
    }
    return count;
}

static bool segment_is(const char *segment, size_t len, const char *word) {
    return strlen(word) == len && strncmp(segment, word, len) == 0;
}

static char *url_text(const WoUrl *url) {
    return wo_format(NULL, "https://%s%s%s%s", url->host, url->path, url->query[0] ? "?" : "",
                     url->query);
}

char *wo_release_endpoint(const char *text) {
    WoUrl url;
    if (!wo_url_parse(text, &url)) {
        return NULL;
    }
    const char *segments[6];
    size_t lengths[6];
    int count = path_segments(url.path, segments, lengths, 6);
    char *endpoint = NULL;
    if (wo_str_eq(url.host, "github.com") && (count == 3 || count == 4) &&
        segment_is(segments[2], lengths[2], "releases") &&
        (count == 3 || segment_is(segments[3], lengths[3], "latest"))) {
        endpoint = wo_format(NULL, "https://api.github.com/repos/%.*s/%.*s/releases/latest",
                             (int)lengths[0], segments[0], (int)lengths[1], segments[1]);
    } else if (wo_str_eq(url.host, "api.github.com") && count == 5 && !url.query[0] &&
               segment_is(segments[0], lengths[0], "repos") &&
               segment_is(segments[3], lengths[3], "releases") &&
               segment_is(segments[4], lengths[4], "latest")) {
        endpoint = url_text(&url);
    } else if (wo_str_iends_with(url.path, ".zip")) {
        endpoint = url_text(&url);
    }
    wo_url_free(&url);
    return endpoint;
}

bool wo_release_endpoint_is_zip(const char *endpoint) {
    WoUrl url;
    if (!wo_url_parse(endpoint, &url)) {
        return false;
    }
    bool zip = wo_str_iends_with(url.path, ".zip");
    wo_url_free(&url);
    return zip;
}

char *wo_link_from_text(const char *text, WoLinkKind kind) {
    if (strlen(text) > 4096) {
        return NULL;
    }
    const char *start = text;
    if ((unsigned char)start[0] == 0xEF && (unsigned char)start[1] == 0xBB &&
        (unsigned char)start[2] == 0xBF) {
        start += 3;
    }
    while (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n') {
        start++;
    }
    size_t len = strlen(start);
    while (len > 0 && (start[len - 1] == ' ' || start[len - 1] == '\t' ||
                       start[len - 1] == '\r' || start[len - 1] == '\n')) {
        len--;
    }
    char *value = wo_strndup(start, len, NULL);
    if (!value) {
        return NULL;
    }
    WoUrl url;
    char *result = NULL;
    if (wo_url_parse(value, &url)) {
        if (kind == WO_LINK_SOURCE) {
            const char *segments[3];
            size_t lengths[3];
            int count = path_segments(url.path, segments, lengths, 3);
            if (wo_str_eq(url.host, "github.com") && !url.query[0] && (count == 1 || count == 2)) {
                result = url_text(&url);
            }
        } else {
            char *endpoint = wo_release_endpoint(value);
            if (endpoint) {
                result = url_text(&url);
            }
            wo_free(endpoint);
        }
        wo_url_free(&url);
    }
    wo_free(value);
    return result;
}

/* --- Versions -------------------------------------------------------------------- */

typedef struct Version {
    const char *core[3];
    size_t core_len[3];
    const char *pre;
    size_t pre_len;
} Version;

static bool is_digit(char c) {
    return c >= '0' && c <= '9';
}

static bool ident_char(char c) {
    return is_digit(c) || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '.' ||
           c == '-';
}

static bool parse_version(const char *text, Version *v) {
    memset(v, 0, sizeof(*v));
    const char *at = text;
    if (*at == 'v') {
        at++;
    }
    for (int i = 0; i < 3; i++) {
        const char *start = at;
        while (is_digit(*at)) {
            at++;
        }
        if (at == start || at - start > 9) {
            return false;
        }
        v->core[i] = start;
        v->core_len[i] = (size_t)(at - start);
        if (i < 2) {
            if (*at != '.') {
                return false;
            }
            at++;
        }
    }
    if (*at == '-') {
        at++;
        v->pre = at;
        while (ident_char(*at)) {
            at++;
        }
        v->pre_len = (size_t)(at - v->pre);
        if (v->pre_len == 0) {
            return false;
        }
    }
    if (*at == '+') {
        at++;
        const char *start = at;
        while (ident_char(*at)) {
            at++;
        }
        if (at == start) {
            return false;
        }
    }
    return *at == 0;
}

/* Compare decimal digit strings of any length numerically. */
static int compare_digits(const char *a, size_t a_len, const char *b, size_t b_len) {
    while (a_len > 1 && *a == '0') {
        a++;
        a_len--;
    }
    while (b_len > 1 && *b == '0') {
        b++;
        b_len--;
    }
    if (a_len != b_len) {
        return a_len < b_len ? -1 : 1;
    }
    int order = memcmp(a, b, a_len);
    return order < 0 ? -1 : order > 0;
}

static bool all_digits(const char *s, size_t len) {
    for (size_t i = 0; i < len; i++) {
        if (!is_digit(s[i])) {
            return false;
        }
    }
    return len > 0;
}

bool wo_version_compare(const char *a, const char *b, int *order) {
    Version va;
    Version vb;
    if (!parse_version(a, &va) || !parse_version(b, &vb)) {
        return false;
    }
    for (int i = 0; i < 3; i++) {
        int c = compare_digits(va.core[i], va.core_len[i], vb.core[i], vb.core_len[i]);
        if (c) {
            *order = c;
            return true;
        }
    }
    if (!va.pre || !vb.pre) {
        *order = (!va.pre && !vb.pre) ? 0 : (!va.pre ? 1 : -1);
        return true;
    }
    const char *pa = va.pre;
    const char *pb = vb.pre;
    const char *ea = va.pre + va.pre_len;
    const char *eb = vb.pre + vb.pre_len;
    while (pa < ea && pb < eb) {
        const char *na = memchr(pa, '.', (size_t)(ea - pa));
        const char *nb = memchr(pb, '.', (size_t)(eb - pb));
        size_t la = na ? (size_t)(na - pa) : (size_t)(ea - pa);
        size_t lb = nb ? (size_t)(nb - pb) : (size_t)(eb - pb);
        bool da = all_digits(pa, la);
        bool db = all_digits(pb, lb);
        int c;
        if (da && db) {
            c = compare_digits(pa, la, pb, lb);
        } else if (da != db) {
            c = da ? -1 : 1;
        } else {
            size_t n = la < lb ? la : lb;
            c = memcmp(pa, pb, n);
            c = c < 0 ? -1 : c > 0 ? 1 : (la == lb ? 0 : (la < lb ? -1 : 1));
        }
        if (c) {
            *order = c;
            return true;
        }
        pa = na ? na + 1 : ea;
        pb = nb ? nb + 1 : eb;
    }
    *order = (pa < ea) ? 1 : (pb < eb) ? -1 : 0;
    return true;
}

/* --- GitHub release selection -------------------------------------------------------- */

static bool asset_name_matches(const char *name) {
    size_t len = strlen(name);
    if (len < 6 || !wo_str_iends_with(name, ".zip")) {
        return false;
    }
    size_t prefix;
    if (wo_str_istarts_with(name, "wo4k")) {
        prefix = 4;
    } else if (wo_str_istarts_with(name, "wo-beta-mod") || wo_str_istarts_with(name, "wo_beta_mod") ||
               wo_str_istarts_with(name, "wo-beta_mod") || wo_str_istarts_with(name, "wo_beta-mod")) {
        prefix = 11;
    } else {
        return false;
    }
    for (size_t i = prefix; i + 4 < len; i++) {
        char c = name[i];
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || is_digit(c) || c == '.' ||
                  c == '_' || c == '-';
        if (!ok) {
            return false;
        }
    }
    return true;
}

void wo_release_asset_free(WoReleaseAsset *asset) {
    wo_free(asset->url);
    wo_free(asset->sha256);
    wo_free(asset->checksum_url);
    wo_free(asset->name);
    memset(asset, 0, sizeof(*asset));
}

static char *https_url(const char *text) {
    WoUrl url;
    if (!text || !wo_url_parse(text, &url)) {
        return NULL;
    }
    char *normal = url_text(&url);
    wo_url_free(&url);
    return normal;
}

bool wo_release_pick_asset(const WoJson *release, WoReleaseAsset *asset, WoError *error) {
    memset(asset, 0, sizeof(*asset));
    if (!wo_json_is(release, WO_JSON_OBJECT)) {
        return wo_fail(error, WO_ERR_FORMAT, "The release information is not valid.");
    }
    if (wo_json_bool(release, "draft", false)) {
        return wo_fail(error, WO_ERR_UNSUPPORTED, "The release is still a draft.");
    }
    const WoJson *assets = wo_json_get(release, "assets");
    const WoJson *chosen = NULL;
    size_t matches = 0;
    for (size_t i = 0; i < wo_json_length(assets); i++) {
        const char *name = wo_json_str(wo_json_at(assets, i), "name", "");
        if (asset_name_matches(name)) {
            chosen = wo_json_at(assets, i);
            matches++;
        }
    }
    if (matches != 1) {
        return wo_fail(error, WO_ERR_FORMAT,
                       "The latest release must contain exactly one WO4K mod ZIP asset.");
    }
    const char *name = wo_json_str(chosen, "name", "");
    asset->name = wo_strdup(name, error);
    asset->url = https_url(wo_json_str(chosen, "browser_download_url", NULL));
    if (!asset->name || !asset->url) {
        wo_release_asset_free(asset);
        return wo_fail(error, WO_ERR_FORMAT, "The release asset has no valid HTTPS download link.");
    }
    const char *digest = wo_json_str(chosen, "digest", "");
    if (wo_str_istarts_with(digest, "sha256:") && wo_is_hex_digest(digest + 7)) {
        asset->sha256 = wo_strdup(digest + 7, error);
        for (char *p = asset->sha256; p && *p; p++) {
            *p = wo_ascii_lower(*p);
        }
    }
    size_t sidecars = 0;
    const WoJson *sidecar = NULL;
    for (size_t i = 0; i < wo_json_length(assets); i++) {
        const char *other = wo_json_str(wo_json_at(assets, i), "name", "");
        if (strncmp(other, name, strlen(name)) == 0 && wo_str_eq(other + strlen(name), ".sha256")) {
            sidecar = wo_json_at(assets, i);
            sidecars++;
        }
    }
    if (sidecars == 1) {
        asset->checksum_url = https_url(wo_json_str(sidecar, "browser_download_url", NULL));
    }
    return true;
}

bool wo_release_parse_checksum(const char *text, char out[65], WoError *error) {
    const char *at = text;
    while (*at == ' ' || *at == '\t' || *at == '\r' || *at == '\n') {
        at++;
    }
    char digest[65];
    for (int i = 0; i < 64; i++) {
        char c = at[i];
        bool hex = is_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
        if (!hex) {
            return wo_fail(error, WO_ERR_FORMAT, "Invalid release checksum file.");
        }
        digest[i] = wo_ascii_lower(c);
    }
    digest[64] = 0;
    const char *rest = at + 64;
    if (*rest != 0 && *rest != ' ' && *rest != '\t' && *rest != '\r' && *rest != '\n') {
        return wo_fail(error, WO_ERR_FORMAT, "Invalid release checksum file.");
    }
    /* At most one line: "<digest>  name" with optional trailing newline. */
    const char *newline = strpbrk(rest, "\r\n");
    if (newline) {
        const char *after = newline;
        while (*after == '\r' || *after == '\n' || *after == ' ' || *after == '\t') {
            after++;
        }
        if (*after) {
            return wo_fail(error, WO_ERR_FORMAT, "Invalid release checksum file.");
        }
    }
    memcpy(out, digest, 65);
    return true;
}

/* --- Inventory ------------------------------------------------------------------- */

static const char *const top_level_files[] = {"Setup.exe", "Play.exe", "README.md", "LICENSE",
                                              "VERSION",   "release.url", "source.url"};
static const char *const release_dirs[] = {"runtime/", "assets/", "manifests/",
                                           "licenses/", "docs/",  "patches/"};
static const char *const forbidden_extensions[] = {
    ".mpg", ".mpeg", ".mp4", ".avi", ".wmv", ".wav", ".mp3", ".iso", ".mdf", ".mds", ".bin",
    ".tga", ".tim",  ".cmp", ".ppm", ".wad", ".swp", ".dat", ".py",  ".cmd", ".bat", ".obj",
    ".lib", ".ps1",  ".psm1", ".vbs", ".js", ".msi", ".scr", ".com", ".hta", ".lnk", ".reg"};
static const char *const allowed_executables[] = {"Setup.exe", "Play.exe",
                                                  "runtime/wo2097-controller.exe",
                                                  "runtime/wo4k-video.exe"};
static const char *const allowed_libraries[] = {"runtime/wohr.dll", "runtime/ddraw.dll",
                                                "runtime/libiconv-2.dll"};

static bool listed(const char *path, const char *const *list, size_t count) {
    for (size_t i = 0; i < count; i++) {
        if (wo_str_ieq(path, list[i])) {
            return true;
        }
    }
    return false;
}

static bool path_shape_ok(const char *path) {
    size_t len = strlen(path);
    if (len == 0 || len > 512 || path[0] == '/' || path[len - 1] == '/') {
        return false;
    }
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)path[i];
        if (c < 0x20 || c == 0x7F || c == '\\' || c == ':') {
            return false;
        }
    }
    const char *at = path;
    for (;;) {
        const char *slash = strchr(at, '/');
        size_t part = slash ? (size_t)(slash - at) : strlen(at);
        if (part == 0 || (part == 1 && at[0] == '.') || (part == 2 && at[0] == '.' && at[1] == '.')) {
            return false;
        }
        if (!slash) {
            return true;
        }
        at = slash + 1;
    }
}

bool wo_release_path_allowed(const char *path, WoError *error) {
    if (!path_shape_ok(path)) {
        return wo_fail(error, WO_ERR_UNSAFE, "Invalid release inventory path \"%s\".", path);
    }
    bool in_dir = false;
    for (size_t i = 0; i < WO_COUNT(release_dirs); i++) {
        if (wo_str_istarts_with(path, release_dirs[i])) {
            in_dir = true;
        }
    }
    if (!in_dir && !listed(path, top_level_files, WO_COUNT(top_level_files))) {
        return wo_fail(error, WO_ERR_UNSAFE, "Unexpected update payload: %s", path);
    }
    for (size_t i = 0; i < WO_COUNT(forbidden_extensions); i++) {
        if (wo_str_iends_with(path, forbidden_extensions[i])) {
            return wo_fail(error, WO_ERR_UNSAFE, "Forbidden update payload: %s", path);
        }
    }
    if (wo_str_iends_with(path, ".exe") &&
        !listed(path, allowed_executables, WO_COUNT(allowed_executables))) {
        return wo_fail(error, WO_ERR_UNSAFE, "Unexpected executable in update: %s", path);
    }
    if (wo_str_iends_with(path, ".dll") &&
        !listed(path, allowed_libraries, WO_COUNT(allowed_libraries))) {
        return wo_fail(error, WO_ERR_UNSAFE, "Unexpected library in update: %s", path);
    }
    return true;
}

void wo_inventory_free(WoInventory *inventory) {
    for (size_t i = 0; i < inventory->count; i++) {
        wo_free(inventory->files[i].path);
    }
    wo_free(inventory->files);
    wo_free(inventory->version);
    memset(inventory, 0, sizeof(*inventory));
}

const WoInventoryFile *wo_inventory_find(const WoInventory *inventory, const char *path) {
    for (size_t i = 0; i < inventory->count; i++) {
        if (wo_str_ieq(inventory->files[i].path, path)) {
            return &inventory->files[i];
        }
    }
    return NULL;
}

bool wo_inventory_parse(const WoJson *json, WoInventory *inventory, WoError *error) {
    memset(inventory, 0, sizeof(*inventory));
    const char *version = wo_json_str(json, "version", NULL);
    int order;
    if (!version || !wo_version_compare(version, "0.0.0", &order)) {
        return wo_fail(error, WO_ERR_FORMAT, "Invalid release version in the file inventory.");
    }
    const WoJson *files = wo_json_get(json, "files");
    size_t count = wo_json_length(files);
    if (!wo_json_is(files, WO_JSON_ARRAY) || count == 0 || count > RELEASE_MAX_FILES) {
        return wo_fail(error, WO_ERR_FORMAT, "The release file inventory is empty or too large.");
    }
    inventory->version = wo_strdup(version, error);
    inventory->files = wo_alloc_array(count, sizeof(WoInventoryFile), error);
    if (!inventory->version || !inventory->files) {
        wo_inventory_free(inventory);
        return false;
    }
    for (size_t i = 0; i < count; i++) {
        const WoJson *entry = wo_json_at(files, i);
        const char *path = wo_json_str(entry, "path", "");
        const char *digest = wo_json_str(entry, "sha256", "");
        const WoJson *bytes = wo_json_get(entry, "bytes");
        if (!wo_release_path_allowed(path, error)) {
            wo_inventory_free(inventory);
            return false;
        }
        if (wo_inventory_find(inventory, path) || wo_str_ieq(path, "release-files.json")) {
            wo_inventory_free(inventory);
            return wo_fail(error, WO_ERR_FORMAT, "The release inventory lists %s twice.", path);
        }
        if (!wo_is_hex_digest(digest) || !bytes || !bytes->is_integer || bytes->integer < 0 ||
            (uint64_t)bytes->integer > RELEASE_FILE_MAX_BYTES) {
            wo_inventory_free(inventory);
            return wo_fail(error, WO_ERR_FORMAT, "Invalid inventory entry for %s.", path);
        }
        WoInventoryFile *file = &inventory->files[inventory->count];
        file->path = wo_strdup(path, error);
        if (!file->path) {
            wo_inventory_free(inventory);
            return false;
        }
        file->bytes = (uint64_t)bytes->integer;
        for (int c = 0; c < 64; c++) {
            file->sha256[c] = wo_ascii_lower(digest[c]);
        }
        file->sha256[64] = 0;
        inventory->count++;
    }
    return true;
}
