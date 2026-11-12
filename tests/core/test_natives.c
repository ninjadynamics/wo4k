#include <stdio.h>
#include <string.h>

#include "core/wo_fs.h"
#include "core/wo_hash.h"
#include "core/wo_natives.h"
#include "core/wo_boards.h"
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
    WoNativeOptions options = {game, plan, NULL, NULL, NULL, false, false, NULL, false};
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

/* A plan may correct single bytes of game data; operations made for the
 * corrected layout apply only where the correction is in effect. */
static void test_patches(void) {
    const char *root = wo_test_dir("natives-patch");
    if (!root) {
        return;
    }
    WoError error = {0};
    char before[WO_SHA256_HEX_SIZE];
    char after[WO_SHA256_HEX_SIZE];
    CHECK_OK(wo_sha256_mem("abc", 3, before, &error) && wo_sha256_mem("axc", 3, after, &error),
             &error);
    char *text = wo_format(
        &error,
        "{\"format\":1,\"revision\":3,\"patches\":[{\"id\":\"fix\",\"file\":\"DATA/FILE.BIN\","
        "\"offset\":1,\"from\":98,\"to\":120,\"sha256\":\"%s\",\"patched_sha256\":\"%s\"}],"
        "\"operations\":[{\"atlas\":\"Pics/Hi-res/SHEET.TGA\",\"x\":32,\"y\":8,\"width\":8,"
        "\"height\":4,\"patch\":\"fix\",\"source\":{\"path\":\"COMMON/MINE.CMP\",\"frame\":0},"
        "\"label\":\"moved\"}]}",
        before, after);
    WoJson *plan = parse(text);
    for (int other = 0; other < 2; other++) {
        char *game = wo_path_join(root, other ? "other" : "retail", &error);
        build_game(game);
        char *file = wo_path_join(game, "DATA/FILE.BIN", &error);
        CHECK_OK(wo_write_file(file, other ? "abd" : "abc", 3, &error), &error);
        WoNativeOptions options = {game, plan, NULL, NULL, NULL, false, false, NULL, false};
        /* The second build finds the correction already made. */
        for (int pass = 0; pass < 2; pass++) {
            WoJson *receipt = NULL;
            CHECK_OK(wo_natives_install(&options, &receipt, &error), &error);
            CHECK(wo_json_int(receipt, "pc_regions", -1) == (other ? 0 : 1));
            wo_json_free(receipt);
            WoBuf bytes = {0};
            CHECK_OK(wo_read_file(file, 16, &bytes, &error), &error);
            CHECK(bytes.len == 3 && memcmp(bytes.data, other ? "abd" : "axc", 3) == 0);
            wo_buf_free(&bytes);
            bool healthy = false;
            CHECK_OK(wo_natives_test(game, plan, false, &healthy, &error), &error);
            CHECK(healthy);
        }
        /* Game data put back as it shipped is corrected by the next build. */
        if (!other) {
            bool healthy = true;
            CHECK_OK(wo_write_file(file, "abc", 3, &error), &error);
            CHECK_OK(wo_natives_test(game, plan, false, &healthy, &error), &error);
            CHECK(!healthy);
        }
        wo_free(file);
        wo_free(game);
    }
    wo_json_free(plan);
    wo_free(text);
}

/* With a PlayStation track archive imported it replaces the PC one, and the
 * track's sheet takes the archive's scenery where its headers place it. */
static void scene_wad(WoBuf *wad, uint16_t base, uint16_t x_words, uint16_t y) {
    WoBuf frame = {0};
    WoBuf cmp = {0};
    tb_tim16(&frame, 4, 2, base);
    frame.data[12] = (uint8_t)(x_words & 255u);
    frame.data[13] = (uint8_t)(x_words >> 8);
    frame.data[14] = (uint8_t)(y & 255u);
    frame.data[15] = (uint8_t)(y >> 8);
    tb_cmp(&cmp, &frame, 1);
    static const char *const names[] = {"scene.cmp"};
    tb_wad(wad, names, &cmp, 1);
    wo_buf_free(&frame);
    wo_buf_free(&cmp);
}

