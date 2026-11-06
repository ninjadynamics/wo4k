/* Minimal test harness. Each tests/test_*.c file is its own program:
 *
 *     static void test_something(void) { CHECK(1 + 1 == 2); }
 *     int main(void) { RUN(test_something); return wo_test_finish(); }
 *
 * Tests run with the repository root as the working directory. */
#ifndef WO_TEST_H
#define WO_TEST_H

#include "core/wo_base.h"

void wo_test_fail(const char *file, int line, const char *expression, const char *detail);
void wo_test_run(const char *name, void (*test)(void));
int wo_test_finish(void);
/* A fresh, empty scratch folder build/test-work/<name>; returns its full
 * path (owned by the harness until exit) or NULL after reporting a failure. */
const char *wo_test_dir(const char *name);
/* Path of a file under tests/fixtures. */
const char *wo_test_fixture(const char *relative);
bool wo_test_str_eq(const char *a, const char *b);

#define CHECK(condition)                                                    \
    do {                                                                    \
        if (!(condition)) {                                                 \
            wo_test_fail(__FILE__, __LINE__, #condition, NULL);             \
        }                                                                   \
    } while (0)

/* Evaluate a bool-returning call that reports through a WoError. */
#define CHECK_OK(call, error)                                               \
    do {                                                                    \
        wo_error_clear(error);                                              \
        if (!(call)) {                                                      \
            wo_test_fail(__FILE__, __LINE__, #call, (error)->message);      \
        }                                                                   \
    } while (0)

/* Expect failure of a given kind. */
#define CHECK_FAILS(call, error, expected_kind)                             \
    do {                                                                    \
        wo_error_clear(error);                                              \
        if (call) {                                                         \
            wo_test_fail(__FILE__, __LINE__, #call, "unexpectedly succeeded"); \
        } else if ((error)->kind != (expected_kind)) {                      \
            wo_test_fail(__FILE__, __LINE__, #call, (error)->message);      \
        }                                                                   \
    } while (0)

#define CHECK_STR(actual, expected)                                         \
    do {                                                                    \
        const char *actual_ = (actual);                                     \
        const char *expected_ = (expected);                                 \
        if (!wo_test_str_eq(actual_, expected_)) {                          \
            wo_test_fail(__FILE__, __LINE__, #actual " == " #expected,      \
                         actual_ ? actual_ : "(null)");                     \
        }                                                                   \
    } while (0)

#define RUN(test) wo_test_run(#test, test)

#endif
