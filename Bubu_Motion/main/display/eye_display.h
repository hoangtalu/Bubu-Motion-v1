#ifndef EYE_DISPLAY_H
#define EYE_DISPLAY_H

#include "lcd_display.h"
#include "eye_animation.h"
#include "bubu_interaction_voice.h"
#include "screen_manager.h"
#include "chat_subtitle.h"
#include "care_model.h"
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
    bool IsHatchingActive() const;
    bool HandleHatchingTap(int x, int y);
    void NotifyUserInteraction();

    // Re-derive which screen is active and publish it to ScreenManager, which
    // applies the screen's render policy. Called from the main event loop after
    // input handlers run and on the 1 Hz clock tick; cheap and idempotent.
    void RefreshScreen() override;

    bool DismissClockScreensaver();
    bool IsClockScreensaverActive() const;
    bool StartSleepMode();
    void StopSleepMode();
    bool IsSleepModeActive() const { return sleep_mode_active_; }

    // NGỦ, from the care menu or the moon bubble: in the bed window this is
    // the night (the bed anchor), any other time a nap. False if Bubu cannot
    // sleep right now (a conversation, hatching).
    bool PutToBed();

    // The care thought bubble (docs/care-system-plan.md §3.6). Checked before
    // "tap outside the eyes opens the menu".
    bool IsTouchOnCareBubble(int x, int y) const;
    enum class CareBubbleTap : uint8_t { None, Handled, StartChat };
    CareBubbleTap HandleCareBubbleTap();

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
    void SetBaseLeftEyeShape(const EyeShape& shape);
    void SetBaseRightEyeShape(const EyeShape& shape);
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

    // Feeding (tap-to-chomp); pacing scales with the current hunger stat.
    void StartFeeding();
    bool IsFeedingActive() const;
    bool HandleFeedTap();

    // Bathing (scrub-to-clean).
    void StartBathing();
    bool IsBathingActive() const;
    bool HandleBathScrub(int x, int y);
    bool HandleBathTap();
    void CancelBathing();

