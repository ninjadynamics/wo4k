#include <string.h>

#include "core/wo_log.h"
#include "core/wo_ops.h"

void wo_progress(WoProgress *progress, WoStage stage, int percent, const char *message) {
    WoJson *data = wo_json_new_object();
    wo_json_put_int(data, "percent", percent);
    wo_json_put_str(data, "message", message);
    wo_log_event("progress", data);
    if (!progress) {
        return;
    }
    progress->stage = stage;
    if (progress->report) {
        progress->report(progress->context, percent, message);
    }
}

static char *copy(const char *text) {
    return wo_strdup(text, NULL);
}

char *wo_user_message(WoStage stage, const WoError *error) {
    WoErrorKind kind = error ? error->kind : WO_ERR_INTERNAL;
    const char *detail = error ? error->message : "";
    /* Conditions that mean the same thing whatever was being done. */
    switch (kind) {
    case WO_ERR_SHARING:
        return copy(stage == WO_STAGE_UPDATE
                        ? "Update could not replace a file that is still open. Close other Setup "
                          "and game windows, reopen Setup, then try Update again."
                        : "A file is still open in another program. Close the game and other WO4K "
                          "windows, then try again.");
    case WO_ERR_ACCESS:
        return copy("Windows blocked changes to the mod folder. Check its permissions and Windows "
                    "Security, then try again.");
    case WO_ERR_BUSY:
        return copy(detail);
    case WO_ERR_CANCELLED:
        return copy("The operation was cancelled.");
    case WO_ERR_LIMIT:
        if (error && (error->code == 112 || error->code == 39)) { /* disk full */
            return copy("There is not enough free disk space. Free some space, then try again.");
        }
        break;
    default:
        break;
    }
    switch (stage) {
    case WO_STAGE_UPDATE:
        if (kind == WO_ERR_NETWORK) {
            return copy("The update could not be downloaded. Check your internet connection and "
                        "try Update again.");
        }
        if (kind == WO_ERR_CHECKSUM || kind == WO_ERR_FORMAT) {
            return copy("The downloaded update is incomplete or damaged. Try Update again.");
        }
        if (kind == WO_ERR_UNSAFE) {
            return copy("The downloaded update failed WO4K's safety checks, so nothing was "
                        "installed. Details are saved in the log.");
        }
        if (kind == WO_ERR_UNSUPPORTED || kind == WO_ERR_NOT_FOUND) {
            return copy(detail);
        }
        return copy("The update could not be completed. Reopen Setup and try Update again; "
                    "details are saved in the log.");
    case WO_STAGE_GAME_SOURCE:
        return copy("The original Windows game source could not be found or verified. Choose the "
                    "PC game ZIP, disc or folder and run Install / repair.");
    case WO_STAGE_PS1_SOURCE:
        return copy("The PlayStation artwork source could not be read. Choose the European "
                    "WipEout 2097 disc, or clear the optional PlayStation source.");
    case WO_STAGE_SOUNDTRACK:
        return copy("The selected soundtrack is missing or incomplete. Restore its disc or music "
                    "folder, or choose No music in Setup.");
    case WO_STAGE_SOUND_EFFECTS:
        return copy("One or more sound effects are missing or damaged. Select your original "
                    "Windows game source and run Install / repair.");
    case WO_STAGE_TEXTURES:
        return copy("One or more track or sprite textures are missing or damaged. Restore the "
                    "configured game sources and run Install / repair.");
    case WO_STAGE_HD_ART: {
        /* The specific problem (which PNG, and why) is useful to the user. */
        char *text = wo_format(NULL, "An HD artwork file could not be loaded. %s", detail);
        return text ? text : copy("An HD artwork file could not be loaded.");
    }
    case WO_STAGE_RUNTIME:
        return copy("A required game component is missing or damaged. Run Update or Install / "
                    "repair; also check Windows Security for a quarantined file.");
    case WO_STAGE_LAUNCH:
        return copy("The game could not be started. Run Install / repair and check Windows "
                    "Security for blocked game files.");
    case WO_STAGE_GAME:
        return copy(detail);
    case WO_STAGE_RESTART:
        return copy("Update completed, but Setup could not reopen. Start Setup.exe from the mod "
                    "folder.");
    case WO_STAGE_GENERAL:
    default:
        if (kind == WO_ERR_BUSY || kind == WO_ERR_UNSUPPORTED) {
            return copy(detail);
        }
        return copy("Setup could not finish checking or repairing the installation. Run Install / "
                    "repair; details are saved in the log.");
    }
}

void wo_source_info_free(WoSourceInfo *info) {
    wo_free(info->pc_music_path);
    wo_free(info->psx_path);
    memset(info, 0, sizeof(*info));
}

void wo_discovery_free(WoDiscovery *found) {
    wo_free(found->game);
    wo_free(found->psx);
    memset(found, 0, sizeof(*found));
}
