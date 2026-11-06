#include <string.h>

#include "core/wo_base.h"
#include "core/wo_io.h"
#include "wo_test.h"

static void test_checked_arithmetic(void) {
    size_t out = 0;
    CHECK(wo_add_size(1, 2, &out) && out == 3);
    CHECK(!wo_add_size(SIZE_MAX, 1, &out));
    CHECK(!wo_mul_size(SIZE_MAX / 2 + 1, 2, &out));
    uint64_t wide = 0;
    CHECK(wo_mul_u64(1u << 20, 1u << 20, &wide) && wide == (1ull << 40));
    CHECK(!wo_add_u64(UINT64_MAX, 1, &wide));
}

static void test_buffers(void) {
    WoBuf buf = {0};
    WoError error = {0};
    for (int i = 0; i < 1000; i++) {
        CHECK_OK(wo_buf_append_byte(&buf, (uint8_t)'a', &error), &error);
    }
    CHECK_OK(wo_buf_printf(&buf, &error, "%d-%s", 42, "x"), &error);
    CHECK(buf.len == 1004);
    char *text = wo_buf_detach(&buf);
    CHECK(text && strcmp(text + 1000, "42-x") == 0);
    CHECK(buf.data == NULL && buf.len == 0);
    wo_free(text);
}

static void test_strings(void) {
    char small[4];
    CHECK(!wo_copy_str(small, sizeof(small), "abcd"));
    CHECK_STR(small, "abc");
    CHECK(wo_copy_str(small, sizeof(small), "ab"));
    CHECK(wo_str_ieq("Track01/TRACK.WAD", "track01/track.wad"));
    CHECK(wo_str_iends_with("MOVIE.MPG", ".mpg"));
    CHECK(!wo_str_iends_with("a", ".mpg"));
    CHECK(wo_str_istarts_with("Common/MENU.DAT", "common/"));
    CHECK(wo_is_hex_digest("3c7a217df6bbc6c153a47abca9ace310ed642f7d6301cb56664d993d4c163ad2"));
    CHECK(!wo_is_hex_digest("3c7a"));
    CHECK(!wo_is_hex_digest("zc7a217df6bbc6c153a47abca9ace310ed642f7d6301cb56664d993d4c163ad2"));
    uint64_t value = 0;
    CHECK(wo_parse_u64("12345", 99999, &value) && value == 12345);
    CHECK(!wo_parse_u64("123456", 99999, &value));
    CHECK(!wo_parse_u64("12a", 99999, &value));
    CHECK(!wo_parse_u64("", 99999, &value));
}

static void test_errors(void) {
    WoError error = {0};
    CHECK(!wo_fail(&error, WO_ERR_FORMAT, "Bad %s", "header"));
    wo_error_context(&error, "Reading %s: ", "x.bin");
    CHECK_STR(error.message, "Reading x.bin: Bad header");
    CHECK(error.kind == WO_ERR_FORMAT);
    CHECK(!wo_fail(NULL, WO_ERR_IO, "ignored"));
}

static void test_memory_source(void) {
    static const uint8_t data[] = {1, 2, 3, 4, 5};
    WoSource source;
    wo_source_from_memory(&source, data, sizeof(data), "five");
    uint8_t out[3];
    WoError error = {0};
    CHECK_OK(wo_source_read(&source, 2, out, 3, &error), &error);
    CHECK(out[0] == 3 && out[2] == 5);
    CHECK_FAILS(wo_source_read(&source, 3, out, 3, &error), &error, WO_ERR_FORMAT);
    CHECK_FAILS(wo_source_read(&source, UINT64_MAX, out, 1, &error), &error, WO_ERR_FORMAT);
    uint8_t *copy = NULL;
    CHECK_FAILS(wo_source_read_alloc(&source, 0, 5, 4, &copy, &error), &error, WO_ERR_LIMIT);
    wo_source_close(&source);
}

static void test_string_list(void) {
    WoStrList list = {0};
    WoError error = {0};
    CHECK_OK(wo_strlist_push(&list, "b", &error), &error);
    CHECK_OK(wo_strlist_push(&list, "A", &error), &error);
    CHECK_OK(wo_strlist_push(&list, "c", &error), &error);
    wo_strlist_sort_i(&list);
    CHECK_STR(list.items[0], "A");
    CHECK_STR(list.items[2], "c");
    CHECK(wo_strlist_contains_i(&list, "B"));
    wo_strlist_free(&list);
}

int main(void) {
    RUN(test_checked_arithmetic);
    RUN(test_buffers);
    RUN(test_strings);
    RUN(test_errors);
    RUN(test_memory_source);
    RUN(test_string_list);
    return wo_test_finish();
}
