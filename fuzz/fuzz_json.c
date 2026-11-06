/* JSON parsing; anything accepted must serialise and parse back, and the
 * output must be stable once written (the first write may normalise
 * numbers, such as -0 to 0). */
#include <string.h>

#include "core/wo_json.h"
#include "fuzz.h"

/* Write value, parse the text and return the reparsed value. */
static WoJson *round_trip(const WoJson *value, bool pretty, WoBuf *text) {
    WoError error = {0};
    WoJson *again = NULL;
    FUZZ_ASSERT(wo_json_write(value, pretty, text, &error));
    FUZZ_ASSERT(wo_json_parse((const char *)text->data, text->len, &again, &error));
    return again;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    WoError error = {0};
    WoJson *value = NULL;
    if (!wo_json_parse((const char *)data, size, &value, &error)) {
        return 0;
    }
    for (int pretty = 0; pretty < 2; pretty++) {
        WoBuf first = {0};
        WoBuf second = {0};
        WoBuf third = {0};
        WoJson *a = round_trip(value, pretty != 0, &first);
        WoJson *b = round_trip(a, pretty != 0, &second);
        WoJson *c = round_trip(b, pretty != 0, &third);
        FUZZ_ASSERT(second.len == third.len && memcmp(second.data, third.data, third.len) == 0);
        wo_json_free(c);
        wo_json_free(b);
        wo_json_free(a);
        wo_buf_free(&third);
        wo_buf_free(&second);
        wo_buf_free(&first);
    }
    WoJson *clone = wo_json_clone(value);
    FUZZ_ASSERT(clone != NULL);
    wo_json_free(clone);
    wo_json_free(value);
    return 0;
}