private:
    struct CareEmotionConfig {
        // Off since 2026-09-29 (user): the random emotion "carousel" is
        // replaced by the idle behaviours in EyeAnimation (look-around,
        // side-matched tilt/gaze, occasional nod/shake). Code kept.
        bool enabled = false;
        uint32_t min_duration_ms = 1000;
        uint32_t max_duration_ms = 3000;
        uint32_t legacy_min_duration_ms = 3000;
        uint32_t legacy_max_duration_ms = 8000;
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

    // The clock only displays hours and minutes, so one refresh per minute
    // avoids waking the LCD path every second in the deepest screen stage.
    static constexpr uint32_t kClockRefreshMs = 60 * 1000;
    // Two-stage screensaver:
    //   5 min  -> sleeping eyes at 4 FPS and 50% brightness
    //   15 min -> clock/date with the eye renderer fully stopped
    static constexpr uint32_t kClockIdleTimeoutMs = 15 * 60 * 1000;
    CareEmotionConfig care_emotion_config_;
    // How long an emotion from Gemini/a game/a voice command stays once the
    // device is idle, before the eyes return to neutral (carousel off).
    static constexpr uint64_t kEmotionReturnMs = 6000;
    uint64_t care_next_emotion_change_ms_ = 0;
    uint64_t care_overlay_until_ms_ = 0;
    uint64_t last_external_emotion_ms_ = 0;
    // ---- Care expression (UpdateCareExpression) ----
    uint64_t care_last_ask_poll_ms_ = 0;
    int care_stage_seen_ = -1;
    int care_gesture_chance_ = -1;     // EyeAnimation's own idle-gesture odds
    int care_weights_[8] = {};         // last mood odds handed to EyeAnimation
    bool care_sleepy_look_ = false;    // the bedtime doze look is ours to undo
    std::string care_base_emotion_ = "neutral";
    std::string care_overlay_emotion_;
    std::string current_eye_emotion_ = "neutral";
    std::string sleep_resume_emotion_ = "neutral";
    bool hatch_was_active_ = false;
    bool hatch_marked_done_ = false;
    bool sleep_mode_active_ = false;
    uint8_t sleep_restore_brightness_ = 75;
    // ---- Status chrome (top-of-screen state readout) ----
    lv_obj_t* status_arc_ = nullptr;   // state indicator hugging the top bezel
    lv_obj_t* bottom_icons_ = nullptr; // wifi / mute / battery row
    lv_timer_t* status_chrome_timer_ = nullptr;
    // Smooth-motion driver for the breathing arc and the voice waves; paused
    // whenever neither is animating.
    lv_timer_t* status_anim_timer_ = nullptr;
    // Voice waves beside the eyes: [side][ring], ring 0 innermost.
    lv_obj_t* voice_waves_[2][3] = {};
    int voice_waves_inward_ = -1;  // -1 = not yet placed
    std::string status_base_text_;
    bool status_busy_ = false;        // append animated waiting dots
    uint8_t status_ellipsis_phase_ = 0;
    // ---- Chat subtitles (what Bubu says, a few words at a time) ----
    lv_obj_t* subtitle_label_ = nullptr;
    lv_timer_t* subtitle_timer_ = nullptr;
    ChatSubtitle subtitle_;
    std::string subtitle_shown_;

    void CreateClockScreensaver(lv_obj_t* parent);
    void UpdateClockScreensaver(uint64_t now_ms);
    void UpdateClockLabels(uint64_t now_ms);
    void UpdateSleepMode(uint64_t now_ms);
    void ShowClockScreensaver();
    void HideClockScreensaver();
    bool CanShowClockScreensaver() const;
    void UpdateMischiefEngineState();
    // Device state -> EyeAnimation::DeviceLook (connecting/listening/speaking).
    void SyncDeviceLook();
    ScreenManager::ScreenId DeriveScreen() const;
    void ApplyScreenPolicy(ScreenManager::ScreenId screen);
    int screen_listener_id_ = -1;
    void HandlePendingInteractionVoices();
    void MaybePlayInteractionVoice(BubuInteractionEvent event);
    void UpdateCareEmotionScheduler(uint64_t now_ms);
    bool ShouldRunCareEmotionScheduler() const;
    const char* SelectCareDrivenEmotion() const;
    const char* SelectOverlayEmotionForBase(const std::string& base_emotion) const;
    void ApplyEmotionInternal(const char* emotion, bool is_external);
    void UpdateHatchingPersistence();
  void DrainFeedBites();
  void UpdateBathState();
    bool CanStartSleep() const;
    void UpdateCareExpression(uint64_t now_ms);
    void PlayCareAsk(care::Need need);
    void SetupStatusChrome();
    void RenderStatusText();
    void UpdateStatusArcColor();
    void StatusChromeTick();
    static void StatusChromeTimerCb(lv_timer_t* timer);
    void SetupVoiceWaves();
    void PlaceVoiceWaves(bool inward);
    void StatusAnimTick();
    static void StatusAnimTimerCb(lv_timer_t* timer);
    void SetupSubtitle();
    void SubtitleTick();
    static void SubtitleTimerCb(lv_timer_t* timer);
    static int MeasureSubtitleText(const std::string& text);
    static uint64_t GetNowMs();

    BubuInteractionVoice interaction_voice_;

    static constexpr uint32_t kSleepIdleTimeoutMs = 5 * 60 * 1000;
    // How often a voice ask is considered; CareSystem rations them (4 a day,
    // 45 minutes apart, only with someone around).
    static constexpr uint32_t kCareAskPollMs = 10 * 1000;
    static constexpr uint8_t kSleepBrightnessPct = 50;
    static constexpr uint32_t kStatusChromeTickMs = 250;
    static constexpr int kStatusArcSize = 210;   // radius ~103, clears the bezel
    static constexpr int kStatusArcWidth = 4;
    static constexpr int kStatusArcStart = 250;  // degrees; 270 = top of screen
    static constexpr int kStatusArcEnd = 290;
    static constexpr uint32_t kStatusAnimTickMs = 50;
    static constexpr uint32_t kArcBreathPeriodMs = 1600;
    static constexpr uint32_t kVoiceWavePeriodMs = 1000;
    // Rings sit just outside the default 80px eyes (half-width 40, gap 10),
    // centred on each eye; the outer ring stays ~15px inside the round glass.
    static constexpr int kVoiceWaveEyeOffsetX = 45;
    static constexpr int kVoiceWaveRadius[3] = {50, 58, 66};
    static constexpr int kVoiceWaveWidth = 3;
    static constexpr int kVoiceWaveHalfSpan = 28;  // degrees either side of horizontal
    static constexpr int kStatusBarTopY = 40;  // measured: keeps the row inside the bezel
    // Subtitle pill: y=163..193, clear of the wifi/mute/battery row (y=211..233).
    // Measured with tools/fit.py against lv_font_montserrat_vn_20: a full 180px
    // chunk's ink (y~166..190, tallest diacritics included) has its worst corner
    // at r=114.3, 5.7px inside the glass; the pill's own corners reach r~119.
    static constexpr int kSubtitleBottomY = 194;
    static constexpr int kSubtitleMaxTextWidth = 180;
    static constexpr int kSubtitlePadX = 8;
    static constexpr int kSubtitlePadY = 2;
    static constexpr uint32_t kSubtitleTickMs = 100;

protected:
    virtual bool AllowIdleClockStatus() const override { return false; }
};

#endif // EYE_DISPLAY_H