static void test_scenery(void) {
    const char *game = wo_test_dir("natives-scenery");
    if (!game) {
        return;
    }
    build_game(game);
    WoError error = {0};
    WoBuf pc = {0};
    WoBuf ps1 = {0};
    scene_wad(&pc, 300, 320 + 8, 4);    /* PC: frame at (8, 4) */
    scene_wad(&ps1, 700, 320 + 20, 10); /* PlayStation: frame at (20, 10) */
    write_buf(game, "TRACK09/TRACK.WAD", &pc);
    WoJson *plan = parse(
        "{\"format\":1,\"revision\":3,\"scenery\":[{\"track\":\"TRACK09\","
        "\"atlases\":[\"Pics/Hi-res/SHEET.TGA\"]}],"
        "\"operations\":[{\"atlas\":\"Pics/Hi-res/SHEET.TGA\",\"x\":8,\"y\":4,\"width\":4,"
        "\"height\":2,\"source\":{\"wad\":\"TRACK09/TRACK.WAD\",\"entry\":\"scene.cmp\",\"frame\":0},"
        "\"label\":\"pc scenery\"}]}");
    char *sheet = wo_path_join(game, "textures/sd/Pics/Hi-res/SHEET.TGA", &error);
    char *wad = wo_path_join(game, "TRACK09/TRACK.WAD", &error);
    char *kept = wo_path_join(game, "textures/native-source/TRACK09/TRACK.WAD", &error);
    WoNativeOptions options = {game, plan, NULL, NULL, NULL, false, false, NULL, false};
    WoJson *receipt = NULL;
    WoBuf bytes = {0};

    /* Without the disc the PC archive and its layout stay. */
    CHECK_OK(wo_natives_install(&options, &receipt, &error), &error);
    wo_json_free(receipt);
    CHECK_OK(wo_read_file(sheet, 1 << 20, &bytes, &error), &error);
    CHECK(wo_le16(bytes.data + 18 + (4 * 64 + 8) * 2) == to_pc(300));
    CHECK(wo_le16(bytes.data + 18 + (10 * 64 + 20) * 2) == 0x1234);
    wo_buf_free(&bytes);
    CHECK(!wo_exists(kept));

    /* The imported archive takes its place; the sheet follows its headers. */
    write_buf(game, "textures/ps1-source/TRACK09/TRACK.WAD", &ps1);
    bool healthy = true;
    CHECK_OK(wo_natives_test(game, plan, false, &healthy, &error), &error);
    CHECK(!healthy);
    for (int pass = 0; pass < 2; pass++) {
        CHECK_OK(wo_natives_install(&options, &receipt, &error), &error);
        CHECK(wo_json_int(receipt, "ps1_regions", -1) == 1);
        wo_json_free(receipt);
        CHECK_OK(wo_read_file(sheet, 1 << 20, &bytes, &error), &error);
        CHECK(wo_le16(bytes.data + 18 + (10 * 64 + 20) * 2) == to_pc(700));
        CHECK(wo_le16(bytes.data + 18 + (11 * 64 + 23) * 2) == to_pc(700 + 7));
        /* What the PlayStation frames do not cover is not cleared: the PC
         * archive repeats shared textures the PlayStation models still use
         * there. (This plan's own placement reads the archive now in place.) */
        CHECK(wo_le16(bytes.data + 18 + (4 * 64 + 8) * 2) == to_pc(700));
        wo_buf_free(&bytes);
        WoBuf now = {0};
        WoBuf old = {0};
        CHECK_OK(wo_read_file(wad, 1 << 20, &now, &error) &&
                     wo_read_file(kept, 1 << 20, &old, &error),
                 &error);
        CHECK(now.len == ps1.len && memcmp(now.data, ps1.data, ps1.len) == 0);
        CHECK(old.len == pc.len && memcmp(old.data, pc.data, pc.len) == 0);
        wo_buf_free(&now);
        wo_buf_free(&old);
        CHECK_OK(wo_natives_test(game, plan, false, &healthy, &error), &error);
        CHECK(healthy);
    }
    wo_free(kept);
    wo_free(wad);
    wo_free(sheet);
    wo_json_free(plan);
    wo_buf_free(&pc);
    wo_buf_free(&ps1);
}

