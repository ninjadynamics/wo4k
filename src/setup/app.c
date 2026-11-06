/* Setup's window: sources, preferences, Install / Repair / Update / Play.
 * The behaviour follows the previous WPF Setup; long operations run as
 * background jobs (work.c) and report back through an event queue. */
#include "setup/app.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "raylib.h"

#include "core/wo_fs.h"
#include "core/wo_log.h"
#include "core/wo_mod.h"
#include "core/wo_ops.h"
#include "core/wo_release.h"
#include "core/wo_settings.h"
#include "setup/ui.h"
#include "setup/win32.h"
#include "setup/work.h"

/* GLFW (built into raylib) is thread-safe for these two calls. */
void glfwPostEmptyEvent(void);
void *glfwGetCurrentContext(void);
void glfwSetWindowShouldClose(void *window, int value);

#define C_WINDOW ((Color){0x0D, 0x11, 0x1B, 255})
#define C_SUBTITLE ((Color){0xA5, 0xB5, 0xCD, 255})
#define C_CAPS ((Color){0xA5, 0xB5, 0xCD, 255})
#define C_HINT_BOX ((Color){0x17, 0x23, 0x35, 255})
#define C_HINT_TEXT ((Color){0xBC, 0xD5, 0xE8, 255})
#define C_OPTION ((Color){0xC7, 0xD3, 0xE6, 255})
#define C_QUIET ((Color){0x9F, 0xB0, 0xCA, 255})
#define C_BETA ((Color){0x66, 0xDB, 0xD0, 255})

enum WidgetId {
    W_GITHUB = 1,
    W_GAME_FIELD,
    W_GAME_BROWSE,
    W_GAME_MAGIC,
    W_PSX_FIELD,
    W_PSX_BROWSE,
    W_PSX_MAGIC,
    W_SOUNDTRACK,
    W_AUDIO,
    W_FULLSCREEN,
    W_HD,
    W_CLEAN,
    W_UPDATE,
    W_INSTALL,
    W_PLAY
};

typedef enum GuiState { GUI_IDLE, GUI_SEARCHING, GUI_BUSY, GUI_UPDATING } GuiState;
typedef enum ScanMode { SCAN_ALL, SCAN_GAME, SCAN_PSX } ScanMode;

static const char *const soundtrack_items[] = {"PC", "PlayStation", "No music"};
static const char *const audio_items[] = {"Mono", "Stereo", "Dolby Surround"};
static const int audio_modes[] = {1, 2, 5};
static const char *const fullscreen_items[] = {"Borderless", "Exclusive"};
static const char *const browse_items[] = {"Select file\xE2\x80\xA6", "Select folder\xE2\x80\xA6"};

typedef struct App {
    WoMod mod;
    char *exe_path;
    AppOptions options;
    Texture2D logo;
    Texture2D github;
    Texture2D ninja;

    UiTextField game;
    UiTextField psx;
    int soundtrack;
    bool soundtrack_enabled[3];
    int audio;
    int fullscreen;
    bool hd;
    bool clean;

    GuiState state;
    bool has_installation;
    bool repair_mode;
    bool installed_ready;
    bool game_verified;
    bool psx_verified;
    bool source_error;
    bool restoring;
    float psx_drop_y; /* drops at or below this go to the PlayStation field */
    bool soundtrack_touched;
    bool restore_psx_default;
    char issue[1024];
    char status[1024];
    char hint[512];
    float progress;

    WoConfig saved;
    WoConfig repair_snapshot;
    bool repair_ready_snapshot;

    ScanMode scan_mode;
    JobKind busy_kind;
    bool busy;
    bool probe_running;
    double probe_due;
    char probed_game[UI_TEXT_MAX];
    char probed_psx[UI_TEXT_MAX];
    volatile long cancel;

    char *source_link;
    char *release_link;
    bool window_focused;
    bool started;
    bool restarting;
    bool quit;
    int layout_sources; /* -1 unknown, else whether the source fields are shown */
} App;

static App app;

/* --- Helpers ------------------------------------------------------------------- */

static void wake(void) {
    glfwPostEmptyEvent();
}

static char *mod_file(const char *relative) {
    return wo_mod_path(&app.mod, relative, NULL);
}

static bool mod_file_exists(const char *relative) {
    char *path = mod_file(relative);
    bool exists = path && wo_is_file(path);
    wo_free(path);
    return exists;
}

static void set_status(const char *text) {
    wo_copy_str(app.status, sizeof(app.status), text ? text : "");
    ui_need_glyphs(app.status);
}

static void set_issue(const char *text) {
    wo_copy_str(app.issue, sizeof(app.issue), text ? text : "");
}

static char *trimmed(const char *text) {
    const char *start = text;
    while (*start == ' ' || *start == '\t') {
        start++;
    }
    size_t len = strlen(start);
    while (len > 0 && (start[len - 1] == ' ' || start[len - 1] == '\t')) {
        len--;
    }
    return wo_strndup(start, len, NULL);
}

