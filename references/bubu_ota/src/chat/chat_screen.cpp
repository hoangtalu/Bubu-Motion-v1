#include "chat_screen.h"
#include <lvgl.h>
#include <cstring>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_task_wdt.h>
#include <Arduino.h>
#include <stdio.h>

LV_FONT_DECLARE(lv_font_montserrat_vn_20);
LV_FONT_DECLARE(lv_font_montserrat_14);

namespace ChatScreen {

static lv_obj_t* sBox = nullptr;
static lv_obj_t* sTextCont = nullptr;
static lv_obj_t* sTextLabel = nullptr;
static bool sVisible = false;

// sCachedText is written from any core (showText) and read from Core 1 (flushPendingText).
// sHasUnreadText signals Core 1 that new text is waiting to be rendered.
static char sCachedText[2048] = {0};
static bool sHasUnreadText = false;

static constexpr int16_t SCREEN_SIZE = 240;
static constexpr int16_t BOX_WIDTH = 196;
static constexpr int16_t BOX_PAD = 12;
static constexpr int16_t MIN_VISIBLE_LINES = 3;
static constexpr int16_t MAX_VISIBLE_LINES = 5;

static bool isPointInsideObj(lv_obj_t* obj, uint16_t x, uint16_t y) {
    if (!obj) return false;
    lv_area_t a;
    lv_obj_get_coords(obj, &a);
    return x >= a.x1 && x <= a.x2 && y >= a.y1 && y <= a.y2;
}

static void updatePopupLayoutForText() {
    if (!sBox || !sTextCont || !sTextLabel) return;

    lv_obj_update_layout(sTextLabel);

    int32_t lineHeight = lv_font_get_line_height(&lv_font_montserrat_vn_20);
    if (lineHeight <= 0) lineHeight = 20;

    int32_t contentHeight = lv_obj_get_height(sTextLabel);
    if (contentHeight <= 0) contentHeight = lineHeight;

    int32_t requiredLines = (contentHeight + lineHeight - 1) / lineHeight;
    int32_t visibleLines = (requiredLines <= MIN_VISIBLE_LINES) ? MIN_VISIBLE_LINES : MAX_VISIBLE_LINES;

    int32_t textHeight = visibleLines * lineHeight + 6;
    int32_t boxHeight = textHeight + BOX_PAD * 2;
    int32_t textWidth = BOX_WIDTH - (BOX_PAD * 2);

    lv_obj_set_size(sTextCont, textWidth, textHeight);
    lv_obj_set_pos(sTextCont, BOX_PAD, BOX_PAD);
    lv_obj_set_width(sTextLabel, textWidth);

    lv_obj_set_size(sBox, BOX_WIDTH, boxHeight);
    lv_obj_set_pos(sBox, (SCREEN_SIZE - BOX_WIDTH) / 2, (SCREEN_SIZE - boxHeight) / 2);
}

void begin() {
    if (sBox) return;

    // Prevent LVGL from scrolling the screen when off-screen buttons are tapped
    lv_obj_clear_flag(lv_screen_active(), LV_OBJ_FLAG_SCROLLABLE);

    sBox = lv_obj_create(lv_screen_active());
    lv_obj_set_style_radius(sBox, 16, 0);
    lv_obj_set_style_bg_color(sBox, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(sBox, 220, 0);
    lv_obj_set_style_border_color(sBox, lv_color_white(), 0);
    lv_obj_set_style_border_width(sBox, 2, 0);
    lv_obj_set_style_pad_all(sBox, 0, 0);
    lv_obj_clear_flag(sBox, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(sBox, LV_OBJ_FLAG_HIDDEN);

    sTextCont = lv_obj_create(sBox);
    lv_obj_set_style_bg_opa(sTextCont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(sTextCont, 0, 0);
    lv_obj_set_style_pad_all(sTextCont, 0, 0);
    lv_obj_add_flag(sTextCont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(sTextCont, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(sTextCont, LV_SCROLLBAR_MODE_OFF);

    sTextLabel = lv_label_create(sTextCont);
    lv_obj_set_style_text_font(sTextLabel, &lv_font_montserrat_vn_20, 0);
    lv_obj_set_style_text_color(sTextLabel, lv_color_white(), 0);
    lv_obj_set_style_text_align(sTextLabel, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(sTextLabel, LV_LABEL_LONG_WRAP);
    lv_label_set_text(sTextLabel, "");

    updatePopupLayoutForText();

    // Any text cached before init (sHasUnreadText = true) will be shown by
    // flushPendingText() on the next DisplaySystem_update() frame.
}

void show() {
    if (!sBox) return;
    if (sCachedText[0] == '\0') return;
    showText(sCachedText);
}

void hide() {
    if (!sBox) return;
    lv_obj_add_flag(sBox, LV_OBJ_FLAG_HIDDEN);
    sVisible = false;
}

bool isVisible() {
    return sVisible;
}

void updateState() {
    // Kept for compatibility with older call sites.
}

// ------------------------------------------------------------------
// showText — safe to call from any core.
// Only caches the text and sets a flag; no LVGL operations here.
// Core 1 picks up the pending text via flushPendingText().
// ------------------------------------------------------------------
void showText(const char* text) {
    if (!text || !*text) return;
    strncpy(sCachedText, text, sizeof(sCachedText) - 1);
    sCachedText[sizeof(sCachedText) - 1] = '\0';
    sHasUnreadText = true;
}

// ------------------------------------------------------------------
// flushPendingText — MUST be called from Core 1 (display loop) only.
// Applies the cached text to LVGL and makes the popup visible.
// ------------------------------------------------------------------
void flushPendingText() {
    if (!sHasUnreadText || sCachedText[0] == '\0') return;
    sHasUnreadText = false;

    if (!sBox || !sTextLabel || !sTextCont) return;

    lv_label_set_text(sTextLabel, sCachedText);
    updatePopupLayoutForText();
    lv_obj_scroll_to_y(sTextCont, 0, LV_ANIM_OFF);
    lv_obj_clear_flag(sBox, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(sBox);
    sVisible = true;
}

void clearText() {
    if (sTextLabel) lv_label_set_text(sTextLabel, "");
    sCachedText[0] = '\0';
    sHasUnreadText = false;
    hide();
}

bool hasUnreadText() {
    return sHasUnreadText;
}

bool handleTap(uint16_t x, uint16_t y) {
    if (!sVisible || !sBox) return false;

    // Tap anywhere dismisses the chat popup
    hide();

    Serial.printf("[ChatScreen] handleTap: dismissed popup x=%u y=%u\n",
                  (unsigned)x, (unsigned)y);

    return true;
}

bool handleLongPress(uint16_t x, uint16_t y) {
    (void)x;
    (void)y;
    if (!sVisible) return false;
    hide();
    return true;
}

}  // namespace ChatScreen
