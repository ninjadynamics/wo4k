#include "core/wo_music.h"

#include <stdio.h>
#include <string.h>

#include "core/wo_disc.h"
#include "core/wo_fs.h"
#include "core/wo_hash.h"
#include "core/wo_tree.h"

#define TOC_MAX_BYTES (1024u * 1024u)
#define TOC_NAME "tracks.ini"

unsigned wo_music_bank_tracks(WoMusicBank bank) {
    return bank == WO_MUSIC_PC ? 10u : 12u;
}

const char *wo_music_bank_label(WoMusicBank bank) {
    return bank == WO_MUSIC_PC ? "Pc" : "Psx";
}

const char *wo_music_bank_folder(WoMusicBank bank) {
    return bank == WO_MUSIC_PC ? "music/pc" : "music/psx";
}

static bool is_space(char c) {
    return c == ' ' || c == '\t' || c == '\v' || c == '\f' || c == '\r' || c == '\n';
}

bool wo_music_toc_matches(const char *text, size_t len, unsigned expected) {
    char number[16];
    snprintf(number, sizeof(number), "%u", expected);
    size_t number_len = strlen(number);
    static const char word[] = "tracks";
    size_t word_len = sizeof(word) - 1;
    for (size_t start = 0; start < len; start++) {
        if (start > 0 && text[start - 1] != '\n') {
            continue;
        }
        if (len - start < word_len) {
            break;
        }
        bool same = true;
        for (size_t i = 0; i < word_len && same; i++) {
            same = wo_ascii_lower(text[start + i]) == word[i];
        }
        if (!same) {
            continue;
        }
        size_t p = start + word_len;
        while (p < len && is_space(text[p])) {
            p++;
        }
        if (p >= len || text[p] != '=') {
            continue;
        }
        p++;
        while (p < len && is_space(text[p])) {
            p++;
        }
        if (len - p < number_len || memcmp(text + p, number, number_len) != 0) {
            continue;
        }
        /* The rest of the line may only be whitespace. */
        for (p += number_len;; p++) {
            if (p == len || text[p] == '\n') {
                return true;
            }
            if (!is_space(text[p])) {
                break;
            }
        }
    }
    return false;
}

/* Read a small text file as the previous installer saw it: UTF-8 or UTF-16
 * (by byte-order mark), with every non-ASCII character replaced by '?'. */
static bool read_ascii_text(const char *path, WoBuf *out, WoError *error) {
    WoBuf raw = {0};
    if (!wo_read_file(path, TOC_MAX_BYTES, &raw, error)) {
        wo_buf_free(&raw);
        return false;
    }
    const uint8_t *p = raw.data;
    size_t len = raw.len;
    bool ok = true;
    if (len >= 2 && ((p[0] == 0xFF && p[1] == 0xFE) || (p[0] == 0xFE && p[1] == 0xFF))) {
        bool little = p[0] == 0xFF;
        for (size_t i = 2; ok && i + 1 < len; i += 2) {
            unsigned unit = little ? (unsigned)(p[i] | (p[i + 1] << 8))
                                   : (unsigned)((p[i] << 8) | p[i + 1]);
            ok = wo_buf_append_byte(out, unit < 0x80 ? (uint8_t)unit : (uint8_t)'?', error);
        }
    } else {
        size_t i = 0;
        if (len >= 3 && p[0] == 0xEF && p[1] == 0xBB && p[2] == 0xBF) {
            i = 3;
        }
        while (ok && i < len) {
            uint8_t c = p[i++];
            if (c < 0x80) {
                ok = wo_buf_append_byte(out, c, error);
                continue;
            }
            /* One '?' per multi-byte sequence. */
            size_t extra = (c & 0xE0) == 0xC0 ? 1 : (c & 0xF0) == 0xE0 ? 2 : (c & 0xF8) == 0xF0 ? 3 : 0;
            while (extra > 0 && i < len && (p[i] & 0xC0) == 0x80) {
                i++;
                extra--;
            }
            ok = wo_buf_append_byte(out, (uint8_t)'?', error);
        }
    }
    wo_buf_free(&raw);
    return ok && wo_buf_terminate(out, error);
}

static bool folder_has_toc(const char *dir, unsigned expected, bool *found, WoError *error) {
    *found = false;
    char *toc = wo_path_join(dir, TOC_NAME, error);
    if (!toc) {
        return false;
    }
    bool ok = true;
    if (wo_is_file(toc)) {
        WoBuf text = {0};
        ok = read_ascii_text(toc, &text, error);
        *found = ok && wo_music_toc_matches((const char *)text.data, text.len, expected);
        wo_buf_free(&text);
    }
    wo_free(toc);
    return ok;
}