static char *read_link(const char *file, WoLinkKind kind) {
    char *path = mod_file(file);
    char *text = NULL;
    char *link = NULL;
    if (path && wo_is_file(path) && wo_read_text(path, 4096, &text, NULL)) {
        link = wo_link_from_text(text, kind);
    }
    wo_free(text);
    wo_free(path);
    return link;
}

static void refresh_links(void) {
    wo_free(app.source_link);
    wo_free(app.release_link);
    app.source_link = read_link("source.url", WO_LINK_SOURCE);
    app.release_link = read_link("release.url", WO_LINK_RELEASE);
}

static void show_failure(const char *message) {
    const char *log = wo_log_path();
    char *text = wo_format(NULL,
                           "%s\n\nDiagnostic log:\n%s\n\nShare this log with the WO4K developer. "
                           "It stays on your PC until you choose to share it.",
                           message, log ? log : "(unavailable)");
    win32_message(GetWindowHandle(), "WO4K - operation failed", text ? text : message, true);
    wo_free(text);
}

/* --- Preferences ----------------------------------------------------------------- */

static void save_preferences(void) {
    if (app.restoring) {
        return;
    }
    WoConfig config = {0};
    config.game_source = trimmed(app.game.text);
    config.pc_music_source = wo_strdup(app.saved.pc_music_source ? app.saved.pc_music_source : "",
                                       NULL);
    config.psx_source = trimmed(app.psx.text);
    config.soundtrack = (WoSoundtrack)app.soundtrack;
    config.hd_textures = app.hd;
    config.clean_audio = app.clean;
    config.sound_mode = audio_modes[app.audio];
    config.fullscreen = (WoFullscreen)app.fullscreen;
    WoConfig saved;
    WoError error = {0};
    if (config.game_source && config.pc_music_source && config.psx_source &&
        wo_config_save(&app.mod, &config, &saved, &error)) {
        wo_config_free(&app.saved);
        app.saved = saved;
    } else if (wo_error_is_set(&error)) {
        wo_log_failure("Saving Setup preferences", &error);
    }
    wo_config_free(&config);
}

static void show_config(const WoConfig *config) {
    app.restoring = true;
    ui_text_field_set(&app.game, config->game_source);
    ui_text_field_set(&app.psx, config->psx_source);
    app.soundtrack = (int)config->soundtrack;
    app.audio = config->sound_mode == 1 ? 0 : config->sound_mode == 5 ? 2 : 1;
    app.fullscreen = (int)config->fullscreen;
    app.hd = config->hd_textures;
    app.clean = config->clean_audio;
    app.restoring = false;
}

/* --- Messages ---------------------------------------------------------------------- */

static const char *source_message(void) {
    char *game = trimmed(app.game.text);
    char *psx = trimmed(app.psx.text);
    const char *message;
    if (app.issue[0]) {
        message = app.issue;
    } else if (app.source_error) {
        message = "The last operation did not complete. Run Install / repair to check the game "
                  "files again.";
    } else if (!game || !game[0]) {
        message = "Please provide the path(s) to your WipeOut 2097 / XL game files.";
    } else if (!app.game_verified) {
        if (!wo_exists(game)) {
            message = app.installed_ready
                          ? "The installed game is ready. Its Windows source is unavailable; locate "
                            "the original ZIP, disc or folder before reinstalling."
                          : "The Windows game source is missing. Choose the current location of your "
                            "PC game ZIP, disc or folder.";
        } else {
            message = "The Windows source could not be verified. Select an original supported "
                      "WipEout 2097 / XL PC game ZIP, disc or folder.";
        }
    } else if (psx && psx[0] && !app.psx_verified) {
        if (!wo_exists(psx)) {
            message = app.installed_ready
                          ? "The installed game is ready. Its PlayStation source is unavailable; "
                            "locate it to restore missing artwork or music."
                          : "The PlayStation source is missing. Choose its current location, or "
                            "clear the optional field.";
        } else {
            message = "The PlayStation source could not be verified. Select the European WipEout "
                      "2097 disc or a complete extracted soundtrack folder.";
        }
    } else if (!psx || !psx[0]) {
        message = "Add the PlayStation version for original artwork and its soundtrack.";
    } else {
        message = "Ready to launch, all systems go!";
    }
    wo_free(game);
    wo_free(psx);
    return message;
}

static void update_source_message(void) {
    char *psx = trimmed(app.psx.text);
    bool psx_empty = !psx || !psx[0];
    wo_free(psx);
    const char *message;
    if (app.has_installation && !app.repair_mode && !app.source_error && !app.issue[0]) {
        message = app.installed_ready ? "Ready to launch, all systems go!"
                                      : "Checking the installed game...";
    } else if (!app.source_error && !app.issue[0] && app.game_verified &&
               (psx_empty || app.psx_verified)) {
        message = app.has_installation ? "Sources are ready. Choose Install to repair the game."
                                       : "Sources are ready. Choose Install to set up the game.";
    } else {
        message = source_message();
    }
    wo_copy_str(app.hint, sizeof(app.hint), message);
}

/* --- State ------------------------------------------------------------------------- */

static void update_layout_state(void) {
    app.has_installation = mod_file_exists("wobble/installation.json");
}

