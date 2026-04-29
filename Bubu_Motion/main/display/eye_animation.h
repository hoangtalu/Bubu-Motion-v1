#ifndef EYE_ANIMATION_H
#define EYE_ANIMATION_H

#include <cstdint>
#include <functional>
#include <lvgl.h>

// Forward declaration
class EyeAnimation;

// Apply an emotion string to an EyeAnimation instance.
// Mood is now persistent (no 2-second reset). Stays until overridden.
void EyeEmotion_Apply(const char* emotion, EyeAnimation* anim);

class EyeAnimation {
public:
    struct EyeShape {
        int w = 80;
        int h = 80;
        int radius = 24;
    };

    struct EyeBounds {
        int16_t x = 0;
        int16_t y = 0;
        int16_t w = 0;
        int16_t h = 0;
    };

    enum class InteractionEvent : uint8_t {
        Blink,
        Mischief,
    };
    using InteractionEventCallback = std::function<void(InteractionEvent)>;

    struct MischiefConfig {
        int min_w = 52;
        int max_w = 96;
        int min_h = 34;
        int max_h = 96;
        int min_radius = 0;
        int max_radius = 36;
        uint8_t min_r = 64;
        uint8_t max_r = 255;
        uint8_t min_g = 64;
        uint8_t max_g = 255;
        uint8_t min_b = 64;
        uint8_t max_b = 255;
        uint32_t change_ms = 160;
        uint32_t retreat_ms = 220;
        uint32_t min_stay_ms = 1800;
        uint32_t max_stay_ms = 20000;
        uint8_t both_sync_chance = 35;
        uint8_t both_independent_chance = 30;
        uint8_t left_only_chance = 18;
        uint8_t right_only_chance = 17;
    };

    EyeAnimation();
    ~EyeAnimation();

    // Core lifecycle
    void Init(lv_obj_t* parent, int screen_w, int screen_h);
    void SetVisible(bool visible);

    // Touch
    void HandleTouch(int x, int y);
    bool IsTouchInsideEyes(int x, int y) const;

    // IMU accelerometer data (g units)
    void SetImuAccel(float ax, float ay);

    // ---- RoboEyes-style runtime setters ----

    // Eye shape — smooth lerp to target each frame
    void SetSize(int w, int h);
    void SetBorderRadius(int r);
    void SetSpaceBetween(int gap);
    void SetLeftEyeSize(int w, int h);
    void SetRightEyeSize(int w, int h);
    void SetLeftEyeBorderRadius(int r);
    void SetRightEyeBorderRadius(int r);
    void SetEyeColor(uint8_t r, uint8_t g, uint8_t b);
    void SetLeftEyeColor(uint8_t r, uint8_t g, uint8_t b);
    void SetRightEyeColor(uint8_t r, uint8_t g, uint8_t b);
    EyeShape GetBaseLeftShape() const;
    EyeShape GetBaseRightShape() const;
    void SetBaseLeftShape(const EyeShape& shape);
    void SetBaseRightShape(const EyeShape& shape);
    void PreviewLeftShape(const EyeShape& shape);
    void PreviewRightShape(const EyeShape& shape);
    void ClearPreviewToBase();
    EyeBounds GetLeftEyeBounds() const;
    EyeBounds GetRightEyeBounds() const;
    void SetMischiefEnabled(bool enabled);
    bool IsMischiefEnabled() const { return mischief_enabled_; }
    void SetMischiefConfig(const MischiefConfig& config);
    const MischiefConfig& GetMischiefConfig() const { return mischief_config_; }
    void TriggerMischief();
    bool ConsumePendingInteractionEvent(InteractionEvent event);
    void SetInteractionEventCallback(InteractionEventCallback callback);

    // Mood — persistent until changed
    void SetMood(bool tired, bool angry, bool happy);
    void SetCurious(bool curious);   // outer eye grows when looking far left/right
    void SetCyclops(bool cyclops);   // single centered eye

