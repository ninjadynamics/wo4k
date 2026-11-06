#include "setup/ui.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "core/wo_base.h"
#include "core/wo_fs.h"
#include "setup/win32.h"

/* Palette (from the previous WPF design). */
#define C_WINDOW ((Color){0x0D, 0x11, 0x1B, 255})
#define C_TEXT ((Color){0xEE, 0xF2, 0xFB, 255})
#define C_BUTTON ((Color){0x25, 0x31, 0x48, 255})
#define C_HOVER ((Color){0xFF, 0x66, 0x33, 255})
#define C_PRESSED ((Color){0xFF, 0x87, 0x5A, 255})
#define C_FOCUS ((Color){0x87, 0x90, 0x9E, 255})
#define C_PRIMARY ((Color){0x66, 0xDB, 0xD0, 255})
#define C_PRIMARY_TEXT ((Color){0x14, 0x20, 0x32, 255})
#define C_WARNING ((Color){0xF2, 0xC9, 0x4C, 255})
#define C_FIELD ((Color){0x17, 0x1F, 0x2D, 255})
#define C_FIELD_TEXT ((Color){0xED, 0xF3, 0xFF, 255})
#define C_FIELD_BORDER ((Color){0x34, 0x42, 0x5C, 255})
#define C_FIELD_HOVER ((Color){0x68, 0x71, 0x80, 255})
#define C_FIELD_FOCUS ((Color){0x98, 0xA2, 0xB1, 255})
#define C_SELECTION ((Color){0x60, 0x67, 0x73, 255})
#define C_CHECK_BORDER ((Color){0x77, 0x80, 0x8F, 255})
#define C_CHECK_FOCUS ((Color){0xCF, 0xD5, 0xDF, 255})
#define C_CHECK_TEXT ((Color){0xD2, 0xDC, 0xEE, 255})
#define C_POPUP_BORDER ((Color){0x60, 0x67, 0x73, 255})
#define C_POPUP_HIGHLIGHT ((Color){0x34, 0x3A, 0x46, 255})
#define C_TIP ((Color){0x1F, 0x28, 0x38, 255})
#define C_PROGRESS ((Color){0x66, 0xDB, 0xD0, 255})
#define C_PROGRESS_TRACK ((Color){0x24, 0x31, 0x49, 255})
#define C_SEARCH_ICON ((Color){0xD7, 0xE3, 0xF7, 255})
#define C_SPARKLE_ICON ((Color){0xFF, 0xD4, 0x5C, 255})

#define DISABLED_OPACITY 0.45f
#define TOOLTIP_DELAY 0.5
#define TOOLTIP_DURATION 60.0
#define MAX_FOCUSABLE 64
#define MAX_CODEPOINTS 4096

typedef enum PopupKind { POPUP_NONE, POPUP_DROPDOWN, POPUP_MENU } PopupKind;

typedef struct FontSpec {
    float size;
    int weight; /* 0 regular, 1 semibold, 2 bold */
} FontSpec;

/* Em sizes (WPF FontSize), one step above the old WPF Setup's, which read
 * small on 4K displays. */
static const FontSpec font_specs[UI_FONT_COUNT] = {
    {13, 0}, {13, 1}, {14, 0}, {16, 0}, {12, 2}, {13, 2}};

static struct {
    float scale;
    Font fonts[UI_FONT_COUNT];
    bool fonts_loaded;
    WoBuf font_files[3];
    int codepoints[MAX_CODEPOINTS];
    int codepoint_count;
    bool glyphs_dirty;

    Vector2 mouse;
    bool pressed;
    bool released;
    bool down;
    bool input_blocked; /* a popup consumed this frame's mouse press */
    double now;

    int active;
    int focus;
    int previous_focus;
    int lost_focus;
    int focus_order[MAX_FOCUSABLE];
    int focus_count;
    bool key_consumed;
    int cursor;
    int drag_field;
    double caret_since;
    bool caret_needed;

    PopupKind popup_kind;
    int popup_id;
    Rectangle popup_anchor;
    const char *const *popup_items;
    const bool *popup_enabled;
    int popup_count;
    int popup_highlight;
    int popup_chosen;
    int popup_chosen_id;
    int menu_request;

    Rectangle tip_rect;
    const char *tip_candidate;
    char tip_text[1024];
    Rectangle tip_shown_rect;
    double tip_since;
    bool tip_visible;
} ui;

/* --- Fonts ------------------------------------------------------------------ */

static const char *const font_files[3][3] = {
    {"segoeui.ttf", "arial.ttf", "tahoma.ttf"},
    {"seguisb.ttf", "segoeuib.ttf", "arialbd.ttf"},
    {"segoeuib.ttf", "arialbd.ttf", "tahomabd.ttf"}};

static void add_codepoint_range(int first, int last) {
    for (int cp = first; cp <= last && ui.codepoint_count < MAX_CODEPOINTS; cp++) {
        ui.codepoints[ui.codepoint_count++] = cp;
    }
}

static bool has_codepoint(int cp) {
    for (int i = 0; i < ui.codepoint_count; i++) {
        if (ui.codepoints[i] == cp) {
            return true;
        }
    }
    return false;
}

static void load_font_files(void) {
    for (int weight = 0; weight < 3; weight++) {
        for (int i = 0; i < 3 && ui.font_files[weight].len == 0; i++) {
            char *path = win32_font_path(font_files[weight][i]);
            if (path) {
                wo_read_file(path, 32u << 20, &ui.font_files[weight], NULL);
            }
            wo_free(path);
        }
    }
}

static void unload_fonts(void) {
    if (!ui.fonts_loaded) {
        return;
    }
    for (int i = 0; i < UI_FONT_COUNT; i++) {
        if (ui.fonts[i].texture.id != GetFontDefault().texture.id) {
            UnloadFont(ui.fonts[i]);
        }
    }
    ui.fonts_loaded = false;
}

static unsigned be16(const uint8_t *p) {
    return (unsigned)p[0] << 8 | p[1];
}