static bool show_sources(void) {
    return !app.has_installation || app.repair_mode;
}

static void set_state(GuiState state) {
    update_layout_state();
    app.state = state;
    if (state == GUI_IDLE) {
        set_status(app.issue[0] ? app.issue : "Ready");
        update_source_message();
    }
}

static bool locked(void) {
    return app.state == GUI_BUSY || app.state == GUI_UPDATING;
}

/* --- Probing the source fields ---------------------------------------------------------- */

static void request_probe(double delay) {
    app.probe_due = GetTime() + delay;
}

static void start_probe(void) {
    app.probe_due = 0;
    if (app.probe_running) {
        request_probe(0.2);
        return;
    }
    JobRequest request = {0};
    request.kind = JOB_PROBE;
    request.probe_game = trimmed(app.game.text);
    request.probe_psx = trimmed(app.psx.text);
    wo_copy_str(app.probed_game, sizeof(app.probed_game), request.probe_game ? request.probe_game : "");
    wo_copy_str(app.probed_psx, sizeof(app.probed_psx), request.probe_psx ? request.probe_psx : "");
    app.probe_running = job_start(&request, &app.cancel);
}

static void apply_probe(const JobEvent *event) {
    char *game = trimmed(app.game.text);
    char *psx = trimmed(app.psx.text);
    bool stale = !game || !psx || strcmp(game, app.probed_game) != 0 ||
                 strcmp(psx, app.probed_psx) != 0;
    bool psx_present = psx && psx[0];
    wo_free(game);
    wo_free(psx);
    if (stale) {
        request_probe(0);
        return;
    }
    app.game_verified = event->game_info.game;
    bool pc_available = event->game_info.pc_music || mod_file_exists("wobble/music/pc/tracks.ini");
    app.psx_verified = psx_present && event->psx_info.psx;
    bool psx_audio = (app.psx_verified && event->psx_info.psx_music) ||
                     mod_file_exists("wobble/music/psx/tracks.ini");
    app.soundtrack_enabled[0] = pc_available;
    app.soundtrack_enabled[1] = psx_audio;
    app.soundtrack_enabled[2] = true;
    if (app.restore_psx_default && psx_audio) {
        app.soundtrack = 1;
        app.restore_psx_default = false;
    } else if (!app.soundtrack_touched || (app.soundtrack == 0 && !pc_available) ||
               (app.soundtrack == 1 && !psx_audio)) {
        app.soundtrack = pc_available ? 0 : psx_audio ? 1 : 2;
    }
    update_source_message();
    save_preferences();
}

/* --- Jobs ---------------------------------------------------------------------------- */

static void start_busy_job(JobKind kind, GuiState state, const char *status, WoConfig *config) {
    JobRequest request = {0};
    request.kind = kind;
    if (config) {
        request.config = *config;
        memset(config, 0, sizeof(*config));
    }
    if (kind == JOB_UPDATE) {
        request.exe_path = wo_strdup(app.exe_path, NULL);
    }
    app.cancel = 0;
    set_status(status);
    app.progress = 0;
    app.busy = job_start(&request, &app.cancel);
    app.busy_kind = kind;
    if (app.busy) {
        set_state(state);
    } else {
        set_status("Setup could not start a background task.");
        set_state(GUI_IDLE);
    }
}

static void start_check(void) {
    if (app.busy) {
        return;
    }
    start_busy_job(JOB_CHECK, GUI_BUSY, "Checking the configured installation...", NULL);
}

static void start_discovery(ScanMode mode) {
    if (app.busy) {
        return;
    }
    app.scan_mode = mode;
    start_busy_job(JOB_DISCOVER, GUI_SEARCHING, "Looking for your game files\xE2\x80\xA6", NULL);
}

static void start_installation(void) {
    if (app.busy) {
        return;
    }
    save_preferences();
    char *game = trimmed(app.game.text);
    bool empty = !game || !game[0];
    wo_free(game);
    if (empty) {
        set_status("Choose your Windows game source to continue.");
        return;
    }
    app.source_error = false;
    set_issue("");
    update_source_message();
    if (app.has_installation) {
        /* The shared repair path reuses retained music and disc caches when
         * an original source is temporarily unavailable. */
        start_busy_job(JOB_REPAIR, GUI_BUSY, "Repairing the installation...", NULL);
    } else {
        WoConfig config;
        if (wo_config_copy(&app.saved, &config, NULL)) {
            start_busy_job(JOB_INSTALL, GUI_BUSY, "Starting installation...", &config);
        }
    }
}

static void start_update(void) {
    if (app.busy) {
        return;
    }
    refresh_links();
    if (!app.release_link) {
        return;
    }
    save_preferences();
    set_issue("");
    app.source_error = false;
    update_source_message();
    start_busy_job(JOB_UPDATE, GUI_UPDATING, "Starting update\xE2\x80\xA6", NULL);
}

static void open_repair(void) {
    wo_config_free(&app.repair_snapshot);
    wo_config_load(&app.mod, &app.repair_snapshot, NULL);
    app.repair_ready_snapshot = app.installed_ready;
    app.repair_mode = true;
    request_probe(0);
    set_state(GUI_IDLE);
    ui_set_focus(W_GAME_FIELD);
}

