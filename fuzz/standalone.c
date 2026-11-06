/* Corpus runner for the fuzz harnesses when libFuzzer is not available:
 *
 *     fuzz_iso.exe fuzz/corpus/iso [more files or folders...]
 *
 * Runs LLVMFuzzerTestOneInput once per file (folders are read one level
 * deep), plus once per file with each of a few deterministic mutations
 * (truncations and byte flips), and reports how many inputs ran. A crash
 * or failed FUZZ_ASSERT aborts the program. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fuzz.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#endif

#define MAX_INPUT (16u * 1024u * 1024u)

static size_t runs;

static void run(const uint8_t *data, size_t size) {
    /* A private copy, so a harness reading past the end is caught by the
     * allocator's guard (or a sanitizer) rather than hitting other data. */
    uint8_t *copy = malloc(size ? size : 1);
    if (!copy) {
        fprintf(stderr, "out of memory\n");
        exit(2);
    }
    memcpy(copy, data, size);
    LLVMFuzzerTestOneInput(copy, size);
    free(copy);
    runs++;
}

static void run_mutations(const uint8_t *data, size_t size) {
    run(data, size);
    uint8_t *work = malloc(size ? size : 1);
    if (!work) {
        exit(2);
    }
    static const size_t cuts[] = {0, 1, 2, 7, 18, 33, 100, 2048};
    for (size_t i = 0; i < sizeof(cuts) / sizeof(cuts[0]); i++) {
        if (cuts[i] < size) {
            run(data, cuts[i]);
            run(data, size - cuts[i]);
        }
    }
    uint32_t seed = 0x9E3779B9u ^ (uint32_t)size;
    for (int round = 0; round < 32 && size > 0; round++) {
        memcpy(work, data, size);
        for (int flips = 0; flips < 1 + round % 4; flips++) {
            seed = seed * 1664525u + 1013904223u;
            size_t at = (seed >> 8) % size;
            work[at] ^= (uint8_t)(1u << (seed & 7u));
            if (round % 5 == 0) {
                work[at] = (seed & 16u) ? 0xFF : 0x00;
            }
        }
        run(work, size);
    }
    free(work);
}

static int run_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "cannot open %s\n", path);
        return 1;
    }
    uint8_t *data = malloc(MAX_INPUT);
    size_t size = data ? fread(data, 1, MAX_INPUT, f) : 0;
    fclose(f);
    if (!data) {
        return 1;
    }
    run_mutations(data, size);
    free(data);
    return 0;
}

static int run_path(const char *path) {
#ifdef _WIN32
    DWORD attributes = GetFileAttributesA(path);
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        fprintf(stderr, "missing %s\n", path);
        return 1;
    }
    if (!(attributes & FILE_ATTRIBUTE_DIRECTORY)) {
        return run_file(path);
    }
    char pattern[MAX_PATH];
    snprintf(pattern, sizeof(pattern), "%s\\*", path);
    WIN32_FIND_DATAA found;
    HANDLE find = FindFirstFileA(pattern, &found);
    int failures = 0;
    if (find == INVALID_HANDLE_VALUE) {
        return 0;
    }
    do {
        if (!(found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            char file[MAX_PATH];
            snprintf(file, sizeof(file), "%s\\%s", path, found.cFileName);
            failures += run_file(file);
        }
    } while (FindNextFileA(find, &found));
    FindClose(find);
    return failures;
#else
    DIR *dir = opendir(path);
    if (!dir) {
        return run_file(path);
    }
    int failures = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') {
            continue;
        }
        char file[4096];
        snprintf(file, sizeof(file), "%s/%s", path, entry->d_name);
        failures += run_file(file);
    }
    closedir(dir);
    return failures;
#endif
}

int main(int argc, char **argv) {
    int failures = 0;
    for (int i = 1; i < argc; i++) {
        failures += run_path(argv[i]);
    }
    printf("%s: %lu inputs ran\n", argc > 0 ? argv[0] : "fuzz", (unsigned long)runs);
    return failures ? 1 : 0;
}