/* --- Trackside boards ---------------------------------------------------------- */

static void put_be16(WoBuf *b, unsigned v) {
    uint8_t bytes[2] = {(uint8_t)(v >> 8), (uint8_t)v};
    wo_buf_append(b, bytes, 2, NULL);
}

#define OBJECT_HEAD (144 + 4 * 8) /* header and four vertices */
#define PANEL_BYTES 44

/* A scenery object: four vertices and `panels` textured quads (kind 8). */
static void scene_object(WoBuf *b, const char *name, int32_t x, unsigned panels, unsigned texture) {
    uint8_t header[144] = {0};
    memcpy(header, name, strlen(name));
    header[17] = 4;               /* vertices */
    header[33] = (uint8_t)panels; /* primitives */
    header[84] = (uint8_t)((uint32_t)x >> 24);
    header[85] = (uint8_t)((uint32_t)x >> 16);
    header[86] = (uint8_t)((uint32_t)x >> 8);
    header[87] = (uint8_t)x;
    wo_buf_append(b, header, sizeof(header), NULL);
    for (int i = 0; i < 4 * 8; i++) {
        wo_buf_append_byte(b, (uint8_t)i, NULL);
    }
    for (unsigned i = 0; i < panels; i++) {
        put_be16(b, 8);
        put_be16(b, 1);
        for (unsigned corner = 0; corner < 4; corner++) {
            put_be16(b, corner);
        }
        put_be16(b, texture);
        for (unsigned k = 0; k < 4 + 8 + 18; k++) {
            wo_buf_append_byte(b, (uint8_t)(0x40 + k + panels), NULL);
        }
    }
}

static void scene_frame(WoBuf *frame, uint16_t base, uint16_t x_words, uint16_t y) {
    tb_tim16(frame, 4, 2, base);
    wo_put_le16(frame->data + 12, x_words);
    wo_put_le16(frame->data + 14, y);
}

/* An archive of a wall, a board and a third object; frames[i] is
 * {base, x, y} of a 4x2 texture. */
static void board_wad(WoBuf *wad, unsigned panels, const char *third, const unsigned *textures,
                      const uint16_t frames[3][3]) {
    WoBuf contents[2] = {{0}, {0}};
    WoBuf tims[3] = {{0}, {0}, {0}};
    scene_object(&contents[0], "wall", 1000, 1, 0);
    scene_object(&contents[0], "redb", 2000, panels, textures[0]);
    scene_object(&contents[0], third, 3000, panels, textures[1]);
    for (int i = 0; i < 3; i++) {
        scene_frame(&tims[i], frames[i][0], (uint16_t)(320 + frames[i][1]), frames[i][2]);
    }
    tb_cmp(&contents[1], tims, 3);
    static const char *const names[] = {"scene.prm", "scene.cmp"};
    tb_wad(wad, names, contents, 2);
    for (int i = 0; i < 3; i++) {
        wo_buf_free(&tims[i]);
    }
    wo_buf_free(&contents[0]);
    wo_buf_free(&contents[1]);
}

/* The scenery models of an archive, and its textures decoded. */
static const uint8_t *scene_of(const WoBuf *archive, WoWad *wad, WoCmp *frames, size_t *size) {
    WoError error = {0};
    CHECK_OK(wo_wad_parse(archive->data, archive->len, wad, &error), &error);
    const WoWadEntry *models = wo_wad_find(wad, "scene.prm");
    const WoWadEntry *textures = wo_wad_find(wad, "scene.cmp");
    CHECK(models && textures);
    CHECK_OK(wo_cmp_decode(archive->data + textures->offset, textures->size, frames, &error),
             &error);
    *size = models->size;
    return archive->data + models->offset;
}

/* The PlayStation's boards are four panels on a placeholder texture. Where
 * the PC's scenery has a board at the same place the archive put in the
 * game folder has the PC's board, with its picture; the others show the
 * pictures the plan names. */
