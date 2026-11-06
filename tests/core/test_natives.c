#include <stdio.h>
#include <string.h>

#include "core/wo_fs.h"
#include "core/wo_hash.h"
#include "core/wo_natives.h"
#include "core/wo_texsrc.h"
#include "iso_builder.h"
#include "tex_builder.h"
#include "wo_test.h"

static const char *PLAN =
    "{\"format\":1,\"revision\":3,"
    "\"archives\":[{\"file\":\"COMMON/MINE.CMP\",\"selection\":\"pc_byte_identical\"},"
    "{\"file\":\"TEXTURES/CHROME.TIM\",\"selection\":\"ps1\"}],"
    "\"operations\":["
    "{\"atlas\":\"Pics/Hi-res/SHEET.TGA\",\"x\":0,\"y\":0,\"width\":32,\"height\":32,"
    "\"source\":{\"wad\":\"TRACK01/TRACK.WAD\",\"entry\":\"library.cmp\",\"table\":\"library.ttf\","
    "\"material\":1,\"level\":32},\"label\":\"road\"},"
    "{\"atlas\":\"Pics/HI-RES/sheet.tga\",\"x\":32,\"y\":0,\"width\":8,\"height\":4,"
    "\"source\":{\"path\":\"COMMON/MINE.CMP\",\"frame\":0},\"label\":\"mine\"},"
    "{\"atlas\":\"Pics/Hi-res/SHEET.TGA\",\"x\":40,\"y\":8,\"width\":4,\"height\":4,"
    "\"source\":{\"ps1\":\"TEXTURES/CHROME.TIM\",\"frame\":0},\"label\":\"chrome\"}]}";

static WoJson *parse(const char *text) {
    WoJson *json = NULL;
    WoError error = {0};
    if (!wo_json_parse(text, strlen(text), &json, &error)) {
        wo_test_fail(__FILE__, __LINE__, "parse", error.message);
    }
    return json;
}

static void write_buf(const char *root, const char *relative, const WoBuf *data) {
    WoError error = {0};
    char *path = wo_path_join(root, relative, &error);
    CHECK_OK(path && wo_write_file(path, data->data, data->len, &error), &error);
    wo_free(path);
}

static uint16_t to_pc(uint16_t ps) {
    return (uint16_t)(((ps & 31u) << 10) | (ps & 0x83E0u) | ((ps >> 10) & 31u));
}

/* Game folder with one 64x32 sheet, a track WAD with two road tiles and
 * a PC CMP. */
static void build_game(const char *game) {
    WoBuf sheet = {0};
    tb_tga(&sheet, 64, 32, 0x1234);
    write_buf(game, "textures/sd/Pics/Hi-res/SHEET.TGA", &sheet);
    wo_buf_free(&sheet);

    WoBuf tiles[2] = {{0}, {0}};
    tb_tim16(&tiles[0], 32, 32, 100);
    tb_tim16(&tiles[1], 32, 32, 5000);
    WoBuf contents[2] = {{0}, {0}};
    tb_cmp(&contents[0], tiles, 2);
    wo_buf_reserve(&contents[1], 84, NULL);
    memset(contents[1].data, 0, 84);
    contents[1].len = 84;
    contents[1].data[42 + 41] = 1; /* material 1, 32-pixel level: tile 1 */
    static const char *const names[] = {"library.cmp", "library.ttf"};
    WoBuf wad = {0};
    tb_wad(&wad, names, contents, 2);
    write_buf(game, "TRACK01/TRACK.WAD", &wad);
    wo_buf_free(&wad);
    for (int i = 0; i < 2; i++) {
        wo_buf_free(&tiles[i]);
        wo_buf_free(&contents[i]);
    }

    WoBuf mine_tim = {0};
    WoBuf mine = {0};
    tb_tim16(&mine_tim, 8, 4, 7);
    tb_cmp(&mine, &mine_tim, 1);
    write_buf(game, "textures/sd/COMMON/MINE.CMP", &mine);
    wo_buf_free(&mine);
    wo_buf_free(&mine_tim);
}

