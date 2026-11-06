#include <math.h>
#include <string.h>

#include "core/wo_audioclean.h"
#include "core/wo_fs.h"
#include "core/wo_hash.h"
#include "core/wo_sfx.h"
#include "wo_test.h"

/* A minimal 22050 Hz mono 16-bit WAV. */
static void make_wav(WoBuf *b, double frequency) {
    size_t samples = 3000;
    uint8_t header[44] = {0};
    memcpy(header, "RIFF", 4);
    wo_put_le32(header + 4, (uint32_t)(36 + samples * 2));
    memcpy(header + 8, "WAVEfmt ", 8);
    wo_put_le32(header + 16, 16);
    wo_put_le16(header + 20, 1);
    wo_put_le16(header + 22, 1);
    wo_put_le32(header + 24, 22050);
    wo_put_le32(header + 28, 44100);
    wo_put_le16(header + 32, 2);
    wo_put_le16(header + 34, 16);
    memcpy(header + 36, "data", 4);
    wo_put_le32(header + 40, (uint32_t)(samples * 2));
    wo_buf_append(b, header, sizeof(header), NULL);
    for (size_t i = 0; i < samples; i++) {
        uint8_t s[2];
        wo_put_le16(s, (uint16_t)(int16_t)(5000.0 * sin((double)i * frequency) + (double)(i % 7) * 40));
        wo_buf_append(b, s, 2, NULL);
    }
}

static void hash_of(const WoBuf *b, char hash[WO_SHA256_HEX_SIZE]) {
    WoError error = {0};
    CHECK_OK(wo_sha256_mem(b->data, b->len, hash, &error), &error);
}

static char *file_hash(const char *dir, const char *name, char hash[WO_SHA256_HEX_SIZE]) {
    WoError error = {0};
    char *path = wo_path_join(dir, name, &error);
    CHECK_OK(path && wo_sha256_file(path, hash, &error), &error);
    return path;
}

