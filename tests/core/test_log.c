#include <string.h>

#include "core/wo_fs.h"
#include "core/wo_log.h"
#include "wo_test.h"

static void test_session_log(void) {
    const char *dir = wo_test_dir("log-session");
    if (!dir) {
        return;
    }
    CHECK(wo_log_start("unit", dir));
    const char *path = wo_log_path();
    CHECK(path && wo_str_istarts_with(path, dir));
    CHECK(path && strstr(path, "\\logs\\unit-") != NULL);

    char *inside = wo_format(NULL, "%s\\wobble\\Wipeout2097.exe", dir);
    WoJson *data = wo_json_new_object();
    wo_json_put_str(data, "file", inside);
    wo_log_event("probe", data);
    WoError failure = {0};
    wo_fail_code(&failure, WO_ERR_ACCESS, 5, "Blocked");
    wo_log_failure("Testing", &failure);

    char *game_log = wo_format(NULL, "%s\\game.log", dir);
    const char *game_text = "line one\r\nDeviceIdentifier: secret\r\n"
                            "adapter {01234567-89ab-cdef-0123-456789abcdef} ok\r\n";
    WoError error = {0};
    CHECK_OK(wo_write_file(game_log, game_text, strlen(game_text), &error), &error);
    wo_log_attach("game.log", game_log, true);
    CHECK(!wo_exists(game_log));
    /* ANSI text and a NUL still give valid JSON; an oversized log keeps its end. */
    char *big_log = wo_format(NULL, "%s\\DDrawCompat.log", dir);
    WoBuf big = {0};
    wo_buf_append_str(&big, "Ger\xE4t name\r\nnul\x01", NULL);
    wo_buf_append_byte(&big, 0, NULL);
    wo_buf_append_str(&big, "byte\r\n", NULL);
    char filler[1001];
    memset(filler, 'f', 1000);
    filler[1000] = 0;
    for (int i = 0; i < 9000; i++) { /* about 9 MB, over the 8 MB limit */
        wo_buf_printf(&big, NULL, "%s %d\r\n", filler, i);
    }
    wo_buf_append_str(&big, "the end\r\n", NULL);
    CHECK_OK(wo_write_file(big_log, big.data, big.len, &error), &error);
    wo_buf_free(&big);
    wo_log_attach("DDrawCompat.log", big_log, true);
    CHECK(!wo_exists(big_log));
    wo_free(big_log);
    char *small_log = wo_format(NULL, "%s\\video.log", dir);
    CHECK_OK(wo_write_file(small_log, "Ger\xE4t\r\n", 6, &error), &error);
    wo_log_attach("video.log", small_log, true);
    wo_free(small_log);
    /* Short records after long ones reuse freed buffers full of old text. */
    for (int i = 0; i < 40; i++) {
        wo_log_text("long", "0123456789abcdefghijklmnopqrstuvwxyz0123456789abcdefghijklmnopqrstuvwxyz"
                            "0123456789abcdefghijklmnopqrstuvwxyz0123456789abcdefghijklmnopqrstuvwxyz");
        wo_log_text("s", "x");
    }

    char *log_copy = wo_strdup(path, NULL);
    wo_log_end();

    char *text = NULL;
    CHECK_OK(wo_read_text(log_copy, 64u << 20, &text, &error), &error);
    CHECK(text && strstr(text, "\"only_last_bytes\":true") != NULL);
    CHECK(text && strstr(text, "\"event\":\"DDrawCompat.log\",\"data\":\"the end\"") != NULL);
    CHECK(text && strstr(text, "\"event\":\"video.log\",\"data\":\"Ger?t\"") != NULL);
    CHECK(text && strstr(text, "\"event\":\"session\"") != NULL);
    CHECK(text && strstr(text, "%MODROOT%\\\\wobble\\\\Wipeout2097.exe") != NULL);
    CHECK(text && strstr(text, "\"kind\":\"access\"") != NULL);
    CHECK(text && strstr(text, "\"event\":\"system\"") != NULL);
    /* The attached log is in the session log itself, one record per line. */
    CHECK(text && strstr(text, "\"event\":\"game_log\",\"data\":{\"file\":\"game.log\"") != NULL);
    CHECK(text && strstr(text, "\"event\":\"game.log\",\"data\":\"line one\"") != NULL);
    CHECK(text && strstr(text, "[identifier omitted]") != NULL);
    CHECK(text && strstr(text, "89ab-cdef") == NULL);
    CHECK(text && strstr(text, "\"lines\":2") != NULL);
    CHECK(text && strstr(text, "secret") == NULL);
    /* Every record is exactly one JSON line, with nothing between records. */
    int records = 0;
    for (const char *line = text; line && *line; records++) {
        const char *end = strstr(line, "\r\n");
        CHECK(end != NULL);
        if (!end) {
            break;
        }
        WoJson *json = NULL;
        WoError parse = {0};
        CHECK_OK(wo_json_parse(line, (size_t)(end - line), &json, &parse), &parse);
        wo_json_free(json);
        line = end + 2;
    }
    CHECK(records > 80);
    wo_free(text);

    /* No separate files beside the session log. */
    char *attachment = wo_format(NULL, "%.*s.game.log", (int)(strlen(log_copy) - 4), log_copy);
    CHECK(attachment && !wo_exists(attachment));
    wo_free(attachment);
    wo_free(log_copy);
    wo_free(game_log);
    wo_free(inside);
}

static void test_timestamp_format(void) {
    char stamp[40];
    wo_log_timestamp(stamp);
    CHECK(strlen(stamp) == 28);
    CHECK(stamp[10] == 'T' && stamp[27] == 'Z' && stamp[19] == '.');
}

static void test_system_info(void) {
    WoJson *info = wo_log_system_info();
    CHECK(wo_json_str(info, "windows", NULL) != NULL);
    CHECK(wo_json_get(info, "cpu") != NULL);
    CHECK(wo_json_is(wo_json_get(info, "gpu"), WO_JSON_ARRAY));
    wo_json_free(info);
}

int main(void) {
    RUN(test_session_log);
    RUN(test_timestamp_format);
    RUN(test_system_info);
    return wo_test_finish();
}
