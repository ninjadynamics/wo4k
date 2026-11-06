/* Random-access byte sources. Parsers read disc images, archives and game
 * containers through WoSource so the same code works on files (Windows) and
 * on memory buffers (tests and fuzzing on any platform). */
#ifndef WO_IO_H
#define WO_IO_H

#include "core/wo_base.h"

typedef struct WoSource WoSource;
struct WoSource {
    uint64_t size;
    /* Read exactly len bytes at offset; offset + len has been bounds-checked
     * against size by wo_source_read before this is called. */
    bool (*read_at)(const WoSource *source, uint64_t offset, void *dst, size_t len,
                    WoError *error);
    void (*close)(WoSource *source);
    void *context;
    const uint8_t *memory;
    char name[260]; /* display name for error messages (UTF-8, may be truncated) */
};

/* The memory must outlive the source. */
void wo_source_from_memory(WoSource *source, const void *data, size_t len, const char *name);
/* Bounds-checked exact read; a read past the end is a WO_ERR_FORMAT failure
 * ("truncated"), since sources are always parsed data. */
bool wo_source_read(const WoSource *source, uint64_t offset, void *dst, size_t len,
                    WoError *error);
/* Read [offset, offset + len) into a new buffer, refusing more than max_len. */
bool wo_source_read_alloc(const WoSource *source, uint64_t offset, size_t len, size_t max_len,
                          uint8_t **out, WoError *error);
void wo_source_close(WoSource *source);

/* Receives produced bytes in order (an extractor's output); returning false
 * stops the producer and passes that error on. */
typedef bool (*WoSinkFn)(void *context, const void *data, size_t len, WoError *error);

#endif