static void cancel_repair(void) {
    app.probe_due = 0;
    if (app.repair_snapshot.game_source) {
        WoConfig saved;
        if (wo_config_save(&app.mod, &app.repair_snapshot, &saved, NULL)) {
            wo_config_free(&app.saved);
            app.saved = saved;
        }
        show_config(&app.repair_snapshot);
    }
    app.repair_mode = false;
    app.installed_ready = app.repair_ready_snapshot;
    request_probe(0);
    set_state(GUI_IDLE);
}

static void restart_after_update(void) {
    char arguments[64];
    snprintf(arguments, sizeof(arguments), "--after-update --wait-pid %u", win32_current_pid());
    WoError error = {0};
    if (win32_start_program(app.exe_path, arguments, app.mod.root, &error)) {
        app.restarting = true;
        app.quit = true;
    } else {
        wo_log_failure("Restarting Setup after update", &error);
        app.source_error = true;
        set_issue("Update completed, but Setup could not reopen. Close this window and open "
                  "Setup.exe.");
        set_state(GUI_IDLE);
    }
}

static void handle_job_done(JobEvent *event) {
    if (event->kind == JOB_PROBE) {
        app.probe_running = false;
        apply_probe(event);
        return;
    }
    app.busy = false;
    if (event->kind == JOB_DISCOVER) {
        if (event->discovery.game && (app.scan_mode == SCAN_GAME ||
                                      (app.scan_mode == SCAN_ALL && !app.game.text[0]))) {
            ui_text_field_set(&app.game, event->discovery.game);
        }
        if (event->discovery.psx && (app.scan_mode == SCAN_PSX ||
                                     (app.scan_mode == SCAN_ALL && !app.psx.text[0]))) {
            ui_text_field_set(&app.psx, event->discovery.psx);
        }
        request_probe(0);
        set_state(GUI_IDLE);
        return;
    }
    WoJson *data = wo_json_new_object();
    wo_json_put_int(data, "job", event->kind);
    wo_json_put_bool(data, "success", event->success);
    wo_log_event("worker_exit", data);
    app.source_error = !event->success;
    set_issue(event->success ? "" : event->message);
    update_layout_state();
    app.installed_ready = event->success && app.has_installation;
    set_status(event->message);
    if (!event->success) {
        show_failure(event->message ? event->message : "The operation failed.");
    }
    if (event->success && event->kind == JOB_UPDATE &&
        event->update.outcome == WO_UPDATE_INSTALLED) {
        restart_after_update();
        return;
    }
    if (event->success) {
        app.progress = 1;
        request_probe(0);
        if (app.repair_mode) {
            wo_config_free(&app.repair_snapshot);
            wo_config_load(&app.mod, &app.repair_snapshot, NULL);
            app.repair_ready_snapshot = app.installed_ready;
        }
    }
    set_state(GUI_IDLE);
    if (event->success) {
        set_status(event->message);
    }
}

static void drain_jobs(void) {
    JobEvent *event;
    while ((event = job_poll()) != NULL) {
        if (event->done) {
            handle_job_done(event);
        } else if (event->kind != JOB_PROBE && event->kind != JOB_DISCOVER) {
            set_status(event->message);
            app.progress = (float)event->percent / 100.0f;
        }
        job_event_free(event);
    }
}

/* --- Layout and drawing -------------------------------------------------------------- */

static void draw_label(const char *text, float x, float y, float width, Color color, UiFont font,
                       const char *tooltip) {
    float height = ui_text_wrapped(font, text, x, y, width, color, true);
    if (tooltip) {
        float w = fminf(width, ui_text_width(font, text));
        ui_tooltip((Rectangle){x, y, w, height}, tooltip);
    }
}

static void source_row(int field_id, int browse_id, int magic_id, UiTextField *field, float x,
                       float y, float width, float height, float spin, const char *tooltip,
                       ScanMode mode) {
    float button = ui_px(53);
    float gap = ui_px(5);
    Rectangle field_rect = {x, y, width - (button + gap) * 2, height};
    Rectangle browse_rect = {field_rect.x + field_rect.width + gap, y, button, height};
    Rectangle magic_rect = {browse_rect.x + button + gap, y, button, height};
    bool editable = !locked();
    if (ui_text_field(field_id, field_rect, field, editable)) {
        if (!app.restoring) {
            app.source_error = false;
            set_issue("");
            app.installed_ready = false;
            request_probe(0.4);
        }
    }
    if (ui_lost_focus(field_id)) {
        request_probe(0);
    }
    ui_tooltip(field_rect, locked() ? "Setup is busy. This source field is locked until it finishes."
                                    : tooltip);
    if (ui_icon_button(browse_id, browse_rect, UI_ICON_SEARCH, 0, editable)) {
        ui_menu_open(browse_id);
    }
    int choice = ui_menu(browse_id, browse_rect, browse_items, 2);
    if (choice >= 0) {
        char *initial = trimmed(field->text);
        char *path = choice == 0 ? win32_pick_file(GetWindowHandle(), initial)
                                 : win32_pick_folder(GetWindowHandle(), initial);
        if (path) {
            ui_text_field_set(field, path);
            request_probe(0);
        }
        wo_free(path);
        wo_free(initial);
    }
    if (ui_icon_button(magic_id, magic_rect, UI_ICON_SPARKLE, app.state == GUI_SEARCHING ? spin : 0,
                       app.state == GUI_IDLE)) {
        start_discovery(mode);
    }
}