/* A PlayStation data image with the given SYSTEM.CNF serial. */
static void build_ps1_iso(const char *path, const char *serial) {
    IsoImage iso;
    iso_init(&iso, 30);
    iso_pvd(&iso, 18, ISO_SECTOR);
    char cnf[64];
    size_t cnf_len = (size_t)snprintf(cnf, sizeof(cnf), "BOOT=cdrom:\\%s;1\r\n", serial);
    size_t at = iso_dir_begin(&iso, 18, ISO_SECTOR);
    at += iso_record(iso_sector(&iso, 18) + at, "SYSTEM.CNF;1", 12, 20, (uint32_t)cnf_len, false);
    iso_record(iso_sector(&iso, 18) + at, "WIPEOUT2", 8, 19, ISO_SECTOR, true);
    iso_file(&iso, 20, cnf, cnf_len);
    at = iso_dir_begin(&iso, 19, ISO_SECTOR);
    at += iso_record(iso_sector(&iso, 19) + at, "COMMON", 6, 24, ISO_SECTOR, true);
    iso_record(iso_sector(&iso, 19) + at, "TEXTURES", 8, 21, ISO_SECTOR, true);
    WoBuf chrome = {0};
    tb_tim16(&chrome, 4, 4, 9);
    at = iso_dir_begin(&iso, 21, ISO_SECTOR);
    at += iso_record(iso_sector(&iso, 21) + at, "CHROME.TIM;1", 12, 23, (uint32_t)chrome.len,
                     false);
    iso_record(iso_sector(&iso, 21) + at, "EXPLODE.CMP;1", 13, 22, 3, false);
    iso_file(&iso, 22, "cmp", 3);
    iso_file(&iso, 23, chrome.data, chrome.len);
    at = iso_dir_begin(&iso, 24, ISO_SECTOR);
    at += iso_record(iso_sector(&iso, 24) + at, "MINE.CMP;1", 10, 25, 4, false);
    iso_record(iso_sector(&iso, 24) + at, "README.TXT;1", 12, 26, 2, false);
    iso_file(&iso, 25, "mine", 4);
    iso_file(&iso, 26, "hi", 2);
    WoError error = {0};
    CHECK_OK(wo_write_file(path, iso.data, iso.sectors * ISO_SECTOR, &error), &error);
    wo_buf_free(&chrome);
    wo_free(iso.data);
}

static char *sheet_hash(const char *game, char hash[WO_SHA256_HEX_SIZE]) {
    WoError error = {0};
    char *path = wo_path_join(game, "textures/sd/Pics/Hi-res/SHEET.TGA", &error);
    CHECK_OK(path && wo_sha256_file(path, hash, &error), &error);
    return path;
}

