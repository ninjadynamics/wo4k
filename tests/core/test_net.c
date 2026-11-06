/* Signature checks run everywhere. Network tests only run when
 * WO4K_NETWORK_TESTS=1, so offline builds stay deterministic. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/wo_fs.h"
#include "core/wo_net.h"
#include "wo_test.h"

static void test_unsigned_file(void) {
    WoSignature signature;
    WoError error = {0};
    CHECK_OK(wo_signature_check("build/tests/core/test_net.exe", &signature, &error), &error);
    CHECK(!signature.trusted);
    CHECK_FAILS(wo_signature_check("build/tests/core/missing.exe", &signature, &error), &error,
                WO_ERR_NOT_FOUND);
}

static void test_signed_file_if_present(void) {
    /* Git for Windows is Authenticode-signed with an embedded signature. */
    const char *candidates[] = {"C:\\Program Files\\Git\\bin\\git.exe",
                                "C:\\Program Files\\Git\\cmd\\git.exe"};
    for (size_t i = 0; i < WO_COUNT(candidates); i++) {
        if (!wo_is_file(candidates[i])) {
            continue;
        }
        WoSignature signature;
        WoError error = {0};
        CHECK_OK(wo_signature_check(candidates[i], &signature, &error), &error);
        CHECK(signature.trusted);
        CHECK(signature.subject[0] != 0);
        CHECK(strlen(signature.thumbprint) == 40);
        return;
    }
    printf("  (skipped: no embedded-signed reference file)\n");
}

static bool network_enabled(void) {
    const char *flag = getenv("WO4K_NETWORK_TESTS");
    return flag && strcmp(flag, "1") == 0;
}

static void test_https_get(void) {
    if (!network_enabled()) {
        printf("  (skipped: set WO4K_NETWORK_TESTS=1)\n");
        return;
    }
    WoBuf body = {0};
    WoError error = {0};
    CHECK_OK(wo_http_get("https://api.github.com/repos/ninjadynamics/wo4k", "application/vnd.github+json",
                         1 << 20, 30, NULL, &body, &error),
             &error);
    CHECK(body.len > 0 && strstr((const char *)body.data, "\"full_name\"") != NULL);
    CHECK_FAILS(wo_http_get("https://api.github.com/repos/ninjadynamics/wo4k", NULL, 10, 30, NULL,
                            &body, &error),
                &error, WO_ERR_LIMIT);
    CHECK_FAILS(wo_http_get("http://api.github.com/", NULL, 10, 30, NULL, &body, &error), &error,
                WO_ERR_UNSAFE);
    wo_buf_free(&body);
}

int main(void) {
    RUN(test_unsigned_file);
    RUN(test_signed_file_if_present);
    RUN(test_https_get);
    return wo_test_finish();
}