    // Autoblinker
    void SetAutoblinker(bool active, int interval_s = 2, int variation_s = 3);

    // Idle repositioning
    void SetIdleMode(bool active, int interval_ms = 500, int variation_ms = 500);

    // Flicker
    void SetHFlicker(bool active, int amplitude = 2);
    void SetVFlicker(bool active, int amplitude = 10);

    // Sweat drops
    void SetSweat(bool active);
    void SetSurprised(bool active);
    void SetSkeptic(bool active, bool left_eye = false);
    void SetTiredLidStrength(float strength);
    void SetAngryLidStrength(float strength);
    void SetSkepticLidStrength(float strength);

    // One-shot animations
    void AnimConfused();   // horizontal shake for 500ms
    void AnimLaugh();      // vertical shake for 500ms

private:
    struct SweatDrop {
        float x_initial = 0.0f;
        float x         = 0.0f;
        float y         = 2.0f;
        float y_max     = 12.0f;
        float w         = 1.0f;
        float h         = 2.0f;
    };

    struct EyePose {
        int w = 80;
        int h = 80;
        int radius = 24;
        uint8_t red = 255;
        uint8_t green = 255;
        uint8_t blue = 255;
    };

    enum class MischiefPhase : uint8_t {
        Waiting,
        Changing,
        Holding,
        Retreating,
    };

    // LVGL objects
    lv_obj_t*    container_   = nullptr;
    lv_obj_t*    canvas_      = nullptr;
    lv_color_t*  canvas_buf_  = nullptr;
    lv_timer_t*  anim_timer_  = nullptr;

    int screen_w_ = 240;
    int screen_h_ = 240;

    // ---- Geometry (runtime, lerped each frame via (cur+next)/2) ----
    int eye_l_w_default_ = 80, eye_l_w_current_ = 80, eye_l_w_next_ = 80;
    int eye_l_h_default_ = 80, eye_l_h_current_ = 1,  eye_l_h_next_ = 80;
    int eye_l_r_default_ = 24, eye_l_r_current_ = 24, eye_l_r_next_ = 24;
    int eye_r_w_default_ = 80, eye_r_w_current_ = 80, eye_r_w_next_ = 80;
    int eye_r_h_default_ = 80, eye_r_h_current_ = 1,  eye_r_h_next_ = 80;
    int eye_r_r_default_ = 24, eye_r_r_current_ = 24, eye_r_r_next_ = 24;
    int gap_default_   = 10, gap_current_   = 10, gap_next_   = 10;

    // Curious mode: height offsets applied to outer eye when looking far left/right
    int eye_l_h_offset_ = 0;
    int eye_r_h_offset_ = 0;

    // Eye center position offsets from screen center
    float off_x_        = 0.0f;
    float off_y_        = 0.0f;
    float off_start_x_  = 0.0f;
    float off_start_y_  = 0.0f;
    float target_off_x_ = 0.0f;
    float target_off_y_ = 0.0f;

    // Idle look timing
    uint32_t next_look_ms_     = 0;
    uint32_t look_start_ms_    = 0;
    uint32_t look_duration_ms_ = 0;
    bool     look_active_      = false;
    int      idle_interval_ms_  = 500;
    int      idle_variation_ms_ = 500;
    bool     idle_active_      = true;

    // Flicker offsets applied per-frame during RenderFrame
    int flicker_off_x_ = 0;
    int flicker_off_y_ = 0;

    // Breathing bounce
    float bounce_y_ = 0.0f;

    // Eye scale (happy grow effect)
    float eye_scale_        = 1.0f;
    float target_eye_scale_ = 1.0f;

    // ---- Blink ----
    bool     blink_active_   = false;
    bool     blink_left_     = true;
    bool     blink_right_    = true;
    uint32_t blink_start_ms_ = 0;
    uint32_t next_blink_ms_  = 0;
    int16_t  top_offset_     = 0;