static uint32_t be32(const uint8_t *p) {
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

/* raylib (stb_truetype) sizes a font by its ascender-to-descender height,
 * which is larger than the em size: about 1.33 em for Segoe UI. The font's
 * head and hhea tables give the ratio. */
static float em_to_height(const WoBuf *file) {
    const uint8_t *data = file->data;
    size_t len = file->len;
    unsigned units = 0;
    int extent = 0;
    unsigned tables = len >= 12 ? be16(data + 4) : 0;
    for (unsigned i = 0; i < tables && 12 + (size_t)(i + 1) * 16 <= len; i++) {
        const uint8_t *record = data + 12 + (size_t)i * 16;
        uint32_t offset = be32(record + 8);
        if (memcmp(record, "head", 4) == 0 && offset <= len - 20) {
            units = be16(data + offset + 18);
        } else if (memcmp(record, "hhea", 4) == 0 && offset <= len - 8) {
            extent = (int16_t)be16(data + offset + 4) - (int16_t)be16(data + offset + 6);
        }
    }
    float ratio = units ? (float)extent / (float)units : 0.0f;
    return ratio >= 1.0f && ratio <= 2.0f ? ratio : 1.0f;
}

static void load_fonts(void) {
    unload_fonts();
    for (int i = 0; i < UI_FONT_COUNT; i++) {
        const WoBuf *file = &ui.font_files[font_specs[i].weight];
        int size = (int)lroundf(font_specs[i].size * em_to_height(file) * ui.scale);
        if (file->len > 0 && file->len < INT32_MAX) {
            ui.fonts[i] = LoadFontFromMemory(".ttf", file->data, (int)file->len, size, ui.codepoints,
                                             ui.codepoint_count);
        }
        if (ui.fonts[i].texture.id == 0) {
            ui.fonts[i] = GetFontDefault();
        }
        SetTextureFilter(ui.fonts[i].texture, TEXTURE_FILTER_BILINEAR);
    }
    ui.fonts_loaded = true;
    ui.glyphs_dirty = false;
}

bool ui_init(void) {
    memset(&ui, 0, sizeof(ui));
    ui.scale = GetWindowScaleDPI().x;
    if (ui.scale < 0.5f) {
        ui.scale = 1.0f;
    }
    add_codepoint_range(32, 126);
    add_codepoint_range(160, 591);   /* Latin-1, Latin Extended-A/B */
    add_codepoint_range(880, 1279);  /* Greek, Cyrillic */
    add_codepoint_range(0x2010, 0x2027);
    add_codepoint_range(0x20AC, 0x20AC);
    add_codepoint_range(0x2122, 0x2122);
    load_font_files();
    load_fonts();
    ui.popup_chosen = -1;
    ui.popup_chosen_id = -1;
    ui.menu_request = -1;
    return true;
}

void ui_shutdown(void) {
    unload_fonts();
    for (int i = 0; i < 3; i++) {
        wo_buf_free(&ui.font_files[i]);
    }
}

void ui_need_glyphs(const char *text) {
    for (const char *p = text; *p;) {
        int size = 0;
        int cp = GetCodepointNext(p, &size);
        if (size <= 0) {
            break;
        }
        if (cp >= 32 && !has_codepoint(cp) && ui.codepoint_count < MAX_CODEPOINTS) {
            ui.codepoints[ui.codepoint_count++] = cp;
            ui.glyphs_dirty = true;
        }
        p += size;
    }
}

float ui_scale(void) {
    return ui.scale;
}

float ui_px(float units) {
    return roundf(units * ui.scale);
}

static Font *font_of(UiFont font) {
    return &ui.fonts[font];
}

float ui_line_height(UiFont font) {
    return (float)font_of(font)->baseSize;
}

float ui_text_width(UiFont font, const char *text) {
    Font *f = font_of(font);
    return MeasureTextEx(*f, text, (float)f->baseSize, 0).x;
}

void ui_text(UiFont font, const char *text, float x, float y, Color color) {
    Font *f = font_of(font);
    DrawTextEx(*f, text, (Vector2){roundf(x), roundf(y)}, (float)f->baseSize, 0, color);
}

float ui_text_wrapped(UiFont font, const char *text, float x, float y, float width, Color color,
                      bool draw) {
    float line = ui_line_height(font) * 1.2f;
    float cursor_y = y;
    char buffer[2048];
    const char *at = text;
    while (*at) {
        /* Find the longest run of whole words that fits. */
        size_t best = 0;
        size_t probe = 0;
        for (;;) {
            size_t next = probe;
            while (at[next] == ' ') {
                next++;
            }
            while (at[next] && at[next] != ' ' && at[next] != '\n') {
                next++;
            }
            if (next >= sizeof(buffer)) {
                break;
            }
            memcpy(buffer, at, next);
            buffer[next] = 0;
            if (best > 0 && ui_text_width(font, buffer) > width) {
                break;
            }
            best = next;
            probe = next;
            if (!at[next] || at[next] == '\n') {
                break;
            }
        }
        if (best == 0) {
            best = strcspn(at, "\n");
            if (best >= sizeof(buffer)) {
                best = sizeof(buffer) - 1;
            }
        }
        memcpy(buffer, at, best);
        buffer[best] = 0;
        if (draw) {
            ui_text(font, buffer, x, cursor_y, color);
        }
        cursor_y += line;
        at += best;
        while (*at == ' ') {
            at++;
        }
        if (*at == '\n') {
            at++;
        }
    }
    return cursor_y - y;
}

/* --- Shapes ------------------------------------------------------------------ */

Color ui_fade(Color color, Color background, float opacity) {
    Color mixed;
    mixed.r = (unsigned char)(color.r * opacity + background.r * (1 - opacity) + 0.5f);
    mixed.g = (unsigned char)(color.g * opacity + background.g * (1 - opacity) + 0.5f);
    mixed.b = (unsigned char)(color.b * opacity + background.b * (1 - opacity) + 0.5f);
    mixed.a = 255;
    return mixed;
}

void ui_round_rect(Rectangle r, float radius, Color fill) {
    float shortest = fminf(r.width, r.height);
    if (shortest <= 0) {
        return;
    }
    float roundness = fminf(1.0f, radius * 2 / shortest);
    if (roundness <= 0) {
        DrawRectangleRec(r, fill);
    } else {
        DrawRectangleRounded(r, roundness, 8, fill);
    }
}

void ui_round_border(Rectangle r, float radius, float thickness, Color color) {
    float shortest = fminf(r.width, r.height);
    if (shortest <= 0) {
        return;
    }
    /* raylib draws the line outside the rectangle; inset to stay within it. */
    Rectangle inner = {r.x + thickness, r.y + thickness, r.width - thickness * 2,
                       r.height - thickness * 2};
    float roundness = fminf(1.0f, fmaxf(0, radius - thickness) * 2 / fminf(inner.width, inner.height));
    DrawRectangleRoundedLinesEx(inner, roundness, 8, thickness, color);
}

/* A rounded rectangle with a border: the border colour fills the outer
 * shape and the fill colour the inset one. */
static void framed(Rectangle r, float radius, float thickness, Color border, Color fill) {
    if (thickness > 0) {
        ui_round_rect(r, radius, border);
        Rectangle inner = {r.x + thickness, r.y + thickness, r.width - thickness * 2,
                           r.height - thickness * 2};
        ui_round_rect(inner, fmaxf(0, radius - thickness), fill);
    } else {
        ui_round_rect(r, radius, fill);
    }
}

static void thick_line(Vector2 a, Vector2 b, float thickness, Color color) {
    DrawLineEx(a, b, thickness, color);
    DrawCircleV(a, thickness / 2, color);
    DrawCircleV(b, thickness / 2, color);
}

static void triangle(Vector2 a, Vector2 b, Vector2 c, Color color) {
    float cross = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
    /* raylib wants counter-clockwise order as seen on screen. */
    if (cross > 0) {
        DrawTriangle(a, c, b, color);
    } else {
        DrawTriangle(a, b, c, color);
    }
}

static void draw_star(Vector2 centre, float outer, float angle, Color color) {
    Vector2 points[8];
    for (int i = 0; i < 8; i++) {
        float a = angle + (float)(i * PI / 4.0 - PI / 2.0);
        float radius = (i % 2 == 0) ? outer : outer * 0.28f;
        points[i] = (Vector2){centre.x + cosf(a) * radius, centre.y + sinf(a) * radius};
    }
    for (int i = 0; i < 8; i++) {
        triangle(centre, points[i], points[(i + 1) % 8], color);
    }
}

static Vector2 rotate_about(Vector2 p, Vector2 centre, float angle) {
    float s = sinf(angle);
    float c = cosf(angle);
    float dx = p.x - centre.x;
    float dy = p.y - centre.y;
    return (Vector2){centre.x + dx * c - dy * s, centre.y + dx * s + dy * c};
}

static void icon_search(Rectangle r, Color color) {
    float s = ui.scale;
    Vector2 c = {r.x + r.width / 2, r.y + r.height / 2};
    Vector2 lens = {c.x - 1.5f * s, c.y - 1.5f * s};
    float radius = 5.0f * s;
    DrawRing(lens, radius - 1.0f * s, radius + 1.0f * s, 0, 360, 36, color);
    thick_line((Vector2){lens.x + radius * 0.72f, lens.y + radius * 0.72f},
               (Vector2){c.x + 7.0f * s, c.y + 7.0f * s}, 2.4f * s, color);
}

static void icon_sparkle(Rectangle r, float degrees, Color color) {
    float s = ui.scale;
    float angle = degrees * (float)PI / 180.0f;
    Vector2 c = {r.x + r.width / 2, r.y + r.height / 2};
    draw_star(rotate_about((Vector2){c.x - 1.0f * s, c.y + 1.0f * s}, c, angle), 7.5f * s, angle,
              color);
    draw_star(rotate_about((Vector2){c.x + 5.5f * s, c.y - 5.0f * s}, c, angle), 3.5f * s, angle,
              color);
    draw_star(rotate_about((Vector2){c.x - 6.0f * s, c.y - 5.5f * s}, c, angle), 2.5f * s, angle,
              color);
}

static void icon_chevron(Rectangle r, Color color) {
    float s = ui.scale;
    Vector2 c = {r.x + r.width / 2, r.y + r.height / 2};
    Vector2 a = {c.x - 4 * s, c.y - 2 * s};
    Vector2 b = {c.x, c.y + 2 * s};
    Vector2 d = {c.x + 4 * s, c.y - 2 * s};
    thick_line(a, b, 1.6f * s, color);
    thick_line(b, d, 1.6f * s, color);
}

static void icon_check(Rectangle box, Color color) {
    float s = ui.scale;
    Vector2 a = {box.x + 3.5f * s, box.y + 8.5f * s};
    Vector2 b = {box.x + 6.5f * s, box.y + 11.5f * s};
    Vector2 c = {box.x + 12.5f * s, box.y + 4.5f * s};
    thick_line(a, b, 2.0f * s, color);
    thick_line(b, c, 2.0f * s, color);
}

/* --- Frame and input ---------------------------------------------------------------- */

void ui_begin_frame(void) {
    float scale = GetWindowScaleDPI().x;
    if (scale >= 0.5f && fabsf(scale - ui.scale) > 0.01f) {
        ui.scale = scale;
        ui.glyphs_dirty = true;
    }
    if (ui.glyphs_dirty) {
        load_fonts();
    }
    ui.now = GetTime();
    ui.mouse = GetMousePosition();
    ui.pressed = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
    ui.released = IsMouseButtonReleased(MOUSE_BUTTON_LEFT);
    ui.down = IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    ui.input_blocked = false;
    ui.focus_count = 0;
    ui.key_consumed = false;
    ui.cursor = MOUSE_CURSOR_DEFAULT;
    ui.tip_candidate = NULL;
    ui.caret_needed = false;
    ui.lost_focus = (ui.previous_focus != ui.focus) ? ui.previous_focus : 0;
    ui.previous_focus = ui.focus;
    ui.popup_chosen = -1;
    ui.popup_chosen_id = -1;

    if (ui.popup_kind != POPUP_NONE) {
        float item_height = ui_line_height(UI_FONT_BODY) + ui_px(14);
        Rectangle list = {ui.popup_anchor.x, ui.popup_anchor.y + ui.popup_anchor.height + ui_px(2),
                          ui.popup_anchor.width, item_height * ui.popup_count + ui_px(2)};
        if (list.y + list.height > GetScreenHeight()) {
            list.y = ui.popup_anchor.y - ui_px(2) - list.height;
        }
        bool close = false;
        if (ui.pressed) {
            if (CheckCollisionPointRec(ui.mouse, list)) {
                int index = (int)((ui.mouse.y - list.y - ui_px(1)) / item_height);
                if (index >= 0 && index < ui.popup_count &&
                    (!ui.popup_enabled || ui.popup_enabled[index])) {
                    ui.popup_chosen = index;
                    ui.popup_chosen_id = ui.popup_id;
                    close = true;
                }
            } else {
                close = true;
            }
            ui.input_blocked = true;
        }
        if (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_TAB)) {
            close = true;
        }
        int step = 0;
        if (IsKeyPressed(KEY_DOWN) || IsKeyPressedRepeat(KEY_DOWN)) {
            step = 1;
        }
        if (IsKeyPressed(KEY_UP) || IsKeyPressedRepeat(KEY_UP)) {
            step = -1;
        }
        for (int tries = 0; step && tries < ui.popup_count; tries++) {
            ui.popup_highlight = (ui.popup_highlight + step + ui.popup_count) % ui.popup_count;
            if (!ui.popup_enabled || ui.popup_enabled[ui.popup_highlight]) {
                break;
            }
        }
        if ((IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE)) && ui.popup_highlight >= 0 &&
            (!ui.popup_enabled || ui.popup_enabled[ui.popup_highlight])) {
            ui.popup_chosen = ui.popup_highlight;
            ui.popup_chosen_id = ui.popup_id;
            close = true;
        }
        if (CheckCollisionPointRec(ui.mouse, list)) {
            int index = (int)((ui.mouse.y - list.y - ui_px(1)) / item_height);
            if (index >= 0 && index < ui.popup_count) {
                ui.popup_highlight = index;
            }
            ui.cursor = MOUSE_CURSOR_POINTING_HAND;
        }
        ui.key_consumed = true;
        if (close) {
            ui.popup_kind = POPUP_NONE;
        }
    }
    if (ui.released) {
        ui.drag_field = 0;
    }
}

