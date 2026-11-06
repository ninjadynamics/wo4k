/* Release metadata: link configuration files, versions, GitHub release
 * selection and package inventories. Portable (no network or file access). */
#ifndef WO_RELEASE_H
#define WO_RELEASE_H

#include "core/wo_base.h"
#include "core/wo_json.h"

/* --- URLs ---------------------------------------------------------------------- */
typedef struct WoUrl {
    char *host;  /* lowercase */
    char *path;  /* starts with '/' */
    char *query; /* without '?', may be "" */
} WoUrl;

/* Strict HTTPS URL: no credentials, no fragment, port 443 only, no spaces,
 * quotes, angle brackets, backslashes or control characters. */
bool wo_url_parse(const char *text, WoUrl *url);
void wo_url_free(WoUrl *url);

typedef enum WoLinkKind { WO_LINK_SOURCE, WO_LINK_RELEASE } WoLinkKind;

/* Content of source.url / release.url (one URL, optional BOM and
 * surrounding whitespace). Returns a normalised URL or NULL when the text
 * is not acceptable for that kind. source.url must be a github.com
 * owner[/repo] page; release.url must resolve to a release endpoint. */
char *wo_link_from_text(const char *text, WoLinkKind kind);
/* GitHub /releases(/latest) page -> API latest-release endpoint; an API
 * endpoint or a direct .zip URL is returned unchanged; anything else NULL. */
char *wo_release_endpoint(const char *url);
bool wo_release_endpoint_is_zip(const char *endpoint);

/* --- Versions -------------------------------------------------------------------- */
/* SemVer-style comparison of "1.2.3", "v1.2.3-beta.24" etc.: *order is
 * negative, zero or positive. False when either version is invalid. */
bool wo_version_compare(const char *a, const char *b, int *order);

/* --- GitHub release selection -------------------------------------------------------- */
typedef struct WoReleaseAsset {
    char *url;          /* browser_download_url of the mod ZIP */
    char *sha256;       /* from GitHub's asset digest, or NULL */
    char *checksum_url; /* the <zip>.sha256 sidecar, or NULL */
    char *name;
} WoReleaseAsset;

/* From the latest-release API document: refuse drafts and require exactly
 * one mod ZIP asset (named wo4k... or wo-beta-mod... .zip). */
bool wo_release_pick_asset(const WoJson *release, WoReleaseAsset *asset, WoError *error);
void wo_release_asset_free(WoReleaseAsset *asset);
/* "<64 hex>  name" (name optional) -> lowercase digest. */
bool wo_release_parse_checksum(const char *text, char out[65], WoError *error);

/* --- Package inventory (release-files.json) ----------------------------------------- */
typedef struct WoInventoryFile {
    char *path;
    uint64_t bytes;
    char sha256[65];
} WoInventoryFile;

typedef struct WoInventory {
    char *version;
    WoInventoryFile *files;
    size_t count;
} WoInventory;

/* Parse and validate: version, unique safe relative paths, digests and
 * sizes. Paths may only be the files and folders a WO4K release contains,
 * and only expected executables/libraries are allowed. */
bool wo_inventory_parse(const WoJson *json, WoInventory *inventory, WoError *error);
void wo_inventory_free(WoInventory *inventory);
const WoInventoryFile *wo_inventory_find(const WoInventory *inventory, const char *path);
/* True when a release path may be shipped at all (same rules as above). */
bool wo_release_path_allowed(const char *path, WoError *error);

#endif
