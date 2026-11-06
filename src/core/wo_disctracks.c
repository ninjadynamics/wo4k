#include "core/wo_disctracks.h"

#include <string.h>

#define MDS_HEADER_BYTES 88u
#define MDS_SESSION_BYTES 24u
#define MDS_TRACK_BYTES 80u
#define CUE_LEAD_IN_FRAMES 150u
#define MSF_MAX_DIGITS 9u

void wo_tracks_free(WoTrackList *list) {
    if (!list) {
        return;
    }
    for (size_t i = 0; i < list->count; i++) {
        wo_free(list->items[i].image);
    }
    wo_free(list->items);
    memset(list, 0, sizeof(*list));
}

size_t wo_tracks_audio_count(const WoTrackList *list) {
    size_t count = 0;
    for (size_t i = 0; i < list->count; i++) {
        if (list->items[i].audio) {
            count++;
        }
    }
    return count;
}

const WoTrack *wo_tracks_first_data(const WoTrackList *list) {
    for (size_t i = 0; i < list->count; i++) {
        if (!list->items[i].audio) {
            return &list->items[i];
        }
    }
    return NULL;
}

/* Append a track; takes ownership of image (also on failure). */
static bool push_track(WoTrackList *list, const WoTrack *track, char *image, WoError *error) {
    if (list->count >= WO_TRACKS_MAX) {
        wo_free(image);
        return wo_fail(error, WO_ERR_FORMAT, "The disc descriptor lists more than %u tracks.",
                       WO_TRACKS_MAX);
    }
    if (list->count == list->cap) {
        size_t cap = list->cap ? list->cap * 2 : 16;
        WoTrack *grown = wo_realloc_array(list->items, cap, sizeof(*grown), error);
        if (!grown) {
            wo_free(image);
            return false;
        }
        list->items = grown;
        list->cap = cap;
    }
    WoTrack *slot = &list->items[list->count++];
    *slot = *track;
    slot->image = image;
    return true;
}

/* --- MDS ---------------------------------------------------------------------- */

bool wo_mds_parse(const uint8_t *data, size_t len, const char *image, WoTrackList *out,
                  WoError *error) {
    memset(out, 0, sizeof(*out));
    if (len < MDS_HEADER_BYTES || memcmp(data, "MEDIA DESCRIPTOR", 16) != 0 || data[16] != 1) {
        return wo_fail(error, WO_ERR_UNSUPPORTED, "Only MDS version 1 images are supported.");
    }
    uint16_t sessions = wo_le16(data + 20);
    uint64_t table = wo_le32(data + 80);
    for (uint64_t s = 0; s < sessions; s++) {
        uint64_t session = table + s * MDS_SESSION_BYTES;
        if (session + MDS_SESSION_BYTES > len) {
            wo_tracks_free(out);
            return wo_fail(error, WO_ERR_FORMAT, "The MDS file has a truncated session table.");
        }
        unsigned count = data[session + 10];
        uint64_t start = wo_le32(data + session + 20);
        for (uint64_t i = 0; i < count; i++) {
            uint64_t at = start + i * MDS_TRACK_BYTES;
            if (at + MDS_TRACK_BYTES > len) {
                wo_tracks_free(out);
                return wo_fail(error, WO_ERR_FORMAT, "The MDS file has a truncated track table.");
            }
            const uint8_t *record = data + at;
            unsigned number = record[4];
            if (number < 1 || number > 99) {
                continue; /* lead-in/lead-out and session entries */
            }
            uint64_t extra = wo_le32(record + 12);
            if (extra + 8 > len) {
                wo_tracks_free(out);
                return wo_fail(error, WO_ERR_FORMAT, "The MDS file has a truncated track length.");
            }
            WoTrack track = {0};
            track.number = (int)number;
            track.audio = (record[0] & 7u) == 1;
            track.stride = wo_le16(record + 16);
            track.offset = wo_le64(record + 40);
            track.frames = wo_le32(data + extra + 4);
            track.start_frame = ((uint64_t)record[9] * 60u + record[10]) * 75u + record[11];
            char *copy = wo_strdup(image, error);
            if (!copy || !push_track(out, &track, copy, error)) {
                wo_tracks_free(out);
                return false;
            }
        }
    }
    return true;
}