    // ---- Autoblinker ----
    bool autoblinker_       = true;
    int  blink_interval_s_  = 2;
    int  blink_variation_s_ = 3;

    // ---- Mood flags (persistent) ----
    bool tired_   = false;
    bool angry_   = false;
    bool happy_   = false;
    bool curious_ = false;
    bool cyclops_ = false;
    bool surprised_ = false;
    bool skeptic_ = false;
    bool skeptic_left_eye_ = false;
    float tired_lid_strength_ = 0.5f;
    float angry_lid_strength_ = 0.5f;
    float skeptic_lid_strength_ = 0.35f;

    // Eyelid lerp state (RoboEyes (cur+next)/2 pattern)
    float eyelids_tired_h_   = 0.0f, eyelids_tired_h_next_   = 0.0f;
    float eyelids_angry_h_   = 0.0f, eyelids_angry_h_next_   = 0.0f;
    float eyelids_happy_off_ = 0.0f, eyelids_happy_off_next_ = 0.0f;
    float eyelids_skeptic_h_ = 0.0f, eyelids_skeptic_h_next_ = 0.0f;

    // ---- Flicker ----
    bool h_flicker_     = false;
    bool h_flicker_alt_ = false;
    int  h_flicker_amp_ = 2;
    bool v_flicker_     = false;
    bool v_flicker_alt_ = false;
    int  v_flicker_amp_ = 10;

    // ---- One-shot animations ----
    bool     confused_active_   = false;
    bool     confused_toggle_   = true;
    uint32_t confused_timer_ms_ = 0;

    bool     laugh_active_    = false;
    bool     laugh_toggle_    = true;
    uint32_t laugh_timer_ms_  = 0;

    // ---- Sweat drops ----
    bool      sweat_active_ = false;
    SweatDrop sweat_drops_[8];

    // ---- Touch reaction ----
    float    touch_off_x_    = 0.0f, touch_off_y_    = 0.0f;
    float    touch_from_x_   = 0.0f, touch_from_y_   = 0.0f;
    float    touch_target_x_ = 0.0f, touch_target_y_ = 0.0f;
    uint32_t touch_start_ms_ = 0;
    uint32_t touch_peak_ms_  = 0;
    uint32_t touch_end_ms_   = 0;

    // ---- IMU offset ----
    float imu_off_x_    = 0.0f, imu_off_y_    = 0.0f;
    float imu_target_x_ = 0.0f, imu_target_y_ = 0.0f;
    float imu_prev_ax_  = 0.0f, imu_prev_ay_  = 0.0f;
    bool imu_has_prev_  = false;
    uint32_t imu_motion_hold_until_ms_ = 0;
    uint32_t imu_rearm_ms_ = 0;

    // ---- Eye color ----
    float l_cr_ = 255.0f, l_cg_ = 255.0f, l_cb_ = 255.0f;  // current
    float l_tr_ = 255.0f, l_tg_ = 255.0f, l_tb_ = 255.0f;  // target
    float r_cr_ = 255.0f, r_cg_ = 255.0f, r_cb_ = 255.0f;  // current
    float r_tr_ = 255.0f, r_tg_ = 255.0f, r_tb_ = 255.0f;  // target
    uint8_t l_base_red_ = 255, l_base_green_ = 255, l_base_blue_ = 255;
    uint8_t r_base_red_ = 255, r_base_green_ = 255, r_base_blue_ = 255;
    uint32_t color_last_ms_ = 0;

    // ---- Mischief Engine ----
    MischiefConfig mischief_config_;
    bool mischief_enabled_ = true;
    bool mischief_started_ = false;
    MischiefPhase mischief_phase_ = MischiefPhase::Waiting;
    uint32_t mischief_phase_start_ms_ = 0;
    uint32_t mischief_phase_duration_ms_ = 0;
    uint32_t mischief_next_cycle_ms_ = 0;
    EyePose mischief_from_left_;
    EyePose mischief_from_right_;
    EyePose mischief_to_left_;
    EyePose mischief_to_right_;
    bool pending_blink_event_ = false;
    bool pending_mischief_event_ = false;
    InteractionEventCallback interaction_event_callback_;

