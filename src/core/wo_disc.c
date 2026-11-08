#include "core/wo_disc.h"

#include <string.h>

#include "core/wo_cdaudio.h"
#include "core/wo_fs.h"

bool wo_mds_tracks(const char *mds_path, WoTrackList *out, WoError *error) {
    memset(out, 0, sizeof(*out));
    WoBuf data = {0};
    if (!wo_read_file(mds_path, WO_DESCRIPTOR_MAX_BYTES, &data, error)) {
        wo_buf_free(&data);
        return false;
    }
    char *image = wo_path_change_extension(mds_path, ".mdf", error);
    bool ok = image != NULL;
    bool header_ok = data.len >= 88 && memcmp(data.data, "MEDIA DESCRIPTOR", 16) == 0 &&
                     data.data[16] == 1;
    if (ok && header_ok && !wo_is_file(image)) {
        ok = wo_fail(error, WO_ERR_NOT_FOUND, "The MDF file that belongs to %s is missing.",
                     wo_path_basename(mds_path));
    }
    ok = ok && wo_mds_parse(data.data, data.len, image, out, error);
    wo_free(image);
    wo_buf_free(&data);
    return ok;
}

typedef struct CueResolver {
    const char *folder;
} CueResolver;

static bool resolve_cue_file(void *context, const char *name, char **path, uint64_t *size,
                             WoError *error) {
    const CueResolver *resolver = context;
    *path = NULL;
    char *full = wo_path_child(resolver->folder, name, error);
    if (!full) {
        wo_error_context(error, "The CUE sheet refers to an unsafe file name: ");
        return false;
    }
    WoFileInfo info;
    if (!wo_stat(full, &info, error)) {
        wo_free(full);
        return false;
    }
    if (!info.exists || info.is_dir) {
        wo_free(full);
        return wo_fail(error, WO_ERR_NOT_FOUND, "The CUE sheet refers to a missing file: %s", name);
    }
    *path = full;
    *size = info.size;
    return true;
}

bool wo_cue_tracks(const char *cue_path, WoTrackList *out, WoError *error) {
    memset(out, 0, sizeof(*out));
    char *full = wo_path_full(cue_path, error);
    char *folder = full ? wo_path_parent(full, error) : NULL;
    WoBuf text = {0};
    bool ok = folder != NULL;
    if (full && !folder) {
        ok = wo_fail(error, WO_ERR_NOT_FOUND, "Cannot find the folder of %s.", cue_path);
    }
    ok = ok && wo_read_file(full, WO_DESCRIPTOR_MAX_BYTES, &text, error);
    if (ok) {
        CueResolver resolver = {folder};
        ok = wo_cue_parse((const char *)text.data, text.len, resolve_cue_file, &resolver, out,
                          error);
        if (!ok) {
            wo_error_context(error, "%s: ", wo_path_basename(cue_path));
        }
    }
    wo_buf_free(&text);
    wo_free(folder);
    wo_free(full);
    return ok;
}

bool wo_disc_tracks(const char *path, WoTrackList *out, WoError *error) {
    memset(out, 0, sizeof(*out));
    const char *ext = wo_path_extension(path);
    if (wo_str_ieq(ext, ".mds") || wo_str_ieq(ext, ".mdf")) {
        char *mds = wo_path_change_extension(path, ".mds", error);
        bool ok = mds && wo_mds_tracks(mds, out, error);
        wo_free(mds);
        return ok;
    }
    if (wo_str_ieq(ext, ".cue") || wo_str_ieq(ext, ".bin")) {
        char *cue = wo_path_change_extension(path, ".cue", error);
        bool ok = cue && wo_cue_tracks(cue, out, error);
        wo_free(cue);
        return ok;
    }
    return true;
}

static bool has_cue_sibling(const char *bin) {
    char *cue = wo_path_change_extension(bin, ".cue", NULL);
    bool exists = cue && wo_is_file(cue);
    wo_free(cue);
    return exists;
}

bool wo_disc_data_track(const char *path, char **image, uint64_t *offset, WoError *error) {
    *image = NULL;
    *offset = 0;
    const char *ext = wo_path_extension(path);
    /* A .bin with no .cue beside it is a plain image, as the probe that
     * offered it treats it. */
    bool described = wo_str_ieq(ext, ".mds") || wo_str_ieq(ext, ".mdf") ||
                     wo_str_ieq(ext, ".cue") ||
                     (wo_str_ieq(ext, ".bin") && has_cue_sibling(path));
    if (!described) {
        *image = wo_path_full(path, error);
        return *image != NULL;
    }
    WoTrackList tracks;
    if (!wo_disc_tracks(path, &tracks, error)) {
        return false;
    }
    const WoTrack *data = wo_tracks_first_data(&tracks);
    bool ok = data != NULL;
    if (!ok) {
        wo_fail(error, WO_ERR_NOT_FOUND,
                "This disc image has no data track; it holds only CD audio.");
    } else {
        *image = wo_strdup(data->image, error);
        *offset = data->offset;
        ok = *image != NULL;
    }
    wo_tracks_free(&tracks);
    return ok;
}