static void option_row(int id, const char *label, const char *tooltip, const char *const *items,
                       const bool *enabled_items, int count, int *value, float x, float y,
                       float width, float height, bool *changed) {
    float label_y = y + (height - ui_line_height(UI_FONT_BODY)) / 2;
    draw_label(label, x, label_y, width - ui_px(220), C_OPTION, UI_FONT_BODY, tooltip);
    Rectangle combo = {x + width - ui_px(210), y, ui_px(210), height};
    *changed = ui_dropdown(id, combo, items, enabled_items, count, value, !locked());
}

static float window_min_width(void) {
    return fmaxf(ui_px(760), ui_px(64 + 360 + 180 + 24 + 16) + (float)app.ninja.width);
}

static void apply_window_size(void) {
    bool sources = show_sources();
    if (app.layout_sources == (int)sources) {
        return;
    }
    app.layout_sources = (int)sources;
    float min_height = ui_px(sources ? 730 : 580);
    float height = ui_px(sources ? 750 : 600);
    int area_x;
    int area_y;
    int area_w;
    int area_h;
    win32_work_area(GetWindowHandle(), &area_x, &area_y, &area_w, &area_h);
    if (height > area_h - ui_px(40)) {
        height = (float)area_h - ui_px(40);
    }
    if (min_height > height) {
        min_height = height;
    }
    SetWindowMinSize((int)window_min_width(), (int)min_height);
    int width = GetScreenWidth();
    if (width < window_min_width()) {
        width = (int)window_min_width();
    }
    SetWindowSize(width, (int)height);
}