bool wo_music_find_folder(const char *source, WoMusicBank bank, char **folder, WoError *error) {
    *folder = NULL;
    if (!source || !*source || !wo_is_dir(source)) {
        return true;
    }
    static const char *const choices[] = {"", "music/pc", "music/psx", "Music/PS1", "Music"};
    unsigned expected = wo_music_bank_tracks(bank);
    for (size_t i = 0; i < WO_COUNT(choices); i++) {
        char *dir = choices[i][0] ? wo_path_join(source, choices[i], error) : wo_strdup(source, error);
        if (!dir) {
            return false;
        }
        bool found = false;
        bool ok = folder_has_toc(dir, expected, &found, error);
        if (ok && found) {
            *folder = wo_path_full(dir, error);
            ok = *folder != NULL;
        }
        wo_free(dir);
        if (!ok || found) {
            return ok;
        }
    }
    return true;
}

/* track00.wav .. track99.mp3 and tracks.ini */
static bool is_music_file_name(const char *name) {
    if (wo_str_ieq(name, TOC_NAME)) {
        return true;
    }
    return strlen(name) == 11 && wo_str_istarts_with(name, "track") && name[5] >= '0' &&
           name[5] <= '9' && name[6] >= '0' && name[6] <= '9' &&
           (wo_str_iends_with(name, ".wav") || wo_str_iends_with(name, ".mp3"));
}

static bool copy_music_folder(const char *folder, const char *destination, WoError *error) {
    if (!wo_mkdirs(destination, error)) {
        return false;
    }
    WoDirList list = {0};
    if (!wo_list_dir(folder, &list, error)) {
        return false;
    }
    bool ok = true;
    for (size_t i = 0; ok && i < list.count; i++) {
        const WoDirEntry *entry = &list.items[i];
        if (entry->is_dir || !is_music_file_name(entry->name)) {
            continue;
        }
        char *from = wo_path_join(folder, entry->name, error);
        char *to = from ? wo_path_child(destination, entry->name, error) : NULL;
        ok = to && wo_copy_file(from, to, error);
        wo_free(to);
        wo_free(from);
    }
    wo_dirlist_free(&list);
    return ok;
}

static bool list_descriptors(const char *source, WoStrList *out, WoError *error) {
    if (!wo_is_dir(source)) {
        return wo_strlist_push(out, source, error);
    }
    WoTreeList tree;
    if (!wo_tree_list(source, &tree, error)) {
        return false;
    }
    bool ok = true;
    for (size_t i = 0; ok && i < tree.count; i++) {
        const WoTreeEntry *entry = &tree.items[i];
        const char *ext = wo_path_extension(entry->path);
        if (!entry->is_dir && (wo_str_ieq(ext, ".mds") || wo_str_ieq(ext, ".cue"))) {
            ok = wo_strlist_push(out, entry->path, error);
        }
    }
    wo_tree_free(&tree);
    return ok;
}

static bool has_cue_sibling(const char *bin) {
    char *cue = wo_path_change_extension(bin, ".cue", NULL);
    bool exists = cue && wo_is_file(cue);
    wo_free(cue);
    return exists;
}

static bool choose_tracks(const WoStrList *descriptors, size_t wanted, bool allow_missing,
                          WoTrackList *chosen, bool *found, WoError *error) {
    *found = false;
    for (size_t i = 0; i < descriptors->count; i++) {
        const char *descriptor = descriptors->items[i];
        if (allow_missing && wo_str_ieq(wo_path_extension(descriptor), ".bin") &&
            !has_cue_sibling(descriptor)) {
            continue;
        }
        WoTrackList tracks;
        if (!wo_disc_tracks(descriptor, &tracks, error)) {
            return false;
        }
        if (wo_tracks_audio_count(&tracks) == wanted) {
            *chosen = tracks;
            *found = true;
            return true;
        }
        wo_tracks_free(&tracks);
    }
    return true;
}

