/* End-to-end Install / repair against this machine's real game sources
 * (named by .state/setup.json) into build/test-work/e2e, compared file by
 * file with the existing installation in wobble/ made by the previous
 * PowerShell installer. Slow (extracts the configured ZIPs); run with
 * "make test-local". Never modifies the sources or wobble/. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "core/wo_fs.h"
#include "core/wo_hash.h"
#include "core/wo_mod.h"
#include "core/wo_ops.h"
#include "core/wo_tree.h"
#include "wo_test.h"

static void print_progress(void *context, int percent, const char *message) {
    (void)context;
    printf("    [%3d%%] %s\n", percent, message);
    fflush(stdout);
}

/* Files whose content legitimately differs from the reference: settings
 * tuned by the user, receipts/state written in a new format, runtime
 * binaries from a newer build and per-install bookkeeping. */
static bool skipped(const char *relative) {
    static const char *const prefixes[] = {"textures/sd/sources.json", "textures/selection.json",
                                           "textures/ps1-source/source.json", "audio/install-state.json",
                                           "installation.json", "Wipeout2097.ini",
                                           "DDrawCompat-Wipeout2097.ini", "CONFIG.DAT", "SAVEGAME.BIN",
                                           "wohr.dll", "ddraw.dll", "wo2097-controller.exe",
                                           "wo4k-video.exe", "libiconv-2.dll", "movies/",
                                           "common-shaders-master/", "textures/hd/"};
    for (size_t i = 0; i < WO_COUNT(prefixes); i++) {
        if (wo_str_istarts_with(relative, prefixes[i])) {
            return true;
        }
    }
    return wo_str_iends_with(relative, ".log") || wo_str_iends_with(relative, ".txt");
}

static bool skip_pc_music;
static bool skip_psx;
static const char *game_source;

static bool same_file(const char *a, const char *b) {
    char x[WO_SHA256_HEX_SIZE];
    char y[WO_SHA256_HEX_SIZE];
    return wo_is_file(a) && wo_is_file(b) && wo_sha256_file(a, x, NULL) && wo_sha256_file(b, y, NULL) &&
           wo_digest_eq(x, y);
}

/* Repairs carry the first install's pristine sheets forward, so an older
 * reference may hold originals that the source folder no longer has. A
 * difference is explained when our pristine copy equals the source's file
 * and the reference's does not. */
static bool explained(const char *reference, const char *actual, const char *relative) {
    const char *tail = NULL;
    if (wo_str_istarts_with(relative, "textures/native-source/")) {
        tail = relative + strlen("textures/native-source/");
    } else if (wo_str_istarts_with(relative, "textures/sd/")) {
        tail = relative + strlen("textures/sd/");
    }
    if (!tail || !game_source || !wo_is_dir(game_source)) {
        return false;
    }
    char *pristine_rel = wo_format(NULL, "textures/native-source/%s", tail);
    char *ours = pristine_rel ? wo_path_child(actual, pristine_rel, NULL) : NULL;
    char *theirs = pristine_rel ? wo_path_child(reference, pristine_rel, NULL) : NULL;
    char *source = wo_path_child(game_source, tail, NULL);
    bool result = ours && theirs && source && same_file(ours, source) && !same_file(theirs, source);
    wo_free(source);
    wo_free(theirs);
    wo_free(ours);
    wo_free(pristine_rel);
    return result;
}

