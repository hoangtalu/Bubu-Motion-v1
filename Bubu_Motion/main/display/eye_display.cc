#include "eye_display.h"

#include <font_awesome.h>

#include <cstring>
#include <string>
#include "application.h"
#include "board.h"
#include "care_system.h"
#include "lvgl_display/lvgl_theme.h"
#include "menu_system.h"
#include "screen_manager.h"
#include "message_board.h"
#include "settings.h"
#include "assets/lang_config.h"
#include <algorithm>
#include <cmath>
#include <array>
#include <ctime>
#include <esp_log.h>
#include <esp_random.h>

#define TAG "EyeDisplay"

extern const lv_font_t lv_font_montserrat_vn_20;
extern const lv_font_t lv_font_montserrat_vn_28;
#if defined(CONFIG_LV_FONT_MONTSERRAT_48) && CONFIG_LV_FONT_MONTSERRAT_48
extern const lv_font_t lv_font_montserrat_48;
#endif

namespace {

struct CareEmotionWeight {
  const char *emotion;
  float base_weight;
  float positive_bias;
  float negative_bias;
  float hunger_need_bias;
  float mood_need_bias;
  float energy_need_bias;
  float clean_need_bias;
};

// Tune this table to control how often each emotion appears.
constexpr std::array<CareEmotionWeight, 25> kCareEmotionTable = {{
    {"neutral", 1.40f, 0.40f, 0.10f, 0.00f, 0.00f, 0.00f, 0.00f},
    {"relaxed", 0.80f, 1.10f, 0.00f, 0.00f, 0.10f, 0.40f, 0.20f},
    {"happy", 1.00f, 2.30f, 0.00f, 0.00f, 0.80f, 0.40f, 0.20f},
    {"laughing", 0.50f, 1.70f, 0.00f, 0.00f, 1.00f, 0.20f, 0.10f},
    {"surprised", 0.35f, 1.00f, 0.20f, 0.20f, 0.70f, 0.30f, 0.20f},
    {"skeptic", 0.25f, 0.20f, 0.90f, 0.20f, 1.20f, 0.50f, 0.40f},
    {"skeptical", 0.25f, 0.20f, 0.90f, 0.20f, 1.20f, 0.50f, 0.40f},
    {"doubt", 0.20f, 0.10f, 1.05f, 0.30f, 1.40f, 0.50f, 0.45f},
    {"worried", 0.35f, 0.00f, 1.30f, 0.45f, 1.45f, 0.70f, 0.65f},
    {"sad", 0.50f, 0.00f, 1.60f, 0.60f, 1.60f, 0.60f, 0.80f},
    {"annoyed", 0.22f, 0.00f, 1.45f, 1.10f, 1.05f, 0.25f, 0.35f},
    {"angry", 0.30f, 0.00f, 1.80f, 1.70f, 1.30f, 0.20f, 0.40f},
    {"sleepy", 0.30f, 0.00f, 2.10f, 0.20f, 0.30f, 2.20f, 0.20f},
    {"embarrassed", 0.20f, 0.00f, 1.20f, 0.20f, 0.70f, 0.40f, 1.90f},
    {"nervous", 0.20f, 0.00f, 1.00f, 0.30f, 1.20f, 1.10f, 0.80f},
    {"anxious", 0.20f, 0.00f, 1.00f, 0.50f, 0.80f, 1.50f, 0.80f},
    {"legacy_emo_love", 0.12f, 0.35f, 0.00f, 0.00f, 0.15f, 0.00f, 0.00f},
    {"legacy_emo_cyclop", 0.10f, 0.00f, 0.25f, 0.00f, 0.10f, 0.00f, 0.00f},
    {"legacy_emo_drunk", 0.10f, 0.00f, 0.35f, 0.10f, 0.20f, 0.10f, 0.10f},
    {"legacy_emo_confuse", 0.10f, 0.10f, 0.50f, 0.20f, 0.80f, 0.40f, 0.30f},
    {"legacy_emo_angry", 0.10f, 0.00f, 0.80f, 0.80f, 0.60f, 0.20f, 0.30f},
    {"legacy_emo_furious", 0.07f, 0.00f, 0.95f, 1.10f, 0.80f, 0.20f, 0.20f},
    {"legacy_emo_banh_chung", 0.08f, 0.80f, 0.00f, 0.00f, 0.40f, 0.20f, 0.60f},
    {"legacy_emo_deadpool", 0.08f, 0.00f, 0.65f, 0.30f, 0.50f, 0.20f, 0.20f},
    {"legacy_emo_cry", 0.09f, 0.00f, 0.95f, 0.40f, 1.20f, 0.50f, 0.60f},
}};

uint32_t RandomRangeU32(uint32_t lo, uint32_t hi) {
  if (lo >= hi) {
    return lo;
  }
  return lo + (esp_random() % (hi - lo + 1));
}

bool IsDisplaySleepAllowedState(DeviceState state) {
  return state == kDeviceStateStarting || state == kDeviceStateIdle;
}

float ToRatio(int stat_0_100) {
  int clamped = std::max(0, std::min(100, stat_0_100));
  return static_cast<float>(clamped) / 100.0f;
}

bool IsLegacyEmotionName(const std::string& emotion) {
  return emotion.rfind("legacy_emo_", 0) == 0;
}

} // namespace

EyeDisplay::EyeDisplay(esp_lcd_panel_io_handle_t io_handle,
                       esp_lcd_panel_handle_t panel_handle, int width,
                       int height, int offset_x, int offset_y, bool mirror_x,
                       bool mirror_y, bool swap_xy)
    : SpiLcdDisplay(io_handle, panel_handle, width, height, offset_x, offset_y,
                    mirror_x, mirror_y, swap_xy),
      subtitle_(&EyeDisplay::MeasureSubtitleText) {}

EyeDisplay::~EyeDisplay() {
  if (screen_listener_id_ >= 0) {
    ScreenManager::RemoveListener(screen_listener_id_);
    screen_listener_id_ = -1;
  }
  if (status_chrome_timer_ != nullptr) {
    lv_timer_delete(status_chrome_timer_);
    status_chrome_timer_ = nullptr;
  }
  if (status_anim_timer_ != nullptr) {
    lv_timer_delete(status_anim_timer_);
    status_anim_timer_ = nullptr;
  }
  if (subtitle_timer_ != nullptr) {
    lv_timer_delete(subtitle_timer_);
    subtitle_timer_ = nullptr;
  }
  eye_animation_.reset();
}

uint64_t EyeDisplay::GetNowMs() { return esp_timer_get_time() / 1000; }

