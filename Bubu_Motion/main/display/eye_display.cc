#include "eye_display.h"
#include "menu_system.h"
#include "message_board.h"
#include "lvgl_display/lvgl_theme.h"
#include "application.h"
#include "care_system.h"
#include "settings.h"
#include <esp_log.h>
#include <esp_random.h>
#include <algorithm>
#include <array>
#include <ctime>

#define TAG "EyeDisplay"

extern const lv_font_t lv_font_montserrat_vn_20;
extern const lv_font_t lv_font_montserrat_vn_28;
extern const lv_font_t lv_font_montserrat_48;

namespace {

struct CareEmotionWeight {
    const char* emotion;
    float base_weight;
    float positive_bias;
    float negative_bias;
    float hunger_need_bias;
    float mood_need_bias;
    float energy_need_bias;
    float clean_need_bias;
};

// Tune this table to control how often each emotion appears.
constexpr std::array<CareEmotionWeight, 16> kCareEmotionTable = {{
    {"neutral",     1.40f, 0.40f, 0.10f, 0.00f, 0.00f, 0.00f, 0.00f},
    {"relaxed",     0.80f, 1.10f, 0.00f, 0.00f, 0.10f, 0.40f, 0.20f},
    {"happy",       1.00f, 2.30f, 0.00f, 0.00f, 0.80f, 0.40f, 0.20f},
    {"laughing",    0.50f, 1.70f, 0.00f, 0.00f, 1.00f, 0.20f, 0.10f},
    {"surprised",   0.35f, 1.00f, 0.20f, 0.20f, 0.70f, 0.30f, 0.20f},
    {"skeptic",     0.25f, 0.20f, 0.90f, 0.20f, 1.20f, 0.50f, 0.40f},
    {"skeptical",   0.25f, 0.20f, 0.90f, 0.20f, 1.20f, 0.50f, 0.40f},
    {"doubt",       0.20f, 0.10f, 1.05f, 0.30f, 1.40f, 0.50f, 0.45f},
    {"worried",     0.35f, 0.00f, 1.30f, 0.45f, 1.45f, 0.70f, 0.65f},
    {"sad",         0.50f, 0.00f, 1.60f, 0.60f, 1.60f, 0.60f, 0.80f},
    {"annoyed",     0.22f, 0.00f, 1.45f, 1.10f, 1.05f, 0.25f, 0.35f},
    {"angry",       0.30f, 0.00f, 1.80f, 1.70f, 1.30f, 0.20f, 0.40f},
    {"sleepy",      0.30f, 0.00f, 2.10f, 0.20f, 0.30f, 2.20f, 0.20f},
    {"embarrassed", 0.20f, 0.00f, 1.20f, 0.20f, 0.70f, 0.40f, 1.90f},
    {"nervous",     0.20f, 0.00f, 1.00f, 0.30f, 1.20f, 1.10f, 0.80f},
    {"anxious",     0.20f, 0.00f, 1.00f, 0.50f, 0.80f, 1.50f, 0.80f},
}};

uint32_t RandomRangeU32(uint32_t lo, uint32_t hi) {
    if (lo >= hi) {
        return lo;
    }
    return lo + (esp_random() % (hi - lo + 1));
}

float ToRatio(int stat_0_100) {
    int clamped = std::max(0, std::min(100, stat_0_100));
    return static_cast<float>(clamped) / 100.0f;
}

}  // namespace

EyeDisplay::EyeDisplay(esp_lcd_panel_io_handle_t io_handle,
                       esp_lcd_panel_handle_t panel_handle,
                       int width, int height,
                       int offset_x, int offset_y,
                       bool mirror_x, bool mirror_y, bool swap_xy)
    : SpiLcdDisplay(io_handle, panel_handle, width, height, offset_x, offset_y, mirror_x, mirror_y, swap_xy) {
}

EyeDisplay::~EyeDisplay() {
    eye_animation_.reset();
}

uint64_t EyeDisplay::GetNowMs() {
    return esp_timer_get_time() / 1000;
}