static void test_pc_only_build(void) {
    const char *game = wo_test_dir("natives-pc");
    if (!game) {
        return;
    }
    build_game(game);
    WoJson *plan = parse(PLAN);
    WoError error = {0};
    char before[WO_SHA256_HEX_SIZE];
    char *sheet = sheet_hash(game, before);
    WoNativeOptions options = {game, plan, NULL, NULL, NULL, false, false};
    WoJson *receipt = NULL;
    CHECK_OK(wo_natives_install(&options, &receipt, &error), &error);
    CHECK(wo_json_int(receipt, "format", 0) == 2 && wo_json_int(receipt, "revision", 0) == 3);
    CHECK(wo_json_int(receipt, "pc_regions", -1) == 2);
    CHECK(wo_json_int(receipt, "ps1_regions", -1) == 0);
    CHECK(wo_json_int(receipt, "pending_ps1_regions", -1) == 1);
    CHECK(wo_json_int(receipt, "sheets", -1) == 1);
    CHECK(!wo_json_bool(receipt, "ps1_textures", true));
    CHECK_STR(wo_json_str(receipt, "selection", ""), "ps1-preferred");
    const WoJson *files = wo_json_get(receipt, "files");
    CHECK(wo_json_length(files) == 1);
    CHECK_STR(wo_json_str(wo_json_at(files, 0), "file", ""), "Pics/Hi-res/SHEET.TGA");

    /* The pristine copy keeps the original; the sheet has the regions. */
    char *pristine = wo_path_join(game, "textures/native-source/Pics/Hi-res/SHEET.TGA", &error);
    char hash[WO_SHA256_HEX_SIZE];
    CHECK_OK(wo_sha256_file(pristine, hash, &error), &error);
    CHECK(wo_digest_eq(hash, before));
    WoBuf bytes = {0};
    CHECK_OK(wo_read_file(sheet, 1 << 20, &bytes, &error), &error);
    CHECK(wo_le16(bytes.data + 18) == to_pc(5000));                     /* road tile 1 */
    CHECK(wo_le16(bytes.data + 18 + (31 * 64 + 31) * 2) == to_pc(5000 + 31 * 32 + 31));
    CHECK(wo_le16(bytes.data + 18 + (3 * 64 + 39) * 2) == to_pc(7 + 3 * 8 + 7)); /* mine */
    CHECK(wo_le16(bytes.data + 18 + (8 * 64 + 40) * 2) == 0x1234); /* chrome still pending */
    wo_buf_free(&bytes);
    char built[WO_SHA256_HEX_SIZE];
    CHECK_OK(wo_sha256_file(sheet, built, &error), &error);
    CHECK_STR(wo_json_str(wo_json_at(files, 0), "sha256", ""), built);
    wo_json_free(receipt);

    /* An incremental launch keeps the healthy build (and its receipt). */
    char *receipt_path = wo_path_join(game, "textures/selection.json", &error);
    WoJson *marked = NULL;
    CHECK_OK(wo_read_json(receipt_path, 1 << 20, &marked, &error), &error);
    wo_json_put_bool(marked, "marker", true);
    CHECK_OK(wo_write_json(receipt_path, marked, &error), &error);
    wo_json_free(marked);
    options.incremental = true;
    CHECK_OK(wo_natives_install(&options, &receipt, &error), &error);
    CHECK(receipt && wo_json_bool(receipt, "marker", false));
    wo_json_free(receipt);
    /* A full rebuild starts from the pristine copy, so it is repeatable. */
    options.incremental = false;
    CHECK_OK(wo_natives_install(&options, &receipt, &error), &error);
    CHECK_OK(wo_sha256_file(sheet, hash, &error), &error);
    CHECK(wo_digest_eq(hash, built));
    wo_json_free(receipt);

    bool healthy = false;
    CHECK_OK(wo_natives_test(game, plan, false, &healthy, &error), &error);
    CHECK(healthy);
    CHECK_OK(wo_natives_test(game, plan, true, &healthy, &error), &error);
    CHECK(!healthy); /* no PlayStation artwork yet */
    WoJson *newer = parse("{\"format\":1,\"revision\":4,\"operations\":[]}");
    CHECK_OK(wo_natives_test(game, newer, false, &healthy, &error), &error);
    CHECK(!healthy);
    wo_json_free(newer);
    /* A changed sheet is not healthy, and an incremental run repairs it. */
    CHECK_OK(wo_write_file(sheet, "x", 1, &error), &error);
    CHECK_OK(wo_natives_test(game, plan, false, &healthy, &error), &error);
    CHECK(!healthy);
    options.incremental = true;
    CHECK_OK(wo_natives_install(&options, &receipt, &error), &error);
    CHECK_OK(wo_sha256_file(sheet, hash, &error), &error);
    CHECK(wo_digest_eq(hash, built));
    wo_json_free(receipt);
    options.incremental = false;

    /* A sheet the plan names but the game lacks is skipped, and keeps the
     * build from counting as healthy, as before. */
    WoJson *extra = parse("{\"format\":1,\"revision\":3,\"operations\":[{\"atlas\":"
                          "\"Pics/Hi-res/SHEET.TGA\",\"source\":{\"path\":\"COMMON/MINE.CMP\"},"
                          "\"width\":8,\"height\":4},{\"atlas\":\"Pics/Lo-res/MISSING.TGA\","
                          "\"source\":{\"path\":\"COMMON/MINE.CMP\"},\"width\":8,\"height\":4}]}");
    options.plan = extra;
    CHECK_OK(wo_natives_install(&options, &receipt, &error), &error);
    CHECK(wo_json_int(receipt, "sheets", -1) == 1 && wo_json_int(receipt, "pc_regions", -1) == 1);
    wo_json_free(receipt);
    CHECK_OK(wo_natives_test(game, extra, false, &healthy, &error), &error);
    CHECK(!healthy);
    wo_json_free(extra);
    options.plan = plan;
    CHECK_OK(wo_natives_install(&options, &receipt, &error), &error);
    wo_json_free(receipt);

    /* Needing PlayStation artwork without its import is an error. */
    options.require_ps1 = true;
    CHECK_FAILS(wo_natives_install(&options, &receipt, &error), &error, WO_ERR_NOT_FOUND);
    CHECK(receipt == NULL);
    options.require_ps1 = false;

    /* A placement outside the sheet fails without replacing it. */
    WoJson *bad = parse("{\"format\":1,\"revision\":3,\"operations\":[{\"atlas\":"
                        "\"Pics/Hi-res/SHEET.TGA\",\"x\":60,\"y\":0,\"width\":8,\"height\":4,"
                        "\"source\":{\"path\":\"COMMON/MINE.CMP\",\"frame\":0}}]}");
    options.plan = bad;
    CHECK_FAILS(wo_natives_install(&options, &receipt, &error), &error, WO_ERR_FORMAT);
    CHECK_OK(wo_sha256_file(sheet, hash, &error), &error);
    CHECK(wo_digest_eq(hash, built));
    wo_json_free(bad);
    /* Missing frames, WAD entries and source files are errors too. */
    bad = parse("{\"format\":1,\"operations\":[{\"atlas\":\"Pics/Hi-res/SHEET.TGA\",\"x\":0,"
                "\"y\":0,\"width\":8,\"height\":4,\"source\":{\"path\":\"COMMON/MINE.CMP\","
                "\"frame\":1}}]}");
    options.plan = bad;
    CHECK_FAILS(wo_natives_install(&options, &receipt, &error), &error, WO_ERR_FORMAT);
    wo_json_free(bad);
    bad = parse("{\"format\":1,\"operations\":[{\"atlas\":\"Pics/Hi-res/SHEET.TGA\",\"x\":0,"
                "\"y\":0,\"width\":32,\"height\":32,\"source\":{\"wad\":\"TRACK01/TRACK.WAD\","
                "\"entry\":\"sky.cmp\",\"frame\":0}}]}");
    options.plan = bad;
    CHECK_FAILS(wo_natives_install(&options, &receipt, &error), &error, WO_ERR_NOT_FOUND);
    wo_json_free(bad);
    bad = parse("{\"format\":1,\"operations\":[{\"atlas\":\"Pics/Hi-res/SHEET.TGA\",\"x\":0,"
                "\"y\":0,\"width\":32,\"height\":32,\"source\":{\"path\":\"../../x.tim\"}}]}");
    options.plan = bad;
    CHECK_FAILS(wo_natives_install(&options, &receipt, &error), &error, WO_ERR_UNSAFE);
    wo_json_free(bad);
    bad = parse("{\"format\":2,\"operations\":[]}");
    options.plan = bad;
    CHECK_FAILS(wo_natives_install(&options, &receipt, &error), &error, WO_ERR_UNSUPPORTED);
    wo_json_free(bad);
    wo_free(receipt_path);
    wo_free(pristine);
    wo_free(sheet);
    wo_json_free(plan);
}