static bool mouse_free(void) {
    return ui.popup_kind == POPUP_NONE && !ui.input_blocked;
}

static void focusable(int id) {
    if (ui.focus_count < MAX_FOCUSABLE) {
        ui.focus_order[ui.focus_count++] = id;
    }
}

static bool key_for(int id, int key) {
    return ui.focus == id && !ui.key_consumed && (IsKeyPressed(key) || IsKeyPressedRepeat(key));
}

/* Hover/press/click handling shared by clickable widgets. */
static bool clickable(int id, Rectangle r, bool enabled, bool *hover, bool *pressed) {
    *hover = false;
    *pressed = false;
    if (!enabled) {
        if (ui.focus == id) {
            ui.focus = 0;
        }
        return false;
    }
    focusable(id);
    bool over = mouse_free() && CheckCollisionPointRec(ui.mouse, r);
    *hover = over;
    if (over) {
        ui.cursor = MOUSE_CURSOR_POINTING_HAND;
        if (ui.pressed) {
            ui.active = id;
            ui.focus = id;
        }
    }
    *pressed = ui.active == id && ui.down && over;
    bool clicked = false;
    if (ui.released && ui.active == id) {
        clicked = over;
        ui.active = 0;
    }
    if (key_for(id, KEY_SPACE) || key_for(id, KEY_ENTER)) {
        clicked = true;
        ui.key_consumed = true;
    }
    return clicked;
}