static void compare_trees(const char *reference, const char *actual) {
    WoTreeList files = {0};
    WoError error = {0};
    CHECK_OK(wo_tree_list(reference, &files, &error), &error);
    size_t compared = 0;
    size_t mismatched = 0;
    size_t missing = 0;
    size_t historical = 0;
    for (size_t i = 0; i < files.count; i++) {
        const WoTreeEntry *entry = &files.items[i];
        if (entry->is_dir || skipped(entry->relative) ||
            (skip_pc_music && wo_str_istarts_with(entry->relative, "music/pc/")) ||
            (skip_psx && (wo_str_istarts_with(entry->relative, "music/psx/") ||
                          wo_str_istarts_with(entry->relative, "textures/")))) {
            continue;
        }
        char *other = wo_path_child(actual, entry->relative, &error);
        char a[WO_SHA256_HEX_SIZE];
        char b[WO_SHA256_HEX_SIZE];
        if (!other || !wo_is_file(other)) {
            if (missing++ < 20) {
                printf("    missing: %s\n", entry->relative);
            }
        } else if (wo_sha256_file(entry->path, a, NULL) && wo_sha256_file(other, b, NULL) &&
                   !wo_digest_eq(a, b)) {
            if (explained(reference, actual, entry->relative)) {
                historical++;
            } else if (mismatched++ < 20) {
                printf("    differs: %s\n", entry->relative);
            }
        }
        compared++;
        wo_free(other);
    }
    printf("    compared %zu files: %zu differ, %zu missing, %zu explained by a changed source\n",
           compared, mismatched, missing, historical);
    CHECK(compared > 100);
    CHECK(mismatched == 0);
    CHECK(missing == 0);
    wo_tree_free(&files);
}

static void test_install_and_reconcile(void) {
    WoMod repo;
    WoConfig config;
    WoError error = {0};
    if (!wo_mod_init(&repo, ".", &error) || !wo_config_load(&repo, &config, &error) ||
        !config.game_source[0] || !wo_exists(config.game_source) || !wo_is_dir(repo.game)) {
        printf("  (skipped: needs configured sources and an existing wobble/ installation)\n");
        wo_mod_free(&repo);
        return;
    }
    /* WO4K_E2E_PC_MUSIC replaces the configured PC music source. */
    const char *pc_override = getenv("WO4K_E2E_PC_MUSIC");
    if (pc_override && *pc_override) {
        wo_free(config.pc_music_source);
        config.pc_music_source = wo_strdup(pc_override, NULL);
    }
    game_source = config.game_source;
    /* A fresh install fails on a configured source that no longer exists, as
     * before; compare only what the available sources can reproduce. */
    if (config.pc_music_source[0] && !wo_exists(config.pc_music_source)) {
        printf("    (PC music source missing: %s; music/pc not compared)\n", config.pc_music_source);
        config.pc_music_source[0] = 0;
        skip_pc_music = true;
    }
    if (config.psx_source[0] && !wo_exists(config.psx_source)) {
        printf("    (PlayStation source missing; PS1 artwork and music not compared)\n");
        config.psx_source[0] = 0;
        skip_psx = true;
    }
    if (skip_pc_music && config.soundtrack == WO_SOUNDTRACK_PC) {
        config.soundtrack = WO_SOUNDTRACK_NONE;
    }
    const char *root = wo_test_dir("e2e");
    if (!root) {
        return;
    }
    static const char *const folders[] = {"manifests", "runtime", "assets", "patches"};
    for (size_t i = 0; i < WO_COUNT(folders); i++) {
        char *from = wo_mod_path(&repo, folders[i], &error);
        char *to = wo_path_join(root, folders[i], &error);
        CHECK_OK(wo_copy_tree(from, to, &error), &error);
        wo_free(to);
        wo_free(from);
    }
    WoMod mod;
    CHECK_OK(wo_mod_init(&mod, root, &error), &error);
    WoProgress progress = {print_progress, NULL, NULL, WO_STAGE_GENERAL};
    time_t started = time(NULL);
    bool installed = wo_install(&mod, &config, false, &progress, &error);
    if (!installed) {
        wo_test_fail(__FILE__, __LINE__, "wo_install", error.message);
    }
    printf("    install took %lds\n", (long)(time(NULL) - started));
    if (installed) {
        compare_trees(repo.game, mod.game);
        started = time(NULL);
        WoReconcileResult result;
        CHECK_OK(wo_reconcile(&mod, false, &progress, &result, &error), &error);
        CHECK(result.installed && !result.repaired);
        printf("    reconcile took %lds\n", (long)(time(NULL) - started));
    }
    wo_mod_free(&mod);
    wo_config_free(&config);
    wo_mod_free(&repo);
}

int main(void) {
    RUN(test_install_and_reconcile);
    return wo_test_finish();
}