static void draw_frame(void) {
    float s = ui_scale();
    float width = (float)GetScreenWidth();
    float height = (float)GetScreenHeight();
    float left = ui_px(32);
    float content = width - ui_px(64);
    float spin = (float)fmod(GetTime() * 360.0 / 1.4, 360.0);
    ClearBackground(C_WINDOW);

    /* Header: logo, Beta, subtitle and GitHub link. */
    float y = ui_px(24);
    DrawTexture(app.logo, (int)left, (int)y, WHITE);
    Rectangle logo_rect = {left, y, (float)app.logo.width, (float)app.logo.height};
    ui_tooltip(logo_rect, "WO4K \xE2\x80\x94 modern enhancements for WipEout 2097 / XL.");
    float beta_x = left + app.logo.width + ui_px(12);
    float beta_y = y + (app.logo.height - ui_line_height(UI_FONT_BETA)) / 2;
    ui_text(UI_FONT_BETA, "Beta", beta_x, beta_y, C_BETA);
    ui_tooltip((Rectangle){beta_x, beta_y, ui_text_width(UI_FONT_BETA, "Beta"),
                           ui_line_height(UI_FONT_BETA)},
               "Beta: the original PC game, enhanced for modern Windows.");
    y += app.logo.height + ui_px(4);
    float github_column = app.github.width + ui_px(16);
    const char *subtitle =
        "Adds widescreen, smooth animation, HD artwork and modern controls to WipeOut 2097 / XL.";
    float subtitle_h = ui_text_wrapped(UI_FONT_SUBTITLE, subtitle, 0, 0, content - github_column,
                                       BLANK, false);
    float row_h = fmaxf(subtitle_h, (float)app.github.height + ui_px(8));
    draw_label(subtitle, left, y + (row_h - subtitle_h) / 2, content - github_column, C_SUBTITLE,
               UI_FONT_SUBTITLE,
               "Play at your display's aspect ratio and refresh rate, with interpolated animation, "
               "extended track visibility, HD interface artwork, SDL3 gamepad support, locally "
               "improved sound effects and switchable PC/PlayStation soundtracks. The original 30 Hz "
               "game simulation is preserved.");
    Rectangle github = {left + content - app.github.width - ui_px(8), y + (row_h - app.github.height - ui_px(8)) / 2,
                        app.github.width + ui_px(8), app.github.height + ui_px(8)};
    if (ui_image_button(W_GITHUB, github, &app.github, app.source_link != NULL)) {
        refresh_links();
        if (app.source_link) {
            win32_open_url(app.source_link);
        }
    }
    y += row_h;

    /* Hint box. */
    y += ui_px(19);
    float hint_h = ui_text_wrapped(UI_FONT_BODY, app.hint, 0, 0, content - ui_px(28), BLANK, false);
    Rectangle hint_box = {left, y, content, hint_h + ui_px(22)};
    ui_round_rect(hint_box, ui_px(8), C_HINT_BOX);
    ui_text_wrapped(UI_FONT_BODY, app.hint, left + ui_px(14), y + ui_px(11), content - ui_px(28),
                    C_HINT_TEXT, true);
    ui_tooltip(hint_box, "Game installation uses your local source files without mounting a disc. "
                         "Only the Update button downloads a mod release.");
    y += hint_box.height + ui_px(17);

    /* Sources. */
    float field_h = ui_line_height(UI_FONT_FIELD) + ui_px(20);
    if (show_sources()) {
        draw_label("WINDOWS GAME", left, y, content, C_CAPS, UI_FONT_CAPS,
                   "Required: PC ZIP/disc image or the small 11 MB PC version. Included PC music is "
                   "extracted automatically; the small version can play without music or with "
                   "optional PlayStation music.");
        y += ui_line_height(UI_FONT_CAPS) + ui_px(5);
        source_row(W_GAME_FIELD, W_GAME_BROWSE, W_GAME_MAGIC, &app.game, left, y, content, field_h,
                   spin,
                   "PC ZIP/disc image or the small 11 MB PC version. Included PC music is extracted "
                   "automatically. The small version works without music or with optional "
                   "PlayStation music.",
                   SCAN_GAME);
        app.psx_drop_y = y + field_h + ui_px(6);
        y += field_h + ui_px(13);
        draw_label("PLAYSTATION GAME \xC2\xB7 OPTIONAL", left, y, content, C_CAPS, UI_FONT_CAPS,
                   "Adds original PlayStation artwork from the European 2097 disc, plus its "
                   "soundtrack when CD audio is included. A data-only image supplies artwork "
                   "without music.");
        y += ui_line_height(UI_FONT_CAPS) + ui_px(5);
        source_row(W_PSX_FIELD, W_PSX_BROWSE, W_PSX_MAGIC, &app.psx, left, y, content, field_h,
                   spin,
                   "Optional European PlayStation disc for original artwork and any included CD "
                   "audio, or an extracted music folder.",
                   SCAN_PSX);
        y += field_h + ui_px(14);
    }

    /* Options. */
    float combo_h = ui_line_height(UI_FONT_BODY) + ui_px(14);
    bool changed = false;
    option_row(W_SOUNDTRACK, "Default soundtrack",
               "Choose installed PC or PlayStation music, or No music. Data-only discs supply "
               "artwork without CD audio. Switch soundtracks during play in Options / Audio.",
               soundtrack_items, app.soundtrack_enabled, 3, &app.soundtrack, left, y, content,
               combo_h, &changed);
    if (changed) {
        app.restore_psx_default = false;
        app.soundtrack_touched = true;
        save_preferences();
    }
    y += combo_h + ui_px(6);
    draw_label("Switch soundtracks anytime in the game's Audio options.", left, y, content, C_QUIET,
               UI_FONT_BODY,
               "When both soundtracks are installed, change between PC and PlayStation music on the "
               "fly in Options / Audio. Your choice is remembered.");
    y += ui_line_height(UI_FONT_BODY) + ui_px(12);
    option_row(W_AUDIO, "Audio output",
               "Choose Mono, Stereo or Dolby Surround. Stereo suits normal speakers and headphones; "
               "Dolby Surround needs a compatible decoder. Saved for the next launch.",
               audio_items, NULL, 3, &app.audio, left, y, content, combo_h, &changed);
    if (changed) {
        save_preferences();
        char *ini = mod_file("wobble/Wipeout2097.ini");
        if (ini && mod_file_exists("wobble/Wipeout2097.exe")) {
            wo_settings_set_sound_mode(ini, audio_modes[app.audio], NULL);
            set_status("Audio output saved for the next launch.");
        }
        wo_free(ini);
    }
    y += combo_h + ui_px(10);
    option_row(W_FULLSCREEN, "Fullscreen mode",
               "Borderless fills the screen and allows easy app switching. Exclusive uses exclusive "
               "fullscreen presentation. Saved for the next launch.",
               fullscreen_items, NULL, 2, &app.fullscreen, left, y, content, combo_h, &changed);
    if (changed) {
        save_preferences();
        char *ini = mod_file("wobble/DDrawCompat-Wipeout2097.ini");
        if (ini && mod_file_exists("wobble/Wipeout2097.exe")) {
            wo_settings_set_fullscreen(ini, (WoFullscreen)app.fullscreen, NULL);
            set_status("Fullscreen mode saved for the next launch.");
        }
        wo_free(ini);
    }
    y += combo_h + ui_px(9);
    Rectangle hd_rect = {left, y, content, ui_px(17)};
    if (ui_checkbox(W_HD, hd_rect, "Install HD artwork", &app.hd, !locked())) {
        save_preferences();
    }
    ui_tooltip((Rectangle){left, y, ui_px(25) + ui_text_width(UI_FONT_BODY, "Install HD artwork"),
                           ui_px(17)},
               "Install the supplied HD fonts, HUD, notice and title. Original SD artwork is "
               "retained for fallback.");
    y += ui_px(17) + ui_px(9);
    Rectangle clean_rect = {left, y, content, ui_px(17)};
    if (ui_checkbox(W_CLEAN, clean_rect, "Improve audio quality", &app.clean, !locked())) {
        save_preferences();
    }
    ui_tooltip((Rectangle){left, y, ui_px(25) + ui_text_width(UI_FONT_BODY, "Improve audio quality"),
                           ui_px(17)},
               "Reduce noise in your original 22 kHz sound effects while preserving their volume. "
               "Audio is processed locally from your game files.");

    /* Footer, laid out from the bottom: buttons aligned with the wordmark. */
    float footer_bottom = height - ui_px(24);
    float shift = fmaxf(0, ((128.0f - 63.5f) - 14.0f * s) / 2.0f);
    float button_h = ui_px(30);
    float buttons_bottom = footer_bottom - shift;
    float row = fmaxf(button_h + shift, (float)app.ninja.height - ui_px(32) - shift);
    float grid_top = footer_bottom - row;
    float progress_y = grid_top - ui_px(28) - fmaxf(2, ui_px(2));
    float status_w = content;
    float status_h = ui_text_wrapped(UI_FONT_BODY, app.status, 0, 0, status_w, BLANK, false);
    float status_y = progress_y - ui_px(8) - status_h;
    draw_label(app.status, left, status_y, status_w, C_QUIET, UI_FONT_BODY, app.status);
    Rectangle progress_rect = {left, progress_y, content, fmaxf(2, ui_px(2))};
    ui_progress(progress_rect, app.progress,
                app.state == GUI_SEARCHING || app.state == GUI_UPDATING);
    ui_tooltip((Rectangle){left, progress_y - ui_px(4), content, ui_px(10)},
               "Current discovery or installation progress. A moving bar means work is continuing "
               "in the background.");
    float ninja_y = footer_bottom + shift - app.ninja.height;
    DrawTexture(app.ninja, (int)left, (int)ninja_y, WHITE);
    ui_tooltip((Rectangle){left, ninja_y, (float)app.ninja.width, (float)app.ninja.height},
               "WO4K mod branding: Ninja Dynamics.");

    bool installed = app.has_installation;
    bool game_ready = mod_file_exists("wobble/Wipeout2097.exe");
    float right = left + content;
    float gap = ui_px(9);
    float play_w = ui_px(160);
    float install_w = installed ? ui_px(100) : ui_px(160);
    float button_y = buttons_bottom - button_h;
    if (installed) {
        Rectangle play = {right - play_w, button_y, play_w, button_h};
        if (ui_button(W_PLAY, play, app.repair_mode ? "Install" : "Play", UI_BUTTON_PRIMARY,
                      app.state == GUI_IDLE && game_ready)) {
            if (app.repair_mode) {
                start_installation();
            } else {
                save_preferences();
                char *play_exe = mod_file("Play.exe");
                WoError error = {0};
                if (!play_exe || !win32_start_program(play_exe, NULL, app.mod.root, &error)) {
                    wo_log_failure("Starting Play.exe", &error);
                    show_failure("Play.exe could not be started. Check Windows Security for a "
                                 "blocked file.");
                }
                wo_free(play_exe);
            }
        }
        right -= play_w + gap;
    }
    const char *install_label = app.repair_mode ? "Cancel" : show_sources() ? "Install" : "Repair";
    UiButtonStyle install_style = app.repair_mode ? UI_BUTTON_WARNING
                                  : installed     ? UI_BUTTON_NORMAL
                                                  : UI_BUTTON_PRIMARY;
    Rectangle install = {right - install_w, button_y, install_w, button_h};
    if (ui_button(W_INSTALL, install, install_label, install_style, app.state == GUI_IDLE)) {
        if (app.repair_mode) {
            cancel_repair();
        } else if (installed) {
            open_repair();
        } else {
            start_installation();
        }
    }
    right -= install_w + gap;
    Rectangle update = {right - ui_px(100), button_y, ui_px(100), button_h};
    if (ui_button(W_UPDATE, update, app.state == GUI_UPDATING ? "Updating\xE2\x80\xA6" : "Update",
                  UI_BUTTON_NORMAL, app.state == GUI_IDLE && app.release_link != NULL)) {
        start_update();
    }
}