static void test_install(void) {
    const char *root = wo_test_dir("sfx");
    if (!root) {
        return;
    }
    WoError error = {0};
    char *source = wo_path_join(root, "original", &error);
    char *game = wo_path_join(root, "game", &error);
    char *audio = wo_path_join(game, "audio", &error);
    WoBuf a = {0};
    WoBuf b = {0};
    make_wav(&a, 0.05);
    make_wav(&b, 0.09);
    char ha[WO_SHA256_HEX_SIZE];
    char hb[WO_SHA256_HEX_SIZE];
    hash_of(&a, ha);
    hash_of(&b, hb);
    char *pa = wo_path_join(source, "CLEAN.WAV", &error);
    char *pb = wo_path_join(source, "KEEP.WAV", &error);
    CHECK_OK(wo_write_file(pa, a.data, a.len, &error) && wo_write_file(pb, b.data, b.len, &error),
             &error);
    /* CLEAN.WAV is a known original; KEEP.WAV is already the cleaned output. */
    char *text = wo_format(NULL,
                           "{\"audio\":[{\"name\":\"CLEAN.WAV\",\"source_hashes\":[\"%s\"],"
                           "\"output_sha256\":\"%064d\"},{\"name\":\"KEEP.WAV\",\"source_hashes\":"
                           "[],\"output_sha256\":\"%s\"}]}",
                           ha, 0, hb);
    WoJson *manifest = NULL;
    CHECK_OK(wo_json_parse(text, strlen(text), &manifest, &error), &error);
    WoSfxOptions options = {manifest, source, game, false, false, NULL};
    WoSfxResult result;
    CHECK_OK(wo_sfx_install(&options, &result, &error), &error);
    CHECK(result.checked == 2 && result.updated == 2);
    uint8_t *cleaned = NULL;
    CHECK_OK(wo_audio_clean(a.data, a.len, "CLEAN.WAV", &cleaned, &error), &error);
    char hc[WO_SHA256_HEX_SIZE];
    CHECK_OK(wo_sha256_mem(cleaned, a.len, hc, &error), &error);
    wo_free(cleaned);
    char h[WO_SHA256_HEX_SIZE];
    wo_free(file_hash(audio, "CLEAN.WAV", h));
    CHECK(wo_digest_eq(h, hc));
    wo_free(file_hash(audio, "KEEP.WAV", h));
    CHECK(wo_digest_eq(h, hb));
    wo_free(file_hash(audio, "source/CLEAN.WAV", h));
    CHECK(wo_digest_eq(h, ha));
    char *state_path = wo_path_join(audio, "install-state.json", &error);
    WoJson *state = NULL;
    CHECK_OK(wo_read_json(state_path, 1 << 20, &state, &error), &error);
    const WoJson *first = wo_json_at(wo_json_get(state, "files"), 0);
    CHECK_STR(wo_json_str(first, "processor", ""), WO_AUDIO_PROCESSOR_ID);
    CHECK_STR(wo_json_str(first, "source_sha256", ""), ha);
    CHECK_STR(wo_json_str(first, "output_sha256", ""), hc);
    wo_json_free(state);

    /* A refresh from the pristine cache with nothing changed does nothing. */
    char *cache = wo_path_join(audio, "source", &error);
    options.source_dir = cache;
    options.incremental = true;
    CHECK_OK(wo_sfx_install(&options, &result, &error), &error);
    CHECK(result.checked == 2 && result.updated == 0);
    /* A damaged output is repaired. */
    char *clean_path = wo_path_join(audio, "CLEAN.WAV", &error);
    CHECK_OK(wo_write_file(clean_path, "x", 1, &error), &error);
    CHECK_OK(wo_sfx_install(&options, &result, &error), &error);
    CHECK(result.updated == 1);
    wo_free(file_hash(audio, "CLEAN.WAV", h));
    CHECK(wo_digest_eq(h, hc));
    /* Switching to original effects reprocesses everything. */
    options.original = true;
    CHECK_OK(wo_sfx_install(&options, &result, &error), &error);
    CHECK(result.updated == 2);
    wo_free(file_hash(audio, "CLEAN.WAV", h));
    CHECK(wo_digest_eq(h, ha));
    /* An unreadable state file only means nothing is reused. */
    CHECK_OK(wo_write_file(state_path, "{broken", 7, &error), &error);
    CHECK_OK(wo_sfx_install(&options, &result, &error), &error);
    CHECK(result.updated == 2);

    /* Custom effects need original mode; nothing changes otherwise. */
    options.original = false;
    options.source_dir = source;
    WoBuf custom = {0};
    make_wav(&custom, 0.2);
    CHECK_OK(wo_write_file(pa, custom.data, custom.len, &error), &error);
    CHECK_FAILS(wo_sfx_install(&options, &result, &error), &error, WO_ERR_CHECKSUM);
    CHECK(strstr(error.message, "CLEAN.WAV"));
    wo_free(file_hash(audio, "CLEAN.WAV", h));
    CHECK(wo_digest_eq(h, ha));
    options.original = true;
    CHECK_OK(wo_sfx_install(&options, &result, &error), &error);
    wo_free(file_hash(audio, "CLEAN.WAV", h));
    char hcustom[WO_SHA256_HEX_SIZE];
    hash_of(&custom, hcustom);
    CHECK(wo_digest_eq(h, hcustom));
    /* Missing effects and unsafe names. */
    CHECK_OK(wo_delete_file(pb, &error), &error);
    CHECK_FAILS(wo_sfx_install(&options, &result, &error), &error, WO_ERR_NOT_FOUND);
    CHECK(strstr(error.message, "KEEP.WAV"));
    WoJson *unsafe = NULL;
    const char *bad = "{\"audio\":[{\"name\":\"../x.wav\"}]}";
    CHECK_OK(wo_json_parse(bad, strlen(bad), &unsafe, &error), &error);
    options.manifest = unsafe;
    CHECK_FAILS(wo_sfx_install(&options, &result, &error), &error, WO_ERR_UNSAFE);
    wo_json_free(unsafe);
    WoJson *empty = wo_json_new_object();
    options.manifest = empty;
    CHECK_FAILS(wo_sfx_install(&options, &result, &error), &error, WO_ERR_FORMAT);
    wo_json_free(empty);

    wo_buf_free(&custom);
    wo_free(clean_path);
    wo_free(cache);
    wo_free(state_path);
    wo_json_free(manifest);
    wo_free(text);
    wo_free(pb);
    wo_free(pa);
    wo_buf_free(&b);
    wo_buf_free(&a);
    wo_free(audio);
    wo_free(game);
    wo_free(source);
}

int main(void) {
    RUN(test_install);
    return wo_test_finish();
}
