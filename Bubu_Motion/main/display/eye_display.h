#ifndef EYE_DISPLAY_H
#define EYE_DISPLAY_H

#include "lcd_display.h"
#include "eye_animation.h"
#include "bubu_interaction_voice.h"
#include <memory>
#include <string>

/**
 * EyeDisplay: An LcdDisplay subclass that shows animated eyes
 * instead of emoji images for emotions.
 *
 * Inherits all the standard LCD display UI (status bar, bottom bar, etc.)
 * but replaces the emoji_box_ center content with a full-screen animated
 * eye canvas that responds to SetEmotion() calls.
 *
 * All RoboEyes runtime setters are forwarded through public methods here
 * so external code can adjust eye appearance at runtime.
 */
class EyeDisplay : public SpiLcdDisplay {
public:
    using MischiefConfig = EyeAnimation::MischiefConfig;
    using EyeShape = EyeAnimation::EyeShape;
    using EyeBounds = EyeAnimation::EyeBounds;

    EyeDisplay(esp_lcd_panel_io_handle_t io_handle,
               esp_lcd_panel_handle_t panel_handle,
               int width, int height,
               int offset_x, int offset_y,
               bool mirror_x, bool mirror_y, bool swap_xy);

    virtual ~EyeDisplay();

    // Override to create eye animation canvas instead of emoji
    virtual void SetupUI() override;

    // Override to drive eye animation from emotion strings (persistent, no timeout)
    virtual void SetEmotion(const char* emotion) override;

    // Override to force black background when theme is refreshed
    virtual void SetTheme(Theme* theme) override;

    virtual void SetStatus(const char* status) override;
    virtual void ShowNotification(const char* notification, int duration_ms = 3000) override;
    virtual void ShowNotification(const std::string& notification, int duration_ms = 3000) override;
    virtual void SetChatMessage(const char* role, const char* content) override;
    virtual void ClearChatMessages() override;
    virtual void UpdateStatusBar(bool update_all = false) override;

    // Handle raw touch coordinates directly
    virtual void HandleTouch(int x, int y) override;
    bool IsTouchOnEyes(int x, int y) const;
    void NotifyUserInteraction();
    bool DismissClockScreensaver();
    bool IsClockScreensaverActive() const;

    // Feed IMU accelerometer data (in g) to drive real-time eye movement
    void SetImuAccel(float ax, float ay);

    // ---- RoboEyes runtime setters (forwarded to EyeAnimation) ----

    // Eye shape — each change smoothly lerps to the new target
    void SetEyeSize(int w, int h);
    void SetEyeBorderRadius(int r);
    void SetEyeSpaceBetween(int gap);
    void SetLeftEyeSize(int w, int h);
    void SetRightEyeSize(int w, int h);
    void SetLeftEyeBorderRadius(int r);
    void SetRightEyeBorderRadius(int r);
    void SetEyeColor(uint8_t r, uint8_t g, uint8_t b);
    void SetLeftEyeColor(uint8_t r, uint8_t g, uint8_t b);
    void SetRightEyeColor(uint8_t r, uint8_t g, uint8_t b);
    EyeShape GetBaseLeftEyeShape() const;
    EyeShape GetBaseRightEyeShape() const;
    void SetBaseLeftEyeShape(const EyeShape& shape);
    void SetBaseRightEyeShape(const EyeShape& shape);
    void PreviewLeftEyeShape(const EyeShape& shape);
    void PreviewRightEyeShape(const EyeShape& shape);
    void ClearEyePreviewToBase();
    EyeBounds GetLeftEyeBounds() const;
    EyeBounds GetRightEyeBounds() const;
    void SetEyeMischiefEnabled(bool enabled);
    bool IsEyeMischiefEnabled() const;
    void SetEyeMischiefConfig(const MischiefConfig& config);
    const MischiefConfig& GetEyeMischiefConfig() const;
    void TriggerEyeMischief();
    void HandleEyeTapMischief();
    void PlayTapVoice();

    // Mood — persistent until changed; affects eyelid shape and eye color
    void SetEyeMood(bool tired, bool angry, bool happy);
    void SetEyeCurious(bool curious);   // outer eye grows when looking far left/right
    void SetEyeCyclops(bool cyclops);   // merge into one centered eye

    // Autoblinker
    void SetEyeAutoblinker(bool active, int interval_s = 2, int variation_s = 3);

    // Idle repositioning
    void SetEyeIdleMode(bool active, int interval_ms = 500, int variation_ms = 500);

    // Flicker effects
    void SetEyeHFlicker(bool active, int amplitude = 2);
    void SetEyeVFlicker(bool active, int amplitude = 10);

    // Animated sweat drops
    void SetEyeSweat(bool active);

    // One-shot animations
    void EyeAnimConfused();   // horizontal shake for 500ms
    void EyeAnimLaugh();      // vertical shake for 500ms

private:
    struct CareEmotionConfig {
        bool enabled = true;
        uint32_t min_duration_ms = 1000;
        uint32_t max_duration_ms = 3000;
        bool overlay_enabled = true;
        uint32_t overlay_duration_ms = 1300;
        uint8_t overlay_chance_pct = 45;
        uint32_t external_override_ms = 4000;
    };

    std::unique_ptr<EyeAnimation> eye_animation_;
    lv_obj_t* clock_screensaver_ = nullptr;
    lv_obj_t* clock_time_label_ = nullptr;
    lv_obj_t* clock_date_label_ = nullptr;
    uint64_t last_user_interaction_ms_ = 0;
    uint64_t last_clock_refresh_ms_ = 0;
    bool clock_screensaver_active_ = false;

    static constexpr uint32_t kClockRefreshMs = 1000;
    static constexpr uint32_t kClockIdleTimeoutMs = 5 * 60 * 1000;
    CareEmotionConfig care_emotion_config_;
    uint64_t care_next_emotion_change_ms_ = 0;
    uint64_t care_overlay_until_ms_ = 0;
    uint64_t last_external_emotion_ms_ = 0;
    std::string care_base_emotion_ = "neutral";
    std::string care_overlay_emotion_;
    std::string current_eye_emotion_ = "neutral";

    void CreateClockScreensaver(lv_obj_t* parent);
    void UpdateClockScreensaver(uint64_t now_ms);
    void UpdateClockLabels(uint64_t now_ms);
    void ShowClockScreensaver();
    void HideClockScreensaver();
    bool CanShowClockScreensaver() const;
    void UpdateMischiefEngineState();
    void HandlePendingInteractionVoices();
    void MaybePlayInteractionVoice(BubuInteractionEvent event);
    void UpdateCareEmotionScheduler(uint64_t now_ms);
    bool ShouldRunCareEmotionScheduler() const;
    const char* SelectCareDrivenEmotion() const;
    const char* SelectOverlayEmotionForBase(const std::string& base_emotion) const;
    void ApplyEmotionInternal(const char* emotion, bool is_external);
    static uint64_t GetNowMs();

    BubuInteractionVoice interaction_voice_;

protected:
    virtual bool AllowIdleClockStatus() const override { return false; }
};

#endif // EYE_DISPLAY_H
