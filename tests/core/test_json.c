#include <string.h>

#include "core/wo_json.h"
#include "wo_test.h"

static WoJson *parse(const char *text) {
    WoJson *value = NULL;
    WoError error = {0};
    if (!wo_json_parse(text, strlen(text), &value, &error)) {
        wo_test_fail(__FILE__, __LINE__, text, error.message);
    }
    return value;
}

static void expect_reject(const char *text) {
    WoJson *value = NULL;
    WoError error = {0};
    if (wo_json_parse(text, strlen(text), &value, &error)) {
        wo_test_fail(__FILE__, __LINE__, text, "was accepted");
        wo_json_free(value);
    }
}

static void test_scalars_and_nesting(void) {
    WoJson *v = parse("{\"a\":1,\"b\":[true,false,null],\"c\":\"x\\u00e9\\ud83d\\ude00\","
                      "\"d\":-2.5e3,\"e\":{\"f\":9223372036854775807}}");
    CHECK(v && v->type == WO_JSON_OBJECT);
    CHECK(wo_json_int(v, "a", 0) == 1);
    CHECK(wo_json_length(wo_json_get(v, "b")) == 3);
    CHECK(wo_json_as_bool(wo_json_at(wo_json_get(v, "b"), 0), false));
    CHECK(wo_json_is(wo_json_at(wo_json_get(v, "b"), 2), WO_JSON_NULL));
    CHECK_STR(wo_json_str(v, "c", NULL), "x\xC3\xA9\xF0\x9F\x98\x80");
    CHECK(wo_json_as_number(wo_json_get(v, "d"), 0) == -2500.0);
    CHECK(!wo_json_get(v, "d")->is_integer);
    CHECK(wo_json_int(wo_json_get(v, "e"), "f", 0) == INT64_MAX);
    wo_json_free(v);
}

static void test_case_insensitive_lookup(void) {
    WoJson *v = parse("{\"GameSource\":\"C:\\\\Games\",\"gamesource2\":1}");
    CHECK_STR(wo_json_str(v, "GameSource", NULL), "C:\\Games");
    CHECK_STR(wo_json_str(v, "gamesource", NULL), "C:\\Games");
    CHECK(!wo_json_get(v, "missing"));
    wo_json_free(v);
}

static void test_bom_and_whitespace(void) {
    WoJson *v = parse("\xEF\xBB\xBF \r\n {\"format\" : 1 } \n");
    CHECK(wo_json_int(v, "format", 0) == 1);
    wo_json_free(v);
}

static void test_rejects(void) {
    expect_reject("");
    expect_reject("{");
    expect_reject("{\"a\":1,}");
    expect_reject("[1,]");
    expect_reject("{\"a\":1,\"a\":2}");
    expect_reject("\"\\u0000\"");
    expect_reject("\"\\ud800\"");
    expect_reject("\"\\udc00\"");
    expect_reject("\"\xC0\xAF\"");
    expect_reject("\"\xED\xA0\x80\"");
    expect_reject("01");
    expect_reject("1.");
    expect_reject("-");
    expect_reject("tru");
    expect_reject("{} x");
    expect_reject("\"tab\there\"");
    char deep[200];
    memset(deep, '[', sizeof(deep) - 1);
    deep[sizeof(deep) - 1] = 0;
    expect_reject(deep);
}

static void test_write_round_trip(void) {
    WoJson *object = wo_json_new_object();
    CHECK(wo_json_put_str(object, "path", "C:\\a \"b\"\n"));
    CHECK(wo_json_put_int(object, "count", -42));
    CHECK(wo_json_put_bool(object, "ok", true));
    WoJson *list = wo_json_new_array();
    CHECK(wo_json_push_str(list, "x"));
    CHECK(wo_json_push(list, wo_json_new_number(0.5)));
    CHECK(wo_json_put(object, "list", list));
    CHECK(wo_json_put_int(object, "count", 7)); /* replace keeps position */

    WoBuf compact = {0};
    WoError error = {0};
    CHECK_OK(wo_json_write(object, false, &compact, &error), &error);
    CHECK_STR((const char *)compact.data,
              "{\"path\":\"C:\\\\a \\\"b\\\"\\n\",\"count\":7,\"ok\":true,\"list\":[\"x\",0.5]}");

    WoJson *again = NULL;
    CHECK_OK(wo_json_parse((const char *)compact.data, compact.len, &again, &error), &error);
    CHECK_STR(wo_json_str(again, "path", NULL), "C:\\a \"b\"\n");

    WoBuf pretty = {0};
    CHECK_OK(wo_json_write(object, true, &pretty, &error), &error);
    CHECK(strstr((const char *)pretty.data, "\n  \"count\": 7,\n") != NULL);

    WoJson *copy = wo_json_clone(object);
    CHECK(copy && wo_json_int(copy, "count", 0) == 7);
    CHECK(wo_json_remove(copy, "path"));
    CHECK(!wo_json_has(copy, "path") && wo_json_has(object, "path"));

    wo_json_free(copy);
    wo_json_free(again);
    wo_json_free(object);
    wo_buf_free(&compact);
    wo_buf_free(&pretty);
}

static void test_put_rejects_wrong_type(void) {
    WoJson *array = wo_json_new_array();
    CHECK(!wo_json_put(array, "x", wo_json_new_int(1)));
    CHECK(!wo_json_push(NULL, wo_json_new_int(1)));
    wo_json_free(array);
}

int main(void) {
    RUN(test_scalars_and_nesting);
    RUN(test_case_insensitive_lookup);
    RUN(test_bom_and_whitespace);
    RUN(test_rejects);
    RUN(test_write_round_trip);
    RUN(test_put_rejects_wrong_type);
    return wo_test_finish();
}