static bool write_sink(void *context, const void *data, size_t len, WoError *error) {
    return wo_file_write(context, data, len, error);
}

/* Create path, fill it through produce, and remove it again on failure. */
typedef bool (*ProduceFn)(void *context, WoFile *file, WoError *error);

static bool write_produced(const char *path, ProduceFn produce, void *context, WoError *error) {
    WoFile *file = NULL;
    if (!wo_file_create(path, &file, error)) {
        return false;
    }
    bool ok = produce(context, file, error);
    bool closed = wo_file_close(file, ok ? error : NULL);
    if (!ok || !closed) {
        wo_delete_file(path, NULL);
        return false;
    }
    return true;
}

typedef struct EntryJob {
    const WoIso *iso;
    const WoIsoEntry *entry;
} EntryJob;

static bool produce_entry(void *context, WoFile *file, WoError *error) {
    const EntryJob *job = context;
    return wo_iso_stream_entry(job->iso, job->entry, write_sink, file, error);
}

bool wo_disc_extract_entry(const WoIso *iso, const WoIsoEntry *entry, const char *path,
                           WoError *error) {
    EntryJob job = {iso, entry};
    if (!write_produced(path, produce_entry, &job, error)) {
        wo_error_context(error, "Extracting %s from the disc: ", entry->path);
        return false;
    }
    return true;
}

bool wo_disc_import(const char *image, uint64_t offset, const char *destination,
                    WoDiscFilterFn filter, void *context, size_t *imported, WoError *error) {
    if (imported) {
        *imported = 0;
    }
    WoSource source;
    if (!wo_source_open_file(&source, image, error)) {
        return false;
    }
    WoIso iso;
    bool ok = wo_iso_open(&iso, &source, offset, error);
    size_t count = 0;
    uint64_t budget = source.size;
    for (size_t i = 0; ok && i < iso.count; i++) {
        const WoIsoEntry *entry = &iso.entries[i];
        if (entry->is_dir || !filter(context, entry->path)) {
            continue;
        }
        char *target = wo_disc_budget_take(&budget, entry, error)
                           ? wo_path_child(destination, entry->path, error)
                           : NULL;
        ok = target && wo_disc_extract_entry(&iso, entry, target, error);
        wo_free(target);
        count += ok ? 1 : 0;
    }
    wo_iso_close(&iso);
    wo_source_close(&source);
    if (imported) {
        *imported = count;
    }
    return ok;
}

bool wo_disc_budget_take(uint64_t *budget, const WoIsoEntry *entry, WoError *error) {
    if (entry->length > *budget) {
        return wo_fail(error, WO_ERR_LIMIT,
                       "The disc image lists more data than it holds (at \"%s\").", entry->path);
    }
    *budget -= entry->length;
    return true;
}

bool wo_disc_import_image(const char *path, const char *destination, WoDiscFilterFn filter,
                          void *context, size_t *imported, WoError *error) {
    char *image = NULL;
    uint64_t offset = 0;
    if (!wo_disc_data_track(path, &image, &offset, error)) {
        return false;
    }
    bool ok = wo_disc_import(image, offset, destination, filter, context, imported, error);
    wo_free(image);
    return ok;
}

typedef struct AudioJob {
    const WoSource *source;
    uint64_t offset;
    uint64_t sectors;
    uint32_t stride;
} AudioJob;

static bool produce_audio(void *context, WoFile *file, WoError *error) {
    const AudioJob *job = context;
    return wo_cdaudio_to_wav(job->source, job->offset, job->sectors, job->stride, write_sink, file,
                             error);
}

bool wo_cdaudio_extract(const char *image, uint64_t offset, uint64_t sectors, uint32_t stride,
                        const char *output, WoError *error) {
    WoSource source;
    if (!wo_source_open_file(&source, image, error)) {
        return false;
    }
    bool ok = wo_cdaudio_check(source.size, offset, sectors, stride, error);
    AudioJob job = {&source, offset, sectors, stride};
    ok = ok && write_produced(output, produce_audio, &job, error);
    wo_source_close(&source);
    if (!ok) {
        wo_error_context(error, "Converting CD audio to %s: ", wo_path_basename(output));
    }
    return ok;
}
