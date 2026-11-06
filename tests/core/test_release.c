#include <stdio.h>
#include <string.h>

#include "core/wo_release.h"
#include "wo_test.h"

static void test_urls(void) {
    WoUrl url;
    CHECK(wo_url_parse("https://GitHub.com:443/a/b?x=1", &url));
    CHECK_STR(url.host, "github.com");
    CHECK_STR(url.path, "/a/b");
    CHECK_STR(url.query, "x=1");
    wo_url_free(&url);
    const char *bad[] = {"http://github.com/a", "https://user@github.com/a",
                         "https://github.com:8443/a", "https://github.com/a#x",
                         "https://github.com/a b", "https:///a", "https://-x\\y",
                         "https://github.com/\"a", "ftp://x", "https://.github.com/"};
    for (size_t i = 0; i < WO_COUNT(bad); i++) {
        if (wo_url_parse(bad[i], &url)) {
            wo_test_fail(__FILE__, __LINE__, bad[i], "accepted");
            wo_url_free(&url);
        }
    }
}

static void test_links(void) {
    char *link = wo_link_from_text("\xEF\xBB\xBF  https://github.com/ninjadynamics/wo4k\r\n",
                                   WO_LINK_SOURCE);
    CHECK_STR(link, "https://github.com/ninjadynamics/wo4k");
    wo_free(link);
    CHECK(wo_link_from_text("https://example.com/wo4k", WO_LINK_SOURCE) == NULL);
    CHECK(wo_link_from_text("https://github.com/a/b/c", WO_LINK_SOURCE) == NULL);
    CHECK(wo_link_from_text("https://github.com/a\nhttps://x", WO_LINK_SOURCE) == NULL);

    link = wo_link_from_text("https://github.com/ninjadynamics/wo4k/releases/latest",
                             WO_LINK_RELEASE);
    CHECK_STR(link, "https://github.com/ninjadynamics/wo4k/releases/latest");
    wo_free(link);
    CHECK(wo_link_from_text("https://github.com/ninjadynamics/wo4k", WO_LINK_RELEASE) == NULL);

    char *endpoint = wo_release_endpoint("https://github.com/ninjadynamics/wo4k/releases/");
    CHECK_STR(endpoint, "https://api.github.com/repos/ninjadynamics/wo4k/releases/latest");
    wo_free(endpoint);
    endpoint = wo_release_endpoint("https://api.github.com/repos/a/b/releases/latest");
    CHECK_STR(endpoint, "https://api.github.com/repos/a/b/releases/latest");
    wo_free(endpoint);
    endpoint = wo_release_endpoint("https://downloads.example.org/wo4k-1.0.0.ZIP");
    CHECK_STR(endpoint, "https://downloads.example.org/wo4k-1.0.0.ZIP");
    CHECK(wo_release_endpoint_is_zip(endpoint));
    wo_free(endpoint);
    CHECK(wo_release_endpoint("https://api.github.com/repos/a/b/releases/latest?x=1") == NULL);
    CHECK(wo_release_endpoint("https://github.com/a/b/tags") == NULL);
}

static int compare(const char *a, const char *b) {
    int order = 99;
    if (!wo_version_compare(a, b, &order)) {
        wo_test_fail(__FILE__, __LINE__, a, b);
    }
    return order;
}

static void test_versions(void) {
    CHECK(compare("0.1.0-beta.24", "0.1.0-beta.24") == 0);
    CHECK(compare("0.1.0-beta.24", "0.1.0-beta.9") > 0);
    CHECK(compare("0.1.0-beta.10", "0.1.0-beta.9") > 0);
    CHECK(compare("v0.1.0", "0.1.0-beta.99") > 0);
    CHECK(compare("0.2.0-alpha", "0.1.9") > 0);
    CHECK(compare("0.2.0-beta.1", "0.1.0-beta.24") > 0);
    CHECK(compare("0.2.0", "0.2.0-beta.1") > 0);
    CHECK(compare("1.0.0-alpha", "1.0.0-alpha.1") < 0);
    CHECK(compare("1.0.0-alpha.1", "1.0.0-alpha.beta") < 0);
    CHECK(compare("1.0.0-rc.1", "1.0.0-beta.11") > 0);
    CHECK(compare("1.0.0+build.5", "1.0.0") == 0);
    CHECK(compare("10.0.0", "9.99.99") > 0);
    int order;
    CHECK(!wo_version_compare("1.0", "1.0.0", &order));
    CHECK(!wo_version_compare("1.0.0-", "1.0.0", &order));
    CHECK(!wo_version_compare("1.0.0 ", "1.0.0", &order));
}

static WoJson *parse(const char *text) {
    WoJson *value = NULL;
    WoError error = {0};
    CHECK_OK(wo_json_parse(text, strlen(text), &value, &error), &error);
    return value;
}

