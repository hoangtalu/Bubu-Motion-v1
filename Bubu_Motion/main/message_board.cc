#include "message_board.h"

#include "application.h"
#include "assets/lang_config.h"
#include "display/display.h"

#include <algorithm>
#include <cstdio>
#include <mutex>
#include <vector>

#include <esp_log.h>
#include <esp_timer.h>

#ifdef HAVE_LVGL
#include <lvgl.h>
extern const lv_font_t lv_font_montserrat_vn_20;
extern const lv_font_t lv_font_montserrat_vn_28;
#endif

namespace MessageBoard {
namespace {

std::mutex s_mutex;
Display* s_display = nullptr;
bool s_initialized = false;
bool s_open = false;
int32_t s_active_reminder_id = 0;
enum class BoardMode {
    kGeneric,
    kReminder,
    kBindCode,
    kSteps,
};
BoardMode s_mode = BoardMode::kGeneric;
bool s_repeat_notification = false;
esp_timer_handle_t s_notification_timer = nullptr;
constexpr int64_t kNotificationRepeatUs = 2500000;
esp_timer_handle_t s_bind_reopen_timer = nullptr;
bool s_bind_reopen_enabled = false;
bool s_bind_reopen_scheduled = false;
std::string s_bind_message;
std::string s_bind_code;
constexpr int64_t kBindReopenUs = 5000000;

// One tutor step card. Held even while the board is closed: a child who closed
// card 2 to think still needs card 3 when it arrives (docs §4).
struct StepCard {
    std::string label;
    std::string expr;
    std::string note;
};
std::vector<StepCard> s_cards;
int s_current_card = 0;

#ifdef HAVE_LVGL
lv_obj_t* s_panel = nullptr;
lv_obj_t* s_body = nullptr;

// Step-card widgets, created on first use like the panel itself.
lv_obj_t* s_step_label = nullptr;   // "bước 2"
lv_obj_t* s_step_expr = nullptr;    // "3 × 12 = ?"
lv_obj_t* s_step_note = nullptr;    // "mỗi rổ 12 quả"
lv_obj_t* s_step_dots[kMaxStepCards] = {nullptr, nullptr, nullptr, nullptr};
lv_obj_t* s_step_back = nullptr;
lv_obj_t* s_step_forward = nullptr;

// Widths measured against the compiled fonts inside the r=97 safe circle, at
// each slot's own height (docs §3). These are pixels, not characters: `m` and
// `i` differ threefold and Vietnamese diacritics stack.
constexpr int32_t kStepLabelMaxWidth = 118;
constexpr int32_t kStepExprMaxWidth = 186;
constexpr int32_t kStepNoteMaxWidth = 178;

// Offsets from the panel centre, which is also the screen centre. The label
// sits high, against the top of the circle, where the glass is too narrow for
// the other two slots anyway — leaving it beside them wasted that arc. The
// expression and the note stay on the widest part of the circle: moving either
// one down costs width (the note's band drops from 179 px to 175 px just 4 px
// lower), and the note is already the tightest fit on the reference problem.
constexpr int32_t kStepLabelDy = -62;
constexpr int32_t kStepExprDy = -8;
constexpr int32_t kStepNoteDy = 24;
constexpr int32_t kStepControlsDy = 66;

// Arrow hit boxes, in screen coordinates (docs §3).
constexpr int32_t kArrowBackX = 75;
constexpr int32_t kArrowForwardX = 165;
constexpr int32_t kArrowY = 186;
constexpr int32_t kArrowHitRadius = 18;

constexpr int32_t kDotSpacing = 15;
constexpr int32_t kDotSize = 6;
constexpr int32_t kDotSizeCurrent = 10;

// Chevrons drawn as lines so they do not depend on a symbol font the compiled
// Vietnamese fonts do not carry.
const lv_point_precise_t kBackChevron[] = {{14, 0}, {2, 11}, {14, 22}};
const lv_point_precise_t kForwardChevron[] = {{2, 0}, {14, 11}, {2, 22}};

int32_t MeasureText(const std::string& text, const lv_font_t* font) {
    lv_point_t size;
    lv_text_get_size(&size, text.c_str(), font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    return size.x;
}

// Decodes one UTF-8 code point. Returns 0 at the end of the string or on a
// malformed byte, which the caller treats as "nothing left to check".
uint32_t NextCodePoint(const std::string& text, size_t* pos) {
    if (*pos >= text.size()) {
        return 0;
    }
    const auto byte = static_cast<unsigned char>(text[*pos]);
    int extra = 0;
    uint32_t cp = 0;
    if (byte < 0x80) {
        cp = byte;
    } else if ((byte & 0xE0) == 0xC0) {
        cp = byte & 0x1F;
        extra = 1;
    } else if ((byte & 0xF0) == 0xE0) {
        cp = byte & 0x0F;
        extra = 2;
    } else if ((byte & 0xF8) == 0xF0) {
        cp = byte & 0x07;
        extra = 3;
    } else {
        *pos = text.size();
        return 0;
    }
    if (*pos + extra >= text.size()) {
        // Truncated sequence: stop rather than read past the end.
        *pos = text.size();
        return 0;
    }
    for (int i = 1; i <= extra; ++i) {
        const auto cont = static_cast<unsigned char>(text[*pos + i]);
        if ((cont & 0xC0) != 0x80) {
            *pos = text.size();
            return 0;
        }
        cp = (cp << 6) | (cont & 0x3F);
    }
    *pos += extra + 1;
    return cp;
}

// Appends the first character the font cannot draw to *missing. Gemini emits
// −, ≈, √, → and friends routinely and none of them are in the compiled fonts,
// so the model has to be told rather than shown an empty box.
bool AllGlyphsPresent(const std::string& text, const lv_font_t* font, std::string* missing) {
    size_t pos = 0;
    while (pos < text.size()) {
        const size_t start = pos;
        const uint32_t cp = NextCodePoint(text, &pos);
        if (cp == 0) {
            break;
        }
        if (cp == '\n' || cp == '\r' || cp == ' ') {
            continue;
        }
        lv_font_glyph_dsc_t dsc;
        if (!lv_font_get_glyph_dsc(font, &dsc, cp, 0)) {
            if (missing != nullptr) {
                *missing = text.substr(start, pos - start);
            }
            return false;
        }
    }
    return true;
}

bool IsInside(lv_obj_t* obj, uint16_t x, uint16_t y) {
    if (obj == nullptr) {
        return false;
    }
    lv_area_t area;
    lv_obj_get_coords(obj, &area);
    return x >= area.x1 && x <= area.x2 && y >= area.y1 && y <= area.y2;
}

void EnsurePanelLocked() {
    if (s_display == nullptr || s_panel != nullptr) {
        return;
    }

    // Round board to match the 240x240 circular display: a content-sized
    // rounded rect used to poke square-ish corners past the round glass.
    // A fixed circular panel (radius = half its own size) plus a narrower
    // label keeps the text within the circle's inscribed square.
    constexpr lv_coord_t kBoardDiameter = 200;
    constexpr lv_coord_t kBodyWidth = 140;

    s_panel = lv_obj_create(lv_screen_active());
    lv_obj_set_size(s_panel, kBoardDiameter, kBoardDiameter);
    lv_obj_center(s_panel);
    lv_obj_set_style_radius(s_panel, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_clip_corner(s_panel, true, 0);
    lv_obj_set_style_bg_color(s_panel, lv_color_hex(0x050812), 0);
    lv_obj_set_style_bg_opa(s_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_panel, 3, 0);
    lv_obj_set_style_border_color(s_panel, lv_color_hex(0x58F5C9), 0);
    lv_obj_set_style_border_opa(s_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(s_panel, 14, 0);
    lv_obj_clear_flag(s_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_panel, LV_OBJ_FLAG_HIDDEN);

    s_body = lv_label_create(s_panel);
    lv_obj_set_width(s_body, kBodyWidth);
    lv_obj_set_style_text_font(s_body, &lv_font_montserrat_vn_20, 0);
    lv_obj_set_style_text_color(s_body, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_align(s_body, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_body, LV_LABEL_LONG_WRAP);
    lv_obj_center(s_body);
}

lv_obj_t* CreateStepLabelLocked(const lv_font_t* font, uint32_t color, int32_t dy) {
    lv_obj_t* label = lv_label_create(s_panel);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    // One line per slot, never wrapped: anything too wide was refused before it
    // reached here, so wrapping could only hide a bug.
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, dy);
    lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);
    return label;
}

lv_obj_t* CreateChevronLocked(const lv_point_precise_t* points, int32_t dx) {
    lv_obj_t* line = lv_line_create(s_panel);
    lv_line_set_points(line, points, 3);
    lv_obj_set_style_line_width(line, 4, 0);
    lv_obj_set_style_line_color(line, lv_color_hex(0x58F5C9), 0);
    lv_obj_set_style_line_rounded(line, true, 0);
    lv_obj_align(line, LV_ALIGN_CENTER, dx, kStepControlsDy);
    lv_obj_add_flag(line, LV_OBJ_FLAG_HIDDEN);
    return line;
}

void EnsureStepObjectsLocked() {
    if (s_panel == nullptr || s_step_label != nullptr) {
        return;
    }
    s_step_label = CreateStepLabelLocked(&lv_font_montserrat_vn_20, 0x9FB4CC, kStepLabelDy);
    s_step_expr = CreateStepLabelLocked(&lv_font_montserrat_vn_28, 0xFFFFFF, kStepExprDy);
    s_step_note = CreateStepLabelLocked(&lv_font_montserrat_vn_20, 0xE8EEF8, kStepNoteDy);

    for (int i = 0; i < kMaxStepCards; ++i) {
        lv_obj_t* dot = lv_obj_create(s_panel);
        lv_obj_set_size(dot, kDotSize, kDotSize);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(dot, lv_color_hex(0x58F5C9), 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(dot, 0, 0);
        lv_obj_set_style_pad_all(dot, 0, 0);
        lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(dot, LV_OBJ_FLAG_HIDDEN);
        s_step_dots[i] = dot;
    }

    // x = 75 and x = 165 on a 240 px screen, i.e. 45 px either side of centre.
    s_step_back = CreateChevronLocked(kBackChevron, kArrowBackX - 120);
    s_step_forward = CreateChevronLocked(kForwardChevron, kArrowForwardX - 120);
}

void HideStepObjectsLocked() {
    lv_obj_t* const objects[] = {s_step_label, s_step_expr, s_step_note, s_step_back, s_step_forward};
    for (lv_obj_t* obj : objects) {
        if (obj != nullptr) {
            lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
        }
    }
    for (lv_obj_t* dot : s_step_dots) {
        if (dot != nullptr) {
            lv_obj_add_flag(dot, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

std::string StepLabelTextLocked(int index) {
    if (index < 0 || index >= static_cast<int>(s_cards.size())) {
        return std::string();
    }
    if (!s_cards[index].label.empty()) {
        return s_cards[index].label;
    }
    return "bước " + std::to_string(index + 1);
}

// Draws the current card. Caller holds s_mutex and the display lock.
void RenderStepsLocked() {
    if (s_panel == nullptr || s_cards.empty()) {
        return;
    }
    EnsureStepObjectsLocked();
    if (s_step_label == nullptr) {
        return;
    }

    s_current_card = std::clamp(s_current_card, 0, static_cast<int>(s_cards.size()) - 1);
    const StepCard& card = s_cards[s_current_card];

    if (s_body != nullptr) {
        lv_obj_add_flag(s_body, LV_OBJ_FLAG_HIDDEN);
    }

    lv_label_set_text(s_step_label, StepLabelTextLocked(s_current_card).c_str());
    lv_label_set_text(s_step_expr, card.expr.c_str());
    lv_label_set_text(s_step_note, card.note.c_str());
    lv_obj_clear_flag(s_step_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_step_expr, LV_OBJ_FLAG_HIDDEN);
    // An empty note leaves its band blank rather than shifting the other slots:
    // the geometry is fixed so a card does not jump when a note is left out.
    if (card.note.empty()) {
        lv_obj_add_flag(s_step_note, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(s_step_note, LV_OBJ_FLAG_HIDDEN);
    }
    // Re-align: the label object resizes to its own text, and LVGL keeps the
    // alignment it was given, but only if asked again after the text changed.
    lv_obj_align(s_step_label, LV_ALIGN_CENTER, 0, kStepLabelDy);
    lv_obj_align(s_step_expr, LV_ALIGN_CENTER, 0, kStepExprDy);
    lv_obj_align(s_step_note, LV_ALIGN_CENTER, 0, kStepNoteDy);

    const int count = static_cast<int>(s_cards.size());
    for (int i = 0; i < kMaxStepCards; ++i) {
        lv_obj_t* dot = s_step_dots[i];
        if (dot == nullptr) {
            continue;
        }
        if (i >= count) {
            lv_obj_add_flag(dot, LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        const bool current = i == s_current_card;
        const int32_t size = current ? kDotSizeCurrent : kDotSize;
        lv_obj_set_size(dot, size, size);
        lv_obj_set_style_bg_opa(dot, current ? LV_OPA_COVER : LV_OPA_40, 0);
        // Dots sit centred as a row, spaced 15 px, between the two arrows.
        const int32_t dx = (2 * i - (count - 1)) * kDotSpacing / 2;
        lv_obj_align(dot, LV_ALIGN_CENTER, dx, kStepControlsDy);
        lv_obj_clear_flag(dot, LV_OBJ_FLAG_HIDDEN);
    }

    // An arrow that would do nothing is not drawn, so a child never taps a dead
    // control: back is live from card 2, forward only while a later card exists.
    const bool has_back = s_current_card > 0;
    const bool has_forward = s_current_card + 1 < count;
    if (s_step_back != nullptr) {
        has_back ? lv_obj_clear_flag(s_step_back, LV_OBJ_FLAG_HIDDEN)
                 : lv_obj_add_flag(s_step_back, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_step_forward != nullptr) {
        has_forward ? lv_obj_clear_flag(s_step_forward, LV_OBJ_FLAG_HIDDEN)
                    : lv_obj_add_flag(s_step_forward, LV_OBJ_FLAG_HIDDEN);
    }

    lv_obj_clear_flag(s_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_panel);
}

void ShowStepsLocked() {
    if (!s_initialized || s_display == nullptr) {
        return;
    }
    DisplayLockGuard ui_lock(s_display);
    EnsurePanelLocked();
    RenderStepsLocked();
}

bool HasBindPayloadLocked();

// A card pushed while a reminder or a bind code owned the glass waits behind
// it. When that board goes away the card takes the screen back, because the
// child is still working the step it holds.
bool RestoreQueuedStepsLocked() {
    if (s_cards.empty() || HasBindPayloadLocked()) {
        return false;
    }
    s_mode = BoardMode::kSteps;
    s_active_reminder_id = 0;
    s_open = true;
    ShowStepsLocked();
    return true;
}

void PlayNotificationSoundLocked() {
    Application::GetInstance().Schedule([]() {
        Application::GetInstance().PlaySound(Lang::Sounds::OGG_NOTIFICATION);
    });
}

std::string BuildBindBodyLocked() {
    if (s_bind_message.empty()) {
        return s_bind_code;
    }
    if (s_bind_code.empty()) {
        return s_bind_message;
    }
    if (s_bind_message.find(s_bind_code) != std::string::npos) {
        return s_bind_message;
    }
    return s_bind_message + "\n\n" + s_bind_code;
}

bool HasBindPayloadLocked() {
    return !s_bind_message.empty() || !s_bind_code.empty();
}

void SetPanelBodyLocked(const std::string& body) {
    if (!s_initialized || s_display == nullptr) {
        return;
    }
    DisplayLockGuard ui_lock(s_display);
    EnsurePanelLocked();
    if (s_panel == nullptr || s_body == nullptr) {
        return;
    }
    // A reminder or a bind code takes the glass back from a step card; the card
    // stack itself survives and is redrawn when that board closes.
    HideStepObjectsLocked();
    lv_obj_clear_flag(s_body, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(s_body, body.c_str());
    lv_obj_clear_flag(s_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_panel);
}

void HidePanelLocked() {
    if (!s_initialized || s_display == nullptr) {
        return;
    }
    DisplayLockGuard ui_lock(s_display);
    if (s_panel != nullptr) {
        lv_obj_add_flag(s_panel, LV_OBJ_FLAG_HIDDEN);
    }
}

void ReminderNotificationTimerCallback(void*) {
    std::lock_guard<std::mutex> lock(s_mutex);
    if (!s_repeat_notification || !s_open) {
        return;
    }
    PlayNotificationSoundLocked();
}

void EnsureNotificationTimerLocked() {
    if (s_notification_timer != nullptr) {
        return;
    }
    esp_timer_create_args_t args = {};
    args.callback = &ReminderNotificationTimerCallback;
    args.name = "msg_board_notif";
    esp_timer_create(&args, &s_notification_timer);
}

void StopReminderNotificationLocked() {
    s_repeat_notification = false;
    if (s_notification_timer != nullptr) {
        esp_timer_stop(s_notification_timer);
    }
}

void StartReminderNotificationLocked() {
    StopReminderNotificationLocked();
    s_repeat_notification = true;
    EnsureNotificationTimerLocked();
    PlayNotificationSoundLocked();
    if (s_notification_timer != nullptr) {
        esp_timer_start_periodic(s_notification_timer, kNotificationRepeatUs);
    }
}

void EnsureBindReopenTimerLocked();
void ReopenBindCodeFromTimer();

void BindReopenTimerCallback(void*) {
    bool schedule_reopen = false;
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        if (!s_bind_reopen_enabled || s_open || !HasBindPayloadLocked()) {
            return;
        }
        if (s_bind_reopen_scheduled) {
            return;
        }
        s_bind_reopen_scheduled = true;
        schedule_reopen = true;
    }

    if (schedule_reopen) {
        Application::GetInstance().Schedule([]() {
            ReopenBindCodeFromTimer();
        });
    }
}

void EnsureBindReopenTimerLocked() {
    if (s_bind_reopen_timer != nullptr) {
        return;
    }
    esp_timer_create_args_t args = {};
    args.callback = &BindReopenTimerCallback;
    args.name = "msg_board_bind";
    esp_timer_create(&args, &s_bind_reopen_timer);
}

void StopBindReopenLocked() {
    s_bind_reopen_enabled = false;
    s_bind_reopen_scheduled = false;
    if (s_bind_reopen_timer != nullptr) {
        esp_timer_stop(s_bind_reopen_timer);
    }
}

void StartBindReopenLocked() {
    if (!HasBindPayloadLocked()) {
        return;
    }
    StopBindReopenLocked();
    s_bind_reopen_enabled = true;
    EnsureBindReopenTimerLocked();
    if (s_bind_reopen_timer != nullptr) {
        esp_timer_start_periodic(s_bind_reopen_timer, kBindReopenUs);
    }
}

void ReopenBindCodeFromTimer() {
    std::lock_guard<std::mutex> lock(s_mutex);
    s_bind_reopen_scheduled = false;
    if (!s_bind_reopen_enabled || s_open || !HasBindPayloadLocked()) {
        return;
    }

    s_mode = BoardMode::kBindCode;
    s_active_reminder_id = 0;
    s_open = true;
    StopReminderNotificationLocked();
    SetPanelBodyLocked(BuildBindBodyLocked());
    PlayNotificationSoundLocked();
}
#endif

}  // namespace

void Begin(Display* display) {
    std::lock_guard<std::mutex> lock(s_mutex);
    if (s_initialized) {
        if (display != nullptr) {
            s_display = display;
        }
        return;
    }

    s_display = display;
    s_initialized = true;

#ifdef HAVE_LVGL
    if (s_display != nullptr) {
        DisplayLockGuard ui_lock(s_display);
        EnsurePanelLocked();
    }
#endif
}

void Open(const std::string& title, const std::string& body, int32_t reminder_id) {
    std::lock_guard<std::mutex> lock(s_mutex);
    if (!s_initialized || s_display == nullptr) {
        return;
    }

    s_active_reminder_id = std::max<int32_t>(0, reminder_id);
    s_mode = BoardMode::kGeneric;
    s_open = true;
    StopReminderNotificationLocked();

#ifdef HAVE_LVGL
    (void)title;
    SetPanelBodyLocked(body);
#else
    (void)title;
    (void)body;
#endif
}

void OpenReminder(int32_t reminder_id, const std::string& message, int hour, int minute) {
    char time_text[8];
    std::snprintf(time_text, sizeof(time_text), "%02d:%02d", hour, minute);
    std::string body = message;
    body += "\n";
    body += time_text;
    Open("REMINDER", body, reminder_id);
    std::lock_guard<std::mutex> lock(s_mutex);
    s_mode = BoardMode::kReminder;
#ifdef HAVE_LVGL
    if (s_open) {
        StartReminderNotificationLocked();
    }
#endif
}

void OpenBindCode(const std::string& message, const std::string& code) {
    std::lock_guard<std::mutex> lock(s_mutex);
    if (!s_initialized || s_display == nullptr) {
        return;
    }

    s_bind_message = message;
    s_bind_code = code;
    s_mode = BoardMode::kBindCode;
    s_active_reminder_id = 0;
    s_open = true;
    StopReminderNotificationLocked();
    StopBindReopenLocked();

#ifdef HAVE_LVGL
    SetPanelBodyLocked(BuildBindBodyLocked());
#else
    (void)message;
    (void)code;
#endif
}

void ClearBindCode() {
    std::lock_guard<std::mutex> lock(s_mutex);
    s_bind_message.clear();
    s_bind_code.clear();
    StopBindReopenLocked();

    if (!s_open || s_mode != BoardMode::kBindCode) {
        return;
    }

    s_open = false;
    s_active_reminder_id = 0;
    s_mode = BoardMode::kGeneric;
    StopReminderNotificationLocked();
#ifdef HAVE_LVGL
    HidePanelLocked();
    RestoreQueuedStepsLocked();
#endif
}

void Close() {
    std::lock_guard<std::mutex> lock(s_mutex);
    const bool has_bind_payload = HasBindPayloadLocked();
    const bool was_steps = s_mode == BoardMode::kSteps;
    s_open = false;
    s_active_reminder_id = 0;
    s_mode = BoardMode::kGeneric;
    StopReminderNotificationLocked();
    if (has_bind_payload) {
        StartBindReopenLocked();
    }
    if (!s_initialized || s_display == nullptr) {
        return;
    }

#ifdef HAVE_LVGL
    HidePanelLocked();
    if (!was_steps) {
        RestoreQueuedStepsLocked();
    }
#endif
}

bool IsOpen() {
    std::lock_guard<std::mutex> lock(s_mutex);
    return s_open;
}

bool HandleTap(uint16_t x, uint16_t y, int32_t* dismissed_reminder_id) {
    std::lock_guard<std::mutex> lock(s_mutex);
    if (dismissed_reminder_id != nullptr) {
        *dismissed_reminder_id = 0;
    }
    if (!s_open) {
        return false;
    }

#ifdef HAVE_LVGL
    {
        if (!s_initialized || s_display == nullptr || s_panel == nullptr) {
            return false;
        }
        DisplayLockGuard ui_lock(s_display);
        if (!IsInside(s_panel, x, y)) {
            return false;
        }
    }
#else
    (void)x;
    (void)y;
#endif

#ifdef HAVE_LVGL
    // Arrows are checked before the close, so a tap meant to change cards does
    // not take the card away instead (docs §4).
    if (s_mode == BoardMode::kSteps && !s_cards.empty()) {
        const int32_t dy = static_cast<int32_t>(y) - kArrowY;
        if (dy >= -kArrowHitRadius && dy <= kArrowHitRadius) {
            const int32_t back_dx = static_cast<int32_t>(x) - kArrowBackX;
            const int32_t forward_dx = static_cast<int32_t>(x) - kArrowForwardX;
            const bool hit_back = back_dx >= -kArrowHitRadius && back_dx <= kArrowHitRadius;
            const bool hit_forward =
                forward_dx >= -kArrowHitRadius && forward_dx <= kArrowHitRadius;
            if (hit_back && s_current_card > 0) {
                --s_current_card;
                ShowStepsLocked();
                return true;
            }
            if (hit_forward && s_current_card + 1 < static_cast<int>(s_cards.size())) {
                ++s_current_card;
                ShowStepsLocked();
                return true;
            }
            // A tap on a dead arrow closes the card like any other tap inside
            // the panel, which is what the child sees: nothing to go back to.
        }
    }
#endif

    if (dismissed_reminder_id != nullptr) {
        *dismissed_reminder_id = s_active_reminder_id;
    }
    s_open = false;
    const bool was_bind_mode = s_mode == BoardMode::kBindCode;
    const bool was_steps = s_mode == BoardMode::kSteps;
    if (was_bind_mode) {
        StartBindReopenLocked();
    } else {
        s_active_reminder_id = 0;
        s_mode = BoardMode::kGeneric;
        StopReminderNotificationLocked();
    }

#ifdef HAVE_LVGL
    HidePanelLocked();
    // Closing a reminder hands the glass back to the card the child was on.
    // Closing the card itself means the child wants the eyes: leave it closed.
    if (!was_steps && !was_bind_mode) {
        RestoreQueuedStepsLocked();
    }
#endif
    return true;
}

bool ShowStep(const std::string& label, const std::string& expr, const std::string& note,
              std::string* error) {
    const auto fail = [error](const std::string& message) {
        if (error != nullptr) {
            *error = message;
        }
        return false;
    };

    std::lock_guard<std::mutex> lock(s_mutex);
    if (!s_initialized || s_display == nullptr) {
        return fail("màn hình chưa sẵn sàng");
    }
    if (expr.empty()) {
        return fail("expr không được để trống");
    }
    if (static_cast<int>(s_cards.size()) >= kMaxStepCards) {
        return fail("đã đủ 4 thẻ cho bài này, gọi self.tutor.end trước khi sang bài mới");
    }

#ifdef HAVE_LVGL
    // Measured against the real fonts, then refused — never clipped. The model
    // gets the slot, the width it asked for and the limit, so it can rewrite.
    {
        DisplayLockGuard ui_lock(s_display);
        struct Slot {
            const char* name;
            const std::string& text;
            const lv_font_t* font;
            int32_t max_width;
        };
        const std::string label_text = label.empty()
                                           ? "bước " + std::to_string(s_cards.size() + 1)
                                           : label;
        const Slot slots[] = {
            {"label", label_text, &lv_font_montserrat_vn_20, kStepLabelMaxWidth},
            {"expr", expr, &lv_font_montserrat_vn_28, kStepExprMaxWidth},
            {"note", note, &lv_font_montserrat_vn_20, kStepNoteMaxWidth},
        };
        for (const Slot& slot : slots) {
            if (slot.text.empty()) {
                continue;
            }
            if (slot.text.find('\n') != std::string::npos) {
                return fail(std::string(slot.name) + " chỉ được một dòng");
            }
            std::string missing;
            if (!AllGlyphsPresent(slot.text, slot.font, &missing)) {
                return fail(std::string(slot.name) + " thiếu ký tự: " + missing);
            }
            const int32_t width = MeasureText(slot.text, slot.font);
            if (width > slot.max_width) {
                char message[96];
                std::snprintf(message, sizeof(message), "%s quá rộng: %dpx, tối đa %dpx",
                              slot.name, static_cast<int>(width),
                              static_cast<int>(slot.max_width));
                return fail(message);
            }
        }
    }
#endif

    s_cards.push_back(StepCard{label, expr, note});
    s_current_card = static_cast<int>(s_cards.size()) - 1;

    // A bind code must win the screen: the toy is unusable until it is entered.
    // The card waits and is drawn when that board closes.
    if (s_open && s_mode == BoardMode::kBindCode) {
        return true;
    }

    s_mode = BoardMode::kSteps;
    s_active_reminder_id = 0;
    s_open = true;
    StopReminderNotificationLocked();
#ifdef HAVE_LVGL
    ShowStepsLocked();
#endif
    return true;
}

void EndSteps() {
    std::lock_guard<std::mutex> lock(s_mutex);
    s_cards.clear();
    s_current_card = 0;
    if (s_mode != BoardMode::kSteps) {
        return;
    }
    s_open = false;
    s_mode = BoardMode::kGeneric;
    s_active_reminder_id = 0;
#ifdef HAVE_LVGL
    if (s_initialized && s_display != nullptr) {
        DisplayLockGuard ui_lock(s_display);
        HideStepObjectsLocked();
    }
    HidePanelLocked();
#endif
}

bool HandleSwipe(bool forward) {
    std::lock_guard<std::mutex> lock(s_mutex);
    if (!s_open || s_mode != BoardMode::kSteps || s_cards.empty()) {
        return false;
    }
    const int count = static_cast<int>(s_cards.size());
    const int next = forward ? s_current_card + 1 : s_current_card - 1;
    if (next < 0 || next >= count) {
        // Consumed anyway: a swipe on an open card is aimed at the card, and
        // letting it through would change the menu screen underneath instead.
        return true;
    }
    s_current_card = next;
#ifdef HAVE_LVGL
    ShowStepsLocked();
#endif
    return true;
}

}  // namespace MessageBoard