/* --- CUE ---------------------------------------------------------------------- */

typedef struct Cursor {
    const char *p;
    const char *end;
} Cursor;

static bool is_space(char c) {
    return c == ' ' || c == '\t' || c == '\v' || c == '\f' || c == '\r' || c == '\n';
}

static bool is_digit(char c) {
    return c >= '0' && c <= '9';
}

static size_t skip_spaces(Cursor *c) {
    size_t skipped = 0;
    while (c->p < c->end && is_space(*c->p)) {
        c->p++;
        skipped++;
    }
    return skipped;
}

static bool take_word(Cursor *c, const char *word) {
    size_t n = strlen(word);
    if ((size_t)(c->end - c->p) < n) {
        return false;
    }
    for (size_t i = 0; i < n; i++) {
        if (wo_ascii_lower(c->p[i]) != wo_ascii_lower(word[i])) {
            return false;
        }
    }
    c->p += n;
    return true;
}

/* One or more digits, at most max_digits of them, as a number. */
static bool take_number(Cursor *c, size_t max_digits, uint64_t *out) {
    uint64_t value = 0;
    size_t digits = 0;
    while (c->p < c->end && is_digit(*c->p)) {
        if (digits == max_digits) {
            return false;
        }
        value = value * 10u + (uint64_t)(*c->p - '0');
        digits++;
        c->p++;
    }
    *out = value;
    return digits > 0;
}

typedef struct CueRecord {
    int number;
    bool audio;
    uint32_t stride;
    int64_t index0;
    int64_t index1;
    size_t file; /* index into the resolved file table */
} CueRecord;

typedef struct CueFile {
    char *path;
    uint64_t size;
} CueFile;

typedef struct CueState {
    CueRecord records[WO_TRACKS_MAX];
    size_t record_count;
    CueFile *files;
    size_t file_count;
    size_t file_cap;
    bool have_file;
} CueState;

static void cue_state_free(CueState *state) {
    for (size_t i = 0; i < state->file_count; i++) {
        wo_free(state->files[i].path);
    }
    wo_free(state->files);
}

static bool cue_file_line(CueState *state, Cursor line, WoCueFileFn resolve, void *context,
                          WoError *error) {
    const char *bad = "Unsupported CUE FILE line; only quoted BINARY files are supported.";
    if (line.p >= line.end || *line.p != '"') {
        return wo_fail(error, WO_ERR_UNSUPPORTED, "%s", bad);
    }
    line.p++;
    const char *name = line.p;
    while (line.p < line.end && *line.p != '"') {
        line.p++;
    }
    size_t name_len = (size_t)(line.p - name);
    if (line.p >= line.end || name_len == 0) {
        return wo_fail(error, WO_ERR_UNSUPPORTED, "%s", bad);
    }
    line.p++;
    if (skip_spaces(&line) == 0 || !take_word(&line, "BINARY")) {
        return wo_fail(error, WO_ERR_UNSUPPORTED, "%s", bad);
    }
    if (state->file_count >= WO_TRACKS_MAX) {
        return wo_fail(error, WO_ERR_FORMAT, "The CUE sheet lists more than %u files.",
                       WO_TRACKS_MAX);
    }
    if (state->file_count == state->file_cap) {
        size_t cap = state->file_cap ? state->file_cap * 2 : 8;
        CueFile *grown = wo_realloc_array(state->files, cap, sizeof(*grown), error);
        if (!grown) {
            return false;
        }
        state->files = grown;
        state->file_cap = cap;
    }
    char *text = wo_strndup(name, name_len, error);
    if (!text) {
        return false;
    }
    if (memchr(text, 0, name_len)) {
        wo_free(text);
        return wo_fail(error, WO_ERR_FORMAT, "The CUE sheet contains a damaged FILE name.");
    }
    CueFile file = {NULL, 0};
    bool ok = resolve(context, text, &file.path, &file.size, error);
    wo_free(text);
    if (!ok) {
        return false;
    }
    if (!file.path) {
        return wo_fail(error, WO_ERR_INTERNAL, "A CUE FILE name was not resolved.");
    }
    state->files[state->file_count++] = file;
    state->have_file = true;
    return true;
}