static void test_boards(void) {
    const char *game = wo_test_dir("natives-boards");
    if (!game) {
        return;
    }
    build_game(game);
    WoError error = {0};
    WoBuf pc = {0};
    WoBuf ps1 = {0};
    WoBuf advert = {0};
    WoBuf adverts = {0};
    /* PlayStation: wall at (20,10), a sign at (40,4), the boards' placeholder
     * at (60,20); two boards. */
    static const unsigned ps1_textures[] = {2, 2};
    static const uint16_t ps1_frames[3][3] = {{700, 20, 10}, {300, 40, 4}, {900, 60, 20}};
    /* PC: wall at (8,4); the same sign; the one board's own picture, which
     * sits where the PlayStation's wall is. The third object is no board. */
    static const unsigned pc_textures[] = {2, 0};
    static const uint16_t pc_frames[3][3] = {{100, 8, 4}, {300, 30, 0}, {500, 20, 10}};
    board_wad(&ps1, 4, "redb_1", ps1_textures, ps1_frames);
    board_wad(&pc, 1, "post", pc_textures, pc_frames);
    scene_frame(&advert, 800, 320 + 50, 20);
    tb_cmp(&adverts, &advert, 1);
    size_t wall = OBJECT_HEAD + PANEL_BYTES;
    size_t one_panel = OBJECT_HEAD + PANEL_BYTES;
    size_t four_panels = OBJECT_HEAD + 4 * PANEL_BYTES;

    /* With nothing to put on them the boards stay, and so does the archive. */
    WoBuf made = {0};
    size_t kept = 9;
    CHECK_OK(wo_boards_keep(ps1.data, ps1.len, NULL, 0, NULL, 0, NULL, 0, &made, &kept, &error),
             &error);
    CHECK(kept == 0 && made.len == ps1.len && memcmp(made.data, ps1.data, ps1.len) == 0);

    /* One of the track's own textures on both boards: no texture is added,
     * and each panel shows a quarter of the 4x2 picture. */
    static const int sign[] = {1};
    WoWad parsed = {0};
    WoCmp frames = {0};
    size_t size = 0;
    CHECK_OK(wo_boards_keep(ps1.data, ps1.len, NULL, 0, NULL, 0, sign, 1, &made, &kept, &error),
             &error);
    CHECK(kept == 2);
    const uint8_t *prm = scene_of(&made, &parsed, &frames, &size);
    CHECK(size == wall + 2 * four_panels && frames.count == 3);
    for (unsigned board = 0; board < 2; board++) {
        for (unsigned q = 0; q < 4; q++) {
            const uint8_t *panel = prm + wall + board * four_panels + OBJECT_HEAD + q * PANEL_BYTES;
            CHECK(panel[12] == 0 && panel[13] == 1);
            for (unsigned corner = 0; corner < 4; corner++) {
                CHECK(panel[18 + corner * 2] == (q % 2) * 2 + corner % 2);
                CHECK(panel[19 + corner * 2] == q / 2);
            }
            CHECK(panel[4] == 0 && panel[5] == 0 && panel[26] == 0x40 + 12 + 4); /* the rest is as it was */
        }
    }
    wo_cmp_free(&frames);
    wo_wad_free(&parsed);

    /* Through the installer: the PC's board where the PC has one, the
     * advert on the other. */
    write_buf(game, "TRACK09/TRACK.WAD", &pc);
    write_buf(game, "textures/ps1-source/TRACK09/TRACK.WAD", &ps1);
    write_buf(game, "textures/ps1-source/TEXTURES/NINERED.CMP", &adverts);
    WoJson *plan = parse("{\"format\":1,\"revision\":3,\"scenery\":[{\"track\":\"TRACK09\","
                         "\"boards\":{\"adverts\":\"TEXTURES/NINERED.CMP\",\"pictures\":[\"advert\"]},"
                         "\"atlases\":[\"Pics/Hi-res/SHEET.TGA\"]}],"
                         "\"operations\":[{\"atlas\":\"Pics/Hi-res/SHEET.TGA\",\"x\":8,\"y\":4,"
                         "\"width\":4,\"height\":2,\"source\":{\"wad\":\"TRACK09/TRACK.WAD\","
                         "\"entry\":\"scene.cmp\",\"frame\":0},\"label\":\"pc scenery\"}]}");
    char *sheet = wo_path_join(game, "textures/sd/Pics/Hi-res/SHEET.TGA", &error);
    char *wad = wo_path_join(game, "TRACK09/TRACK.WAD", &error);
    WoNativeOptions options = {game, plan, NULL, NULL, NULL, false, false, NULL, false};
    for (int pass = 0; pass < 2; pass++) {
        WoJson *receipt = NULL;
        WoBuf bytes = {0};
        bool healthy = false;
        CHECK_OK(wo_natives_install(&options, &receipt, &error), &error);
        wo_json_free(receipt);
        CHECK_OK(wo_natives_test(game, plan, false, &healthy, &error), &error);
        CHECK(healthy);
        /* The sheet: the PlayStation's textures where its archive has them,
         * the PC board's picture where the PC's wall used to be (its own
         * place is the PlayStation wall's), the advert where its header
         * says. */
        CHECK_OK(wo_read_file(sheet, 1 << 20, &bytes, &error), &error);
        CHECK(wo_le16(bytes.data + 18 + (10 * 64 + 20) * 2) == to_pc(700));
        CHECK(wo_le16(bytes.data + 18 + (4 * 64 + 40) * 2) == to_pc(300));
        CHECK(wo_le16(bytes.data + 18 + (4 * 64 + 8) * 2) == to_pc(500));
        CHECK(wo_le16(bytes.data + 18 + (5 * 64 + 11) * 2) == to_pc(500 + 7));
        CHECK(wo_le16(bytes.data + 18 + (20 * 64 + 50) * 2) == to_pc(800));
        wo_buf_free(&bytes);

        /* The archive: the PlayStation's wall, the PC's board of one panel
         * numbered for its picture, added fourth, and the PlayStation's
         * other board with the advert, added fifth. */
        CHECK_OK(wo_read_file(wad, 1 << 20, &bytes, &error), &error);
        prm = scene_of(&bytes, &parsed, &frames, &size);
        CHECK(size == wall + one_panel + four_panels && frames.count == 5);
        CHECK(memcmp(prm + wall, "redb", 5) == 0 && prm[wall + 33] == 1);
        CHECK(prm[wall + OBJECT_HEAD + 12] == 0 && prm[wall + OBJECT_HEAD + 13] == 3);
        CHECK(prm[wall + OBJECT_HEAD + 14] == 0x40 + 0 + 1); /* the PC's panel, otherwise untouched */
        const uint8_t *other = prm + wall + one_panel;
        CHECK(memcmp(other, "redb_1", 7) == 0 && other[33] == 4);
        for (unsigned q = 0; q < 4; q++) {
            const uint8_t *panel = other + OBJECT_HEAD + q * PANEL_BYTES;
            CHECK(panel[13] == 4 && panel[18] == (q % 2) * 2 && panel[20] == (q % 2) * 2 + 1);
        }
        uint32_t x = 0;
        uint32_t y = 0;
        uint32_t per_word = 0;
        CHECK_OK(wo_tim_origin(frames.data + frames.offsets[3], frames.sizes[3], &x, &y, &per_word,
                               &error),
                 &error);
        CHECK(x == 320 + 8 && y == 4 && wo_le16(frames.data + frames.offsets[3] + 20) == 500);
        CHECK_OK(wo_tim_origin(frames.data + frames.offsets[4], frames.sizes[4], &x, &y, &per_word,
                               &error),
                 &error);
        CHECK(x == 320 + 50 && y == 20 && wo_le16(frames.data + frames.offsets[4] + 20) == 800);
        CHECK(wo_le16(frames.data + frames.offsets[0] + 20) == 700);
        wo_cmp_free(&frames);
        wo_wad_free(&parsed);
        wo_buf_free(&bytes);
    }

    /* A PC board showing a picture the PlayStation has takes that texture's
     * number, and nothing is added. The other board, given no picture,
     * stays as it is. */
    WoBuf same = {0};
    static const unsigned same_textures[] = {1, 0};
    board_wad(&same, 1, "post", same_textures, pc_frames);
    CHECK_OK(wo_boards_keep(ps1.data, ps1.len, same.data, same.len, adverts.data, adverts.len,
                            NULL, 0, &made, &kept, &error),
             &error);
    CHECK(kept == 1);
    prm = scene_of(&made, &parsed, &frames, &size);
    CHECK(size == wall + one_panel + four_panels && frames.count == 3);
    CHECK(prm[wall + OBJECT_HEAD + 13] == 1 && prm[wall + one_panel + OBJECT_HEAD + 13] == 2);
    wo_cmp_free(&frames);
    wo_wad_free(&parsed);
    wo_buf_free(&same);

    /* A model file with a primitive this code does not know is left alone. */
    CHECK_OK(wo_wad_parse(ps1.data, ps1.len, &parsed, &error), &error);
    const WoWadEntry *models = wo_wad_find(&parsed, "scene.prm");
    CHECK(models != NULL);
    if (models) {
        ps1.data[models->offset + OBJECT_HEAD + 1] = 9;
        CHECK_OK(wo_boards_keep(ps1.data, ps1.len, pc.data, pc.len, adverts.data, adverts.len,
                                sign, 1, &made, &kept, &error),
                 &error);
        CHECK(kept == 0 && made.len == ps1.len && memcmp(made.data, ps1.data, ps1.len) == 0);
    }
    wo_wad_free(&parsed);
    wo_buf_free(&made);
    wo_free(wad);
    wo_free(sheet);
    wo_json_free(plan);
    wo_buf_free(&adverts);
    wo_buf_free(&advert);
    wo_buf_free(&pc);
    wo_buf_free(&ps1);
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
    /* The three releases of the game, and nothing else. */
    CHECK(wo_ps1_serial_in("BOOT = cdrom:\\SCUS_943.51;1", 28));
    CHECK(wo_ps1_serial_in("BOOT = cdrom:\\sips_600.10;1", 28));
    CHECK(wo_ps1_serial_in("SLES-003.27", 11));
    CHECK(!wo_ps1_serial_in("BOOT = cdrom:\\SCUS_944.07;1", 28));
    CHECK(!wo_ps1_serial_in("SLES_003.2", 10));

    WoPs1Disc found_disc;
    bool found = false;
    CHECK_OK(wo_ps1_find_disc(discs, &found_disc, &found, &error), &error);
    CHECK(found && found_disc.image && wo_str_iends_with(found_disc.image, "wipeout.iso"));
    wo_ps1_disc_free(&found_disc);
    CHECK_OK(wo_ps1_find_disc(decoy, &found_disc, &found, &error), &error);
    CHECK(!found);

    WoJson *plan = parse(PLAN);
    WoNativeOptions options = {game, plan, discs, "D:\\chosen.zip", NULL, false, true, NULL, false};
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
    WoNativeOptions again = {game, plan, NULL, NULL, NULL, false, true, NULL, false};
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
    WoNativeOptions retained = {stage, plan, NULL, NULL, game, false, true, NULL, false};
    CHECK_OK(wo_natives_install(&retained, &receipt, &error), &error);
    CHECK_OK(wo_sha256_file(stage_sheet, after, &error), &error);
    CHECK(wo_digest_eq(before, after));
    wo_json_free(receipt);

    /* A repair from original game data takes the freshly extracted sheets as
     * pristine: a damaged cached copy is not carried into it. */
    WoBuf original = {0};
    CHECK_OK(wo_read_file(pristine, 1 << 20, &original, &error), &error);
    char *fresh = wo_path_join(root, "fresh", &error);
    char *fresh_sheet = wo_path_join(fresh, "textures/sd/Pics/Hi-res/SHEET.TGA", &error);
    char *fresh_wad = wo_path_join(fresh, "TRACK01/TRACK.WAD", &error);
    char *fresh_mine = wo_path_join(fresh, "textures/sd/COMMON/MINE.CMP", &error);
    CHECK_OK(wo_copy_file(pristine, fresh_sheet, &error) &&
                 wo_copy_file(wad_from, fresh_wad, &error) &&
                 wo_copy_file(mine_from, fresh_mine, &error) &&
                 wo_write_file(pristine, original.data, 40, &error),
             &error);
    WoNativeOptions kept = {fresh, plan, NULL, NULL, game, false, true, NULL, false};
    CHECK(!wo_natives_install(&kept, &receipt, &error));
    wo_error_clear(&error);
    WoNativeOptions repaired = {fresh, plan, NULL, NULL, game, false, true, NULL, true};
    CHECK_OK(wo_write_file(fresh_sheet, original.data, original.len, &error) &&
                 wo_natives_install(&repaired, &receipt, &error),
             &error);
    CHECK_OK(wo_sha256_file(fresh_sheet, after, &error), &error);
    CHECK(wo_digest_eq(before, after));
    wo_json_free(receipt);
    CHECK_OK(wo_write_file(pristine, original.data, original.len, &error), &error);

    /* A plan names the exact sheet its placements were located in. A game
     * folder with another sheet (an earlier layout) keeps the retained
     * pristine copy; with no such copy the sheet is installed as it is,
     * without placements. */
    char pinned_hash[WO_SHA256_HEX_SIZE];
    CHECK_OK(wo_sha256_mem(original.data, original.len, pinned_hash, &error), &error);
    char *pinned_text = wo_format(&error, "{\"sheets\":[{\"file\":\"Pics/HI-RES/SHEET.TGA\","
                                          "\"sha256\":\"%s\"}],%s",
                                  pinned_hash, PLAN + 1);
    WoJson *pinned = parse(pinned_text);
    uint8_t *other = wo_alloc(original.len, &error);
    CHECK(other != NULL);
    if (other) {
        memcpy(other, original.data, original.len);
        other[original.len - 1] ^= 0x55; /* another layout: any other bytes */
    }
    char *older = wo_path_join(root, "older", &error);
    char *older_sheet = wo_path_join(older, "textures/sd/Pics/Hi-res/SHEET.TGA", &error);
    char *older_wad = wo_path_join(older, "TRACK01/TRACK.WAD", &error);
    char *older_mine = wo_path_join(older, "textures/sd/COMMON/MINE.CMP", &error);
    CHECK_OK(wo_write_file(older_sheet, other, original.len, &error) &&
                 wo_copy_file(wad_from, older_wad, &error) &&
                 wo_copy_file(mine_from, older_mine, &error),
             &error);
    WoNativeOptions with_cache = {older, pinned, NULL, NULL, game, false, true, NULL, true};
    CHECK_OK(wo_natives_install(&with_cache, &receipt, &error), &error);
    CHECK_OK(wo_sha256_file(older_sheet, after, &error), &error);
    CHECK(wo_digest_eq(before, after));
    wo_json_free(receipt);
    char *alone = wo_path_join(root, "alone", &error);
    char *alone_sheet = wo_path_join(alone, "textures/sd/Pics/Hi-res/SHEET.TGA", &error);
    char *alone_wad = wo_path_join(alone, "TRACK01/TRACK.WAD", &error);
    char *alone_mine = wo_path_join(alone, "textures/sd/COMMON/MINE.CMP", &error);
    CHECK_OK(wo_write_file(alone_sheet, other, original.len, &error) &&
                 wo_copy_file(wad_from, alone_wad, &error) &&
                 wo_copy_file(mine_from, alone_mine, &error),
             &error);
    WoNativeOptions without_cache = {alone, pinned, NULL, NULL, NULL, false, false, NULL, true};
    CHECK_OK(wo_natives_install(&without_cache, &receipt, &error), &error);
    WoBuf untouched = {0};
    CHECK_OK(wo_read_file(alone_sheet, 1 << 20, &untouched, &error), &error);
    CHECK(other && untouched.len == original.len && memcmp(untouched.data, other, original.len) == 0);
    wo_buf_free(&untouched);
    wo_json_free(receipt);
    wo_free(alone_mine);
    wo_free(alone_wad);
    wo_free(alone_sheet);
    wo_free(alone);
    wo_free(older_mine);
    wo_free(older_wad);
    wo_free(older_sheet);
    wo_free(older);
    wo_free(other);
    wo_json_free(pinned);
    wo_free(pinned_text);
    wo_buf_free(&original);
    wo_free(fresh_mine);
    wo_free(fresh_wad);
    wo_free(fresh_sheet);
    wo_free(fresh);

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
    RUN(test_patches);
    RUN(test_scenery);
    RUN(test_boards);
    RUN(test_ps1_import);
    RUN(test_roads);
    return wo_test_finish();
}