void EyeDisplay::SetupUI() {
  // Force dark theme before base SetupUI so all elements are black
  auto *dark_theme = LvglThemeManager::GetInstance().GetTheme("dark");
  if (dark_theme) {
    current_theme_ = dark_theme;
  }

  // Call base SetupUI to create all standard UI elements
  SpiLcdDisplay::SetupUI();

  DisplayLockGuard lock(this);

  // Force the entire screen and all UI elements to black background
  lv_obj_t *screen = lv_screen_active();
  lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

  if (container_) {
    lv_obj_set_style_bg_color(container_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(container_, LV_OPA_COVER, 0);
  }
  if (top_bar_) {
    lv_obj_set_style_bg_color(top_bar_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(top_bar_, LV_OPA_TRANSP, 0);
  }
  if (status_bar_) {
    lv_obj_set_style_bg_color(status_bar_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(status_bar_, LV_OPA_TRANSP, 0);
  }
  if (bottom_bar_) {
    lv_obj_set_style_bg_color(bottom_bar_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(bottom_bar_, LV_OPA_TRANSP, 0);
  }
  if (content_) {
    lv_obj_set_style_bg_color(content_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(content_, LV_OPA_COVER, 0);
  }

  // Round-screen geometry: at y=14 (where the stock bars sit) the visible chord
  // is only ~112px wide, so left/right-aligned icons and long status text are
  // hidden behind the bezel. Drop the status row to y=34, where ~189px shows.
  lv_obj_set_style_pad_left(status_bar_, 8, 0);
  lv_obj_set_style_pad_right(status_bar_, 8, 0);
  lv_obj_align(status_bar_, LV_ALIGN_TOP_MID, 0, kStatusBarTopY);

  // Hide the standard emoji box - we'll use eye animation instead
  if (emoji_box_ != nullptr) {
    lv_obj_add_flag(emoji_box_, LV_OBJ_FLAG_HIDDEN);
  }

  // Create eye animation on the active screen
  eye_animation_ = std::make_unique<EyeAnimation>();
  eye_animation_->Init(screen, width_, height_);
  eye_animation_->SetInteractionEventCallback(
      [this](EyeAnimation::InteractionEvent event) {
        switch (event) {
        case EyeAnimation::InteractionEvent::Mischief:
          Application::GetInstance().Schedule([this]() {
            MaybePlayInteractionVoice(BubuInteractionEvent::Mischief);
          });
          break;
        case EyeAnimation::InteractionEvent::Blink:
          Application::GetInstance().Schedule([this]() {
            MaybePlayInteractionVoice(BubuInteractionEvent::Blink);
          });
          break;
        }
      });
  Settings hatch_settings("bubu", true);
  if (hatch_settings.GetBool("hatched", false) != true) {
    hatch_settings.SetBool("hatched", true);
  }
  hatch_marked_done_ = true;
  hatch_was_active_ = false;

  Settings display_settings("display", false);
  SetBaseLeftEyeShape({
      display_settings.GetInt("eye_l_w", 80),
      display_settings.GetInt("eye_l_h", 80),
      display_settings.GetInt("eye_l_r", 24),
  });
  SetBaseRightEyeShape({
      display_settings.GetInt("eye_r_w", 80),
      display_settings.GetInt("eye_r_h", 80),
      display_settings.GetInt("eye_r_r", 24),
  });
  CreateClockScreensaver(screen);
  last_user_interaction_ms_ = GetNowMs();

  // The active screen decides what the renderer is allowed to do.
  screen_listener_id_ = ScreenManager::AddListener(
      [this](ScreenManager::ScreenId, ScreenManager::ScreenId to) {
        ApplyScreenPolicy(to);
      });
  RefreshScreen();

  // The standard LCD top bar is created inside container_, which sits behind
  // the full-screen eye canvas on this board. Re-parent it onto the screen and
  // keep both bars in the foreground so boot/status text remains visible.
  // The stock top bar put its glyphs at x~8 and x~230, both outside the circle.
  // Rather than re-centre them into the arc's space, retire it: every status
  // glyph now shares one row at the bottom.
  if (top_bar_) {
    lv_obj_add_flag(top_bar_, LV_OBJ_FLAG_HIDDEN);
  }
  if (status_bar_) {
    lv_obj_move_foreground(status_bar_);
  }
  bottom_icons_ = lv_obj_create(screen);
  lv_obj_remove_style_all(bottom_icons_);
  lv_obj_set_size(bottom_icons_, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(bottom_icons_, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(bottom_icons_, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(bottom_icons_, 6, 0);
  lv_obj_align(bottom_icons_, LV_ALIGN_BOTTOM_MID, 0, -6);
  lv_obj_remove_flag(bottom_icons_, LV_OBJ_FLAG_SCROLLABLE);
  if (mute_label_) {
    lv_obj_set_parent(mute_label_, bottom_icons_);
  }
  if (network_label_) {
    lv_obj_set_parent(network_label_, bottom_icons_);
  }
  if (battery_label_) {
    lv_obj_set_parent(battery_label_, bottom_icons_);
    lv_obj_set_style_margin_left(battery_label_, 0, 0);
    lv_obj_remove_flag(battery_label_, LV_OBJ_FLAG_HIDDEN);
  }
  lv_obj_move_foreground(bottom_icons_);

  SetupStatusChrome();
  // Chat subtitles (ChatSubtitle + this widget/timer) disabled for the 1.7.6
  // release: the whole feature has never been flashed to real hardware
  // (created + revised same day, 2026-09-17, host-tested only). Leaving the
  // call out means subtitle_label_/subtitle_timer_ stay null and
  // SetChatMessage/SubtitleTick become no-ops for anything screen-visible.
  // Re-enable by restoring this call once verified on a bench device.
  // SetupSubtitle();

  // Initialize menu system (overlays on top of eyes)
  MenuSystem::Begin(this);
  MessageBoard::Begin(this);

  ESP_LOGI(
      TAG,
      "EyeDisplay UI setup complete (black background + animated eyes + menu)");
}

void EyeDisplay::CreateClockScreensaver(lv_obj_t *parent) {
  clock_screensaver_ = lv_obj_create(parent);
  lv_obj_set_size(clock_screensaver_, LV_HOR_RES, LV_VER_RES);
  lv_obj_center(clock_screensaver_);
  lv_obj_set_style_bg_color(clock_screensaver_, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(clock_screensaver_, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(clock_screensaver_, 0, 0);
  lv_obj_set_style_radius(clock_screensaver_, 0, 0);
  lv_obj_set_style_pad_all(clock_screensaver_, 0, 0);
  lv_obj_clear_flag(clock_screensaver_, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(clock_screensaver_, LV_OBJ_FLAG_HIDDEN);

  clock_time_label_ = lv_label_create(clock_screensaver_);
  lv_obj_set_width(clock_time_label_, LV_HOR_RES);
  lv_obj_set_style_text_color(clock_time_label_, lv_color_white(), 0);
#if defined(CONFIG_LV_FONT_MONTSERRAT_48) && CONFIG_LV_FONT_MONTSERRAT_48
  lv_obj_set_style_text_font(clock_time_label_, &lv_font_montserrat_48, 0);
#else
  lv_obj_set_style_text_font(clock_time_label_, &lv_font_montserrat_vn_28, 0);
#endif
  lv_obj_set_style_text_align(clock_time_label_, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_text(clock_time_label_, "--:--");
  lv_obj_align(clock_time_label_, LV_ALIGN_CENTER, 0, -18);

  clock_date_label_ = lv_label_create(clock_screensaver_);
  lv_obj_set_width(clock_date_label_, LV_HOR_RES);
  lv_obj_set_style_text_color(clock_date_label_, lv_color_white(), 0);
  lv_obj_set_style_text_font(clock_date_label_, &lv_font_montserrat_vn_20, 0);
  lv_obj_set_style_text_align(clock_date_label_, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_text(clock_date_label_, "--/--/--");
  lv_obj_align_to(clock_date_label_, clock_time_label_, LV_ALIGN_OUT_BOTTOM_MID,
                  0, 10);
}

void EyeDisplay::SetTheme(Theme *theme) {
  // Let the base class apply fonts, text colors, etc.
  LcdDisplay::SetTheme(theme);

  // Then force black background on everything
  DisplayLockGuard lock(this);
  lv_obj_t *screen = lv_screen_active();
  lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

  if (container_) {
    lv_obj_set_style_bg_color(container_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(container_, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_image_src(container_, nullptr, 0);
  }
  if (top_bar_) {
    lv_obj_set_style_bg_color(top_bar_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(top_bar_, LV_OPA_TRANSP, 0);
  }
  if (status_bar_) {
    lv_obj_set_style_bg_color(status_bar_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(status_bar_, LV_OPA_TRANSP, 0);
  }
  if (content_) {
    lv_obj_set_style_bg_color(content_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(content_, LV_OPA_COVER, 0);
  }
  if (bottom_bar_) {
    lv_obj_set_style_bg_color(bottom_bar_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(bottom_bar_, LV_OPA_TRANSP, 0);
  }

  // Force white text for visibility on black background
  lv_obj_set_style_text_color(screen, lv_color_white(), 0);
  if (network_label_)
    lv_obj_set_style_text_color(network_label_, lv_color_white(), 0);
  if (status_label_)
    lv_obj_set_style_text_color(status_label_, lv_color_white(), 0);
  if (notification_label_)
    lv_obj_set_style_text_color(notification_label_, lv_color_white(), 0);
  if (mute_label_)
    lv_obj_set_style_text_color(mute_label_, lv_color_white(), 0);
  if (battery_label_)
    lv_obj_set_style_text_color(battery_label_, lv_color_white(), 0);
  if (emoji_label_)
    lv_obj_set_style_text_color(emoji_label_, lv_color_white(), 0);
  if (chat_message_label_)
    lv_obj_set_style_text_color(chat_message_label_, lv_color_white(), 0);
  if (battery_label_) {
    lv_obj_align(battery_label_, LV_ALIGN_BOTTOM_MID, 0, -6);
    lv_obj_move_foreground(battery_label_);
    lv_obj_clear_flag(battery_label_, LV_OBJ_FLAG_HIDDEN);
  }

  ESP_LOGI(TAG, "SetTheme: forced black background");
}

void EyeDisplay::NotifyUserInteraction() {
  last_user_interaction_ms_ = GetNowMs();
  HideClockScreensaver();
  if (sleep_mode_active_) {
    StopSleepMode();
  }
}

bool EyeDisplay::DismissClockScreensaver() {
  last_user_interaction_ms_ = GetNowMs();
  if (!clock_screensaver_active_) {
    return false;
  }
  HideClockScreensaver();
  if (sleep_mode_active_) {
    StopSleepMode();
  }
  return true;
}

bool EyeDisplay::IsClockScreensaverActive() const {
  return clock_screensaver_active_;
}

bool EyeDisplay::StartSleepMode() {
  if (!eye_animation_) {
    return false;
  }
  if (IsHatchingActive()) {
    return false;
  }
  if (sleep_mode_active_) {
    return true;
  }
  if (!IsDisplaySleepAllowedState(Application::GetInstance().GetDeviceState())) {
    return false;
  }

  HideClockScreensaver();
  sleep_mode_active_ = true;
  sleep_last_energy_tick_ms_ = GetNowMs();
  sleep_resume_emotion_ = current_eye_emotion_;
  eye_animation_->SetSleepMode(true);

  if (auto *backlight = Board::GetInstance().GetBacklight()) {
    sleep_restore_brightness_ = backlight->brightness();
    if (sleep_restore_brightness_ == 0) {
      sleep_restore_brightness_ = 75;
    }
    backlight->SetBrightness(kSleepBrightnessPct);
  }

  UpdateMischiefEngineState();
  return true;
}

void EyeDisplay::StopSleepMode() {
  if (!sleep_mode_active_) {
    return;
  }

  // The 15-minute clock is the second stage of the same rest session.
  // Leaving rest must dismiss it as well as restore the eyes and brightness.
  HideClockScreensaver();
  sleep_mode_active_ = false;
  sleep_last_energy_tick_ms_ = 0;
  if (eye_animation_) {
    eye_animation_->SetSleepMode(false);
  }

  if (auto *backlight = Board::GetInstance().GetBacklight()) {
    backlight->SetBrightness(sleep_restore_brightness_);
  }

  ApplyEmotionInternal(current_eye_emotion_.c_str(), false);
  last_user_interaction_ms_ = GetNowMs();
  UpdateMischiefEngineState();
}

namespace {

// States where Bubu is waiting on something -- these get the animated dots.
bool IsWaitingStatus(const char *status) {
  if (status == nullptr) {
    return false;
  }
  static const char *const kWaiting[] = {
      Lang::Strings::INITIALIZING,       Lang::Strings::CONNECTING,
      Lang::Strings::PLEASE_WAIT,        Lang::Strings::CHECKING_NEW_VERSION,
      Lang::Strings::LOADING_PROTOCOL,   Lang::Strings::REGISTERING_NETWORK,
      Lang::Strings::DETECTING_MODULE,   Lang::Strings::UPGRADING,
      Lang::Strings::ACTIVATION,
  };
  for (const char *candidate : kWaiting) {
    if (candidate != nullptr && std::strcmp(status, candidate) == 0) {
      return true;
    }
  }
  return false;
}

// States the arc and voice waves already convey, so no words are drawn. What
// stays as text is what a parent must read: OTA, asset download, activation,
// errors and alerts.
bool IsSilentStatus(const char *status) {
  static const char *const kSilent[] = {
      Lang::Strings::STANDBY,           Lang::Strings::LISTENING,
      Lang::Strings::SPEAKING,          Lang::Strings::CONNECTING,
      Lang::Strings::INITIALIZING,      Lang::Strings::REGISTERING_NETWORK,
      Lang::Strings::DETECTING_MODULE,  Lang::Strings::LOADING_PROTOCOL,
      Lang::Strings::CHECKING_NEW_VERSION,
  };
  for (const char *candidate : kSilent) {
    if (candidate != nullptr && std::strcmp(status, candidate) == 0) {
      return true;
    }
  }
  return false;
}

}  // namespace

void EyeDisplay::SetStatus(const char *status) {
  NotifyUserInteraction();
  const char *safe = (status != nullptr) ? status : "";
  // Idle, listening, speaking and plain loading need no words: the arc and the
  // voice waves say it. Rendering them as empty is deterministic, unlike
  // collapsing them on a timer.
  const bool silent = IsSilentStatus(safe);
  status_base_text_ = silent ? "" : safe;
  status_busy_ = !silent && IsWaitingStatus(safe);
  status_ellipsis_phase_ = 0;
  SpiLcdDisplay::SetStatus(safe);
  RenderStatusText();
  UpdateStatusArcColor();
}

void EyeDisplay::ShowNotification(const char *notification, int duration_ms) {
  NotifyUserInteraction();
  SpiLcdDisplay::ShowNotification(notification, duration_ms);
}

void EyeDisplay::ShowNotification(const std::string &notification,
                                  int duration_ms) {
  ShowNotification(notification.c_str(), duration_ms);
}

void EyeDisplay::SetChatMessage(const char *role, const char *content) {
  NotifyUserInteraction();
  // Not forwarded to SpiLcdDisplay::SetChatMessage(): its chat_message_label_/
  // bottom_bar_ sit behind EyeAnimation's full-screen canvas and are never
  // visible on this board. Subtitles are drawn by our own label instead (see
  // SetupSubtitle), paced to the speaker by ChatSubtitle.
  if (role == nullptr || content == nullptr) {
    return;
  }
  const uint64_t now_ms = GetNowMs();
  const uint64_t voice_ms =
      Application::GetInstance().GetAudioService().GetVoicePlayedMs();
  DisplayLockGuard lock(this);
  if (std::strcmp(role, "assistant") == 0) {
    subtitle_.AppendAssistant(content, voice_ms, now_ms);
  } else if (std::strcmp(role, "user") == 0) {
    subtitle_.AppendUser(content, now_ms);
  } else if (content[0] == '\0') {
    // "system" with empty content is how Application clears the chat line
    // (channel closed, connecting, power save). Non-empty system text (boot
    // user agent, OTA progress) goes through SetStatus/MessageBoard here, as
    // it always has.
    subtitle_.Clear();
  }
}

void EyeDisplay::ClearChatMessages() {
  NotifyUserInteraction();
  SpiLcdDisplay::ClearChatMessages();
  DisplayLockGuard lock(this);
  subtitle_.Clear();
}

int EyeDisplay::MeasureSubtitleText(const std::string &text) {
  lv_point_t size;
  lv_text_get_size(&size, text.c_str(), &lv_font_montserrat_vn_20, 0, 0,
                   LV_COORD_MAX, LV_TEXT_FLAG_NONE);
  return size.x;
}

void EyeDisplay::SetupSubtitle() {
  subtitle_label_ = lv_label_create(lv_screen_active());
  lv_obj_set_style_text_font(subtitle_label_, &lv_font_montserrat_vn_20, 0);
  lv_obj_set_style_text_color(subtitle_label_, lv_color_hex(0xE8EEF8), 0);
  lv_obj_set_style_text_align(subtitle_label_, LV_TEXT_ALIGN_CENTER, 0);
  // Dark pill so the words stay legible over whatever the eyes are doing.
  lv_obj_set_style_bg_color(subtitle_label_, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(subtitle_label_, LV_OPA_70, 0);
  lv_obj_set_style_radius(subtitle_label_, 14, 0);
  lv_obj_set_style_pad_hor(subtitle_label_, kSubtitlePadX, 0);
  lv_obj_set_style_pad_ver(subtitle_label_, kSubtitlePadY, 0);
  lv_label_set_long_mode(subtitle_label_, LV_LABEL_LONG_CLIP);
  lv_obj_set_size(subtitle_label_, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_remove_flag(subtitle_label_, LV_OBJ_FLAG_CLICKABLE);
  lv_label_set_text(subtitle_label_, "");
  lv_obj_align(subtitle_label_, LV_ALIGN_BOTTOM_MID, 0,
               kSubtitleBottomY - static_cast<int>(LV_VER_RES));
  lv_obj_add_flag(subtitle_label_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(subtitle_label_);

  subtitle_timer_ = lv_timer_create(SubtitleTimerCb, kSubtitleTickMs, this);
}

void EyeDisplay::SubtitleTimerCb(lv_timer_t *timer) {
  auto *self = static_cast<EyeDisplay *>(lv_timer_get_user_data(timer));
  if (self != nullptr) {
    self->SubtitleTick();
  }
}

// Runs inside lv_timer_handler, which already holds the display lock; taking
// DisplayLockGuard here would deadlock.
void EyeDisplay::SubtitleTick() {
  if (subtitle_label_ == nullptr) {
    return;
  }
  // Ticked even while hidden, so pacing keeps up with the voice when a menu
  // closes mid-reply.
  const uint64_t sub_voice_ms =
      Application::GetInstance().GetAudioService().GetVoicePlayedMs();
  const uint64_t sub_now_ms = GetNowMs();
  const std::string &text = subtitle_.Tick(sub_voice_ms, sub_now_ms);
  {
    // TEMP [SUB] timing capture -- remove after calibration.
    static uint64_t dbg_last_v = 0, dbg_last_adv = 0;
    static bool dbg_playing = false;
    static std::string dbg_last_text;
    if (sub_voice_ms != dbg_last_v) {
      if (!dbg_playing) {
        ESP_LOGI(TAG, "[SUB] voice start t=%u v=%u", (unsigned)sub_now_ms, (unsigned)dbg_last_v);
        dbg_playing = true;
      }
      dbg_last_v = sub_voice_ms;
      dbg_last_adv = sub_now_ms;
    } else if (dbg_playing && sub_now_ms - dbg_last_adv >= 300) {
      ESP_LOGI(TAG, "[SUB] voice stop t=%u v=%u", (unsigned)dbg_last_adv, (unsigned)dbg_last_v);
      dbg_playing = false;
    }
    if (text != dbg_last_text) {
      ESP_LOGI(TAG, "[SUB] show t=%u v=%u \"%s\"", (unsigned)sub_now_ms, (unsigned)sub_voice_ms, text.c_str());
      dbg_last_text = text;
    }
  }

  const bool visible = !text.empty() && !hide_subtitle_ &&
                       ScreenManager::Current() == ScreenManager::ScreenId::Main &&
                       !MessageBoard::IsOpen();
  if (!visible) {
    if (!lv_obj_has_flag(subtitle_label_, LV_OBJ_FLAG_HIDDEN)) {
      lv_obj_add_flag(subtitle_label_, LV_OBJ_FLAG_HIDDEN);
    }
    return;
  }
  if (text != subtitle_shown_) {
    subtitle_shown_ = text;
    lv_label_set_text(subtitle_label_, subtitle_shown_.c_str());
    // Content-sized, so re-anchor after the width changes.
    lv_obj_align(subtitle_label_, LV_ALIGN_BOTTOM_MID, 0,
                 kSubtitleBottomY - static_cast<int>(LV_VER_RES));
  }
  if (lv_obj_has_flag(subtitle_label_, LV_OBJ_FLAG_HIDDEN)) {
    lv_obj_remove_flag(subtitle_label_, LV_OBJ_FLAG_HIDDEN);
  }
}

void EyeDisplay::UpdateStatusBar(bool update_all) {
  uint64_t now_ms = GetNowMs();
  SpiLcdDisplay::UpdateStatusBar(update_all);

  if (bottom_icons_ != nullptr) {
    DisplayLockGuard lock(this);
    if (MenuSystem::IsAnyOpen() || !ScreenManager::Policy().status_chrome) {
      lv_obj_add_flag(bottom_icons_, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_remove_flag(bottom_icons_, LV_OBJ_FLAG_HIDDEN);
    }
  }

  // The WiFi glyph is only worth screen space when something is wrong with the
  // link; a healthy connection says nothing the user needs.
  if (network_label_ != nullptr) {
    DisplayLockGuard lock(this);
    const char *icon = lv_label_get_text(network_label_);
    const bool problem =
        icon != nullptr &&
        (std::strcmp(icon, FONT_AWESOME_WIFI_SLASH) == 0 ||
         std::strcmp(icon, FONT_AWESOME_WIFI_WEAK) == 0);
    if (problem && !MenuSystem::IsAnyOpen()) {
      lv_obj_remove_flag(network_label_, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(network_label_, LV_OBJ_FLAG_HIDDEN);
    }
  }

  UpdateHatchingPersistence();
  if (IsHatchingActive()) {
    HideClockScreensaver();
    return;
  }
  UpdateSleepMode(now_ms);
  UpdateClockScreensaver(now_ms);
  DrainFeedBites();
  UpdateBathState();
  UpdateCareEmotionScheduler(now_ms);
  UpdateMischiefEngineState();
  HandlePendingInteractionVoices();
}

// ---------------------------------------------------------------------------
// Status chrome
//
// A state-coloured dot sits to the left of the status text; both are centred as
// a group at y=34, inside the round bezel's visible chord. When idle the text
// collapses away and only the dot remains.
// ---------------------------------------------------------------------------
void EyeDisplay::SetupStatusChrome() {
  if (status_bar_ == nullptr || status_label_ == nullptr) {
    return;
  }

  // Auto-width so the dot can be pinned to the text's real left edge; the stock
  // fixed 192px width with circular scrolling made that impossible.
  lv_label_set_long_mode(status_label_, LV_LABEL_LONG_CLIP);
  lv_obj_set_width(status_label_, LV_SIZE_CONTENT);
  lv_obj_set_style_text_font(status_label_, &lv_font_montserrat_vn_20, 0);
  lv_obj_set_style_text_color(status_label_, lv_color_hex(0xE8EEF8), 0);
  lv_obj_align(status_label_, LV_ALIGN_CENTER, 0, 0);

  if (notification_label_ != nullptr) {
    lv_obj_set_style_text_font(notification_label_, &lv_font_montserrat_vn_20, 0);
  }

  // A short arc riding the top bezel: far from the eyes, and it reads as chrome
  // rather than part of the face.
  status_arc_ = lv_arc_create(lv_screen_active());
  lv_obj_remove_style(status_arc_, nullptr, LV_PART_KNOB);
  lv_obj_remove_flag(status_arc_, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(status_arc_, kStatusArcSize, kStatusArcSize);
  lv_obj_center(status_arc_);
  lv_arc_set_rotation(status_arc_, 0);
  lv_arc_set_bg_angles(status_arc_, kStatusArcStart, kStatusArcEnd);
  lv_arc_set_angles(status_arc_, kStatusArcStart, kStatusArcEnd);
  lv_obj_set_style_arc_opa(status_arc_, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(status_arc_, LV_OPA_TRANSP, LV_PART_KNOB);
  lv_obj_set_style_arc_width(status_arc_, kStatusArcWidth, LV_PART_INDICATOR);
  lv_obj_set_style_arc_rounded(status_arc_, true, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(status_arc_, lv_color_hex(0x5A6068), LV_PART_INDICATOR);
  lv_obj_move_foreground(status_arc_);

  status_base_text_ = Lang::Strings::INITIALIZING;
  status_busy_ = true;
  RenderStatusText();
  UpdateStatusArcColor();

  SetupVoiceWaves();

  status_chrome_timer_ = lv_timer_create(StatusChromeTimerCb, kStatusChromeTickMs, this);
  status_anim_timer_ = lv_timer_create(StatusAnimTimerCb, kStatusAnimTickMs, this);
}

// Three rings either side of the eyes, fixed in place; the eyes may drift over
// them. Listening: yellow, the wave runs inward. Speaking: green, outward.
void EyeDisplay::SetupVoiceWaves() {
  lv_obj_t *screen = lv_screen_active();
  for (int side = 0; side < 2; ++side) {
    for (int ring = 0; ring < 3; ++ring) {
      const int size = kVoiceWaveRadius[ring] * 2;
      lv_obj_t *arc = lv_arc_create(screen);
      lv_obj_remove_style(arc, nullptr, LV_PART_KNOB);
      lv_obj_remove_flag(arc, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_set_size(arc, size, size);
      lv_arc_set_rotation(arc, 0);
      lv_obj_set_style_arc_opa(arc, LV_OPA_TRANSP, LV_PART_MAIN);
      lv_obj_set_style_arc_width(arc, kVoiceWaveWidth, LV_PART_INDICATOR);
      lv_obj_set_style_arc_rounded(arc, true, LV_PART_INDICATOR);
      lv_obj_add_flag(arc, LV_OBJ_FLAG_HIDDEN);
      // Inward rings' circles are centred off-screen; without FLOATING the
      // screen counts them as overflow and draws a scrollbar at the bottom.
      lv_obj_add_flag(arc, LV_OBJ_FLAG_FLOATING);
      voice_waves_[side][ring] = arc;
    }
  }
  PlaceVoiceWaves(false);
}

// Speaking: rings bulge away from the eyes, ")))" on the right. Listening: each
// ring is turned 180 degrees about its own midpoint, "(((" on the right, so it
// cups toward the eye. The midpoint stays put, so only the curvature flips.
void EyeDisplay::PlaceVoiceWaves(bool inward) {
  if (voice_waves_inward_ == static_cast<int>(inward)) {
    return;
  }
  voice_waves_inward_ = inward;
  for (int side = 0; side < 2; ++side) {
    const int out = side == 0 ? -1 : 1;  // direction away from the face centre
    for (int ring = 0; ring < 3; ++ring) {
      lv_obj_t *arc = voice_waves_[side][ring];
      if (arc == nullptr) {
        continue;
      }
      // `ring` is the slot (0 = nearest the eye); the midpoint of each slot is
      // fixed. Inward rings also swap sizes: largest curve nearest the eye,
      // tightest at the edge, like a wave converging on the face.
      const int mid_x = kVoiceWaveEyeOffsetX + kVoiceWaveRadius[ring];
      const int r = kVoiceWaveRadius[inward ? 2 - ring : ring];
      // Circle centre sits r behind the midpoint: toward the eye for outward
      // rings, away from it for inward ones.
      const int cx = out * (inward ? mid_x + r : mid_x - r);
      lv_obj_set_size(arc, r * 2, r * 2);
      // LVGL angles: 0 = right, clockwise. The drawn span faces the midpoint.
      const bool faces_right = (out > 0) != inward;
      const int mid = faces_right ? 0 : 180;
      lv_obj_align(arc, LV_ALIGN_CENTER, cx, 0);
      lv_arc_set_bg_angles(arc, (mid - kVoiceWaveHalfSpan + 360) % 360,
                           (mid + kVoiceWaveHalfSpan) % 360);
      lv_arc_set_angles(arc, (mid - kVoiceWaveHalfSpan + 360) % 360,
                        (mid + kVoiceWaveHalfSpan) % 360);
    }
  }
}

void EyeDisplay::StatusAnimTimerCb(lv_timer_t *timer) {
  auto *self = static_cast<EyeDisplay *>(lv_timer_get_user_data(timer));
  if (self != nullptr) {
    self->StatusAnimTick();
  }
}

void EyeDisplay::StatusAnimTick() {
  if (status_arc_ == nullptr) {
    return;
  }
  const auto state = Application::GetInstance().GetDeviceState();
  const bool chrome = ScreenManager::Policy().status_chrome;
  const bool breathing =
      chrome && (state == kDeviceStateStarting || state == kDeviceStateConnecting ||
                 state == kDeviceStateActivating ||
                 state == kDeviceStateWifiConfiguring ||
                 state == kDeviceStateUpgrading);
  const bool listening = state == kDeviceStateListening;
  const bool waves = chrome && (listening || state == kDeviceStateSpeaking);
  const uint32_t now = lv_tick_get();

  if (breathing) {
    const float t = static_cast<float>(now % kArcBreathPeriodMs) / kArcBreathPeriodMs;
    const float level = 0.5f + 0.5f * std::cos(2.0f * static_cast<float>(M_PI) * t);
    lv_obj_set_style_arc_opa(status_arc_, static_cast<lv_opa_t>(70 + 185 * level),
                             LV_PART_INDICATOR);
  } else {
    lv_obj_set_style_arc_opa(status_arc_, LV_OPA_COVER, LV_PART_INDICATOR);
  }

  if (waves) {
    PlaceVoiceWaves(listening);
  }
  const lv_color_t wave_color = lv_color_hex(listening ? 0xF5C542 : 0x5ADC82);
  const float t = static_cast<float>(now % kVoiceWavePeriodMs) / kVoiceWavePeriodMs;
  for (int ring = 0; ring < 3; ++ring) {
    // Outward (speaking): inner ring leads. Inward (listening): outer leads.
    const int order = listening ? 2 - ring : ring;
    // Rings fire one per quarter period, then a quarter of rest: the pause is
    // what makes the direction readable (a seamless 3-phase loop reads as
    // either way). Each ring snaps on and fades, so the lit edge leads.
    float d = t - order / 4.0f;
    d -= std::floor(d);
    const float level = d < 0.45f ? 1.0f - d / 0.45f : 0.0f;
    const lv_opa_t opa = static_cast<lv_opa_t>(35 + 220 * level * level);
    for (int side = 0; side < 2; ++side) {
      lv_obj_t *arc = voice_waves_[side][ring];
      if (arc == nullptr) {
        continue;
      }
      if (!waves) {
        lv_obj_add_flag(arc, LV_OBJ_FLAG_HIDDEN);
        continue;
      }
      lv_obj_remove_flag(arc, LV_OBJ_FLAG_HIDDEN);
      lv_obj_set_style_arc_color(arc, wave_color, LV_PART_INDICATOR);
      lv_obj_set_style_arc_opa(arc, opa, LV_PART_INDICATOR);
      lv_obj_move_foreground(arc);
    }
  }

  // Nothing moving: stop ticking until the next state change resumes us.
  if (!breathing && !waves) {
    lv_timer_pause(status_anim_timer_);
  }
}

void EyeDisplay::StatusChromeTimerCb(lv_timer_t *timer) {
  auto *self = static_cast<EyeDisplay *>(lv_timer_get_user_data(timer));
  if (self != nullptr) {
    self->StatusChromeTick();
  }
}

void EyeDisplay::RenderStatusText() {
  if (status_label_ == nullptr) {
    return;
  }

  std::string text = status_base_text_;
  if (status_busy_) {
    text.append(status_ellipsis_phase_, '.');
  }
  lv_label_set_text(status_label_, text.c_str());

  if (text.empty()) {
    lv_obj_add_flag(status_label_, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_remove_flag(status_label_, LV_OBJ_FLAG_HIDDEN);
  }

}

void EyeDisplay::UpdateStatusArcColor() {
  if (status_arc_ == nullptr) {
    return;
  }
  uint32_t color = 0x5A6068;
  switch (Application::GetInstance().GetDeviceState()) {
  case kDeviceStateIdle:
  case kDeviceStateListening:
  case kDeviceStateSpeaking:
    color = 0x5ADC82;   // green: fine; the voice waves tell listening from speaking
    break;
  case kDeviceStateConnecting:
  case kDeviceStateStarting:
  case kDeviceStateActivating:
  case kDeviceStateWifiConfiguring:
    color = 0xF5B23C;   // amber
    break;
  case kDeviceStateUpgrading:
    color = 0x50C8DC;   // cyan
    break;
  case kDeviceStateFatalError:
    color = 0xEB5050;   // red
    break;
  default:
    break;
  }
  lv_obj_set_style_arc_color(status_arc_, lv_color_hex(color), LV_PART_INDICATOR);
  // Breathing and the voice waves need the fast tick; it pauses itself again
  // once nothing is moving.
  if (status_anim_timer_ != nullptr) {
    lv_timer_resume(status_anim_timer_);
  }
}

void EyeDisplay::SyncDeviceLook() {
  if (!eye_animation_) {
    return;
  }
  EyeAnimation::DeviceLook look = EyeAnimation::DeviceLook::Idle;
  switch (Application::GetInstance().GetDeviceState()) {
  case kDeviceStateConnecting:
    look = EyeAnimation::DeviceLook::Connecting;
    break;
  case kDeviceStateListening:
    look = EyeAnimation::DeviceLook::Listening;
    break;
  case kDeviceStateSpeaking:
    look = EyeAnimation::DeviceLook::Speaking;
    break;
  default:
    break;
  }
  eye_animation_->SetDeviceLook(look);
}

void EyeDisplay::StatusChromeTick() {
  DisplayLockGuard lock(this);
  // Polled here (every kStatusChromeTickMs) so the eyes follow the session:
  // connecting = head shake, listening = still, speaking = still + bounce.
  SyncDeviceLook();
  if (status_label_ == nullptr || status_arc_ == nullptr) {
    return;
  }

  if (!ScreenManager::Policy().status_chrome) {
    lv_obj_add_flag(status_arc_, LV_OBJ_FLAG_HIDDEN);
    for (auto &side : voice_waves_) {
      for (lv_obj_t *arc : side) {
        if (arc != nullptr) {
          lv_obj_add_flag(arc, LV_OBJ_FLAG_HIDDEN);
        }
      }
    }
    return;
  }
  // The arc lives on the bezel, so notifications no longer collide with it.
  lv_obj_remove_flag(status_arc_, LV_OBJ_FLAG_HIDDEN);

  UpdateStatusArcColor();

  if (status_busy_) {
    status_ellipsis_phase_ = static_cast<uint8_t>((status_ellipsis_phase_ + 1) % 4);
    RenderStatusText();
  }
}

void EyeDisplay::SetEmotion(const char *emotion) {
  if (!eye_animation_) {
    // Fall back to default behavior if eye animation not initialized
    SpiLcdDisplay::SetEmotion(emotion);
    return;
  }

  ESP_LOGI(TAG, "SetEmotion: %s", emotion ? emotion : "null");
  ApplyEmotionInternal(emotion, true);
}

void EyeDisplay::ApplyEmotionInternal(const char *emotion, bool is_external) {
  const char *safe_emotion =
      (emotion != nullptr && emotion[0] != '\0') ? emotion : "neutral";
  if (is_external) {
    last_external_emotion_ms_ = GetNowMs();
    care_overlay_emotion_.clear();
    care_overlay_until_ms_ = 0;
  }
  current_eye_emotion_ = safe_emotion;
  if (sleep_mode_active_) {
    return;
  }

  // Apply emotion directly via RoboEyes persistent mood setters (no auto reset)
  EyeEmotion_Apply(safe_emotion, eye_animation_.get());

  // Also hide the default emoji display (in case it was shown)
  DisplayLockGuard lock(this);
  if (emoji_box_ != nullptr) {
    lv_obj_add_flag(emoji_box_, LV_OBJ_FLAG_HIDDEN);
  }

  UpdateMischiefEngineState();
}

void EyeDisplay::UpdateHatchingPersistence() {
  const bool active_now = IsHatchingActive();
  if (hatch_was_active_ && !active_now && !hatch_marked_done_) {
    Settings hatch_settings("bubu", true);
    hatch_settings.SetBool("hatched", true);
    hatch_marked_done_ = true;
    ESP_LOGI(TAG, "Hatching finished; persisted bubu.hatched=true");
  }
  hatch_was_active_ = active_now;
}

void EyeDisplay::HandleTouch(int x, int y) {
  if (IsHatchingActive()) {
    if (eye_animation_) {
      eye_animation_->HandleTouch(x, y);
    }
    return;
  }
  if (sleep_mode_active_) {
    return;
  }
  if (eye_animation_) {
    eye_animation_->HandleTouch(x, y);
  }
}

bool EyeDisplay::IsTouchOnEyes(int x, int y) const {
  if (IsHatchingActive()) {
    return false;
  }
  return eye_animation_ && eye_animation_->IsTouchInsideEyes(x, y);
}

bool EyeDisplay::IsHatchingActive() const {
  return eye_animation_ && eye_animation_->IsHatchingActive();
}

bool EyeDisplay::HandleHatchingTap(int x, int y) {
  if (!IsHatchingActive() || !eye_animation_) {
    return false;
  }
  eye_animation_->HandleTouch(x, y);
  return true;
}

void EyeDisplay::SetImuAccel(float ax, float ay) {
  if (eye_animation_) {
    eye_animation_->SetImuAccel(ax, ay);
  }
}

// ---- RoboEyes runtime setters ----

void EyeDisplay::SetEyeSize(int w, int h) {
  if (eye_animation_)
    eye_animation_->SetSize(w, h);
}

void EyeDisplay::SetEyeBorderRadius(int r) {
  if (eye_animation_)
    eye_animation_->SetBorderRadius(r);
}

void EyeDisplay::SetEyeSpaceBetween(int gap) {
  if (eye_animation_)
    eye_animation_->SetSpaceBetween(gap);
}

void EyeDisplay::SetLeftEyeSize(int w, int h) {
  if (eye_animation_)
    eye_animation_->SetLeftEyeSize(w, h);
}

void EyeDisplay::SetRightEyeSize(int w, int h) {
  if (eye_animation_)
    eye_animation_->SetRightEyeSize(w, h);
}

void EyeDisplay::SetLeftEyeBorderRadius(int r) {
  if (eye_animation_)
    eye_animation_->SetLeftEyeBorderRadius(r);
}

void EyeDisplay::SetRightEyeBorderRadius(int r) {
  if (eye_animation_)
    eye_animation_->SetRightEyeBorderRadius(r);
}

void EyeDisplay::SetEyeColor(uint8_t r, uint8_t g, uint8_t b) {
  if (eye_animation_)
    eye_animation_->SetEyeColor(r, g, b);
}

void EyeDisplay::SetBaseLeftEyeShape(const EyeShape &shape) {
  if (eye_animation_) {
    eye_animation_->SetBaseLeftShape(shape);
  }
}

void EyeDisplay::SetBaseRightEyeShape(const EyeShape &shape) {
  if (eye_animation_) {
    eye_animation_->SetBaseRightShape(shape);
  }
}

void EyeDisplay::SetEyeMischiefEnabled(bool enabled) {
  if (eye_animation_) {
    eye_animation_->SetMischiefEnabled(enabled);
  }
}

bool EyeDisplay::IsEyeMischiefEnabled() const {
  return eye_animation_ && eye_animation_->IsMischiefEnabled();
}

void EyeDisplay::SetEyeMischiefConfig(const MischiefConfig &config) {
  if (eye_animation_) {
    eye_animation_->SetMischiefConfig(config);
  }
}

const EyeDisplay::MischiefConfig &EyeDisplay::GetEyeMischiefConfig() const {
  static const MischiefConfig fallback{};
  if (!eye_animation_) {
    return fallback;
  }
  return eye_animation_->GetMischiefConfig();
}

void EyeDisplay::TriggerEyeMischief() {
  if (eye_animation_) {
    eye_animation_->TriggerMischief();
  }
}

void EyeDisplay::HandleEyeTapMischief() {
  if (!eye_animation_) {
    return;
  }
  eye_animation_->TriggerMischief();
}

void EyeDisplay::PlayTapVoice() {
  if (IsHatchingActive()) {
    return;
  }
  if (sleep_mode_active_) {
    return;
  }
  const bool allow_playback = !clock_screensaver_active_;
  const std::string_view sound = interaction_voice_.GetVoiceForEvent(
      BubuInteractionEvent::EyeTap, GetNowMs(), allow_playback);
  if (!sound.empty()) {
    Application::GetInstance().Schedule(
        [sound]() { Application::GetInstance().PlayOverlaySound(sound); });
  }
}

void EyeDisplay::SetEyeMood(bool tired, bool angry, bool happy) {
  if (eye_animation_)
    eye_animation_->SetMood(tired, angry, happy);
}

void EyeDisplay::SetEyeCurious(bool curious) {
  if (eye_animation_)
    eye_animation_->SetCurious(curious);
}

void EyeDisplay::SetEyeCyclops(bool cyclops) {
  if (eye_animation_)
    eye_animation_->SetCyclops(cyclops);
}

void EyeDisplay::SetEyeAutoblinker(bool active, int interval_s,
                                   int variation_s) {
  if (eye_animation_)
    eye_animation_->SetAutoblinker(active, interval_s, variation_s);
}

void EyeDisplay::SetEyeIdleMode(bool active, int interval_ms,
                                int variation_ms) {
  if (eye_animation_)
    eye_animation_->SetIdleMode(active, interval_ms, variation_ms);
}

void EyeDisplay::SetEyeHFlicker(bool active, int amplitude) {
  if (eye_animation_)
    eye_animation_->SetHFlicker(active, amplitude);
}

void EyeDisplay::SetEyeVFlicker(bool active, int amplitude) {
  if (eye_animation_)
    eye_animation_->SetVFlicker(active, amplitude);
}

void EyeDisplay::SetEyeSweat(bool active) {
  if (eye_animation_)
    eye_animation_->SetSweat(active);
}

void EyeDisplay::EyeAnimConfused() {
  if (eye_animation_)
    eye_animation_->AnimConfused();
}

void EyeDisplay::StartFeeding() {
  if (!eye_animation_) {
    return;
  }
  NotifyUserInteraction();
  DisplayLockGuard lock(this);
  eye_animation_->StartFeeding(lv_tick_get(), CareSystem::GetHunger());
}

bool EyeDisplay::IsFeedingActive() const {
  return eye_animation_ && eye_animation_->IsFeedingActive();
}

bool EyeDisplay::HandleFeedTap() {
  if (!eye_animation_ || !eye_animation_->IsFeedingActive()) {
    return false;
  }
  DisplayLockGuard lock(this);
  return eye_animation_->HandleFeedTap(lv_tick_get());
}

// Credit hunger per chomp rather than all at once on menu activation, so an
// abandoned feeding gives partial credit and a refused one gives none.
void EyeDisplay::DrainFeedBites() {
  if (!eye_animation_) {
    return;
  }
  const int bites = eye_animation_->ConsumeFeedBites();
  for (int i = 0; i < bites; ++i) {
    CareSystem::AddHunger(CareSystem::kFeedBiteBoost);
  }
}

void EyeDisplay::StartBathing() {
  if (!eye_animation_) {
    return;
  }
  NotifyUserInteraction();
  DisplayLockGuard lock(this);
  eye_animation_->StartBathing(lv_tick_get());
}

bool EyeDisplay::IsBathingActive() const {
  return eye_animation_ && eye_animation_->IsBathingActive();
}

bool EyeDisplay::HandleBathScrub(int x, int y) {
  if (!eye_animation_ || !eye_animation_->IsBathingActive()) {
    return false;
  }
  DisplayLockGuard lock(this);
  return eye_animation_->HandleBathScrub(x, y);
}

bool EyeDisplay::HandleBathTap() {
  if (!eye_animation_) {
    return false;
  }
  return eye_animation_->HandleBathTap();
}

void EyeDisplay::CancelBathing() {
  // No DisplayLockGuard: callers already hold the lock.
  if (eye_animation_) {
    eye_animation_->CancelBathing();
  }
}

// Keep the persistent grime in step with the cleanliness stat, and credit the
// bath once the sequence actually reaches its clean beat -- crediting up front
// would clear the smudges before the user ever saw them.
void EyeDisplay::UpdateBathState() {
  if (!eye_animation_) {
    return;
  }
  // Credit first, then recompute grime -- the other order would recompute from
  // the pre-bath cleanliness and flash the smudges back for a tick.
  if (eye_animation_->ConsumeBathCompleted()) {
    CareSystem::AddCleanliness(CareSystem::kBathBoost);
    ESP_LOGI(TAG, "Bath completed; cleanliness +%d", CareSystem::kBathBoost);
  }
  eye_animation_->SetDirtyLevel(CareSystem::GetCleanliness());
}

void EyeDisplay::EyeAnimLaugh() {
  if (eye_animation_)
    eye_animation_->AnimLaugh();
}

bool EyeDisplay::CanShowClockScreensaver() const {
  if (clock_screensaver_ == nullptr || !setup_ui_called_) {
    return false;
  }
  if (IsHatchingActive()) {
    return false;
  }
  if (MenuSystem::IsAnyOpen()) {
    return false;
  }

  if (notification_label_ != nullptr &&
      !lv_obj_has_flag(notification_label_, LV_OBJ_FLAG_HIDDEN)) {
    return false;
  }

  return Application::GetInstance().GetDeviceState() == kDeviceStateIdle;
}

void EyeDisplay::UpdateClockLabels(uint64_t now_ms) {
  if (clock_time_label_ == nullptr || clock_date_label_ == nullptr) {
    return;
  }
  if (now_ms - last_clock_refresh_ms_ < kClockRefreshMs) {
    return;
  }
  last_clock_refresh_ms_ = now_ms;

  time_t now = time(nullptr);
  struct tm tm_info;
  localtime_r(&now, &tm_info);
  DisplayLockGuard lock(this);
  if (tm_info.tm_year < 2025 - 1900) {
    lv_label_set_text(clock_time_label_, "--:--");
    lv_label_set_text(clock_date_label_, "--/--/--");
    return;
  }

  char time_buf[16];
  char date_buf[16];
  strftime(time_buf, sizeof(time_buf), "%H:%M", &tm_info);
  strftime(date_buf, sizeof(date_buf), "%d/%m/%y", &tm_info);
  lv_label_set_text(clock_time_label_, time_buf);
  lv_label_set_text(clock_date_label_, date_buf);
}

void EyeDisplay::ShowClockScreensaver() {
  if (clock_screensaver_active_) {
    return;
  }

  DisplayLockGuard lock(this);
  if (eye_animation_) {
    eye_animation_->SetVisible(false);
  }
  if (top_bar_) {
    lv_obj_add_flag(top_bar_, LV_OBJ_FLAG_HIDDEN);
  }
  if (status_bar_) {
    lv_obj_add_flag(status_bar_, LV_OBJ_FLAG_HIDDEN);
  }
  if (bottom_bar_) {
    lv_obj_add_flag(bottom_bar_, LV_OBJ_FLAG_HIDDEN);
  }
  if (content_) {
    lv_obj_add_flag(content_, LV_OBJ_FLAG_HIDDEN);
  }
  if (clock_screensaver_) {
    lv_obj_clear_flag(clock_screensaver_, LV_OBJ_FLAG_HIDDEN);
    // This object is created before later status/menu siblings. Bring it to the
    // top when activated so the clock is the sole owner of the screen.
    lv_obj_move_foreground(clock_screensaver_);
  }
  clock_screensaver_active_ = true;
}

void EyeDisplay::HideClockScreensaver() {
  if (!clock_screensaver_active_) {
    return;
  }

  DisplayLockGuard lock(this);
  if (clock_screensaver_) {
    lv_obj_add_flag(clock_screensaver_, LV_OBJ_FLAG_HIDDEN);
  }
  if (content_) {
    lv_obj_clear_flag(content_, LV_OBJ_FLAG_HIDDEN);
  }
  if (bottom_bar_) {
    lv_obj_clear_flag(bottom_bar_, LV_OBJ_FLAG_HIDDEN);
  }
  if (status_bar_) {
    lv_obj_clear_flag(status_bar_, LV_OBJ_FLAG_HIDDEN);
  }
  if (top_bar_) {
    lv_obj_clear_flag(top_bar_, LV_OBJ_FLAG_HIDDEN);
  }
  if (eye_animation_) {
    eye_animation_->SetVisible(true);
  }
  clock_screensaver_active_ = false;
  UpdateMischiefEngineState();
}

void EyeDisplay::UpdateClockScreensaver(uint64_t now_ms) {
  // Show this stage on schedule even before SNTP is ready. UpdateClockLabels()
  // renders --:--/-- as a safe fallback and replaces it once time is valid.
  if (!CanShowClockScreensaver() ||
      now_ms - last_user_interaction_ms_ < kClockIdleTimeoutMs) {
    HideClockScreensaver();
    return;
  }

  UpdateClockLabels(now_ms);
  ShowClockScreensaver();
}

void EyeDisplay::UpdateSleepMode(uint64_t now_ms) {
  if (IsHatchingActive()) {
    return;
  }
  if (sleep_mode_active_) {
    if (!IsDisplaySleepAllowedState(Application::GetInstance().GetDeviceState()) ||
        MenuSystem::IsAnyOpen()) {
      StopSleepMode();
      return;
    }

    while (sleep_last_energy_tick_ms_ != 0 &&
           now_ms - sleep_last_energy_tick_ms_ >= kSleepEnergyTickMs) {
      CareSystem::AddEnergy(kSleepEnergyBoostPerTick);
      sleep_last_energy_tick_ms_ += kSleepEnergyTickMs;
    }
    return;
  }

  if (MenuSystem::IsAnyOpen()) {
    return;
  }
  if (!IsDisplaySleepAllowedState(Application::GetInstance().GetDeviceState())) {
    return;
  }
  if (now_ms - last_user_interaction_ms_ < kSleepIdleTimeoutMs) {
    return;
  }

  StartSleepMode();
}

// Which screen owns the display right now.
//
// Step 1 derives this from the booleans that already exist, so the screen model
// cannot drift from actual behaviour. Step 2 inverts that: ScreenManager becomes
// the source of truth and these booleans go away. Menu sub-panels all collapse
// to Menu here because they share one policy -- step 2 splits them out.
ScreenManager::ScreenId EyeDisplay::DeriveScreen() const {
  using ScreenId = ScreenManager::ScreenId;

  if (IsHatchingActive()) {
    return ScreenId::Hatching;
  }
  if (clock_screensaver_active_) {
    return ScreenId::Clock;
  }
  if (sleep_mode_active_) {
    return ScreenId::Sleep;
  }
  if (MenuSystem::IsAnyOpen()) {
    // MenuSystem knows which panel is up; it owns that half of the mapping.
    return MenuSystem::ActiveScreen();
  }
  if (MenuSystem::IsCelebrationActive()) {
    // The level-up overlay covers the screen, so the eyes stop rather than
    // rendering underneath it while its GIF decodes.
    return ScreenId::Celebration;
  }
  if (IsFeedingActive()) {
    return ScreenId::Feeding;
  }
  if (IsBathingActive()) {
    return ScreenId::Bathing;
  }
  return ScreenId::Main;
}

void EyeDisplay::RefreshScreen() {
  ScreenManager::Set(DeriveScreen());
}

void EyeDisplay::ApplyScreenPolicy(ScreenManager::ScreenId screen) {
  if (!eye_animation_) {
    return;
  }
  const ScreenManager::ScreenPolicy policy = ScreenManager::PolicyFor(screen);
  DisplayLockGuard lock(this);
  eye_animation_->SetRenderPolicy(policy.eye_fps, policy.eye_static);
  // Status widgets are siblings of the eye canvas, so the render policy alone
  // cannot hide them. Bind them to the same screen ownership decision to keep
  // both screensaver stages free of stale battery/Wi-Fi/status overlays.
  if (status_bar_) {
    if (policy.status_chrome) {
      lv_obj_remove_flag(status_bar_, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(status_bar_, LV_OBJ_FLAG_HIDDEN);
    }
  }
  if (bottom_icons_) {
    if (policy.status_chrome) {
      lv_obj_remove_flag(bottom_icons_, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(bottom_icons_, LV_OBJ_FLAG_HIDDEN);
    }
  }
}

void EyeDisplay::UpdateMischiefEngineState() {
  if (!eye_animation_) {
    return;
  }

  eye_animation_->SetMischiefEnabled(ScreenManager::Policy().mischief);
}

void EyeDisplay::HandlePendingInteractionVoices() {
  if (!eye_animation_) {
    return;
  }

  if (eye_animation_->ConsumePendingInteractionEvent(
          EyeAnimation::InteractionEvent::Mischief)) {
    MaybePlayInteractionVoice(BubuInteractionEvent::Mischief);
  }
  if (eye_animation_->ConsumePendingInteractionEvent(
          EyeAnimation::InteractionEvent::Blink)) {
    MaybePlayInteractionVoice(BubuInteractionEvent::Blink);
  }
}

void EyeDisplay::MaybePlayInteractionVoice(BubuInteractionEvent event) {
  const Vox::Mood mood =
      eye_animation_ ? eye_animation_->GetMischiefMood() : Vox::Mood::Mumble;
  const std::string_view sound = interaction_voice_.GetVoiceForEvent(
      event, GetNowMs(), ScreenManager::Policy().interaction_voices, mood);
  if (!sound.empty()) {
    Application::GetInstance().PlayOverlaySound(sound);
    if (event == BubuInteractionEvent::Mischief && eye_animation_) {
      // Stretch the pose to cover however long this clip actually plays so
      // it never retreats to neutral mid-voice.
      const uint32_t duration_ms = interaction_voice_.GetVoiceDurationMs(sound);
      eye_animation_->ExtendMischiefHold(duration_ms);
    }
  }
}

bool EyeDisplay::ShouldRunCareEmotionScheduler() const {
  if (!care_emotion_config_.enabled || !eye_animation_) {
    return false;
  }
  if (!ScreenManager::Policy().care_emotions) {
    return false;
  }
  return Application::GetInstance().GetDeviceState() == kDeviceStateIdle;
}

const char *EyeDisplay::SelectCareDrivenEmotion() const {
  const int hunger = CareSystem::GetHunger();
  const int mood = CareSystem::GetMood();
  const int energy = CareSystem::GetEnergy();
  const int clean = CareSystem::GetCleanliness();

  const float hunger_ok = ToRatio(hunger);
  const float mood_ok = ToRatio(mood);
  const float energy_ok = ToRatio(energy);
  const float clean_ok = ToRatio(clean);
  const float positive = (hunger_ok + mood_ok + energy_ok + clean_ok) * 0.25f;
  const float negative = 1.0f - positive;
  const float hunger_need = 1.0f - hunger_ok;
  const float mood_need = 1.0f - mood_ok;
  const float energy_need = 1.0f - energy_ok;
  const float clean_need = 1.0f - clean_ok;

  std::array<float, kCareEmotionTable.size()> weights{};
  float total_weight = 0.0f;
  for (size_t i = 0; i < kCareEmotionTable.size(); ++i) {
    const auto &rule = kCareEmotionTable[i];
    float weight =
        rule.base_weight + rule.positive_bias * positive +
        rule.negative_bias * negative + rule.hunger_need_bias * hunger_need +
        rule.mood_need_bias * mood_need + rule.energy_need_bias * energy_need +
        rule.clean_need_bias * clean_need;
    if (weight < 0.0f) {
      weight = 0.0f;
    }
    weights[i] = weight;
    total_weight += weight;
  }

  if (total_weight <= 0.0001f) {
    return "neutral";
  }

  const float pick =
      (static_cast<float>(esp_random()) / 4294967295.0f) * total_weight;
  float acc = 0.0f;
  for (size_t i = 0; i < kCareEmotionTable.size(); ++i) {
    acc += weights[i];
    if (pick <= acc) {
      return kCareEmotionTable[i].emotion;
    }
  }
  return kCareEmotionTable.back().emotion;
}

const char *
EyeDisplay::SelectOverlayEmotionForBase(const std::string &base_emotion) const {
  if (!care_emotion_config_.overlay_enabled ||
      care_emotion_config_.overlay_duration_ms == 0) {
    return nullptr;
  }
  if (base_emotion.rfind("legacy_emo_", 0) == 0) {
    return nullptr;
  }
  if ((esp_random() % 100U) >= care_emotion_config_.overlay_chance_pct) {
    return nullptr;
  }

  struct OverlayWeight {
    const char *emotion;
    uint8_t weight;
  };
  const auto pick_weighted = [](const OverlayWeight *table,
                                size_t count) -> const char * {
    if (table == nullptr || count == 0) {
      return nullptr;
    }
    uint32_t total = 0;
    for (size_t i = 0; i < count; ++i) {
      total += table[i].weight;
    }
    if (total == 0) {
      return nullptr;
    }
    const uint32_t pick = esp_random() % total;
    uint32_t acc = 0;
    for (size_t i = 0; i < count; ++i) {
      acc += table[i].weight;
      if (pick < acc) {
        return table[i].emotion;
      }
    }
    return table[count - 1].emotion;
  };

  static constexpr std::array<OverlayWeight, 3> kHappyOverlays = {{
      {"surprised", 45},
      {"thinking", 35},
      {"confused", 20},
  }};
  static constexpr std::array<OverlayWeight, 3> kSadOverlays = {{
      {"thinking", 50},
      {"confused", 35},
      {"surprised", 15},
  }};
  static constexpr std::array<OverlayWeight, 3> kAngryOverlays = {{
      {"confused", 45},
      {"thinking", 40},
      {"surprised", 15},
  }};

  const int mood = CareSystem::GetMood();
  const int energy = CareSystem::GetEnergy();
  if (energy < 25) {
    return "thinking";
  }
  if (base_emotion == "happy") {
    return pick_weighted(kHappyOverlays.data(), kHappyOverlays.size());
  }
  if (base_emotion == "sad") {
    return pick_weighted(kSadOverlays.data(), kSadOverlays.size());
  }
  if (base_emotion == "angry") {
    return pick_weighted(kAngryOverlays.data(), kAngryOverlays.size());
  }
  if (mood < 30) {
    return "confused";
  }
  return "thinking";
}

void EyeDisplay::UpdateCareEmotionScheduler(uint64_t now_ms) {
  // With the carousel off nothing else ever replaces an emotion, so one sent by
  // Gemini, a game or a voice command would stay forever. Once the device is
  // idle, hold it kEmotionReturnMs after it arrived, then go back to neutral so
  // the idle eyes take over. sleepy is left alone: it is a state (power save),
  // not a reaction.
  if (!care_emotion_config_.enabled && eye_animation_ &&
      Application::GetInstance().GetDeviceState() == kDeviceStateIdle &&
      last_external_emotion_ms_ != 0 &&
      now_ms - last_external_emotion_ms_ >= kEmotionReturnMs &&
      current_eye_emotion_ != "neutral" && current_eye_emotion_ != "sleepy") {
    ESP_LOGI(TAG, "Emotion '%s' held %u ms, back to neutral", current_eye_emotion_.c_str(),
             static_cast<unsigned>(kEmotionReturnMs));
    ApplyEmotionInternal("neutral", false);
  }
  if (!ShouldRunCareEmotionScheduler()) {
    care_next_emotion_change_ms_ = 0;
    care_overlay_until_ms_ = 0;
    care_overlay_emotion_.clear();
    return;
  }

  const uint64_t external_hold_until =
      last_external_emotion_ms_ + care_emotion_config_.external_override_ms;
  if (now_ms < external_hold_until) {
    care_next_emotion_change_ms_ = external_hold_until;
    care_overlay_until_ms_ = 0;
    care_overlay_emotion_.clear();
    return;
  }

  if (care_next_emotion_change_ms_ == 0) {
    care_base_emotion_ = current_eye_emotion_;
    care_next_emotion_change_ms_ =
        now_ms + RandomRangeU32(care_emotion_config_.min_duration_ms,
                                care_emotion_config_.max_duration_ms);
    return;
  }

  if (now_ms < care_next_emotion_change_ms_) {
    if (!care_overlay_emotion_.empty() && now_ms >= care_overlay_until_ms_) {
      care_overlay_emotion_.clear();
      care_overlay_until_ms_ = 0;
      if (!care_base_emotion_.empty() &&
          current_eye_emotion_ != care_base_emotion_) {
        ApplyEmotionInternal(care_base_emotion_.c_str(), false);
      }
    }
    return;
  }

  const char *selected = SelectCareDrivenEmotion();
  if (selected != nullptr) {
    // Avoid long streaks of identical emotion.
    if (care_base_emotion_ == selected) {
      const char *retry = SelectCareDrivenEmotion();
      if (retry != nullptr) {
        selected = retry;
      }
    }
    care_base_emotion_ = selected;
    care_overlay_emotion_.clear();
    care_overlay_until_ms_ = 0;
    ApplyEmotionInternal(care_base_emotion_.c_str(), false);

    const char *overlay = SelectOverlayEmotionForBase(care_base_emotion_);
    if (overlay != nullptr && overlay[0] != '\0') {
      care_overlay_emotion_ = overlay;
      care_overlay_until_ms_ =
          now_ms + care_emotion_config_.overlay_duration_ms;
      if (care_overlay_emotion_ != care_base_emotion_) {
        ApplyEmotionInternal(care_overlay_emotion_.c_str(), false);
      }
    }

    ESP_LOGD(TAG, "Care emotion pick: base=%s overlay=%s (H:%d M:%d E:%d C:%d)",
             care_base_emotion_.c_str(),
             care_overlay_emotion_.empty() ? "-"
                                           : care_overlay_emotion_.c_str(),
             CareSystem::GetHunger(), CareSystem::GetMood(),
             CareSystem::GetEnergy(), CareSystem::GetCleanliness());
  }

  const bool legacy_selected = IsLegacyEmotionName(care_base_emotion_);
  const uint32_t min_duration =
      legacy_selected ? care_emotion_config_.legacy_min_duration_ms
                      : care_emotion_config_.min_duration_ms;
  const uint32_t max_duration =
      legacy_selected ? care_emotion_config_.legacy_max_duration_ms
                      : care_emotion_config_.max_duration_ms;
  care_next_emotion_change_ms_ = now_ms + RandomRangeU32(min_duration, max_duration);
}
