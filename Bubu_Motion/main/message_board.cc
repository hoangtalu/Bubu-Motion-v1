#include "message_board.h"

#include "application.h"
#include "assets/lang_config.h"
#include "display/display.h"

#include <algorithm>
#include <cstdio>
#include <mutex>

#include <esp_timer.h>

#ifdef HAVE_LVGL
#include <lvgl.h>
extern const lv_font_t lv_font_montserrat_vn_20;
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

#ifdef HAVE_LVGL
lv_obj_t* s_panel = nullptr;
lv_obj_t* s_body = nullptr;

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
#endif
}

void Close() {
    std::lock_guard<std::mutex> lock(s_mutex);
    const bool has_bind_payload = HasBindPayloadLocked();
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

    if (dismissed_reminder_id != nullptr) {
        *dismissed_reminder_id = s_active_reminder_id;
    }
    s_open = false;
    const bool was_bind_mode = s_mode == BoardMode::kBindCode;
    if (was_bind_mode) {
        StartBindReopenLocked();
    } else {
        s_active_reminder_id = 0;
        s_mode = BoardMode::kGeneric;
        StopReminderNotificationLocked();
    }

#ifdef HAVE_LVGL
    HidePanelLocked();
#endif
    return true;
}

}  // namespace MessageBoard