    // ---- Touch hit-test boxes ----
    EyeBounds left_eye_box_, right_eye_box_;
    EyeBounds left_touch_box_, right_touch_box_;

    // ---- Constants ----
    static constexpr int   CLOSED_HEIGHT  = 6;
    static constexpr int   TOUCH_PAD_X    = 18;
    static constexpr int   TOUCH_PAD_Y    = 24;
    static constexpr float HAPPY_SCALE    = 1.15f;
    static constexpr int   SURPRISED_RADIUS = 32;
    static constexpr int   BOUNCE_AMPL    = 8;

    static constexpr uint32_t BLINK_CLOSE_MS  = 40;
    static constexpr uint32_t BLINK_HOLD_MS   = 20;
    static constexpr uint32_t BLINK_OPEN_MS   = 80;
    static constexpr int16_t  BLINK_OFFSET_PX = 50;

    static constexpr uint32_t EYE_COLOR_FADE_MS = 500;

    // Curious mode: px from edge of idle range to trigger outer eye height boost
    static constexpr float CURIOUS_THRESHOLD = 4.0f;
    static constexpr int   CURIOUS_H_BOOST   = 8;

    // One-shot durations
    static constexpr uint32_t CONFUSED_DURATION_MS = 500;
    static constexpr uint32_t LAUGH_DURATION_MS    = 500;

    // IMU
    static constexpr float IMU_SENSITIVITY = 14.0f;
    static constexpr float IMU_MAX_OFFSET  = 18.0f;
    static constexpr float IMU_LERP_FAST   = 0.65f;
    static constexpr float IMU_LERP_RETURN = 0.09f;
    static constexpr float IMU_DEAD_ZONE   = 0.02f;
    static constexpr float IMU_SHAKE_TRIGGER_G = 0.33f;
    static constexpr uint32_t IMU_MOTION_HOLD_MS = 700;
    static constexpr uint32_t IMU_REARM_DELAY_MS = 1200;

    // ---- Private methods ----
    void CreateEyeObjects(lv_obj_t* parent);
    void Update(uint32_t now_ms);
    void UpdateGeometryLerp();
    void UpdateEyelidLerp();
    void UpdateAutoblinker(uint32_t now_ms);
    void UpdateBlink(uint32_t now_ms);
    void UpdateIdleLook(uint32_t now_ms);
    void UpdateCuriousMode();
    void UpdateFlicker();
    void UpdateConfused(uint32_t now_ms);
    void UpdateLaugh(uint32_t now_ms);
    void UpdateTouchReaction(uint32_t now_ms);
    void UpdateEyeColor(uint32_t now_ms);
    void UpdateMoodColor();
    void UpdateImuOffset();
    void UpdateMischief(uint32_t now_ms);
    void ApplyMischiefPose(const EyePose& left_pose, const EyePose& right_pose);
    void SyncBasePoseTargets();
    EyePose GetBaseLeftPose() const;
    EyePose GetBaseRightPose() const;
    EyePose MakeRandomPoseFromBase(const EyePose& base_pose) const;
    EyePose LerpPose(const EyePose& from, const EyePose& to, float t) const;
    void ClampPose(EyePose* pose) const;
    void ClampShape(EyeShape* shape) const;
    void EmitInteractionEvent(InteractionEvent event);
    void StartMischiefCycle(uint32_t now_ms);
    void FinishMischiefCycle();
    void RenderFrame();
    void ScheduleNextBlink();
    void DrawSweatDrops(lv_layer_t* layer);

    static void TimerCallback(lv_timer_t* timer);
};

#endif // EYE_ANIMATION_H
