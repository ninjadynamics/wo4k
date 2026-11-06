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

    char *log_copy = wo_strdup(path, NULL);
    wo_log_end();

    char *text = NULL;
    CHECK_OK(wo_read_text(log_copy, 1 << 20, &text, &error), &error);
    CHECK(text && strstr(text, "\"event\":\"session\"") != NULL);
    CHECK(text && strstr(text, "%MODROOT%\\\\wobble\\\\Wipeout2097.exe") != NULL);
    CHECK(text && strstr(text, "\"kind\":\"access\"") != NULL);
    CHECK(text && strstr(text, "\"event\":\"system\"") != NULL);
    CHECK(text && strstr(text, "\"event\":\"game_log_tail\"") != NULL);
    CHECK(text && strstr(text, "secret") == NULL);
    wo_free(text);

    char *attachment = wo_format(NULL, "%.*s.game.log", (int)(strlen(log_copy) - 4), log_copy);
    CHECK_OK(wo_read_text(attachment, 1 << 20, &text, &error), &error);
    CHECK(text && strstr(text, "line one") != NULL);
    CHECK(text && strstr(text, "secret") == NULL);
    CHECK(text && strstr(text, "[identifier omitted]") != NULL);
    wo_free(text);
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