static bool convert_tracks(const WoTrackList *tracks, const char *destination, WoError *error) {
    WoBuf toc = {0};
    bool ok = wo_tracks_write_ini(tracks, &toc, error);
    for (size_t i = 0; ok && i < tracks->count; i++) {
        const WoTrack *t = &tracks->items[i];
        if (!t->audio) {
            continue;
        }
        char name[32];
        snprintf(name, sizeof(name), "track%02d.wav", t->number);
        char *target = wo_path_child(destination, name, error);
        ok = target && wo_cdaudio_extract(t->image, t->offset, t->frames, t->stride, target, error);
        wo_free(target);
    }
    char *toc_path = ok ? wo_path_child(destination, TOC_NAME, error) : NULL;
    ok = toc_path && wo_write_file_atomic(toc_path, toc.data, toc.len, error);
    wo_free(toc_path);
    wo_buf_free(&toc);
    return ok;
}

bool wo_music_extract(const char *source, const char *destination, WoMusicBank bank,
                      bool allow_missing, bool *extracted, WoError *error) {
    *extracted = false;
    char *full = wo_path_full(source, error);
    if (!full) {
        return false;
    }
    if (!wo_exists(full)) {
        wo_fail(error, WO_ERR_NOT_FOUND, "The music source does not exist: %s", full);
        wo_free(full);
        return false;
    }
    char *folder = NULL;
    bool ok = wo_music_find_folder(full, bank, &folder, error);
    if (ok && folder) {
        ok = copy_music_folder(folder, destination, error);
        *extracted = ok;
        wo_free(folder);
        wo_free(full);
        return ok;
    }
    WoStrList descriptors = {0};
    WoTrackList chosen = {0};
    bool found = false;
    size_t wanted = wo_music_bank_tracks(bank) - 1u;
    ok = ok && list_descriptors(full, &descriptors, error) &&
         choose_tracks(&descriptors, wanted, allow_missing, &chosen, &found, error);
    if (ok && !found && !allow_missing) {
        ok = wo_fail(error, WO_ERR_NOT_FOUND,
                     "No %s CD audio found. Use a matching MDS/MDF or CUE/BIN set, its ZIP, or an "
                     "existing music folder. A data-only ISO has no CD audio.",
                     wo_music_bank_label(bank));
    }
    if (ok && found) {
        ok = convert_tracks(&chosen, destination, error);
        *extracted = ok;
    }
    wo_tracks_free(&chosen);
    wo_strlist_free(&descriptors);
    wo_free(full);
    return ok;
}

static bool files_identical(const char *a, const char *b, bool *same, WoError *error) {
    char ha[WO_SHA256_HEX_SIZE];
    char hb[WO_SHA256_HEX_SIZE];
    *same = false;
    if (!wo_sha256_file(a, ha, error) || !wo_sha256_file(b, hb, error)) {
        return false;
    }
    *same = wo_digest_eq(ha, hb);
    return true;
}

static bool install_song(const char *folder, const char *destination, unsigned track,
                         WoError *error) {
    char stem[16];
    snprintf(stem, sizeof(stem), "track%02u", track);
    char name[32];
    snprintf(name, sizeof(name), "%s.wav", stem);
    char *path = wo_path_join(folder, name, error);
    if (path && !wo_is_file(path)) {
        snprintf(name, sizeof(name), "%s.mp3", stem);
        wo_free(path);
        path = wo_path_join(folder, name, error);
    }
    if (!path) {
        return false;
    }
    if (!wo_is_file(path)) {
        wo_free(path);
        return wo_fail(error, WO_ERR_NOT_FOUND, "Missing music: %s", stem);
    }
    bool is_wav = wo_str_iends_with(name, ".wav");
    char *target = wo_path_child(destination, name, error);
    bool ok = target != NULL;
    if (ok && wo_is_file(target)) {
        bool same = false;
        ok = files_identical(path, target, &same, error);
        if (ok && !same) {
            ok = wo_copy_file(path, target, error);
        }
    } else if (ok) {
        ok = wo_copy_file(path, target, error);
    }
    /* The game prefers WAV. Remove the other encoding only now that its
     * replacement is installed, so a stale WAV can never hide a new MP3. */
    snprintf(name, sizeof(name), "%s%s", stem, is_wav ? ".mp3" : ".wav");
    char *alternate = ok ? wo_path_child(destination, name, error) : NULL;
    ok = alternate && wo_delete_file(alternate, error);
    wo_free(alternate);
    wo_free(target);
    wo_free(path);
    return ok;
}