static void focus_outline(int id, Rectangle r, float radius) {
    if (ui.focus == id) {
        ui_round_border(r, radius, fmaxf(1, ui_px(1)), C_FOCUS);
    }
}

bool ui_button(int id, Rectangle r, const char *label, UiButtonStyle style, bool enabled) {
    bool hover;
    bool pressed;
    bool clicked = clickable(id, r, enabled, &hover, &pressed);
    Color fill = style == UI_BUTTON_PRIMARY ? C_PRIMARY
                 : style == UI_BUTTON_WARNING ? C_WARNING
                                              : C_BUTTON;
    Color text = style == UI_BUTTON_NORMAL ? C_TEXT : C_PRIMARY_TEXT;
    if (pressed) {
        fill = C_PRESSED;
        text = WHITE;
    } else if (hover) {
        fill = C_HOVER;
        text = WHITE;
    }
    if (!enabled) {
        fill = ui_fade(fill, C_WINDOW, DISABLED_OPACITY);
        text = ui_fade(text, C_WINDOW, DISABLED_OPACITY);
    }
    float radius = ui_px(4);
    if (ui.focus == id && enabled) {
        framed(r, radius, fmaxf(1, ui_px(1)), C_FOCUS, fill);
    } else {
        ui_round_rect(r, radius, fill);
    }
    UiFont font = style == UI_BUTTON_NORMAL ? UI_FONT_BODY : UI_FONT_BODY_STRONG;
    float width = ui_text_width(font, label);
    ui_text(font, label, r.x + (r.width - width) / 2, r.y + (r.height - ui_line_height(font)) / 2,
            text);
    return clicked;
}

