/* Immediate-mode widgets for Setup, drawn with raylib in WO4K's own dark
 * style (no raygui). All rectangles are in physical pixels; use ui_px() to
 * convert layout units (96-DPI pixels). Widgets are identified by small
 * integer ids that stay the same from frame to frame. */
#ifndef SETUP_UI_H
#define SETUP_UI_H

#include <stdbool.h>

#include "raylib.h"

typedef enum UiFont {
    UI_FONT_BODY,        /* 13, regular */
    UI_FONT_BODY_STRONG, /* 13, semibold (primary buttons) */
    UI_FONT_FIELD,       /* 14, regular (text fields) */
    UI_FONT_SUBTITLE,    /* 16, regular */
    UI_FONT_CAPS,        /* 12, bold (section labels) */
    UI_FONT_BETA,        /* 13, bold */
    UI_FONT_COUNT
} UiFont;

typedef enum UiButtonStyle {
    UI_BUTTON_NORMAL,
    UI_BUTTON_PRIMARY,
    UI_BUTTON_WARNING
} UiButtonStyle;

typedef enum UiIcon { UI_ICON_SEARCH, UI_ICON_SPARKLE } UiIcon;

#define UI_TEXT_MAX 1024

typedef struct UiTextField {
    char text[UI_TEXT_MAX];
    int cursor; /* byte offsets on code point boundaries */
    int anchor;
    float scroll;
} UiTextField;

bool ui_init(void);
void ui_shutdown(void);
/* Call after BeginDrawing / before EndDrawing. */
void ui_begin_frame(void);
void ui_end_frame(void);
/* True when something is moving (caret, tooltip delay, popups) and the
 * caller should keep rendering frames instead of waiting for input. */
bool ui_animating(void);
/* True when a popup currently owns the mouse. */
bool ui_popup_open(void);

float ui_scale(void);
float ui_px(float units);
float ui_line_height(UiFont font);
float ui_text_width(UiFont font, const char *text);
void ui_text(UiFont font, const char *text, float x, float y, Color color);
/* Word-wrapped text; returns the height used. draw=false only measures. */
float ui_text_wrapped(UiFont font, const char *text, float x, float y, float width, Color color,
                      bool draw);
/* Make sure the fonts can show every character of text. */
void ui_need_glyphs(const char *text);

void ui_round_rect(Rectangle r, float radius, Color fill);
void ui_round_border(Rectangle r, float radius, float thickness, Color color);
Color ui_fade(Color color, Color background, float opacity);

bool ui_button(int id, Rectangle r, const char *label, UiButtonStyle style, bool enabled);
bool ui_icon_button(int id, Rectangle r, UiIcon icon, float angle_degrees, bool enabled);
bool ui_image_button(int id, Rectangle r, const Texture2D *image, bool enabled);
/* Returns true when the text changed this frame. */
bool ui_text_field(int id, Rectangle r, UiTextField *field, bool enabled);
void ui_text_field_set(UiTextField *field, const char *text);
bool ui_checkbox(int id, Rectangle r, const char *label, bool *value, bool enabled);
/* A horizontal slider over minimum..maximum in whole steps; returns true
 * when the value changed. Left and Right move it by one step. */
bool ui_slider(int id, Rectangle r, int minimum, int maximum, int step, int *value, bool enabled);
/* While true, widgets are drawn but take no mouse, keyboard or focus and
 * show no tooltip: what lies behind a dialog drawn after them. */
void ui_behind(bool behind);
/* Returns true when the selection changed. Disabled items cannot be chosen. */
bool ui_dropdown(int id, Rectangle r, const char *const *items, const bool *item_enabled,
                 int count, int *selected, bool enabled);
/* A popup menu anchored below a widget: open it with ui_menu_open, then
 * call ui_menu every frame; returns the chosen index once, else -1. */
void ui_menu_open(int id);
int ui_menu(int id, Rectangle anchor, const char *const *items, int count);

void ui_tooltip(Rectangle r, const char *text);
void ui_progress(Rectangle r, float fraction, bool indeterminate);

/* Keyboard focus. */
bool ui_has_focus(int id);
/* True once, in the frame where id lost keyboard focus. */
bool ui_lost_focus(int id);
void ui_set_focus(int id);

#endif