/* --- Main loop -------------------------------------------------------------------------- */

static Texture2D load_art(const char *name) {
    Texture2D texture = {0};
    char *relative = wo_format(NULL, "assets/ui/%s", name);
    char *path = relative ? mod_file(relative) : NULL;
    WoBuf bytes = {0};
    if (path && wo_read_file(path, 16u << 20, &bytes, NULL) && bytes.len < INT32_MAX) {
        Image image = LoadImageFromMemory(".png", bytes.data, (int)bytes.len);
        if (image.data) {
            texture = LoadTextureFromImage(image);
            SetTextureFilter(texture, TEXTURE_FILTER_POINT);
            UnloadImage(image);
        }
    }
    wo_buf_free(&bytes);
    wo_free(path);
    wo_free(relative);
    return texture;
}

static void trace_to_log(int level, const char *text, va_list args) {
    if (level < LOG_WARNING) {
        return;
    }
    char message[512];
    vsnprintf(message, sizeof(message), text, args);
    wo_log_text("raylib", message);
}

static void startup(void) {
    app.started = true;
    if (app.options.after_update) {
        start_busy_job(JOB_REPAIR, GUI_BUSY,
                       "Finishing the update: running Install / repair for the saved configuration...",
                       NULL);
    } else if (app.has_installation) {
        start_check();
    } else {
        start_discovery(SCAN_ALL);
    }
}