static bool cue_track_line(CueState *state, Cursor line, WoError *error) {
    uint64_t number = 0;
    const char *bad = "The CUE sheet has a damaged TRACK line.";
    if (skip_spaces(&line) == 0 || !take_number(&line, MSF_MAX_DIGITS, &number) || skip_spaces(&line) == 0 ||
        line.p >= line.end) {
        return wo_fail(error, WO_ERR_FORMAT, "%s", bad);
    }
    if (number > 99) {
        return wo_fail(error, WO_ERR_FORMAT, "CUE track number %llu is out of range.",
                       (unsigned long long)number);
    }
    const char *kind = line.p;
    while (line.p < line.end && !is_space(*line.p)) {
        line.p++;
    }
    size_t kind_len = (size_t)(line.p - kind);
    if (!state->have_file) {
        return wo_fail(error, WO_ERR_FORMAT, "The CUE sheet has a TRACK line before any FILE line.");
    }
    if (state->record_count >= WO_TRACKS_MAX) {
        return wo_fail(error, WO_ERR_FORMAT, "The CUE sheet lists more than %u tracks.",
                       WO_TRACKS_MAX);
    }
    CueRecord *record = &state->records[state->record_count++];
    memset(record, 0, sizeof(*record));
    record->number = (int)number;
    record->audio = kind_len == 5 && wo_str_istarts_with(kind, "AUDIO");
    record->stride = 2352;
    static const char *const strides[] = {"/2048", "/2352", "/2448"};
    static const uint32_t values[] = {2048, 2352, 2448};
    for (size_t i = 0; i < WO_COUNT(strides); i++) {
        if (kind_len >= 5 && wo_str_istarts_with(kind + kind_len - 5, strides[i])) {
            record->stride = values[i];
        }
    }
    record->index0 = -1;
    record->index1 = -1;
    record->file = state->file_count - 1;
    return true;
}

/* "INDEX 00 mm:ss:ff" or "INDEX 01 mm:ss:ff"; other indexes are ignored. */
static bool cue_index_line(CueState *state, Cursor line, WoError *error) {
    if (skip_spaces(&line) == 0 || (size_t)(line.end - line.p) < 2 || line.p[0] != '0' ||
        (line.p[1] != '0' && line.p[1] != '1')) {
        return true;
    }
    bool first = line.p[1] == '1';
    line.p += 2;
    if (skip_spaces(&line) == 0 || line.p >= line.end || !is_digit(*line.p)) {
        return true;
    }
    uint64_t parts[3];
    for (int i = 0; i < 3; i++) {
        if (i > 0) {
            if (line.p >= line.end || *line.p != ':') {
                return true;
            }
            line.p++;
            if (line.p >= line.end || !is_digit(*line.p)) {
                return true;
            }
        }
        if (!take_number(&line, MSF_MAX_DIGITS, &parts[i])) {
            return wo_fail(error, WO_ERR_FORMAT, "The CUE sheet has an invalid INDEX time.");
        }
    }
    if (state->record_count == 0) {
        return wo_fail(error, WO_ERR_FORMAT, "The CUE sheet has an INDEX line before any TRACK line.");
    }
    int64_t frame = (int64_t)((parts[0] * 60u + parts[1]) * 75u + parts[2]);
    CueRecord *record = &state->records[state->record_count - 1];
    if (first) {
        record->index1 = frame;
    } else {
        record->index0 = frame;
    }
    return true;
}

static bool cue_line(CueState *state, Cursor line, WoCueFileFn resolve, void *context,
                     WoError *error) {
    skip_spaces(&line);
    Cursor rest = line;
    if (take_word(&rest, "FILE") && rest.p < rest.end && is_space(*rest.p)) {
        skip_spaces(&rest);
        return cue_file_line(state, rest, resolve, context, error);
    }
    rest = line;
    if (take_word(&rest, "TRACK") && (rest.p == rest.end || is_space(*rest.p))) {
        return cue_track_line(state, rest, error);
    }
    rest = line;
    if (take_word(&rest, "INDEX")) {
        return cue_index_line(state, rest, error);
    }
    return true;
}