bool ui_icon_button(int id, Rectangle r, UiIcon icon, float angle_degrees, bool enabled) {
    bool hover;
    bool pressed;
    bool clicked = clickable(id, r, enabled, &hover, &pressed);
    Color fill = pressed ? C_PRESSED : hover ? C_HOVER : C_BUTTON;
    Color glyph = icon == UI_ICON_SEARCH ? C_SEARCH_ICON : C_SPARKLE_ICON;
    if (hover || pressed) {
        glyph = WHITE;
    }
    if (!enabled) {
        fill = ui_fade(fill, C_WINDOW, DISABLED_OPACITY);
        glyph = ui_fade(glyph, C_WINDOW, DISABLED_OPACITY);
    }
    float radius = ui_px(4);
    if (ui.focus == id && enabled) {
        framed(r, radius, fmaxf(1, ui_px(1)), C_FOCUS, fill);
    } else {
        ui_round_rect(r, radius, fill);
    }
    if (icon == UI_ICON_SEARCH) {
        icon_search(r, glyph);
    } else {
        icon_sparkle(r, angle_degrees, glyph);
    }
    return clicked;
}

bool ui_image_button(int id, Rectangle r, const Texture2D *image, bool enabled) {
    bool hover;
    bool pressed;
    bool clicked = clickable(id, r, enabled, &hover, &pressed);
    if (enabled && (hover || pressed)) {
        ui_round_rect(r, ui_px(4), pressed ? C_PRESSED : C_HOVER);
    }
    if (image && image->id) {
        Color tint = enabled ? WHITE : Fade(WHITE, DISABLED_OPACITY);
        DrawTexture(*image, (int)(r.x + (r.width - image->width) / 2),
                    (int)(r.y + (r.height - image->height) / 2), tint);
    }
    if (ui.focus == id && enabled) {
        focus_outline(id, r, ui_px(4));
    }
    return clicked;
}

/* --- Text fields ---------------------------------------------------------------------- */

static float prefix_width(UiFont font, const char *text, int bytes) {
    char buffer[UI_TEXT_MAX];
    if (bytes >= (int)sizeof(buffer)) {
        bytes = (int)sizeof(buffer) - 1;
    }
    memcpy(buffer, text, (size_t)bytes);
    buffer[bytes] = 0;
    return ui_text_width(font, buffer);
}

static int previous_boundary(const char *text, int at) {
    if (at <= 0) {
        return 0;
    }
    at--;
    while (at > 0 && (text[at] & 0xC0) == 0x80) {
        at--;
    }
    return at;
}

static int next_boundary(const char *text, int at) {
    int len = (int)strlen(text);
    if (at >= len) {
        return len;
    }
    at++;
    while (at < len && (text[at] & 0xC0) == 0x80) {
        at++;
    }
    return at;
}

static bool word_char(char c) {
    return c != ' ' && c != '\\' && c != '/' && c != '.' && c != '-' && c != '_';
}

static int word_left(const char *text, int at) {
    while (at > 0 && !word_char(text[at - 1])) {
        at = previous_boundary(text, at);
    }
    while (at > 0 && word_char(text[at - 1])) {
        at = previous_boundary(text, at);
    }
    return at;
}

static int word_right(const char *text, int at) {
    int len = (int)strlen(text);
    while (at < len && word_char(text[at])) {
        at = next_boundary(text, at);
    }
    while (at < len && !word_char(text[at])) {
        at = next_boundary(text, at);
    }
    return at;
}

static void delete_range(UiTextField *field, int from, int to) {
    if (from > to) {
        int swap = from;
        from = to;
        to = swap;
    }
    int len = (int)strlen(field->text);
    memmove(field->text + from, field->text + to, (size_t)(len - to + 1));
    field->cursor = from;
    field->anchor = from;
}

static bool delete_selection(UiTextField *field) {
    if (field->cursor == field->anchor) {
        return false;
    }
    delete_range(field, field->cursor, field->anchor);
    return true;
}

static bool insert_text(UiTextField *field, const char *text) {
    delete_selection(field);
    /* Single-line: drop control characters (pasted newlines, tabs). */
    char clean[UI_TEXT_MAX];
    size_t n = 0;
    for (const char *p = text; *p && n + 1 < sizeof(clean); p++) {
        if ((unsigned char)*p >= 0x20 && *p != 0x7F) {
            clean[n++] = *p;
        }
    }
    clean[n] = 0;
    size_t len = strlen(field->text);
    if (n == 0 || len + n >= UI_TEXT_MAX) {
        return false;
    }
    /* Keep whole UTF-8 sequences if truncated. */
    memmove(field->text + field->cursor + n, field->text + field->cursor,
            len - (size_t)field->cursor + 1);
    memcpy(field->text + field->cursor, clean, n);
    field->cursor += (int)n;
    field->anchor = field->cursor;
    return true;
}

void ui_text_field_set(UiTextField *field, const char *text) {
    wo_copy_str(field->text, sizeof(field->text), text ? text : "");
    field->cursor = (int)strlen(field->text);
    field->anchor = field->cursor;
    field->scroll = 0;
    ui_need_glyphs(field->text);
}

static int position_at(UiTextField *field, float x) {
    int best = 0;
    float best_distance = 1e9f;
    int len = (int)strlen(field->text);
    for (int at = 0;; at = next_boundary(field->text, at)) {
        float distance = fabsf(prefix_width(UI_FONT_FIELD, field->text, at) - x);
        if (distance < best_distance) {
            best_distance = distance;
            best = at;
        }
        if (at >= len) {
            break;
        }
    }
    return best;
}

