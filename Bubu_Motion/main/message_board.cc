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
bool s_repeat_notification = false;
esp_timer_handle_t s_notification_timer = nullptr;
constexpr int64_t kNotificationRepeatUs = 2500000;

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

    s_panel = lv_obj_create(lv_screen_active());
    lv_obj_set_size(s_panel, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_center(s_panel);
    lv_obj_set_style_radius(s_panel, 18, 0);
    lv_obj_set_style_bg_color(s_panel, lv_color_hex(0x050812), 0);
    lv_obj_set_style_bg_opa(s_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_panel, 3, 0);
    lv_obj_set_style_border_color(s_panel, lv_color_hex(0x58F5C9), 0);
    lv_obj_set_style_border_opa(s_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(s_panel, 14, 0);
    lv_obj_clear_flag(s_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_panel, LV_OBJ_FLAG_HIDDEN);

    s_body = lv_label_create(s_panel);
    lv_obj_set_width(s_body, 170);
    lv_obj_set_style_text_font(s_body, &lv_font_montserrat_vn_20, 0);
    lv_obj_set_style_text_color(s_body, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_align(s_body, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_body, LV_LABEL_LONG_WRAP);
    lv_obj_center(s_body);
}

void PlayReminderNotificationSoundLocked() {
    Application::GetInstance().Schedule([]() {
        Application::GetInstance().PlaySound(Lang::Sounds::OGG_NOTIFICATION);
    });
}

void ReminderNotificationTimerCallback(void*) {
    std::lock_guard<std::mutex> lock(s_mutex);
    if (!s_repeat_notification || !s_open) {
        return;
    }
    PlayReminderNotificationSoundLocked();
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
    PlayReminderNotificationSoundLocked();
    if (s_notification_timer != nullptr) {
        esp_timer_start_periodic(s_notification_timer, kNotificationRepeatUs);
    }
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
    s_open = true;
    StopReminderNotificationLocked();

#ifdef HAVE_LVGL
    DisplayLockGuard ui_lock(s_display);
    EnsurePanelLocked();
    if (s_panel == nullptr) {
        return;
    }
    (void)title;
    lv_label_set_text(s_body, body.c_str());
    lv_obj_clear_flag(s_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_panel);
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
#ifdef HAVE_LVGL
    std::lock_guard<std::mutex> lock(s_mutex);
    if (s_open) {
        StartReminderNotificationLocked();
    }
#endif
}

void Close() {
    std::lock_guard<std::mutex> lock(s_mutex);
    s_open = false;
    s_active_reminder_id = 0;
    StopReminderNotificationLocked();
    if (!s_initialized || s_display == nullptr) {
        return;
    }

#ifdef HAVE_LVGL
    DisplayLockGuard ui_lock(s_display);
    if (s_panel != nullptr) {
        lv_obj_add_flag(s_panel, LV_OBJ_FLAG_HIDDEN);
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
    if (!s_initialized || s_display == nullptr || s_panel == nullptr) {
        return false;
    }
    DisplayLockGuard ui_lock(s_display);
    if (!IsInside(s_panel, x, y)) {
        return false;
    }
#else
    (void)x;
    (void)y;
#endif

    if (dismissed_reminder_id != nullptr) {
        *dismissed_reminder_id = s_active_reminder_id;
    }
    s_open = false;
    s_active_reminder_id = 0;
    StopReminderNotificationLocked();

#ifdef HAVE_LVGL
    lv_obj_add_flag(s_panel, LV_OBJ_FLAG_HIDDEN);
#endif
    return true;
}

}  // namespace MessageBoard