static void test_asset_selection(void) {
    WoJson *release = parse(
        "{\"draft\":false,\"assets\":["
        "{\"name\":\"wo4k-0.2.0.zip\",\"browser_download_url\":\"https://github.com/x/wo4k-0.2.0.zip\","
        "\"digest\":\"sha256:ABCDEF0123456789abcdef0123456789abcdef0123456789abcdef0123456789\"},"
        "{\"name\":\"wo4k-0.2.0.zip.sha256\",\"browser_download_url\":\"https://github.com/x/s\"},"
        "{\"name\":\"notes.txt\",\"browser_download_url\":\"https://github.com/x/n\"}]}");
    WoReleaseAsset asset;
    WoError error = {0};
    CHECK_OK(wo_release_pick_asset(release, &asset, &error), &error);
    CHECK_STR(asset.url, "https://github.com/x/wo4k-0.2.0.zip");
    CHECK_STR(asset.sha256, "abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789");
    CHECK_STR(asset.checksum_url, "https://github.com/x/s");
    wo_release_asset_free(&asset);
    wo_json_free(release);

    release = parse("{\"draft\":true,\"assets\":[]}");
    CHECK_FAILS(wo_release_pick_asset(release, &asset, &error), &error, WO_ERR_UNSUPPORTED);
    wo_json_free(release);
    release = parse("{\"assets\":[{\"name\":\"wo4k-a.zip\",\"browser_download_url\":\"https://a/1\"},"
                    "{\"name\":\"wo-beta-mod-b.zip\",\"browser_download_url\":\"https://a/2\"}]}");
    CHECK_FAILS(wo_release_pick_asset(release, &asset, &error), &error, WO_ERR_FORMAT);
    wo_json_free(release);
    release = parse("{\"assets\":[{\"name\":\"wo4k.zip\",\"browser_download_url\":\"http://a/1\"}]}");
    CHECK_FAILS(wo_release_pick_asset(release, &asset, &error), &error, WO_ERR_FORMAT);
    wo_json_free(release);
}

static void test_checksum_text(void) {
    char digest[65];
    WoError error = {0};
    const char *good = "0123456789ABCDEF0123456789abcdef0123456789abcdef0123456789abcdef  wo4k.zip\r\n";
    CHECK_OK(wo_release_parse_checksum(good, digest, &error), &error);
    CHECK_STR(digest, "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");
    CHECK_FAILS(wo_release_parse_checksum("abc", digest, &error), &error, WO_ERR_FORMAT);
    CHECK_FAILS(wo_release_parse_checksum(
                    "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\nextra line",
                    digest, &error),
                &error, WO_ERR_FORMAT);
}

static void test_inventory(void) {
    const char *digest = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
    char text[2048];
    snprintf(text, sizeof(text),
             "{\"version\":\"0.2.0\",\"files\":["
             "{\"path\":\"Setup.exe\",\"bytes\":10,\"sha256\":\"%s\"},"
             "{\"path\":\"runtime/wohr.dll\",\"bytes\":20,\"sha256\":\"%s\"},"
             "{\"path\":\"assets/hd/title.png\",\"bytes\":30,\"sha256\":\"%s\"}]}",
             digest, digest, digest);
    WoJson *json = parse(text);
    WoInventory inventory;
    WoError error = {0};
    CHECK_OK(wo_inventory_parse(json, &inventory, &error), &error);
    CHECK(inventory.count == 3);
    CHECK(wo_inventory_find(&inventory, "RUNTIME/WOHR.DLL") != NULL);
    wo_inventory_free(&inventory);
    wo_json_free(json);

    const char *bad_paths[] = {"scripts/Setup.ps1", "../x", "runtime/evil.exe", "Setup.ps1",
                               "runtime/x.dll", "docs/a\\b.md", "assets/../Setup.exe",
                               "manifests/run.cmd", "C:/x"};
    for (size_t i = 0; i < WO_COUNT(bad_paths); i++) {
        snprintf(text, sizeof(text),
                 "{\"version\":\"0.2.0\",\"files\":[{\"path\":\"%s\",\"bytes\":1,\"sha256\":\"%s\"}]}",
                 bad_paths[i], digest);
        json = parse(text);
        if (wo_inventory_parse(json, &inventory, &error)) {
            wo_test_fail(__FILE__, __LINE__, bad_paths[i], "accepted");
            wo_inventory_free(&inventory);
        }
        wo_json_free(json);
    }
    snprintf(text, sizeof(text),
             "{\"version\":\"0.2.0\",\"files\":[{\"path\":\"VERSION\",\"bytes\":1,\"sha256\":\"%s\"},"
             "{\"path\":\"version\",\"bytes\":1,\"sha256\":\"%s\"}]}",
             digest, digest);
    json = parse(text);
    CHECK_FAILS(wo_inventory_parse(json, &inventory, &error), &error, WO_ERR_FORMAT);
    wo_json_free(json);
    json = parse("{\"version\":\"latest\",\"files\":[]}");
    CHECK_FAILS(wo_inventory_parse(json, &inventory, &error), &error, WO_ERR_FORMAT);
    wo_json_free(json);
}

int main(void) {
    RUN(test_urls);
    RUN(test_links);
    RUN(test_versions);
    RUN(test_asset_selection);
    RUN(test_checksum_text);
    RUN(test_inventory);
    return wo_test_finish();
}