bool ui_text_field(int id, Rectangle r, UiTextField *field, bool enabled) {
    bool changed = false;
    float pad_x = ui_px(10);
    float inner_width = r.width - pad_x * 2;
    bool over = enabled && mouse_free() && CheckCollisionPointRec(ui.mouse, r);
    if (!enabled && ui.focus == id) {
        ui.focus = 0;
    }
    if (enabled) {
        focusable(id);
    }
    if (over) {
        ui.cursor = MOUSE_CURSOR_IBEAM;
        if (ui.pressed) {
            bool was_focused = ui.focus == id;
            ui.focus = id;
            ui.active = id;
            ui.drag_field = id;
            int at = position_at(field, ui.mouse.x - r.x - pad_x + field->scroll);
            bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
            field->cursor = at;
            if (!shift || !was_focused) {
                field->anchor = at;
            }
            ui.caret_since = ui.now;
        }
    }
    if (ui.drag_field == id && ui.down && ui.focus == id) {
        field->cursor = position_at(field, ui.mouse.x - r.x - pad_x + field->scroll);
    }
    if (ui.released && ui.active == id) {
        ui.active = 0;
    }
    bool focused = enabled && ui.focus == id;
    if (focused && !ui.key_consumed) {
        bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
        bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
        int len = (int)strlen(field->text);
        int before = field->cursor;
        int codepoint;
        while ((codepoint = GetCharPressed()) != 0) {
            if (codepoint >= 32 && !ctrl) {
                int size = 0;
                const char *utf8 = CodepointToUTF8(codepoint, &size);
                char piece[8] = {0};
                memcpy(piece, utf8, (size_t)(size < 7 ? size : 7));
                changed |= insert_text(field, piece);
            }
        }
        if (key_for(id, KEY_LEFT)) {
            field->cursor = ctrl ? word_left(field->text, field->cursor)
                                 : previous_boundary(field->text, field->cursor);
        }
        if (key_for(id, KEY_RIGHT)) {
            field->cursor = ctrl ? word_right(field->text, field->cursor)
                                 : next_boundary(field->text, field->cursor);
        }
        if (key_for(id, KEY_HOME)) {
            field->cursor = 0;
        }
        if (key_for(id, KEY_END)) {
            field->cursor = len;
        }
        if (field->cursor != before && !shift) {
            field->anchor = field->cursor;
        }
        if (key_for(id, KEY_BACKSPACE)) {
            if (!delete_selection(field) && field->cursor > 0) {
                int from = ctrl ? word_left(field->text, field->cursor)
                                : previous_boundary(field->text, field->cursor);
                delete_range(field, from, field->cursor);
            }
            changed = true;
        }
        if (key_for(id, KEY_DELETE)) {
            if (!delete_selection(field) && field->cursor < len) {
                int to = ctrl ? word_right(field->text, field->cursor)
                              : next_boundary(field->text, field->cursor);
                delete_range(field, field->cursor, to);
            }
            changed = true;
        }
        if (ctrl && key_for(id, KEY_A)) {
            field->anchor = 0;
            field->cursor = (int)strlen(field->text);
        }
        if (ctrl && (key_for(id, KEY_C) || key_for(id, KEY_X)) && field->cursor != field->anchor) {
            int from = field->cursor < field->anchor ? field->cursor : field->anchor;
            int to = field->cursor < field->anchor ? field->anchor : field->cursor;
            char selection[UI_TEXT_MAX];
            memcpy(selection, field->text + from, (size_t)(to - from));
            selection[to - from] = 0;
            SetClipboardText(selection);
            if (IsKeyPressed(KEY_X)) {
                delete_selection(field);
                changed = true;
            }
        }
        if (ctrl && key_for(id, KEY_V)) {
            const char *clip = GetClipboardText();
            if (clip) {
                /* Pasted paths often come quoted from Explorer's "Copy as path". */
                char trimmed[UI_TEXT_MAX];
                wo_copy_str(trimmed, sizeof(trimmed), clip);
                size_t n = strlen(trimmed);
                while (n > 0 && (trimmed[n - 1] == '\r' || trimmed[n - 1] == '\n' ||
                                 trimmed[n - 1] == ' ')) {
                    trimmed[--n] = 0;
                }
                const char *start = trimmed;
                if (n >= 2 && trimmed[0] == '"' && trimmed[n - 1] == '"') {
                    trimmed[n - 1] = 0;
                    start = trimmed + 1;
                }
                changed |= insert_text(field, start);
            }
        }
        if (field->cursor != before || changed) {
            ui.caret_since = ui.now;
        }
        if (changed) {
            ui_need_glyphs(field->text);
        }
    }
    /* Keep the caret visible; show the start of the path when unfocused. */
    float caret_x = prefix_width(UI_FONT_FIELD, field->text, field->cursor);
    if (!focused) {
        field->scroll = 0;
    } else if (caret_x - field->scroll > inner_width) {
        field->scroll = caret_x - inner_width;
    } else if (caret_x - field->scroll < 0) {
        field->scroll = caret_x;
    }
    Color border = focused ? C_FIELD_FOCUS : over ? C_FIELD_HOVER : C_FIELD_BORDER;
    Color fill = C_FIELD;
    Color text = C_FIELD_TEXT;
    if (!enabled) {
        border = ui_fade(border, C_WINDOW, DISABLED_OPACITY);
        fill = ui_fade(fill, C_WINDOW, DISABLED_OPACITY);
        text = ui_fade(text, C_WINDOW, DISABLED_OPACITY);
    }
    framed(r, ui_px(4), fmaxf(1, ui_px(1)), border, fill);
    float text_y = r.y + (r.height - ui_line_height(UI_FONT_FIELD)) / 2;
    BeginScissorMode((int)(r.x + pad_x / 2), (int)r.y, (int)(r.width - pad_x), (int)r.height);
    if (focused && field->cursor != field->anchor) {
        int from = field->cursor < field->anchor ? field->cursor : field->anchor;
        int to = field->cursor < field->anchor ? field->anchor : field->cursor;
        float x0 = prefix_width(UI_FONT_FIELD, field->text, from);
        float x1 = prefix_width(UI_FONT_FIELD, field->text, to);
        DrawRectangleRec((Rectangle){r.x + pad_x + x0 - field->scroll, text_y, x1 - x0,
                                     ui_line_height(UI_FONT_FIELD)},
                         C_SELECTION);
    }
    ui_text(UI_FONT_FIELD, field->text, r.x + pad_x - field->scroll, text_y, text);
    if (focused) {
        ui.caret_needed = true;
        bool visible = fmod(ui.now - ui.caret_since, 1.06) < 0.53;
        if (visible) {
            float x = roundf(r.x + pad_x + caret_x - field->scroll);
            DrawRectangleRec((Rectangle){x, text_y, fmaxf(1, ui_px(1)), ui_line_height(UI_FONT_FIELD)},
                             C_FIELD_TEXT);
        }
    }
    EndScissorMode();
    return changed;
}