static void test_ps1_import(void) {
    const char *root = wo_test_dir("natives-ps1");
    if (!root) {
        return;
    }
    WoError error = {0};
    char *game = wo_path_join(root, "game", &error);
    char *discs = wo_path_join(root, "discs", &error);
    char *disc = wo_path_join(discs, "b/wipeout.iso", &error);
    char *decoy = wo_path_join(discs, "a/other.iso", &error);
    build_game(game);
    build_ps1_iso(disc, "SLES_003.27");
    build_ps1_iso(decoy, "SCUS_944.07");

    WoPs1Disc found_disc;
    bool found = false;
    CHECK_OK(wo_ps1_find_disc(discs, &found_disc, &found, &error), &error);
    CHECK(found && found_disc.image && wo_str_iends_with(found_disc.image, "wipeout.iso"));
    wo_ps1_disc_free(&found_disc);
    CHECK_OK(wo_ps1_find_disc(decoy, &found_disc, &found, &error), &error);
    CHECK(!found);

    WoJson *plan = parse(PLAN);
    WoNativeOptions options = {game, plan, discs, "D:\\chosen.zip", NULL, false, true};
    WoJson *receipt = NULL;
    CHECK_OK(wo_natives_install(&options, &receipt, &error), &error);
    CHECK(wo_json_int(receipt, "ps1_regions", -1) == 1);
    CHECK(wo_json_int(receipt, "pending_ps1_regions", -1) == 0);
    CHECK(wo_json_bool(receipt, "ps1_textures", false));
    wo_json_free(receipt);
    /* Byte-identical PC archives and other files are not imported. */
    char *chrome = wo_path_join(game, "textures/ps1-source/TEXTURES/CHROME.TIM", &error);
    char *explode = wo_path_join(game, "textures/ps1-source/TEXTURES/EXPLODE.CMP", &error);
    char *mine = wo_path_join(game, "textures/ps1-source/COMMON/MINE.CMP", &error);
    char *readme = wo_path_join(game, "textures/ps1-source/COMMON/README.TXT", &error);
    CHECK(wo_is_file(chrome) && wo_is_file(explode) && !wo_exists(mine) && !wo_exists(readme));
    char *record_path = wo_path_join(game, "textures/ps1-source/source.json", &error);
    WoJson *record = NULL;
    CHECK_OK(wo_read_json(record_path, 1 << 20, &record, &error), &error);
    CHECK(wo_json_int(record, "files", 0) == 2);
    CHECK_STR(wo_json_str(record, "edition", ""), WO_PS1_EDITION);
    CHECK_STR(wo_json_str(record, "configured_source", ""), "D:\\chosen.zip");
    wo_json_free(record);
    WoBuf sheet = {0};
    char *sheet_path = wo_path_join(game, "textures/sd/Pics/Hi-res/SHEET.TGA", &error);
    CHECK_OK(wo_read_file(sheet_path, 1 << 20, &sheet, &error), &error);
    CHECK(wo_le16(sheet.data + 18 + (8 * 64 + 40) * 2) == to_pc(9));
    wo_buf_free(&sheet);
    bool healthy = false;
    CHECK_OK(wo_natives_test(game, plan, true, &healthy, &error), &error);
    CHECK(healthy);

    /* A later source-free rebuild keeps the imported artwork. */
    char before[WO_SHA256_HEX_SIZE];
    char after[WO_SHA256_HEX_SIZE];
    CHECK_OK(wo_sha256_file(sheet_path, before, &error), &error);
    WoNativeOptions again = {game, plan, NULL, NULL, NULL, false, true};
    CHECK_OK(wo_natives_install(&again, &receipt, &error), &error);
    CHECK_OK(wo_sha256_file(sheet_path, after, &error), &error);
    CHECK(wo_digest_eq(before, after));
    wo_json_free(receipt);

    /* A new folder retains the caches of a previous installation. */
    char *stage = wo_path_join(root, "stage", &error);
    char *stage_sheet = wo_path_join(stage, "textures/sd/Pics/Hi-res/SHEET.TGA", &error);
    char *pristine = wo_path_join(game, "textures/native-source/Pics/Hi-res/SHEET.TGA", &error);
    char *wad_from = wo_path_join(game, "TRACK01/TRACK.WAD", &error);
    char *wad_to = wo_path_join(stage, "TRACK01/TRACK.WAD", &error);
    char *mine_from = wo_path_join(game, "textures/sd/COMMON/MINE.CMP", &error);
    char *mine_to = wo_path_join(stage, "textures/sd/COMMON/MINE.CMP", &error);
    CHECK_OK(wo_copy_file(pristine, stage_sheet, &error) && wo_copy_file(wad_from, wad_to, &error) &&
                 wo_copy_file(mine_from, mine_to, &error),
             &error);
    WoNativeOptions retained = {stage, plan, NULL, NULL, game, false, true};
    CHECK_OK(wo_natives_install(&retained, &receipt, &error), &error);
    CHECK_OK(wo_sha256_file(stage_sheet, after, &error), &error);
    CHECK(wo_digest_eq(before, after));
    wo_json_free(receipt);

    /* Sources without the disc. */
    char *music = wo_path_join(root, "music", &error);
    char *toc = wo_path_join(music, "tracks.ini", &error);
    CHECK_OK(wo_write_file(toc, "[CD]\r\nTracks=12\r\n", 17, &error), &error);
    bool imported = true;
    char *ps1 = wo_path_join(root, "import", &error);
    CHECK_OK(wo_ps1_import_textures(music, ps1, plan, NULL, &imported, &error), &error);
    CHECK(!imported && !wo_exists(ps1));
    CHECK_FAILS(wo_ps1_import_textures(game, ps1, plan, NULL, &imported, &error), &error,
                WO_ERR_NOT_FOUND);
    CHECK_FAILS(wo_ps1_import_textures(decoy, ps1, plan, NULL, &imported, &error), &error,
                WO_ERR_NOT_FOUND);
    char *missing = wo_path_join(root, "nowhere", &error);
    CHECK_FAILS(wo_ps1_import_textures(missing, ps1, plan, NULL, &imported, &error), &error,
                WO_ERR_NOT_FOUND);
    wo_free(missing);
    wo_free(ps1);
    wo_free(toc);
    wo_free(music);
    wo_free(mine_to);
    wo_free(mine_from);
    wo_free(wad_to);
    wo_free(wad_from);
    wo_free(pristine);
    wo_free(stage_sheet);
    wo_free(stage);
    wo_free(sheet_path);
    wo_free(record_path);
    wo_free(readme);
    wo_free(mine);
    wo_free(explode);
    wo_free(chrome);
    wo_json_free(plan);
    wo_free(decoy);
    wo_free(disc);
    wo_free(discs);
    wo_free(game);
}