bool wo_music_install(const char *source_folder, const char *game_root, WoMusicBank bank,
                      unsigned *songs, WoError *error) {
    if (songs) {
        *songs = 0;
    }
    char *full = wo_path_full(source_folder, error);
    char *folder = NULL;
    bool ok = full && wo_music_find_folder(full, bank, &folder, error);
    if (ok && !folder) {
        ok = wo_fail(error, WO_ERR_NOT_FOUND,
                     "No %s soundtrack with a matching tracks.ini was found in %s.",
                     wo_music_bank_label(bank), full);
    }
    char *destination = ok ? wo_path_child(game_root, wo_music_bank_folder(bank), error) : NULL;
    char *toc_path = destination ? wo_path_join(folder, TOC_NAME, error) : NULL;
    WoBuf toc = {0};
    ok = toc_path && read_ascii_text(toc_path, &toc, error) && wo_mkdirs(destination, error);
    unsigned count = wo_music_bank_tracks(bank);
    for (unsigned track = 2; ok && track <= count; track++) {
        ok = install_song(folder, destination, track, error);
    }
    char *target_toc = ok ? wo_path_child(destination, TOC_NAME, error) : NULL;
    ok = target_toc != NULL;
    if (ok) {
        bool same = false;
        if (wo_is_file(target_toc)) {
            WoBuf current = {0};
            ok = read_ascii_text(target_toc, &current, error);
            same = ok && current.len == toc.len && memcmp(current.data, toc.data, toc.len) == 0;
            wo_buf_free(&current);
        }
        if (ok && !same) {
            ok = wo_write_file_atomic(target_toc, toc.data, toc.len, error);
        }
    }
    if (ok && songs) {
        *songs = count - 1u;
    }
    wo_free(target_toc);
    wo_buf_free(&toc);
    wo_free(toc_path);
    wo_free(destination);
    wo_free(folder);
    wo_free(full);
    return ok;
}

static bool read_header(const char *path, uint8_t header[12], size_t *got, WoError *error) {
    *got = 0;
    WoFile *file = NULL;
    if (!wo_file_open_read(path, &file, error)) {
        return false;
    }
    bool ok = true;
    while (ok && *got < 12) {
        size_t n = 0;
        ok = wo_file_read(file, header + *got, 12 - *got, &n, error);
        if (n == 0) {
            break;
        }
        *got += n;
    }
    wo_file_close(file, NULL);
    return ok;
}

static bool song_header_ok(const char *path, bool is_wav, bool *valid, WoError *error) {
    uint8_t header[12];
    size_t got = 0;
    *valid = false;
    if (!read_header(path, header, &got, error)) {
        return false;
    }
    if (is_wav) {
        *valid = got >= 12 && memcmp(header, "RIFF", 4) == 0 && memcmp(header + 8, "WAVE", 4) == 0;
    } else {
        *valid = got >= 3 && (memcmp(header, "ID3", 3) == 0 ||
                              (header[0] == 0xFF && (header[1] & 0xE0) == 0xE0));
    }
    return true;
}

bool wo_music_files(const char *game_root, WoMusicBank bank, WoStrList *files, WoError *error) {
    memset(files, 0, sizeof(*files));
    char *folder = wo_path_join(game_root, wo_music_bank_folder(bank), error);
    if (!folder) {
        return false;
    }
    bool found = false;
    bool ok = true;
    if (wo_is_dir(folder)) {
        ok = folder_has_toc(folder, wo_music_bank_tracks(bank), &found, error);
    }
    if (!ok || !found) {
        wo_free(folder);
        return ok;
    }
    char *toc = wo_path_join(folder, TOC_NAME, error);
    ok = toc && wo_strlist_push_owned(files, toc, error);
    bool complete = ok;
    unsigned count = wo_music_bank_tracks(bank);
    for (unsigned track = 2; ok && complete && track <= count; track++) {
        char name[32];
        snprintf(name, sizeof(name), "track%02u.wav", track);
        char *path = wo_path_join(folder, name, error);
        bool is_wav = true;
        if (path && !wo_is_file(path)) {
            snprintf(name, sizeof(name), "track%02u.mp3", track);
            wo_free(path);
            path = wo_path_join(folder, name, error);
            is_wav = false;
        }
        WoFileInfo info;
        ok = path && wo_stat(path, &info, error);
        complete = ok && info.exists && !info.is_dir && info.size >= 4;
        bool valid = false;
        if (complete) {
            ok = song_header_ok(path, is_wav, &valid, error);
            complete = ok && valid;
        }
        if (complete) {
            ok = wo_strlist_push_owned(files, path, error);
            path = NULL;
        }
        wo_free(path);
    }
    if (!ok || !complete) {
        wo_strlist_free(files);
    }
    wo_free(folder);
    return ok;
}