/* --- Check boxes, dropdowns and menus ---------------------------------------------------- */

bool ui_checkbox(int id, Rectangle r, const char *label, bool *value, bool enabled) {
    float box = ui_px(17);
    float width = box + ui_px(8) + ui_text_width(UI_FONT_BODY, label);
    Rectangle hit = {r.x, r.y, width, fmaxf(box, r.height)};
    bool hover;
    bool pressed;
    bool clicked = clickable(id, hit, enabled, &hover, &pressed);
    if (clicked) {
        *value = !*value;
    }
    Rectangle square = {r.x, r.y + (hit.height - box) / 2, box, box};
    Color border = ui.focus == id && enabled ? C_CHECK_FOCUS : C_CHECK_BORDER;
    Color fill = hover ? C_POPUP_HIGHLIGHT : C_FIELD;
    Color check = C_PRIMARY;
    Color text = C_CHECK_TEXT;
    if (!enabled) {
        border = ui_fade(border, C_WINDOW, DISABLED_OPACITY);
        fill = ui_fade(fill, C_WINDOW, DISABLED_OPACITY);
        check = ui_fade(check, C_WINDOW, DISABLED_OPACITY);
        text = ui_fade(text, C_WINDOW, DISABLED_OPACITY);
    }
    framed(square, ui_px(3), fmaxf(1, ui_px(1)), border, fill);
    if (*value) {
        icon_check(square, check);
    }
    ui_text(UI_FONT_BODY, label, square.x + box + ui_px(8),
            r.y + (hit.height - ui_line_height(UI_FONT_BODY)) / 2, text);
    return clicked;
}

static void open_popup(PopupKind kind, int id, Rectangle anchor, const char *const *items,
                       const bool *enabled, int count, int highlight) {
    ui.popup_kind = kind;
    ui.popup_id = id;
    ui.popup_anchor = anchor;
    ui.popup_items = items;
    ui.popup_enabled = enabled;
    ui.popup_count = count;
    ui.popup_highlight = highlight;
}

bool ui_dropdown(int id, Rectangle r, const char *const *items, const bool *item_enabled,
                 int count, int *selected, bool enabled) {
    bool changed = false;
    if (ui.popup_chosen_id == id && ui.popup_chosen >= 0 && ui.popup_chosen != *selected) {
        *selected = ui.popup_chosen;
        changed = true;
    }
    if (ui.popup_kind == POPUP_DROPDOWN && ui.popup_id == id) {
        ui.popup_anchor = r;
        ui.popup_items = items;
        ui.popup_enabled = item_enabled;
    }
    bool hover;
    bool pressed;
    bool clicked = clickable(id, r, enabled, &hover, &pressed);
    bool alt = IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT);
    if (key_for(id, KEY_F4) || (alt && key_for(id, KEY_DOWN))) {
        clicked = true;
        ui.key_consumed = true;
    }
    if (clicked && ui.popup_kind == POPUP_NONE) {
        open_popup(POPUP_DROPDOWN, id, r, items, item_enabled, count, *selected);
    }
    /* Up/Down change the value directly while the closed dropdown has focus. */
    int step = key_for(id, KEY_DOWN) ? 1 : key_for(id, KEY_UP) ? -1 : 0;
    if (step && enabled && ui.popup_kind == POPUP_NONE) {
        for (int candidate = *selected + step; candidate >= 0 && candidate < count; candidate += step) {
            if (!item_enabled || item_enabled[candidate]) {
                *selected = candidate;
                changed = true;
                break;
            }
        }
        ui.key_consumed = true;
    }
    bool focused = enabled && ui.focus == id;
    Color border = focused ? C_FIELD_FOCUS : hover ? C_FIELD_HOVER : C_FIELD_BORDER;
    Color fill = C_FIELD;
    Color text = C_TEXT;
    if (!enabled) {
        border = ui_fade(border, C_WINDOW, DISABLED_OPACITY);
        fill = ui_fade(fill, C_WINDOW, DISABLED_OPACITY);
        text = ui_fade(text, C_WINDOW, DISABLED_OPACITY);
    }
    framed(r, ui_px(4), fmaxf(1, ui_px(1)), border, fill);
    const char *label = (*selected >= 0 && *selected < count) ? items[*selected] : "";
    ui_text(UI_FONT_BODY, label, r.x + ui_px(10), r.y + (r.height - ui_line_height(UI_FONT_BODY)) / 2,
            text);
    icon_chevron((Rectangle){r.x + r.width - ui_px(28), r.y, ui_px(20), r.height}, text);
    return changed;
}

void ui_menu_open(int id) {
    ui.menu_request = id;
}

int ui_menu(int id, Rectangle anchor, const char *const *items, int count) {
    if (ui.menu_request == id) {
        ui.menu_request = -1;
        open_popup(POPUP_MENU, id, anchor, items, NULL, count, 0);
    }
    if (ui.popup_kind == POPUP_MENU && ui.popup_id == id) {
        ui.popup_anchor = anchor;
        ui.popup_items = items;
    }
    if (ui.popup_chosen_id == id) {
        return ui.popup_chosen;
    }
    return -1;
}

/* --- Tooltips, progress and focus ------------------------------------------------------- */

void ui_tooltip(Rectangle r, const char *text) {
    if (text && *text && mouse_free() && !ui.down && CheckCollisionPointRec(ui.mouse, r)) {
        ui.tip_rect = r;
        ui.tip_candidate = text;
    }
}

