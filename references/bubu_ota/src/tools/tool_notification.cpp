#include "tool_notification.h"
#include "sound/sound_system.h"
#include "logger.h"

DEFINE_MODULE_LOGGER(ToolNotifLog)

namespace ToolNotification {

// Pre-created UI objects (hidden by default)
static lv_obj_t* sReminderContainer = nullptr;
static lv_obj_t* sReminderTitle = nullptr;
static lv_obj_t* sReminderContent = nullptr;

static lv_obj_t* sNoteContainer = nullptr;
static lv_obj_t* sNoteTitle = nullptr;
static lv_obj_t* sNoteContent = nullptr;

static lv_timer_t* sAutoDismissTimer = nullptr;

// Auto-dismiss callback
static void autoDismissCallback(lv_timer_t* timer) {
  dismiss();
}

void begin() {
  // Pre-create reminder notification (hidden)
  sReminderContainer = lv_obj_create(lv_scr_act());
  lv_obj_set_size(sReminderContainer, 200, 100);
  lv_obj_align(sReminderContainer, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_bg_color(sReminderContainer, lv_color_hex(0x333333), 0);
  lv_obj_set_style_border_color(sReminderContainer, lv_color_hex(0xFFAA00), 0);
  lv_obj_set_style_border_width(sReminderContainer, 2, 0);
  lv_obj_set_style_radius(sReminderContainer, 10, 0);
  lv_obj_add_flag(sReminderContainer, LV_OBJ_FLAG_HIDDEN);  // Hidden by default

  sReminderTitle = lv_label_create(sReminderContainer);
  lv_label_set_text(sReminderTitle, "🔔 Reminder");
  lv_obj_set_style_text_color(sReminderTitle, lv_color_hex(0xFFAA00), 0);
  lv_obj_align(sReminderTitle, LV_ALIGN_TOP_MID, 0, 10);

  sReminderContent = lv_label_create(sReminderContainer);
  lv_label_set_long_mode(sReminderContent, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(sReminderContent, 180);
  lv_obj_align(sReminderContent, LV_ALIGN_CENTER, 0, 10);
  lv_obj_set_style_text_align(sReminderContent, LV_TEXT_ALIGN_CENTER, 0);

  // Pre-create note notification (hidden)
  sNoteContainer = lv_obj_create(lv_scr_act());
  lv_obj_set_size(sNoteContainer, 220, 180);
  lv_obj_align(sNoteContainer, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_bg_color(sNoteContainer, lv_color_hex(0x222222), 0);
  lv_obj_set_style_border_color(sNoteContainer, lv_color_hex(0x00AAFF), 0);
  lv_obj_set_style_border_width(sNoteContainer, 2, 0);
  lv_obj_set_style_radius(sNoteContainer, 10, 0);
  lv_obj_add_flag(sNoteContainer, LV_OBJ_FLAG_HIDDEN);  // Hidden by default

  sNoteTitle = lv_label_create(sNoteContainer);
  lv_obj_set_style_text_color(sNoteTitle, lv_color_hex(0x00AAFF), 0);
  lv_obj_align(sNoteTitle, LV_ALIGN_TOP_MID, 0, 10);

  sNoteContent = lv_label_create(sNoteContainer);
  lv_label_set_long_mode(sNoteContent, LV_LABEL_LONG_SCROLL_CIRCULAR);
  lv_obj_set_width(sNoteContent, 200);
  lv_obj_set_pos(sNoteContent, 10, 40);
  lv_obj_set_style_text_align(sNoteContent, LV_TEXT_ALIGN_LEFT, 0);

  ToolNotifLog::println("[Notif] UI pre-created and ready");
}

void showReminder(const char* title, uint32_t reminderId) {
  ToolNotifLog::printf("[Notif] Reminder: %s (ID %u)\n", title, reminderId);

  // Play reminder sound
  SoundSystem::reminderChime();

  // Hide note if visible
  if (sNoteContainer) {
    lv_obj_add_flag(sNoteContainer, LV_OBJ_FLAG_HIDDEN);
  }

  // Update reminder content and show
  if (sReminderContainer && sReminderContent) {
    lv_label_set_text(sReminderContent, title);
    lv_obj_clear_flag(sReminderContainer, LV_OBJ_FLAG_HIDDEN);

    // Cancel any existing timer
    if (sAutoDismissTimer) {
      lv_timer_del(sAutoDismissTimer);
    }

    // Auto-dismiss after 5 seconds
    sAutoDismissTimer = lv_timer_create(autoDismissCallback, 5000, nullptr);
  }
}

void showNote(const char* title, const char* content) {
  ToolNotifLog::printf("[Notif] Note: %s\n", title);

  // Hide reminder if visible
  if (sReminderContainer) {
    lv_obj_add_flag(sReminderContainer, LV_OBJ_FLAG_HIDDEN);
  }

  // Update note content and show
  if (sNoteContainer && sNoteTitle && sNoteContent) {
    lv_label_set_text_fmt(sNoteTitle, "📝 %s", title);
    lv_label_set_text(sNoteContent, content);
    lv_obj_clear_flag(sNoteContainer, LV_OBJ_FLAG_HIDDEN);

    // Cancel any existing timer
    if (sAutoDismissTimer) {
      lv_timer_del(sAutoDismissTimer);
    }

    // Auto-dismiss after 10 seconds (longer for notes)
    sAutoDismissTimer = lv_timer_create(autoDismissCallback, 10000, nullptr);
  }
}

void dismiss() {
  // Hide both containers
  if (sReminderContainer) {
    lv_obj_add_flag(sReminderContainer, LV_OBJ_FLAG_HIDDEN);
  }
  if (sNoteContainer) {
    lv_obj_add_flag(sNoteContainer, LV_OBJ_FLAG_HIDDEN);
  }

  // Cancel auto-dismiss timer
  if (sAutoDismissTimer) {
    lv_timer_del(sAutoDismissTimer);
    sAutoDismissTimer = nullptr;
  }
}

}  // namespace ToolNotification