static void handle_dropped_files(void) {
    if (!IsFileDropped()) {
        return;
    }
    FilePathList files = LoadDroppedFiles();
    if (files.count > 0 && !locked() && show_sources()) {
        bool psx = app.psx_drop_y > 0 && GetMousePosition().y >= app.psx_drop_y;
        ui_text_field_set(psx ? &app.psx : &app.game, files.paths[0]);
        request_probe(0);
    }
    UnloadDroppedFiles(files);
}

static void handle_close_request(void) {
    if (!WindowShouldClose()) {
        return;
    }
    if (app.busy && app.busy_kind != JOB_DISCOVER) {
        set_status("Please wait for setup to finish before closing.");
        glfwSetWindowShouldClose(glfwGetCurrentContext(), 0);
        return;
    }
    app.quit = true;
}

int app_run(const char *mod_root, const char *exe_path, const AppOptions *options) {
    memset(&app, 0, sizeof(app));
    app.options = *options;
    app.layout_sources = -1;
    app.soundtrack_touched = true;
    app.soundtrack_enabled[2] = true;
    WoError error = {0};
    if (!wo_mod_init(&app.mod, mod_root, &error)) {
        win32_message(NULL, "WO4K Setup", error.message, true);
        return 1;
    }
    app.exe_path = wo_strdup(exe_path, NULL);
    jobs_init(&app.mod, wake);

    SetTraceLogCallback(trace_to_log);
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT | FLAG_WINDOW_HIDDEN);
    InitWindow(820, 750, APP_WINDOW_TITLE);
    if (!IsWindowReady()) {
        wo_log_text("window_failed", "OpenGL 3.3 window creation failed.");
        win32_message(NULL, "WO4K Setup",
                      "Setup could not open its window. It needs a graphics driver with OpenGL 3.3 "
                      "support. Update your graphics driver and try again.",
                      true);
        wo_mod_free(&app.mod);
        return 1;
    }
    SetExitKey(KEY_NULL);
    SetTargetFPS(60);
    ui_init();
    app.logo = load_art("wo4k-logo-app.png");
    app.github = load_art("github.png");
    app.ninja = load_art("ninja-logo.png");
    refresh_links();

    if (!wo_config_load(&app.mod, &app.saved, &error)) {
        wo_log_failure("Reading the saved Setup configuration", &error);
        app.saved.game_source = wo_strdup("", NULL);
        app.saved.pc_music_source = wo_strdup("", NULL);
        app.saved.psx_source = wo_strdup("", NULL);
        app.saved.sound_mode = 2;
    }
    show_config(&app.saved);
    app.restore_psx_default = app.saved.soundtrack == WO_SOUNDTRACK_PSX;
    update_layout_state();
    set_state(GUI_IDLE);

    /* Size and centre the window for this monitor's DPI, then show it. */
    int area_x;
    int area_y;
    int area_w;
    int area_h;
    win32_work_area(GetWindowHandle(), &area_x, &area_y, &area_w, &area_h);
    apply_window_size();
    SetWindowPosition(area_x + (area_w - GetScreenWidth()) / 2,
                      area_y + (area_h - GetScreenHeight()) / 2);
    ClearWindowState(FLAG_WINDOW_HIDDEN);

    while (!app.quit) {
        drain_jobs();
        handle_close_request();
        handle_dropped_files();
        if (IsWindowFocused() && !app.window_focused) {
            refresh_links();
        }
        app.window_focused = IsWindowFocused();
        if (app.probe_due > 0 && GetTime() >= app.probe_due) {
            start_probe();
        }
        apply_window_size();
        BeginDrawing();
        ui_begin_frame();
        draw_frame();
        ui_end_frame();
        bool animating = ui_animating() || app.state == GUI_SEARCHING ||
                         app.state == GUI_UPDATING || app.probe_due > 0;
        if (animating) {
            DisableEventWaiting();
        } else {
            EnableEventWaiting();
        }
        EndDrawing();
        if (!app.started) {
            startup();
        }
    }
    if (!app.restarting) {
        save_preferences();
    }
    /* Let background jobs notice the cancellation before freeing state. */
    app.cancel = 1;
    for (int i = 0; i < 40 && jobs_running_total() > 0; i++) {
        WaitTime(0.05);
        drain_jobs();
    }
    UnloadTexture(app.logo);
    UnloadTexture(app.github);
    UnloadTexture(app.ninja);
    ui_shutdown();
    CloseWindow();
    wo_config_free(&app.saved);
    wo_config_free(&app.repair_snapshot);
    wo_free(app.source_link);
    wo_free(app.release_link);
    wo_free(app.exe_path);
    if (jobs_running_total() == 0) {
        wo_mod_free(&app.mod);
    }
    return 0;
}