void ui_progress(Rectangle r, float fraction, bool indeterminate) {
    DrawRectangleRec(r, C_PROGRESS_TRACK);
    if (indeterminate) {
        float band = r.width * 0.25f;
        float phase = (float)fmod(ui.now * 0.6, 1.0);
        float x = r.x - band + (r.width + band) * phase;
        float left = fmaxf(r.x, x);
        float right = fminf(r.x + r.width, x + band);
        if (right > left) {
            DrawRectangleRec((Rectangle){left, r.y, right - left, r.height}, C_PROGRESS);
        }
    } else if (fraction > 0) {
        DrawRectangleRec((Rectangle){r.x, r.y, r.width * fminf(1, fraction), r.height}, C_PROGRESS);
    }
}

bool ui_has_focus(int id) {
    return ui.focus == id;
}

bool ui_lost_focus(int id) {
    return ui.lost_focus == id;
}

void ui_set_focus(int id) {
    ui.focus = id;
}

bool ui_popup_open(void) {
    return ui.popup_kind != POPUP_NONE;
}

bool ui_animating(void) {
    bool tip_pending = ui.tip_candidate && !ui.tip_visible;
    return ui.caret_needed || tip_pending || ui.popup_kind != POPUP_NONE;
}

static void draw_popup(void) {
    if (ui.popup_kind == POPUP_NONE) {
        return;
    }
    float item_height = ui_line_height(UI_FONT_BODY) + ui_px(14);
    Rectangle list = {ui.popup_anchor.x, ui.popup_anchor.y + ui.popup_anchor.height + ui_px(2),
                      ui.popup_anchor.width, item_height * ui.popup_count + ui_px(2)};
    if (list.y + list.height > GetScreenHeight()) {
        list.y = ui.popup_anchor.y - ui_px(2) - list.height;
    }
    DrawRectangleRec(list, C_POPUP_BORDER);
    DrawRectangleRec((Rectangle){list.x + 1, list.y + 1, list.width - 2, list.height - 2}, C_FIELD);
    for (int i = 0; i < ui.popup_count; i++) {
        Rectangle item = {list.x + 1, list.y + 1 + item_height * i, list.width - 2, item_height};
        bool enabled = !ui.popup_enabled || ui.popup_enabled[i];
        if (i == ui.popup_highlight && enabled) {
            DrawRectangleRec(item, C_POPUP_HIGHLIGHT);
        }
        Color text = enabled ? C_TEXT : ui_fade(C_TEXT, C_FIELD, DISABLED_OPACITY);
        ui_text(UI_FONT_BODY, ui.popup_items[i], item.x + ui_px(10),
                item.y + (item.height - ui_line_height(UI_FONT_BODY)) / 2, text);
    }
}

static void draw_tooltip(void) {
    if (!ui.tip_candidate) {
        ui.tip_visible = false;
        ui.tip_since = 0;
        return;
    }
    bool same = ui.tip_since > 0 && strcmp(ui.tip_text, ui.tip_candidate) == 0 &&
                ui.tip_shown_rect.x == ui.tip_rect.x && ui.tip_shown_rect.y == ui.tip_rect.y;
    if (!same) {
        wo_copy_str(ui.tip_text, sizeof(ui.tip_text), ui.tip_candidate);
        ui.tip_shown_rect = ui.tip_rect;
        ui.tip_since = ui.now;
        ui.tip_visible = false;
        ui_need_glyphs(ui.tip_text);
        return;
    }
    double age = ui.now - ui.tip_since;
    ui.tip_visible = age >= TOOLTIP_DELAY;
    if (!ui.tip_visible || age > TOOLTIP_DELAY + TOOLTIP_DURATION) {
        return;
    }
    float max_width = ui_px(360);
    float pad_x = ui_px(8);
    float pad_y = ui_px(6);
    float text_width = fminf(max_width, ui_text_width(UI_FONT_BODY, ui.tip_text));
    float height = ui_text_wrapped(UI_FONT_BODY, ui.tip_text, 0, 0, max_width, BLANK, false);
    Rectangle box = {ui.mouse.x + ui_px(12), ui.mouse.y + ui_px(18), text_width + pad_x * 2,
                     height + pad_y * 2};
    if (box.x + box.width > GetScreenWidth() - ui_px(4)) {
        box.x = GetScreenWidth() - ui_px(4) - box.width;
    }
    if (box.y + box.height > GetScreenHeight() - ui_px(4)) {
        box.y = ui.mouse.y - ui_px(8) - box.height;
    }
    if (box.x < ui_px(4)) {
        box.x = ui_px(4);
    }
    if (box.y < ui_px(4)) {
        box.y = ui_px(4);
    }
    framed(box, ui_px(4), fmaxf(1, ui_px(1)), C_POPUP_BORDER, C_TIP);
    ui_text_wrapped(UI_FONT_BODY, ui.tip_text, box.x + pad_x, box.y + pad_y, max_width, C_TEXT, true);
}

void ui_end_frame(void) {
    /* Clicking empty space clears keyboard focus. */
    if (ui.pressed && !ui.input_blocked && ui.active == 0 && ui.drag_field == 0) {
        ui.focus = 0;
    }
    if (!ui.key_consumed && IsKeyPressed(KEY_TAB) && ui.focus_count > 0) {
        bool back = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
        int index = -1;
        for (int i = 0; i < ui.focus_count; i++) {
            if (ui.focus_order[i] == ui.focus) {
                index = i;
            }
        }
        if (index < 0) {
            index = back ? 0 : ui.focus_count - 1;
        }
        index = (index + (back ? -1 : 1) + ui.focus_count) % ui.focus_count;
        ui.focus = ui.focus_order[index];
    }
    /* Characters typed while no text field had focus are discarded. */
    while (GetCharPressed() != 0) {
    }
    draw_popup();
    draw_tooltip();
    static int last_cursor = -1;
    if (ui.cursor != last_cursor) {
        SetMouseCursor(ui.cursor);
        last_cursor = ui.cursor;
    }
}
