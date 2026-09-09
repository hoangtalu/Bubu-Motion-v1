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

    enum class LegacyEmotionMode : uint8_t {
        None,
        Love,
        Cyclop,
        Drunk,
        Confuse,
        Angry,
        Furious,
        BanhChung,
        Deadpool,
        Cry,
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

    // Render policy, driven by the active screen (see ScreenManager).
    // fps == 0 pauses the render timer and hides the container, so both the CPU
    // render and the LCD flush stop. static_pose freezes the idle float, bounce
    // and flicker while leaving the sleep Z particles drifting.
    void SetRenderPolicy(uint8_t fps, bool static_pose);
    uint8_t GetRenderFps() const { return render_fps_; }

    void StartHatching(uint32_t now_ms);
    bool IsHatchingActive() const;

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
    void TriggerGamePlus(bool left_eye);
    void SetGameMode(bool active);
    bool IsGameMode() const { return game_mode_active_; }
    void SetMoodColorAutoEnabled(bool enabled);
    bool IsMoodColorAutoEnabled() const { return mood_color_auto_enabled_; }
    void SetBaseLeftShape(const EyeShape& shape);
    void SetBaseRightShape(const EyeShape& shape);
    void SetMischiefEnabled(bool enabled);
    bool IsMischiefEnabled() const { return mischief_enabled_; }
    void SetMischiefConfig(const MischiefConfig& config);
    const MischiefConfig& GetMischiefConfig() const { return mischief_config_; }
    void TriggerMischief();
    // Extends the current mischief cycle's Holding-phase duration to at
    // least min_hold_ms, so the pose doesn't retreat while a voice line it
    // just triggered is still playing. Takes effect once the Changing phase
    // (the transition into the pose) finishes and Holding's duration is
    // picked; a no-op if that has already happened for this cycle.
    void ExtendMischiefHold(uint32_t min_hold_ms);
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
    void SetLegacyEmotionMode(LegacyEmotionMode mode);
    void SetTiredLidStrength(float strength);
    void SetAngryLidStrength(float strength);
    void SetSkepticLidStrength(float strength);
    void SetSleepMode(bool active);
    bool IsSleepMode() const { return sleep_mode_; }

    // One-shot animations
    void AnimConfused();   // horizontal shake for 500ms
    void AnimLaugh();      // vertical shake for 500ms

    // Bathing — scrub-to-clean sequence driven by the care menu.
    void StartBathing(uint32_t now_ms);
    bool IsBathingActive() const { return bath_.active; }
    bool HandleBathScrub(int x, int y);   // finger drag; true when consumed
    bool HandleBathTap();                 // swallow taps for the duration
    bool ConsumeBathCompleted();          // one-shot: credit cleanliness
    void CancelBathing();
    // Persistent grime shown between baths; driven by the cleanliness stat.
    void SetDirtyLevel(int cleanliness);

    // Feeding — tap-to-chomp sequence driven by the care menu.
    // hunger scales the pacing; hunger >= kFeedFullThreshold plays the refusal branch.
    void StartFeeding(uint32_t now_ms, int hunger);
    bool IsFeedingActive() const { return feed_.active; }
    bool HandleFeedTap(uint32_t now_ms);   // true when the tap was consumed
    int ConsumeFeedBites();                // pending bites for the care system to credit

private:
    struct SweatDrop {
        float x         = 0.0f;
        float y         = 0.0f;
        float w         = 2.0f;
        float h         = 3.0f;
        float speed     = 1.0f;
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

    struct HatchRuntime {
        bool active = false;
        uint8_t phase = 1;
        uint32_t start_ms = 0;
        uint32_t phase_start_ms = 0;
        uint32_t tap_bob_start_ms = 0;
        uint32_t tap_bob_duration_ms = 0;
        float tap_bob_amp = 0.0f;
        uint32_t phase2_boost_until_ms = 0;
        bool moving = false;
        uint32_t move_start_ms = 0;
        uint32_t move_duration_ms = 0;
        uint32_t stop_until_ms = 0;
        float pos_x = 0.0f;
        float pos_y = 0.0f;
        float move_start_x = 0.0f;
        float move_start_y = 0.0f;
        float move_target_x = 0.0f;
        float move_target_y = 0.0f;
        uint32_t twitch_start_ms = 0;
        uint32_t twitch_duration_ms = 0;
        float twitch_x = 0.0f;
        float twitch_y = 0.0f;
        bool blink_started = false;
        uint32_t blink_start_ms = 0;
    };

    struct GamePlusFx {
        bool active = false;
        bool left_eye = true;
        float start_x = 0.0f;
        float start_y = 0.0f;
        uint32_t start_ms = 0;
    };

    enum class BathPhase : uint8_t {
        Enter,
        Scrub,
        Rinse,
        Clean,
        Exit,
    };

    struct FoamBlob {
        bool active = false;
        float x = 0.0f;
        float y = 0.0f;
        float vy = 0.0f;
        uint8_t r = 6;
    };

    struct BathDroplet {
        float x = 0.0f;
        float y = 0.0f;
        float speed = 1.0f;
        uint8_t len = 6;
    };

    struct BathRuntime {
        bool active = false;
        BathPhase phase = BathPhase::Enter;
        uint32_t start_ms = 0;
        uint32_t phase_start_ms = 0;
        uint32_t phase_duration_ms = 0;
        uint32_t last_scrub_ms = 0;
        float head_y = 0.0f;
        float head_from_y = 0.0f;
        float head_to_y = 0.0f;
        // Scrub bookkeeping: accumulated finger travel drives progress.
        float scrub_progress = 0.0f;
        float foam_debt = 0.0f;
        float last_x = 0.0f;
        float last_y = 0.0f;
        bool has_last = false;
        uint8_t smudges_hidden = 0;
        bool completed_pending = false;
    };

    enum class FeedPhase : uint8_t {
        Approach,
        Waiting,
        Chomp,
        Refuse,
        Savor,
        Exit,
    };

    struct FeedCrumb {
        bool active = false;
        float x = 0.0f;
        float y = 0.0f;
        float vx = 0.0f;
        float vy = 0.0f;
        uint8_t size = 2;
        uint32_t start_ms = 0;
    };

    struct FeedRuntime {
        bool active = false;
        FeedPhase phase = FeedPhase::Approach;
        uint32_t start_ms = 0;
        uint32_t phase_start_ms = 0;
        uint32_t phase_duration_ms = 0;
        uint32_t auto_chomp_ms = 0;
        uint8_t kibble_left = 6;
        uint8_t bites_pending = 0;
        bool refuse = false;
        // Dish position: eases from off-screen bottom up to the hover pose.
        float food_y = 0.0f;
        float food_from_y = 0.0f;
        float food_to_y = 0.0f;
        float squash = 1.0f;
        // Pacing / intensity, picked from hunger at StartFeeding.
        uint32_t approach_ms = 500;
        float shake_px = 3.0f;
        float squint = 0.55f;
        // Pre-feed state restored on exit.
        bool prev_tired = false;
        bool prev_angry = false;
        bool prev_happy = false;
        bool prev_idle = true;
        int prev_gap = 10;
    };

    // LVGL objects
    lv_obj_t*    container_   = nullptr;
    lv_obj_t*    canvas_      = nullptr;
    lv_color_t*  canvas_buf_  = nullptr;
    lv_timer_t*  anim_timer_  = nullptr;

    // Render policy (see SetRenderPolicy). Defaults match the timer created in Init().
    uint8_t render_fps_   = 30;
    bool    static_pose_  = false;

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
    float angry_bounce_off_y_ = 0.0f;
    struct ZParticle {
        bool active = false;
        float x = 0.0f;
        float y = 0.0f;
        float start_x = 0.0f;
        float start_y = 0.0f;
        uint32_t start_ms = 0;
        uint32_t duration_ms = 0;
        float x_drift = 0.0f;
    };
    static constexpr int NUM_Z_PARTICLES = 4;
    ZParticle z_particles_[NUM_Z_PARTICLES];
    uint32_t next_z_spawn_ms_ = 0;

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
    bool sleep_mode_ = false;
    LegacyEmotionMode legacy_emotion_mode_ = LegacyEmotionMode::None;
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
    static constexpr int kSweatDropCount = 30;
    SweatDrop sweat_drops_[kSweatDropCount];

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
    bool mood_color_auto_enabled_ = true;
    bool game_mode_active_ = false;
    uint32_t color_last_ms_ = 0;

    // ---- Mischief Engine ----
    MischiefConfig mischief_config_;
    bool mischief_enabled_ = true;
    bool mischief_started_ = false;
    MischiefPhase mischief_phase_ = MischiefPhase::Waiting;
    uint32_t mischief_phase_start_ms_ = 0;
    uint32_t mischief_phase_duration_ms_ = 0;
    uint32_t mischief_next_cycle_ms_ = 0;
    uint32_t mischief_min_hold_extension_ms_ = 0;
    EyePose mischief_from_left_;
    EyePose mischief_from_right_;
    EyePose mischief_to_left_;
    EyePose mischief_to_right_;
    bool pending_blink_event_ = false;
    bool pending_mischief_event_ = false;
    InteractionEventCallback interaction_event_callback_;
    HatchRuntime hatch_;

    // ---- Touch hit-test boxes ----
    EyeBounds left_eye_box_, right_eye_box_;
    EyeBounds left_touch_box_, right_touch_box_;
    static constexpr int kGamePlusFxCount = 6;
    GamePlusFx game_plus_fx_[kGamePlusFxCount];

    // ---- Bathing ----
    static constexpr int kBathFoamCount = 14;
    static constexpr int kBathDropletCount = 22;
    static constexpr int kSmudgeMaxCount = 6;
    BathRuntime bath_;
    FoamBlob bath_foam_[kBathFoamCount];
    BathDroplet bath_droplets_[kBathDropletCount];
    uint8_t dirty_count_ = 0;

    // ---- Feeding ----
    static constexpr int kFeedCrumbCount = 8;
    FeedRuntime feed_;
    FeedCrumb feed_crumbs_[kFeedCrumbCount];
    float feed_squint_px_ = 0.0f;   // extra bottom-lid mask added during a chomp
    float feed_shake_y_ = 0.0f;     // vertical chomp shake added to the eye centre

    void DrawGamePlusFx(lv_layer_t* layer, uint32_t now_ms);

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
    static constexpr uint32_t ANGRY_BOUNCE_PERIOD_MS = 300;
    static constexpr uint32_t SLEEP_BOUNCE_PERIOD_MS = ANGRY_BOUNCE_PERIOD_MS * 2;
    static constexpr uint32_t SLEEP_ZZZ_PERIOD_MS = 2200;
    static constexpr int SLEEP_ZZZ_RISE_PX = 32;
    static constexpr uint32_t HATCH_TOTAL_MS = 300000;
    static constexpr uint32_t HATCH_PHASE1_MS = 60000;
    static constexpr uint32_t HATCH_PHASE2_MS = 90000;
    static constexpr uint32_t HATCH_PHASE3_MS = 120000;
    static constexpr uint32_t HATCH_PHASE4_MS = 30000;
    static constexpr int16_t HATCH_BASE_SIZE = 90;

    // ---- Bathing ----
    static constexpr int kBathHeadY = 22;             // showerhead resting Y
    static constexpr int kBathHeadW = 54;
    static constexpr int kBathHeadH = 12;
    static constexpr float kBathScrubTarget = 500.0f; // px of finger travel
    static constexpr float kBathFoamEveryPx = 40.0f;
    static constexpr uint32_t kBathEnterMs = 400;
    static constexpr uint32_t kBathRinseMs = 1200;
    static constexpr uint32_t kBathCleanMs = 900;
    static constexpr uint32_t kBathExitMs = 300;
    static constexpr uint32_t kBathIdleAdvanceMs = 6000;
    static constexpr uint32_t kBathHardCapMs = 20000;
    // Grime thresholds (cleanliness -> smudge count)
    static constexpr int kDirtyHeavy = 20;
    static constexpr int kDirtyMedium = 40;
    static constexpr int kDirtyLight = 60;

    // ---- Feeding ----
    static constexpr int kFeedKibbleCount = 6;
    static constexpr int kFeedBiteCount = 3;          // 2 kibble per bite
    static constexpr int kFeedFullThreshold = 85;     // at/above this Bubu refuses
    static constexpr int kFeedStarvingThreshold = 30;
    static constexpr int kFeedDishY = 196;            // dish top edge, screen coords
    static constexpr int kFeedDishW = 62;
    static constexpr int kFeedDishH = 20;
    static constexpr int kFeedDishRadius = 8;
    static constexpr int kFeedConvergedGap = 7;
    static constexpr float kFeedEyeScale = 1.10f;
    static constexpr uint32_t kFeedChompMs = 260;
    static constexpr uint32_t kFeedRefuseMs = 900;
    static constexpr uint32_t kFeedSavorMs = 900;
    static constexpr uint32_t kFeedExitMs = 300;
    static constexpr uint32_t kFeedCrumbLifeMs = 380;
    static constexpr uint32_t kFeedHardCapMs = 12000;

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
    void UpdateAngryBounce(uint32_t now_ms);
    void UpdateFlicker();
    void UpdateConfused(uint32_t now_ms);
    void UpdateLaugh(uint32_t now_ms);
    void UpdateTouchReaction(uint32_t now_ms);
    void UpdateEyeColor(uint32_t now_ms);
    void UpdateMoodColor();
    void UpdateImuOffset();
    void UpdateSleepMode(uint32_t now_ms);
    void HatchEnterPhase(uint8_t phase, uint32_t now_ms);
    void HatchHandleTap(uint32_t now_ms);
    void HatchUpdatePhase3(uint32_t now_ms);
    void HatchFinish(uint32_t now_ms);
    void UpdateHatching(uint32_t now_ms);
    void BathEnterPhase(BathPhase phase, uint32_t now_ms);
    void UpdateBathing(uint32_t now_ms);
    void BathSpawnFoam(float x, float y);
    void BathFinish();
    void DrawBathShower(lv_layer_t* layer) const;
    void DrawBathWater(lv_layer_t* layer) const;
    void DrawBathFoam(lv_layer_t* layer) const;
    void DrawSmudges(lv_layer_t* layer) const;
    void FeedEnterPhase(FeedPhase phase, uint32_t now_ms);
    void UpdateFeeding(uint32_t now_ms);
    void FeedTakeBite(uint32_t now_ms);
    void FeedSpawnCrumbs(uint32_t now_ms);
    void FeedFinish();
    void DrawFeedFood(lv_layer_t* layer) const;
    void DrawFeedCrumbs(lv_layer_t* layer, uint32_t now_ms) const;
    void DrawFeedSparkles(lv_layer_t* layer, uint32_t now_ms) const;
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
    void RenderHatchingFrame(uint32_t now_ms);
    void DrawHatchEgg(lv_layer_t* layer, float cx, float cy, float w, float h,
                      float radius, float lobe, bool glow) const;
    void DrawHatchEyes(lv_layer_t* layer, float cx, float cy, float split_t,
                       float base_size, float base_radius,
                       float blink_scale) const;
    void ScheduleNextBlink();
    void DrawSweatDrops(lv_layer_t* layer);
    void DrawSleepZzz(lv_layer_t* layer, uint32_t now_ms, int anchor_y) const;
    void DrawLegacyLove(lv_layer_t* layer, uint32_t now_ms) const;
    void DrawLegacyCyclop(lv_layer_t* layer, uint32_t now_ms) const;
    void DrawLegacyDrunk(lv_layer_t* layer, uint32_t now_ms) const;
    void DrawLegacyConfuse(lv_layer_t* layer, uint32_t now_ms) const;
    void DrawLegacyAngry(lv_layer_t* layer, uint32_t now_ms) const;
    void DrawLegacyFurious(lv_layer_t* layer, uint32_t now_ms) const;
    void DrawLegacyBanhChung(lv_layer_t* layer, uint32_t now_ms) const;
    void DrawLegacyDeadpool(lv_layer_t* layer, uint32_t now_ms) const;
    void DrawLegacyCry(lv_layer_t* layer, uint32_t now_ms) const;

    static void TimerCallback(lv_timer_t* timer);
};

#endif // EYE_ANIMATION_H