/* --- Road composition straight from a source cache ------------------------------ */

typedef struct MemoryFiles {
    const WoBuf *wad;
    size_t loads;
} MemoryFiles;

static bool load_memory(void *context, WoTexOrigin origin, const char *relative, size_t max,
                        WoBuf *out, WoError *error) {
    MemoryFiles *files = context;
    files->loads++;
    if (origin != WO_TEX_FROM_GAME || !wo_str_ieq(relative, "T/TRACK.WAD")) {
        return wo_fail(error, WO_ERR_NOT_FOUND, "No file %s.", relative);
    }
    if (files->wad->len > max) {
        return wo_fail(error, WO_ERR_LIMIT, "Too large.");
    }
    return wo_buf_append(out, files->wad->data, files->wad->len, error);
}

static void test_roads(void) {
    WoBuf tiles[17];
    memset(tiles, 0, sizeof(tiles));
    for (int i = 0; i < 17; i++) {
        tb_tim16(&tiles[i], 32, 32, (uint16_t)(i * 1024));
    }
    WoBuf contents[2] = {{0}, {0}};
    tb_cmp(&contents[0], tiles, 17);
    wo_buf_reserve(&contents[1], 42, NULL);
    memset(contents[1].data, 0, 42);
    contents[1].len = 42;
    for (int i = 0; i < 16; i++) {
        contents[1].data[i * 2 + 1] = (uint8_t)(15 - i); /* reversed 4x4 layout */
    }
    contents[1].data[33] = 16; /* the 64-pixel level's first tile */
    contents[1].data[41] = 16;
    static const char *const names[] = {"library.cmp", "library.ttf"};
    WoBuf wad = {0};
    tb_wad(&wad, names, contents, 2);
    MemoryFiles files = {&wad, 0};
    WoError error = {0};
    WoTexSources *sources = wo_texsrc_new(load_memory, &files, &error);
    WoNativeImage road;
    CHECK_OK(wo_texsrc_road(sources, "T/TRACK.WAD", "library.cmp", "library.ttf", 0, 128, &road,
                            &error),
             &error);
    CHECK(road.width == 128 && road.pixels[0] == 15 * 1024);
    CHECK(road.pixels[127 * 128 + 127] == (uint16_t)(0 * 1024 + 1023));
    wo_native_image_free(&road);
    CHECK_OK(wo_texsrc_road(sources, "T/TRACK.WAD", "library.cmp", "library.ttf", 0, 64, &road,
                            &error),
             &error);
    CHECK(road.width == 64 && road.pixels[0] == 16 * 1024 && road.pixels[32] == 0);
    wo_native_image_free(&road);
    CHECK(files.loads == 1); /* cached */
    CHECK_FAILS(wo_texsrc_road(sources, "T/TRACK.WAD", "library.cmp", "library.ttf", 1, 32, &road,
                               &error),
                &error, WO_ERR_FORMAT);
    CHECK_FAILS(wo_texsrc_road(sources, "T/TRACK.WAD", "library.cmp", "library.ttf", -1, 32,
                               &road, &error),
                &error, WO_ERR_FORMAT);
    CHECK_FAILS(wo_texsrc_road(sources, "T/TRACK.WAD", "library.cmp", "library.ttf", 0, 16, &road,
                               &error),
                &error, WO_ERR_FORMAT);
    CHECK_FAILS(wo_texsrc_road(sources, "T/OTHER.WAD", "library.cmp", "library.ttf", 0, 32, &road,
                               &error),
                &error, WO_ERR_NOT_FOUND);
    CHECK_FAILS(wo_texsrc_frame(sources, NULL, "T/TRACK.WAD", "library.cmp", false, 17, &road,
                                &error),
                &error, WO_ERR_FORMAT);
    CHECK_OK(wo_texsrc_frame(sources, NULL, "T/TRACK.WAD", "library.cmp", false, 16, &road, &error),
             &error);
    wo_native_image_free(&road);
    wo_texsrc_free(sources);
    /* Layout tables must be whole 42-byte records; tiles must be 32x32. */
    wo_buf_free(&wad);
    contents[1].len = 41;
    tb_wad(&wad, names, contents, 2);
    sources = wo_texsrc_new(load_memory, &files, &error);
    CHECK_FAILS(wo_texsrc_road(sources, "T/TRACK.WAD", "library.cmp", "library.ttf", 0, 32, &road,
                               &error),
                &error, WO_ERR_FORMAT);
    wo_texsrc_free(sources);
    wo_buf_free(&wad);
    wo_buf_free(&contents[0]);
    WoBuf small = {0};
    tb_tim16(&small, 16, 16, 0);
    tb_cmp(&contents[0], &small, 1);
    contents[1].len = 42;
    memset(contents[1].data, 0, 42);
    tb_wad(&wad, names, contents, 2);
    sources = wo_texsrc_new(load_memory, &files, &error);
    CHECK_FAILS(wo_texsrc_road(sources, "T/TRACK.WAD", "library.cmp", "library.ttf", 0, 32, &road,
                               &error),
                &error, WO_ERR_FORMAT);
    wo_texsrc_free(sources);
    wo_buf_free(&small);
    wo_buf_free(&wad);
    for (int i = 0; i < 2; i++) {
        wo_buf_free(&contents[i]);
    }
    for (int i = 0; i < 17; i++) {
        wo_buf_free(&tiles[i]);
    }
}

int main(void) {
    RUN(test_pc_only_build);
    RUN(test_ps1_import);
    RUN(test_roads);
    return wo_test_finish();
}
