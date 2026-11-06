/* Shared declarations for the fuzz harnesses. Each fuzz_*.c defines
 * LLVMFuzzerTestOneInput and links only portable core sources, so the same
 * file builds with libFuzzer (clang -fsanitize=fuzzer) or with the
 * standalone corpus runner in standalone.c. */
#ifndef WO_FUZZ_H
#define WO_FUZZ_H

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

/* A broken invariant is a finding: crash so the fuzzer reports it. */
#define FUZZ_ASSERT(condition)                                                 \
    do {                                                                       \
        if (!(condition)) {                                                    \
            abort();                                                           \
        }                                                                      \
    } while (0)

#endif