void EyeDisplay::SetupUI() {
    // Force dark theme before base SetupUI so all elements are black
    auto* dark_theme = LvglThemeManager::GetInstance().GetTheme("dark");
    if (dark_theme) {
        current_theme_ = dark_theme;
    }

    // Call base SetupUI to create all standard UI elements
    SpiLcdDisplay::SetupUI();

    DisplayLockGuard lock(this);

    // Force the entire screen and all UI elements to black background
    lv_obj_t* screen = lv_screen_active();
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

    // Add padding for round screen
    lv_obj_set_style_pad_left(status_bar_, LV_HOR_RES * 0.2, 0);
    lv_obj_set_style_pad_right(status_bar_, LV_HOR_RES * 0.2, 0);

    // Hide the standard emoji box - we'll use eye animation instead
    if (emoji_box_ != nullptr) {
        lv_obj_add_flag(emoji_box_, LV_OBJ_FLAG_HIDDEN);
    }

    // Create eye animation on the active screen
    eye_animation_ = std::make_unique<EyeAnimation>();
    eye_animation_->Init(screen, width_, height_);
    eye_animation_->SetInteractionEventCallback([this](EyeAnimation::InteractionEvent event) {
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

    // The standard LCD top bar is created inside container_, which sits behind the
    // full-screen eye canvas on this board. Re-parent it onto the screen and keep
    // both bars in the foreground so boot/status text remains visible.
    if (top_bar_) {
        lv_obj_set_parent(top_bar_, screen);
        lv_obj_align(top_bar_, LV_ALIGN_TOP_MID, 0, 0);
        lv_obj_move_foreground(top_bar_);
    }
    if (status_bar_) {
        lv_obj_move_foreground(status_bar_);
    }

    // Initialize menu system (overlays on top of eyes)
    MenuSystem::Begin(this);
    MessageBoard::Begin(this);

    ESP_LOGI(TAG, "EyeDisplay UI setup complete (black background + animated eyes + menu)");
}

void EyeDisplay::CreateClockScreensaver(lv_obj_t* parent) {
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
    lv_obj_set_style_text_font(clock_time_label_, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_align(clock_time_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(clock_time_label_, "--:--");
    lv_obj_align(clock_time_label_, LV_ALIGN_CENTER, 0, -18);

    clock_date_label_ = lv_label_create(clock_screensaver_);
    lv_obj_set_width(clock_date_label_, LV_HOR_RES);
    lv_obj_set_style_text_color(clock_date_label_, lv_color_white(), 0);
    lv_obj_set_style_text_font(clock_date_label_, &lv_font_montserrat_vn_20, 0);
    lv_obj_set_style_text_align(clock_date_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(clock_date_label_, "--/--/--");
    lv_obj_align_to(clock_date_label_, clock_time_label_, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);
}

void EyeDisplay::SetTheme(Theme* theme) {
    // Let the base class apply fonts, text colors, etc.
    LcdDisplay::SetTheme(theme);

    // Then force black background on everything
    DisplayLockGuard lock(this);
    lv_obj_t* screen = lv_screen_active();
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
    if (network_label_) lv_obj_set_style_text_color(network_label_, lv_color_white(), 0);
    if (status_label_) lv_obj_set_style_text_color(status_label_, lv_color_white(), 0);
    if (notification_label_) lv_obj_set_style_text_color(notification_label_, lv_color_white(), 0);
    if (mute_label_) lv_obj_set_style_text_color(mute_label_, lv_color_white(), 0);
    if (battery_label_) lv_obj_set_style_text_color(battery_label_, lv_color_white(), 0);
    if (emoji_label_) lv_obj_set_style_text_color(emoji_label_, lv_color_white(), 0);
    if (chat_message_label_) lv_obj_set_style_text_color(chat_message_label_, lv_color_white(), 0);

    ESP_LOGI(TAG, "SetTheme: forced black background");
}

void EyeDisplay::NotifyUserInteraction() {
    last_user_interaction_ms_ = GetNowMs();
    HideClockScreensaver();
}

bool EyeDisplay::DismissClockScreensaver() {
    last_user_interaction_ms_ = GetNowMs();
    if (!clock_screensaver_active_) {
        return false;
    }
    HideClockScreensaver();
    return true;
}

bool EyeDisplay::IsClockScreensaverActive() const {
    return clock_screensaver_active_;
}

void EyeDisplay::SetStatus(const char* status) {
    NotifyUserInteraction();
    SpiLcdDisplay::SetStatus(status);
}

void EyeDisplay::ShowNotification(const char* notification, int duration_ms) {
    NotifyUserInteraction();
    SpiLcdDisplay::ShowNotification(notification, duration_ms);
}

void EyeDisplay::ShowNotification(const std::string& notification, int duration_ms) {
    ShowNotification(notification.c_str(), duration_ms);
}

void EyeDisplay::SetChatMessage(const char* role, const char* content) {
    NotifyUserInteraction();
    SpiLcdDisplay::SetChatMessage(role, content);
}

void EyeDisplay::ClearChatMessages() {
    NotifyUserInteraction();
    SpiLcdDisplay::ClearChatMessages();
}

void EyeDisplay::UpdateStatusBar(bool update_all) {
    uint64_t now_ms = GetNowMs();
    SpiLcdDisplay::UpdateStatusBar(update_all);
    UpdateClockScreensaver(now_ms);
    UpdateCareEmotionScheduler(now_ms);
    UpdateMischiefEngineState();
    HandlePendingInteractionVoices();
}

void EyeDisplay::SetEmotion(const char* emotion) {
    if (!eye_animation_) {
        // Fall back to default behavior if eye animation not initialized
        SpiLcdDisplay::SetEmotion(emotion);
        return;
    }

    ESP_LOGI(TAG, "SetEmotion: %s", emotion ? emotion : "null");
    ApplyEmotionInternal(emotion, true);
}

void EyeDisplay::ApplyEmotionInternal(const char* emotion, bool is_external) {
    const char* safe_emotion = (emotion != nullptr && emotion[0] != '\0') ? emotion : "neutral";
    if (is_external) {
        last_external_emotion_ms_ = GetNowMs();
        care_overlay_emotion_.clear();
        care_overlay_until_ms_ = 0;
    }
    current_eye_emotion_ = safe_emotion;

    // Apply emotion directly via RoboEyes persistent mood setters (no auto reset)
    EyeEmotion_Apply(safe_emotion, eye_animation_.get());

    // Also hide the default emoji display (in case it was shown)
    DisplayLockGuard lock(this);
    if (emoji_box_ != nullptr) {
        lv_obj_add_flag(emoji_box_, LV_OBJ_FLAG_HIDDEN);
    }

    UpdateMischiefEngineState();
}

void EyeDisplay::HandleTouch(int x, int y) {
    if (eye_animation_) {
        eye_animation_->HandleTouch(x, y);
    }
}

bool EyeDisplay::IsTouchOnEyes(int x, int y) const {
    return eye_animation_ && eye_animation_->IsTouchInsideEyes(x, y);
}

void EyeDisplay::SetImuAccel(float ax, float ay) {
    if (eye_animation_) {
        eye_animation_->SetImuAccel(ax, ay);
    }
}

// ---- RoboEyes runtime setters ----

void EyeDisplay::SetEyeSize(int w, int h) {
    if (eye_animation_) eye_animation_->SetSize(w, h);
}

void EyeDisplay::SetEyeBorderRadius(int r) {
    if (eye_animation_) eye_animation_->SetBorderRadius(r);
}

void EyeDisplay::SetEyeSpaceBetween(int gap) {
    if (eye_animation_) eye_animation_->SetSpaceBetween(gap);
}

void EyeDisplay::SetLeftEyeSize(int w, int h) {
    if (eye_animation_) eye_animation_->SetLeftEyeSize(w, h);
}

void EyeDisplay::SetRightEyeSize(int w, int h) {
    if (eye_animation_) eye_animation_->SetRightEyeSize(w, h);
}

void EyeDisplay::SetLeftEyeBorderRadius(int r) {
    if (eye_animation_) eye_animation_->SetLeftEyeBorderRadius(r);
}

void EyeDisplay::SetRightEyeBorderRadius(int r) {
    if (eye_animation_) eye_animation_->SetRightEyeBorderRadius(r);
}

void EyeDisplay::SetEyeColor(uint8_t r, uint8_t g, uint8_t b) {
    if (eye_animation_) eye_animation_->SetEyeColor(r, g, b);
}

void EyeDisplay::SetLeftEyeColor(uint8_t r, uint8_t g, uint8_t b) {
    if (eye_animation_) eye_animation_->SetLeftEyeColor(r, g, b);
}

void EyeDisplay::SetRightEyeColor(uint8_t r, uint8_t g, uint8_t b) {
    if (eye_animation_) eye_animation_->SetRightEyeColor(r, g, b);
}

EyeDisplay::EyeShape EyeDisplay::GetBaseLeftEyeShape() const {
    if (!eye_animation_) {
        return {};
    }
    return eye_animation_->GetBaseLeftShape();
}

EyeDisplay::EyeShape EyeDisplay::GetBaseRightEyeShape() const {
    if (!eye_animation_) {
        return {};
    }
    return eye_animation_->GetBaseRightShape();
}

void EyeDisplay::SetBaseLeftEyeShape(const EyeShape& shape) {
    if (eye_animation_) {
        eye_animation_->SetBaseLeftShape(shape);
    }
}

void EyeDisplay::SetBaseRightEyeShape(const EyeShape& shape) {
    if (eye_animation_) {
        eye_animation_->SetBaseRightShape(shape);
    }
}

void EyeDisplay::PreviewLeftEyeShape(const EyeShape& shape) {
    if (eye_animation_) {
        eye_animation_->PreviewLeftShape(shape);
    }
}

void EyeDisplay::PreviewRightEyeShape(const EyeShape& shape) {
    if (eye_animation_) {
        eye_animation_->PreviewRightShape(shape);
    }
}

void EyeDisplay::ClearEyePreviewToBase() {
    if (eye_animation_) {
        eye_animation_->ClearPreviewToBase();
    }
}

EyeDisplay::EyeBounds EyeDisplay::GetLeftEyeBounds() const {
    if (!eye_animation_) {
        return {};
    }
    return eye_animation_->GetLeftEyeBounds();
}

EyeDisplay::EyeBounds EyeDisplay::GetRightEyeBounds() const {
    if (!eye_animation_) {
        return {};
    }
    return eye_animation_->GetRightEyeBounds();
}

void EyeDisplay::SetEyeMischiefEnabled(bool enabled) {
    if (eye_animation_) {
        eye_animation_->SetMischiefEnabled(enabled);
    }
}

bool EyeDisplay::IsEyeMischiefEnabled() const {
    return eye_animation_ && eye_animation_->IsMischiefEnabled();
}

void EyeDisplay::SetEyeMischiefConfig(const MischiefConfig& config) {
    if (eye_animation_) {
        eye_animation_->SetMischiefConfig(config);
    }
}

const EyeDisplay::MischiefConfig& EyeDisplay::GetEyeMischiefConfig() const {
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
    const bool allow_playback = !clock_screensaver_active_;
    const std::string_view sound = interaction_voice_.GetVoiceForEvent(BubuInteractionEvent::EyeTap, GetNowMs(), allow_playback);
    if (!sound.empty()) {
        Application::GetInstance().Schedule([sound]() {
            Application::GetInstance().PlaySound(sound);
        });
    }
}

void EyeDisplay::SetEyeMood(bool tired, bool angry, bool happy) {
    if (eye_animation_) eye_animation_->SetMood(tired, angry, happy);
}

void EyeDisplay::SetEyeCurious(bool curious) {
    if (eye_animation_) eye_animation_->SetCurious(curious);
}

void EyeDisplay::SetEyeCyclops(bool cyclops) {
    if (eye_animation_) eye_animation_->SetCyclops(cyclops);
}

void EyeDisplay::SetEyeAutoblinker(bool active, int interval_s, int variation_s) {
    if (eye_animation_) eye_animation_->SetAutoblinker(active, interval_s, variation_s);
}

void EyeDisplay::SetEyeIdleMode(bool active, int interval_ms, int variation_ms) {
    if (eye_animation_) eye_animation_->SetIdleMode(active, interval_ms, variation_ms);
}

void EyeDisplay::SetEyeHFlicker(bool active, int amplitude) {
    if (eye_animation_) eye_animation_->SetHFlicker(active, amplitude);
}

void EyeDisplay::SetEyeVFlicker(bool active, int amplitude) {
    if (eye_animation_) eye_animation_->SetVFlicker(active, amplitude);
}

void EyeDisplay::SetEyeSweat(bool active) {
    if (eye_animation_) eye_animation_->SetSweat(active);
}

void EyeDisplay::EyeAnimConfused() {
    if (eye_animation_) eye_animation_->AnimConfused();
}

void EyeDisplay::EyeAnimLaugh() {
    if (eye_animation_) eye_animation_->AnimLaugh();
}

bool EyeDisplay::CanShowClockScreensaver() const {
    if (clock_screensaver_ == nullptr || !setup_ui_called_) {
        return false;
    }

    if (MenuSystem::IsAnyOpen()) {
        return false;
    }

    if (notification_label_ != nullptr && !lv_obj_has_flag(notification_label_, LV_OBJ_FLAG_HIDDEN)) {
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
    UpdateClockLabels(now_ms);

    time_t now = time(nullptr);
    struct tm tm_info;
    localtime_r(&now, &tm_info);
    const bool time_valid = tm_info.tm_year >= 2025 - 1900;

    if (!time_valid || !CanShowClockScreensaver() ||
        now_ms - last_user_interaction_ms_ < kClockIdleTimeoutMs) {
        HideClockScreensaver();
        return;
    }

    ShowClockScreensaver();
}

void EyeDisplay::UpdateMischiefEngineState() {
    if (!eye_animation_) {
        return;
    }

    bool should_run = !clock_screensaver_active_ &&
                      !MenuSystem::IsAnyOpen();
    eye_animation_->SetMischiefEnabled(should_run);
}

void EyeDisplay::HandlePendingInteractionVoices() {
    if (!eye_animation_) {
        return;
    }

    if (eye_animation_->ConsumePendingInteractionEvent(EyeAnimation::InteractionEvent::Mischief)) {
        MaybePlayInteractionVoice(BubuInteractionEvent::Mischief);
    }
    if (eye_animation_->ConsumePendingInteractionEvent(EyeAnimation::InteractionEvent::Blink)) {
        MaybePlayInteractionVoice(BubuInteractionEvent::Blink);
    }
}

void EyeDisplay::MaybePlayInteractionVoice(BubuInteractionEvent event) {
    const bool allow_playback = !clock_screensaver_active_ &&
                                !MenuSystem::IsAnyOpen();
    const std::string_view sound = interaction_voice_.GetVoiceForEvent(event, GetNowMs(), allow_playback);
    if (!sound.empty()) {
        Application::GetInstance().PlaySound(sound);
    }
}

bool EyeDisplay::ShouldRunCareEmotionScheduler() const {
    if (!care_emotion_config_.enabled || !eye_animation_) {
        return false;
    }
    if (clock_screensaver_active_ || MenuSystem::IsAnyOpen()) {
        return false;
    }
    return Application::GetInstance().GetDeviceState() == kDeviceStateIdle;
}

const char* EyeDisplay::SelectCareDrivenEmotion() const {
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
        const auto& rule = kCareEmotionTable[i];
        float weight = rule.base_weight
                     + rule.positive_bias * positive
                     + rule.negative_bias * negative
                     + rule.hunger_need_bias * hunger_need
                     + rule.mood_need_bias * mood_need
                     + rule.energy_need_bias * energy_need
                     + rule.clean_need_bias * clean_need;
        if (weight < 0.0f) {
            weight = 0.0f;
        }
        weights[i] = weight;
        total_weight += weight;
    }

    if (total_weight <= 0.0001f) {
        return "neutral";
    }

    const float pick = (static_cast<float>(esp_random()) / 4294967295.0f) * total_weight;
    float acc = 0.0f;
    for (size_t i = 0; i < kCareEmotionTable.size(); ++i) {
        acc += weights[i];
        if (pick <= acc) {
            return kCareEmotionTable[i].emotion;
        }
    }
    return kCareEmotionTable.back().emotion;
}

const char* EyeDisplay::SelectOverlayEmotionForBase(const std::string& base_emotion) const {
    if (!care_emotion_config_.overlay_enabled || care_emotion_config_.overlay_duration_ms == 0) {
        return nullptr;
    }
    if ((esp_random() % 100U) >= care_emotion_config_.overlay_chance_pct) {
        return nullptr;
    }

    struct OverlayWeight {
        const char* emotion;
        uint8_t weight;
    };
    const auto pick_weighted = [](const OverlayWeight* table, size_t count) -> const char* {
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
        {"surprised", 45}, {"thinking", 35}, {"confused", 20},
    }};
    static constexpr std::array<OverlayWeight, 3> kSadOverlays = {{
        {"thinking", 50}, {"confused", 35}, {"surprised", 15},
    }};
    static constexpr std::array<OverlayWeight, 3> kAngryOverlays = {{
        {"confused", 45}, {"thinking", 40}, {"surprised", 15},
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
    if (!ShouldRunCareEmotionScheduler()) {
        care_next_emotion_change_ms_ = 0;
        care_overlay_until_ms_ = 0;
        care_overlay_emotion_.clear();
        return;
    }

    const uint64_t external_hold_until = last_external_emotion_ms_ + care_emotion_config_.external_override_ms;
    if (now_ms < external_hold_until) {
        care_next_emotion_change_ms_ = external_hold_until;
        care_overlay_until_ms_ = 0;
        care_overlay_emotion_.clear();
        return;
    }

    if (care_next_emotion_change_ms_ == 0) {
        care_base_emotion_ = current_eye_emotion_;
        care_next_emotion_change_ms_ = now_ms + RandomRangeU32(
            care_emotion_config_.min_duration_ms,
            care_emotion_config_.max_duration_ms);
        return;
    }

    if (now_ms < care_next_emotion_change_ms_) {
        if (!care_overlay_emotion_.empty() && now_ms >= care_overlay_until_ms_) {
            care_overlay_emotion_.clear();
            care_overlay_until_ms_ = 0;
            if (!care_base_emotion_.empty() && current_eye_emotion_ != care_base_emotion_) {
                ApplyEmotionInternal(care_base_emotion_.c_str(), false);
            }
        }
        return;
    }

    const char* selected = SelectCareDrivenEmotion();
    if (selected != nullptr) {
        // Avoid long streaks of identical emotion.
        if (care_base_emotion_ == selected) {
            const char* retry = SelectCareDrivenEmotion();
            if (retry != nullptr) {
                selected = retry;
            }
        }
        care_base_emotion_ = selected;
        care_overlay_emotion_.clear();
        care_overlay_until_ms_ = 0;
        ApplyEmotionInternal(care_base_emotion_.c_str(), false);

        const char* overlay = SelectOverlayEmotionForBase(care_base_emotion_);
        if (overlay != nullptr && overlay[0] != '\0') {
            care_overlay_emotion_ = overlay;
            care_overlay_until_ms_ = now_ms + care_emotion_config_.overlay_duration_ms;
            if (care_overlay_emotion_ != care_base_emotion_) {
                ApplyEmotionInternal(care_overlay_emotion_.c_str(), false);
            }
        }

        ESP_LOGD(TAG, "Care emotion pick: base=%s overlay=%s (H:%d M:%d E:%d C:%d)",
                 care_base_emotion_.c_str(),
                 care_overlay_emotion_.empty() ? "-" : care_overlay_emotion_.c_str(),
                 CareSystem::GetHunger(),
                 CareSystem::GetMood(),
                 CareSystem::GetEnergy(),
                 CareSystem::GetCleanliness());
    }

    care_next_emotion_change_ms_ = now_ms + RandomRangeU32(
        care_emotion_config_.min_duration_ms,
        care_emotion_config_.max_duration_ms);
}