/* Sector count of a file, rounded to nearest with ties to even, exactly as
 * the previous implementation computed it. */
static uint64_t file_frames(uint64_t size, uint32_t stride) {
    uint64_t q = size / stride;
    uint64_t r = size % stride;
    if (r * 2 > stride || (r * 2 == stride && (q & 1u))) {
        q++;
    }
    return q;
}

static bool cue_layout(const CueState *state, WoTrackList *out, WoError *error) {
    uint64_t base_frame = CUE_LEAD_IN_FRAMES;
    uint64_t previous_size = 0;
    size_t last_file = SIZE_MAX;
    for (size_t i = 0; i < state->record_count; i++) {
        const CueRecord *r = &state->records[i];
        if (r->index1 < 0) {
            return wo_fail(error, WO_ERR_FORMAT, "CUE track %d has no INDEX 01.", r->number);
        }
        const CueFile *file = &state->files[r->file];
        bool same_file = last_file != SIZE_MAX &&
                         wo_str_ieq(state->files[last_file].path, file->path);
        if (!same_file) {
            base_frame += previous_size;
            last_file = r->file;
            previous_size = file_frames(file->size, r->stride);
        }
        int64_t end = (int64_t)previous_size;
        if (i + 1 < state->record_count) {
            const CueRecord *next = &state->records[i + 1];
            if (wo_str_ieq(state->files[next->file].path, file->path)) {
                end = next->index0 >= 0 ? next->index0 : next->index1;
            }
        }
        int64_t frames = end - r->index1;
        if (frames <= 0) {
            return wo_fail(error, WO_ERR_FORMAT, "CUE track %d has an invalid length.", r->number);
        }
        WoTrack track = {0};
        track.number = r->number;
        track.audio = r->audio;
        track.stride = r->stride;
        track.offset = (uint64_t)r->index1 * r->stride;
        track.frames = (uint64_t)frames;
        track.start_frame = base_frame + (uint64_t)r->index1;
        char *image = wo_strdup(file->path, error);
        if (!image || !push_track(out, &track, image, error)) {
            return false;
        }
    }
    return true;
}

bool wo_cue_parse(const char *text, size_t len, WoCueFileFn resolve, void *context,
                  WoTrackList *out, WoError *error) {
    memset(out, 0, sizeof(*out));
    if (len > WO_DESCRIPTOR_MAX_BYTES) {
        return wo_fail(error, WO_ERR_LIMIT, "The CUE sheet is larger than %u bytes.",
                       WO_DESCRIPTOR_MAX_BYTES);
    }
    if (len >= 3 && memcmp(text, "\xEF\xBB\xBF", 3) == 0) {
        text += 3;
        len -= 3;
    }
    CueState *state = wo_alloc(sizeof(*state), error);
    if (!state) {
        return false;
    }
    const char *end = text + len;
    const char *p = text;
    bool ok = true;
    while (ok && p < end) {
        const char *line_end = p;
        while (line_end < end && *line_end != '\n' && *line_end != '\r') {
            line_end++;
        }
        Cursor line = {p, line_end};
        ok = cue_line(state, line, resolve, context, error);
        p = line_end;
        if (p < end && *p == '\r') {
            p++;
            if (p < end && *p == '\n') {
                p++;
            }
        } else if (p < end && *p == '\n') {
            p++;
        }
    }
    ok = ok && cue_layout(state, out, error);
    cue_state_free(state);
    wo_free(state);
    if (!ok) {
        wo_tracks_free(out);
    }
    return ok;
}

/* --- tracks.ini ----------------------------------------------------------------- */

bool wo_tracks_write_ini(const WoTrackList *list, WoBuf *out, WoError *error) {
    if (!wo_buf_printf(out, error, "[CD]\r\nTracks=%zu\r\n\r\n", list->count)) {
        return false;
    }
    for (size_t i = 0; i < list->count; i++) {
        const WoTrack *t = &list->items[i];
        if (!wo_buf_printf(out, error, "[Track%02d]\r\nStartFrame=%llu\r\nFrames=%llu\r\n\r\n",
                           t->number, (unsigned long long)t->start_frame,
                           (unsigned long long)t->frames)) {
            return false;
        }
    }
    return true;
}
