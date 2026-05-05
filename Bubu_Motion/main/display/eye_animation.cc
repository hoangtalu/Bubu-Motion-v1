#include "eye_animation.h"

#include <cmath>
#include <cstring>
#include <algorithm>
#include <utility>

#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_random.h>

#define TAG "EyeAnimation"

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------
namespace {

int RandomInt(int lo, int hi) {
    if (lo >= hi) return lo;
    return lo + static_cast<int>(esp_random() % static_cast<uint32_t>(hi - lo + 1));
}

float ClampFloat(float v, float lo, float hi) {
    return v < lo ? lo : v > hi ? hi : v;
}

int16_t RoundToInt(float v) {
    return static_cast<int16_t>(v >= 0.0f ? v + 0.5f : v - 0.5f);
}

float EaseOutCubic(float t) {
    float inv = 1.0f - t;
    return 1.0f - inv * inv * inv;
}

float EaseInOutCubic(float t) {
    if (t < 0.5f) return 4.0f * t * t * t;
    float inv = -2.0f * t + 2.0f;
    return 1.0f - inv * inv * inv / 2.0f;
}

float Lerp(float a, float b, float t) { return a + (b - a) * t; }

// RoboEyes-style half-step lerp: cur = (cur + next) / 2
inline void HalfStep(float& cur, float next) { cur = (cur + next) / 2.0f; }
inline void HalfStepI(int& cur, int next) { cur = (cur + next) / 2; }

} // namespace

// ---------------------------------------------------------------------------
// EyeEmotion_Apply  (replaces old EyeEmotion_FromString + SetEmotion(enum))
// ---------------------------------------------------------------------------
void EyeEmotion_Apply(const char* emotion, EyeAnimation* anim) {
    if (!emotion || !anim) return;

    // Reset sweat — re-enabled only for matching emotions
    anim->SetSweat(false);
    anim->SetSurprised(false);
    anim->SetSkeptic(false);
    anim->SetTiredLidStrength(0.5f);
    anim->SetAngryLidStrength(0.5f);
    anim->SetSkepticLidStrength(0.35f);

    if (strcmp(emotion, "neutral") == 0 || strcmp(emotion, "relaxed") == 0 ||
        strcmp(emotion, "cool") == 0) {
        anim->SetMood(false, false, false);
        anim->SetCurious(false);
        return;
    }
    if (strcmp(emotion, "happy") == 0 || strcmp(emotion, "funny") == 0) {
        anim->SetMood(false, false, true);
        return;
    }
    if (strcmp(emotion, "laughing") == 0 || strcmp(emotion, "confident") == 0 ||
        strcmp(emotion, "loving") == 0  || strcmp(emotion, "kissy") == 0 ||
        strcmp(emotion, "delicious") == 0 || strcmp(emotion, "shocked") == 0) {
        anim->SetMood(false, false, true);
        anim->AnimLaugh();
        return;
    }
    if (strcmp(emotion, "surprised") == 0) {
        anim->SetMood(false, false, false);
        anim->SetCurious(false);
        anim->SetSurprised(true);
        return;
    }
    if (strcmp(emotion, "sad") == 0 || strcmp(emotion, "crying") == 0) {
        anim->SetMood(true, false, false);
        anim->SetTiredLidStrength(0.55f);   // Steeper than worried.
        return;
    }
    if (strcmp(emotion, "worried") == 0) {
        anim->SetMood(true, false, false);
        anim->SetTiredLidStrength(0.30f);   // Shallower than sad.
        return;
    }
    if (strcmp(emotion, "embarrassed") == 0) {
        anim->SetMood(true, false, false);
        anim->SetSweat(true);
        return;
    }
    if (strcmp(emotion, "nervous") == 0 || strcmp(emotion, "anxious") == 0) {
        anim->SetMood(false, false, false);
        anim->SetSweat(true);
        return;
    }
    if (strcmp(emotion, "angry") == 0) {
        anim->SetMood(false, true, false);
        anim->SetAngryLidStrength(0.55f);   // Steeper than annoyed.
        return;
    }
    if (strcmp(emotion, "annoyed") == 0) {
        anim->SetMood(false, true, false);
        anim->SetAngryLidStrength(0.30f);   // Shallower than angry.
        return;
    }
    if (strcmp(emotion, "sleepy") == 0) {
        anim->SetMood(true, false, false);
        return;
    }
    if (strcmp(emotion, "thinking") == 0 || strcmp(emotion, "winking") == 0 ||
        strcmp(emotion, "silly") == 0) {
        anim->SetCurious(true);
        return;
    }
    if (strcmp(emotion, "skeptic") == 0 || strcmp(emotion, "skeptical") == 0) {
        anim->SetMood(false, false, false);
        anim->SetCurious(false);
        // Skeptic: asymmetrical single-eye inner-corner top lid.
        // Randomize side so skeptic can appear on either eye.
        anim->SetSkeptic(true, RandomInt(0, 1) == 0);
        anim->SetSkepticLidStrength(0.30f); // Shallower than doubt.
        return;
    }
    if (strcmp(emotion, "doubt") == 0 || strcmp(emotion, "doubtful") == 0) {
        anim->SetMood(false, false, false);
        anim->SetCurious(false);
        anim->SetSkeptic(true, RandomInt(0, 1) == 0);
        anim->SetSkepticLidStrength(0.55f); // Steeper than skeptic.
        return;
    }
    if (strcmp(emotion, "confused") == 0) {
        anim->AnimConfused();
        return;
    }
    // Unknown emotion — go neutral
    anim->SetMood(false, false, false);
    anim->SetCurious(false);
}

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------
EyeAnimation::EyeAnimation() {
    // Init sweat drop positions — y starts at 12 (10px lower than before)
    for (auto& d : sweat_drops_) {
        d = {0.0f, 0.0f, 12.0f, 22.0f, 1.0f, 2.0f};
    }
}

EyeAnimation::~EyeAnimation() {
    if (anim_timer_) { lv_timer_delete(anim_timer_); anim_timer_ = nullptr; }
    if (canvas_buf_) { heap_caps_free(canvas_buf_); canvas_buf_ = nullptr; }
}

// ---------------------------------------------------------------------------
// Init
// ---------------------------------------------------------------------------
void EyeAnimation::Init(lv_obj_t* parent, int sw, int sh) {
    screen_w_ = sw;
    screen_h_ = sh;

    // Spread 8 sweat drops evenly across screen width
    for (int i = 0; i < 8; i++) {
        sweat_drops_[i].x_initial = static_cast<float>(sw * (i + 1) / 9);
        sweat_drops_[i].x         = sweat_drops_[i].x_initial;
        sweat_drops_[i].y         = 12.0f + static_cast<float>(RandomInt(0, 20));
        sweat_drops_[i].y_max     = static_cast<float>(RandomInt(20, 35));
    }

    CreateEyeObjects(parent);
    SyncBasePoseTargets();
    ScheduleNextBlink();
    next_look_ms_ = lv_tick_get() + static_cast<uint32_t>(
        idle_interval_ms_ + RandomInt(0, idle_variation_ms_));
    mischief_next_cycle_ms_ = lv_tick_get() + RandomInt(
        static_cast<int>(mischief_config_.min_stay_ms),
        static_cast<int>(mischief_config_.max_stay_ms));
    anim_timer_   = lv_timer_create(TimerCallback, 33, this);
    RenderFrame();
    ESP_LOGI(TAG, "RoboEyes initialized (%dx%d)", sw, sh);
}

void EyeAnimation::CreateEyeObjects(lv_obj_t* parent) {
    container_ = lv_obj_create(parent);
    lv_obj_set_size(container_, screen_w_, screen_h_);
    lv_obj_align(container_, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(container_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(container_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(container_, 0, 0);
    lv_obj_set_style_pad_all(container_, 0, 0);
    lv_obj_set_scrollbar_mode(container_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(container_, LV_OBJ_FLAG_SCROLLABLE);

    canvas_ = lv_canvas_create(container_);
    lv_obj_set_size(canvas_, screen_w_, screen_h_);
    lv_obj_align(canvas_, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_border_width(canvas_, 0, 0);
    lv_obj_set_style_pad_all(canvas_, 0, 0);

    size_t bytes = static_cast<size_t>(screen_w_) * screen_h_ * sizeof(lv_color_t);
    canvas_buf_ = static_cast<lv_color_t*>(
        heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!canvas_buf_) {
        canvas_buf_ = static_cast<lv_color_t*>(heap_caps_malloc(bytes, MALLOC_CAP_8BIT));
    }
    if (!canvas_buf_) {
        ESP_LOGE(TAG, "Failed to allocate eye canvas buffer");
        return;
    }
    lv_canvas_set_buffer(canvas_, canvas_buf_, screen_w_, screen_h_, LV_COLOR_FORMAT_RGB565);
}

// ---------------------------------------------------------------------------
// Public setters
// ---------------------------------------------------------------------------
void EyeAnimation::SetVisible(bool visible) {
    if (!container_) return;
    if (visible) lv_obj_remove_flag(container_, LV_OBJ_FLAG_HIDDEN);
    else         lv_obj_add_flag(container_,    LV_OBJ_FLAG_HIDDEN);
}

void EyeAnimation::SetSize(int w, int h) {
    SetBaseLeftShape({w, h, eye_l_r_default_});
    SetBaseRightShape({w, h, eye_r_r_default_});
}

void EyeAnimation::SetBorderRadius(int r) {
    SetBaseLeftShape({eye_l_w_default_, eye_l_h_default_, r});
    SetBaseRightShape({eye_r_w_default_, eye_r_h_default_, r});
}

void EyeAnimation::SetSpaceBetween(int gap) {
    gap_next_ = gap; gap_default_ = gap;
}

void EyeAnimation::SetLeftEyeSize(int w, int h) {
    SetBaseLeftShape({w, h, eye_l_r_default_});
}

void EyeAnimation::SetRightEyeSize(int w, int h) {
    SetBaseRightShape({w, h, eye_r_r_default_});
}

void EyeAnimation::SetLeftEyeBorderRadius(int r) {
    SetBaseLeftShape({eye_l_w_default_, eye_l_h_default_, r});
}

void EyeAnimation::SetRightEyeBorderRadius(int r) {
    SetBaseRightShape({eye_r_w_default_, eye_r_h_default_, r});
}

void EyeAnimation::SetEyeColor(uint8_t r, uint8_t g, uint8_t b) {
    SetLeftEyeColor(r, g, b);
    SetRightEyeColor(r, g, b);
}

void EyeAnimation::SetLeftEyeColor(uint8_t r, uint8_t g, uint8_t b) {
    l_base_red_ = r;
    l_base_green_ = g;
    l_base_blue_ = b;
    l_tr_ = static_cast<float>(r);
    l_tg_ = static_cast<float>(g);
    l_tb_ = static_cast<float>(b);
}

void EyeAnimation::SetRightEyeColor(uint8_t r, uint8_t g, uint8_t b) {
    r_base_red_ = r;
    r_base_green_ = g;
    r_base_blue_ = b;
    r_tr_ = static_cast<float>(r);
    r_tg_ = static_cast<float>(g);
    r_tb_ = static_cast<float>(b);
}

EyeAnimation::EyeShape EyeAnimation::GetBaseLeftShape() const {
    return {eye_l_w_default_, eye_l_h_default_, eye_l_r_default_};
}

EyeAnimation::EyeShape EyeAnimation::GetBaseRightShape() const {
    return {eye_r_w_default_, eye_r_h_default_, eye_r_r_default_};
}

void EyeAnimation::SetBaseLeftShape(const EyeShape& shape) {
    EyeShape clamped = shape;
    ClampShape(&clamped);
    eye_l_w_default_ = clamped.w;
    eye_l_h_default_ = clamped.h;
    eye_l_r_default_ = clamped.radius;
    eye_l_w_current_ = clamped.w;
    eye_l_h_current_ = clamped.h;
    eye_l_r_current_ = clamped.radius;
    eye_l_w_next_ = clamped.w;
    eye_l_h_next_ = clamped.h;
    eye_l_r_next_ = clamped.radius;
}

void EyeAnimation::SetBaseRightShape(const EyeShape& shape) {
    EyeShape clamped = shape;
    ClampShape(&clamped);
    eye_r_w_default_ = clamped.w;
    eye_r_h_default_ = clamped.h;
    eye_r_r_default_ = clamped.radius;
    eye_r_w_current_ = clamped.w;
    eye_r_h_current_ = clamped.h;
    eye_r_r_current_ = clamped.radius;
    eye_r_w_next_ = clamped.w;
    eye_r_h_next_ = clamped.h;
    eye_r_r_next_ = clamped.radius;
}

void EyeAnimation::PreviewLeftShape(const EyeShape& shape) {
    EyeShape clamped = shape;
    ClampShape(&clamped);
    eye_l_w_next_ = clamped.w;
    eye_l_h_next_ = clamped.h;
    eye_l_r_next_ = clamped.radius;
}

void EyeAnimation::PreviewRightShape(const EyeShape& shape) {
    EyeShape clamped = shape;
    ClampShape(&clamped);
    eye_r_w_next_ = clamped.w;
    eye_r_h_next_ = clamped.h;
    eye_r_r_next_ = clamped.radius;
}

void EyeAnimation::ClearPreviewToBase() {
    eye_l_w_current_ = eye_l_w_default_;
    eye_l_h_current_ = eye_l_h_default_;
    eye_l_r_current_ = eye_l_r_default_;
    eye_r_w_current_ = eye_r_w_default_;
    eye_r_h_current_ = eye_r_h_default_;
    eye_r_r_current_ = eye_r_r_default_;
    eye_l_w_next_ = eye_l_w_default_;
    eye_l_h_next_ = eye_l_h_default_;
    eye_l_r_next_ = eye_l_r_default_;
    eye_r_w_next_ = eye_r_w_default_;
    eye_r_h_next_ = eye_r_h_default_;
    eye_r_r_next_ = eye_r_r_default_;
}

EyeAnimation::EyeBounds EyeAnimation::GetLeftEyeBounds() const {
    return left_eye_box_;
}

EyeAnimation::EyeBounds EyeAnimation::GetRightEyeBounds() const {
    return right_eye_box_;
}

void EyeAnimation::SetMischiefEnabled(bool enabled) {
    if (mischief_enabled_ == enabled) {
        return;
    }

    mischief_enabled_ = enabled;
    if (!mischief_enabled_) {
        FinishMischiefCycle();
        return;
    }

    mischief_next_cycle_ms_ = lv_tick_get() + RandomInt(
        static_cast<int>(mischief_config_.min_stay_ms),
        static_cast<int>(mischief_config_.max_stay_ms));
}

void EyeAnimation::SetMischiefConfig(const MischiefConfig& config) {
    mischief_config_ = config;
    if (mischief_config_.min_w > mischief_config_.max_w) {
        std::swap(mischief_config_.min_w, mischief_config_.max_w);
    }
    if (mischief_config_.min_h > mischief_config_.max_h) {
        std::swap(mischief_config_.min_h, mischief_config_.max_h);
    }
    if (mischief_config_.min_radius > mischief_config_.max_radius) {
        std::swap(mischief_config_.min_radius, mischief_config_.max_radius);
    }
    if (mischief_config_.min_r > mischief_config_.max_r) {
        std::swap(mischief_config_.min_r, mischief_config_.max_r);
    }
    if (mischief_config_.min_g > mischief_config_.max_g) {
        std::swap(mischief_config_.min_g, mischief_config_.max_g);
    }
    if (mischief_config_.min_b > mischief_config_.max_b) {
        std::swap(mischief_config_.min_b, mischief_config_.max_b);
    }
    if (mischief_config_.min_stay_ms > mischief_config_.max_stay_ms) {
        std::swap(mischief_config_.min_stay_ms, mischief_config_.max_stay_ms);
    }
    if (mischief_config_.change_ms == 0) {
        mischief_config_.change_ms = 1;
    }
    if (mischief_config_.retreat_ms == 0) {
        mischief_config_.retreat_ms = 1;
    }
}

void EyeAnimation::TriggerMischief() {
    uint32_t now_ms = lv_tick_get();
    StartMischiefCycle(now_ms);
}

bool EyeAnimation::ConsumePendingInteractionEvent(InteractionEvent event) {
    switch (event) {
        case InteractionEvent::Blink: {
            bool pending = pending_blink_event_;
            pending_blink_event_ = false;
            return pending;
        }
        case InteractionEvent::Mischief: {
            bool pending = pending_mischief_event_;
            pending_mischief_event_ = false;
            return pending;
        }
    }
    return false;
}

void EyeAnimation::SetInteractionEventCallback(InteractionEventCallback callback) {
    interaction_event_callback_ = std::move(callback);
}

void EyeAnimation::EmitInteractionEvent(InteractionEvent event) {
    if (interaction_event_callback_) {
        interaction_event_callback_(event);
        return;
    }

    switch (event) {
        case InteractionEvent::Blink:
            pending_blink_event_ = true;
            break;
        case InteractionEvent::Mischief:
            pending_mischief_event_ = true;
            break;
    }
}

void EyeAnimation::SetMood(bool tired, bool angry, bool happy) {
    tired_ = tired; angry_ = angry; happy_ = happy;
    UpdateMoodColor();
    target_eye_scale_ = happy_ ? HAPPY_SCALE : 1.0f;
}

void EyeAnimation::SetCurious(bool curious) { curious_ = curious; }
void EyeAnimation::SetCyclops(bool cyclops) { cyclops_ = cyclops; }

void EyeAnimation::SetAutoblinker(bool active, int interval_s, int variation_s) {
    autoblinker_       = active;
    blink_interval_s_  = interval_s;
    blink_variation_s_ = variation_s;
}

void EyeAnimation::SetIdleMode(bool active, int interval_ms, int variation_ms) {
    idle_active_       = active;
    idle_interval_ms_  = std::max(0, interval_ms);
    idle_variation_ms_ = std::max(0, variation_ms);
}

void EyeAnimation::SetHFlicker(bool active, int amplitude) {
    h_flicker_     = active;
    h_flicker_amp_ = amplitude;
}

void EyeAnimation::SetVFlicker(bool active, int amplitude) {
    v_flicker_     = active;
    v_flicker_amp_ = amplitude;
}

void EyeAnimation::SetSweat(bool active) { sweat_active_ = active; }
void EyeAnimation::SetSurprised(bool active) { surprised_ = active; }
void EyeAnimation::SetSkeptic(bool active, bool left_eye) {
    skeptic_ = active;
    skeptic_left_eye_ = left_eye;
}
void EyeAnimation::SetTiredLidStrength(float strength) {
    tired_lid_strength_ = ClampFloat(strength, 0.0f, 1.0f);
}
void EyeAnimation::SetAngryLidStrength(float strength) {
    angry_lid_strength_ = ClampFloat(strength, 0.0f, 1.0f);
}
void EyeAnimation::SetSkepticLidStrength(float strength) {
    skeptic_lid_strength_ = ClampFloat(strength, 0.0f, 1.0f);
}

void EyeAnimation::AnimConfused() {
    confused_active_ = true;
    confused_toggle_ = true;
}

void EyeAnimation::AnimLaugh() {
    laugh_active_ = true;
    laugh_toggle_ = true;
}

void EyeAnimation::SetImuAccel(float ax, float ay) {
    uint32_t now_ms = lv_tick_get();

    if (!imu_has_prev_) {
        imu_prev_ax_ = ax;
        imu_prev_ay_ = ay;
        imu_has_prev_ = true;
        return;
    }

    float delta_x = ax - imu_prev_ax_;
    float delta_y = ay - imu_prev_ay_;
    float delta_mag = std::sqrt(delta_x * delta_x + delta_y * delta_y);

    imu_prev_ax_ = ax;
    imu_prev_ay_ = ay;

    // Deliberate device motion should read as a clear reaction, not a vague drift.
    if (delta_mag >= IMU_SHAKE_TRIGGER_G && now_ms >= imu_rearm_ms_) {
        AnimConfused();
        imu_motion_hold_until_ms_ = now_ms + IMU_MOTION_HOLD_MS;
        imu_rearm_ms_ = now_ms + IMU_REARM_DELAY_MS;
        look_active_ = false;
        target_off_x_ = 0.0f;
        target_off_y_ = 0.0f;
    }

    // IMU no longer drives a continuous eye offset in the main render path.
    imu_target_x_ = 0.0f;
    imu_target_y_ = 0.0f;
}

// ---------------------------------------------------------------------------
// Touch
// ---------------------------------------------------------------------------
void EyeAnimation::HandleTouch(int x, int y) {
    uint32_t now = lv_tick_get();

    float dir_x = static_cast<float>(x - screen_w_ / 2);
    float dir_y = static_cast<float>(y - screen_h_ / 2);
    float len   = std::sqrt(dir_x * dir_x + dir_y * dir_y);
    if (len > 1.0f) { dir_x /= len; dir_y /= len; }

    touch_from_x_   = touch_off_x_;
    touch_from_y_   = touch_off_y_;
    touch_target_x_ = dir_x * 10.0f;
    touch_target_y_ = dir_y * 8.0f;
    touch_start_ms_ = now;
    touch_peak_ms_  = now + 110;
    touch_end_ms_   = now + 360;

    blink_active_   = true;
    blink_start_ms_ = now;
    blink_left_     = true;
    blink_right_    = true;
    if (x >= left_eye_box_.x && x <= left_eye_box_.x + left_eye_box_.w &&
        y >= left_eye_box_.y && y <= left_eye_box_.y + left_eye_box_.h) {
        blink_right_ = false;
    } else if (x >= right_eye_box_.x && x <= right_eye_box_.x + right_eye_box_.w &&
               y >= right_eye_box_.y && y <= right_eye_box_.y + right_eye_box_.h) {
        blink_left_ = false;
    }
}

bool EyeAnimation::IsTouchInsideEyes(int x, int y) const {
    auto hit = [&](const EyeBounds& b) {
        return x >= b.x && x <= b.x + b.w && y >= b.y && y <= b.y + b.h;
    };
    return hit(left_touch_box_) || hit(right_touch_box_);
}

// ---------------------------------------------------------------------------
// Timer callback
// ---------------------------------------------------------------------------
void EyeAnimation::TimerCallback(lv_timer_t* timer) {
    auto* self = static_cast<EyeAnimation*>(lv_timer_get_user_data(timer));
    if (!self) return;
    self->Update(lv_tick_get());
    self->RenderFrame();
}

// ---------------------------------------------------------------------------
// Update pipeline
// ---------------------------------------------------------------------------
void EyeAnimation::Update(uint32_t now_ms) {
    UpdateGeometryLerp();
    UpdateEyeColor(now_ms);
    UpdateMischief(now_ms);
    UpdateEyelidLerp();
    UpdateAutoblinker(now_ms);
    UpdateBlink(now_ms);
    UpdateIdleLook(now_ms);
    UpdateCuriousMode();
    UpdateAngryBounce(now_ms);
    UpdateFlicker();
    UpdateConfused(now_ms);
    UpdateLaugh(now_ms);
    UpdateTouchReaction(now_ms);
    UpdateImuOffset();

    // Breathing bounce (sine wave) — disabled
    // float t = static_cast<float>(now_ms % 4000U) / 4000.0f;
    // bounce_y_ = std::sin(t * 2.0f * static_cast<float>(M_PI)) * static_cast<float>(BOUNCE_AMPL);
    bounce_y_ = 0.0f;

    // Scale lerp
    eye_scale_ += (target_eye_scale_ - eye_scale_) * 0.16f;
}

// ---- Geometry lerp: (cur + next) / 2 each frame ----
void EyeAnimation::UpdateGeometryLerp() {
    // Curious height offsets are applied during curious mode update;
    // we lerp the base geometry here without them.
    HalfStepI(eye_l_w_current_, eye_l_w_next_);
    HalfStepI(eye_l_h_current_, eye_l_h_next_);
    HalfStepI(eye_l_r_current_, eye_l_r_next_);
    HalfStepI(eye_r_w_current_, eye_r_w_next_);
    HalfStepI(eye_r_h_current_, eye_r_h_next_);
    HalfStepI(eye_r_r_current_, eye_r_r_next_);
    HalfStepI(gap_current_,   gap_next_);

    // Clamp
    if (eye_l_h_current_ < 1) eye_l_h_current_ = 1;
    if (eye_r_h_current_ < 1) eye_r_h_current_ = 1;

    if (eye_l_r_current_ > eye_l_h_current_ / 2) eye_l_r_current_ = eye_l_h_current_ / 2;
    if (eye_l_r_current_ > eye_l_w_current_ / 2) eye_l_r_current_ = eye_l_w_current_ / 2;
    if (eye_r_r_current_ > eye_r_h_current_ / 2) eye_r_r_current_ = eye_r_h_current_ / 2;
    if (eye_r_r_current_ > eye_r_w_current_ / 2) eye_r_r_current_ = eye_r_w_current_ / 2;

    if (surprised_) {
        int left_target = std::min(SURPRISED_RADIUS, std::min(eye_l_h_current_ / 2, eye_l_w_current_ / 2));
        int right_target = std::min(SURPRISED_RADIUS, std::min(eye_r_h_current_ / 2, eye_r_w_current_ / 2));
        HalfStepI(eye_l_r_current_, left_target);
        HalfStepI(eye_r_r_current_, right_target);
    }

    int widest_eye = std::max(eye_l_w_current_, eye_r_w_current_);
    if (gap_current_ < -widest_eye) gap_current_ = -widest_eye;
}

// ---- Eyelid lerp ----
void EyeAnimation::UpdateEyelidLerp() {
    float avg_eye_h = static_cast<float>(eye_l_h_current_ + eye_r_h_current_) / 2.0f;

    // Set targets based on current mood
    if (tired_ && !angry_) {
        eyelids_tired_h_next_   = avg_eye_h * tired_lid_strength_;
        eyelids_angry_h_next_   = 0.0f;
    } else if (angry_ && !tired_) {
        eyelids_angry_h_next_   = avg_eye_h * angry_lid_strength_;
        eyelids_tired_h_next_   = 0.0f;
    } else {
        eyelids_tired_h_next_   = 0.0f;
        eyelids_angry_h_next_   = 0.0f;
    }
    eyelids_happy_off_next_ = happy_ ?
        avg_eye_h / 2.0f + 3.0f : 0.0f;
    eyelids_skeptic_h_next_ = skeptic_ ? avg_eye_h * skeptic_lid_strength_ : 0.0f;

    HalfStep(eyelids_tired_h_,   eyelids_tired_h_next_);
    HalfStep(eyelids_angry_h_,   eyelids_angry_h_next_);
    HalfStep(eyelids_happy_off_, eyelids_happy_off_next_);
    HalfStep(eyelids_skeptic_h_, eyelids_skeptic_h_next_);
}

// ---- Autoblinker (RoboEyes pattern) ----
void EyeAnimation::UpdateAutoblinker(uint32_t now_ms) {
    if (!autoblinker_) return;
    if (blink_active_) return;  // already blinking, don't schedule
    if (next_blink_ms_ != 0 && now_ms < next_blink_ms_) return;

    // Trigger blink
    blink_active_   = true;
    blink_start_ms_ = now_ms;
    blink_left_     = true;
    blink_right_    = true;
    // 20% chance of single-eye blink
    if (RandomInt(0, 4) == 0) {
        if (RandomInt(0, 1) == 0) blink_right_ = false;
        else                      blink_left_  = false;
    }
    EmitInteractionEvent(InteractionEvent::Blink);
    ScheduleNextBlink();
}

void EyeAnimation::ScheduleNextBlink() {
    next_blink_ms_ = lv_tick_get() +
        static_cast<uint32_t>(blink_interval_s_) * 1000u +
        static_cast<uint32_t>(RandomInt(0, blink_variation_s_)) * 1000u;
}

// ---- 3-phase blink (smooth, kept from original) ----
void EyeAnimation::UpdateBlink(uint32_t now_ms) {
    if (!blink_active_) { top_offset_ = 0; return; }

    uint32_t elapsed = now_ms - blink_start_ms_;
    if (elapsed < BLINK_CLOSE_MS) {
        top_offset_ = static_cast<int16_t>(BLINK_OFFSET_PX *
            static_cast<float>(elapsed) / BLINK_CLOSE_MS);
        return;
    }
    if (elapsed < BLINK_CLOSE_MS + BLINK_HOLD_MS) {
        top_offset_ = BLINK_OFFSET_PX; return;
    }
    if (elapsed < BLINK_CLOSE_MS + BLINK_HOLD_MS + BLINK_OPEN_MS) {
        float p = static_cast<float>(elapsed - BLINK_CLOSE_MS - BLINK_HOLD_MS) / BLINK_OPEN_MS;
        top_offset_ = static_cast<int16_t>(BLINK_OFFSET_PX * (1.0f - p));
        return;
    }
    blink_active_ = false;
    top_offset_   = 0;
}

// ---- Idle look (EaseInOutCubic, smoother than RoboEyes' instant jump) ----
void EyeAnimation::UpdateIdleLook(uint32_t now_ms) {
    if (!idle_active_) return;

    if (now_ms < imu_motion_hold_until_ms_) {
        look_active_ = false;
        off_x_ += (0.0f - off_x_) * 0.35f;
        off_y_ += (0.0f - off_y_) * 0.35f;
        return;
    }

    if (!look_active_ && now_ms >= next_look_ms_) {
        off_start_x_     = off_x_;
        off_start_y_     = off_y_;
        target_off_x_    = static_cast<float>(RandomInt(-6, 6));
        target_off_y_    = static_cast<float>(RandomInt(-6, 6));
        look_start_ms_   = now_ms;
        look_duration_ms_= static_cast<uint32_t>(RandomInt(180, 320));
        look_active_     = true;
    }

    if (!look_active_) return;

    float t = static_cast<float>(now_ms - look_start_ms_) /
              static_cast<float>(look_duration_ms_);
    if (t >= 1.0f) {
        off_x_       = target_off_x_;
        off_y_       = target_off_y_;
        look_active_ = false;
        next_look_ms_ = now_ms + static_cast<uint32_t>(
            idle_interval_ms_ + RandomInt(0, idle_variation_ms_));
        return;
    }
    float eased = EaseInOutCubic(ClampFloat(t, 0.0f, 1.0f));
    off_x_ = Lerp(off_start_x_, target_off_x_, eased);
    off_y_ = Lerp(off_start_y_, target_off_y_, eased);
}

// ---- Curious mode: outer eye grows when looking far left/right ----
void EyeAnimation::UpdateCuriousMode() {
    if (!curious_) {
        eye_l_h_offset_ = 0;
        eye_r_h_offset_ = 0;
        return;
    }
    // When offset_x is negative (looking left), left eye is outer → boost left
    // When offset_x is positive (looking right), right eye is outer → boost right
    eye_l_h_offset_ = (off_x_ <= -CURIOUS_THRESHOLD) ? CURIOUS_H_BOOST : 0;
    eye_r_h_offset_ = (off_x_ >=  CURIOUS_THRESHOLD) ? CURIOUS_H_BOOST : 0;
}

// ---- Angry pulse: lively up/down movement while angry mood is active ----
void EyeAnimation::UpdateAngryBounce(uint32_t now_ms) {
    if (!angry_ || tired_ || skeptic_) {
        angry_bounce_off_y_ = 0.0f;
        return;
    }

    float phase = static_cast<float>(now_ms % ANGRY_BOUNCE_PERIOD_MS) /
                  static_cast<float>(ANGRY_BOUNCE_PERIOD_MS);
    float triangle_01 = (phase < 0.5f) ? (phase * 2.0f) : (2.0f - phase * 2.0f);
    float signed_wave = triangle_01 * 2.0f - 1.0f;  // -1..1

    // Match esp32-eyes intent: larger pulse for "angry", shallower for "annoyed".
    float amplitude_px = 1.0f + (angry_lid_strength_ * 2.0f);
    angry_bounce_off_y_ = signed_wave * amplitude_px;
}

// ---- Flicker: alternating ± offset each frame ----
void EyeAnimation::UpdateFlicker() {
    flicker_off_x_ = 0;
    flicker_off_y_ = 0;
    if (h_flicker_) {
        flicker_off_x_ = h_flicker_alt_ ? h_flicker_amp_ : -h_flicker_amp_;
        h_flicker_alt_ = !h_flicker_alt_;
    }
    if (v_flicker_) {
        flicker_off_y_ = v_flicker_alt_ ? v_flicker_amp_ : -v_flicker_amp_;
        v_flicker_alt_ = !v_flicker_alt_;
    }
}

// ---- Confused: one-shot hFlicker for CONFUSED_DURATION_MS ----
void EyeAnimation::UpdateConfused(uint32_t now_ms) {
    if (!confused_active_) return;
    if (confused_toggle_) {
        SetHFlicker(true, 20);
        confused_timer_ms_ = now_ms;
        confused_toggle_   = false;
    } else if (now_ms >= confused_timer_ms_ + CONFUSED_DURATION_MS) {
        SetHFlicker(false, 0);
        confused_toggle_ = true;
        confused_active_ = false;
    }
}

// ---- Laugh: one-shot vFlicker for LAUGH_DURATION_MS ----
void EyeAnimation::UpdateLaugh(uint32_t now_ms) {
    if (!laugh_active_) return;
    if (laugh_toggle_) {
        SetVFlicker(true, 5);
        laugh_timer_ms_ = now_ms;
        laugh_toggle_   = false;
    } else if (now_ms >= laugh_timer_ms_ + LAUGH_DURATION_MS) {
        SetVFlicker(false, 0);
        laugh_toggle_ = true;
        laugh_active_ = false;
    }
}

// ---- Touch reaction ----
void EyeAnimation::UpdateTouchReaction(uint32_t now_ms) {
    if (touch_end_ms_ == 0 || now_ms >= touch_end_ms_) {
        touch_off_x_ = 0.0f; touch_off_y_ = 0.0f; return;
    }
    if (now_ms <= touch_peak_ms_) {
        float t = static_cast<float>(now_ms - touch_start_ms_) /
                  static_cast<float>(touch_peak_ms_ - touch_start_ms_);
        float e = EaseOutCubic(ClampFloat(t, 0.0f, 1.0f));
        touch_off_x_ = Lerp(touch_from_x_, touch_target_x_, e);
        touch_off_y_ = Lerp(touch_from_y_, touch_target_y_, e);
        return;
    }
    float t = static_cast<float>(now_ms - touch_peak_ms_) /
              static_cast<float>(touch_end_ms_ - touch_peak_ms_);
    float e = EaseInOutCubic(ClampFloat(t, 0.0f, 1.0f));
    touch_off_x_ = Lerp(touch_target_x_, 0.0f, e);
    touch_off_y_ = Lerp(touch_target_y_, 0.0f, e);
}

// ---- Eye color fade ----
void EyeAnimation::UpdateEyeColor(uint32_t now_ms) {
    if (color_last_ms_ == 0) {
        l_cr_ = l_tr_; l_cg_ = l_tg_; l_cb_ = l_tb_;
        r_cr_ = r_tr_; r_cg_ = r_tg_; r_cb_ = r_tb_;
        color_last_ms_ = now_ms; return;
    }
    uint32_t dt = now_ms - color_last_ms_;
    color_last_ms_ = now_ms;
    float alpha = ClampFloat(static_cast<float>(dt) / EYE_COLOR_FADE_MS, 0.0f, 1.0f);
    l_cr_ += (l_tr_ - l_cr_) * alpha;
    l_cg_ += (l_tg_ - l_cg_) * alpha;
    l_cb_ += (l_tb_ - l_cb_) * alpha;
    r_cr_ += (r_tr_ - r_cr_) * alpha;
    r_cg_ += (r_tg_ - r_cg_) * alpha;
    r_cb_ += (r_tb_ - r_cb_) * alpha;
}

// ---- Mood color targets ----
void EyeAnimation::UpdateMoodColor() {
    if (angry_) {
        SetEyeColor(255, 120, 120);
    } else if (tired_) {
        SetEyeColor(170, 210, 255);
    } else if (happy_) {
        SetEyeColor(255, 220, 120);
    } else {
        SetEyeColor(255, 255, 255);
    }
}

// ---- IMU offset ----
void EyeAnimation::UpdateImuOffset() {
    (void)IMU_SENSITIVITY;
    (void)IMU_MAX_OFFSET;
    (void)IMU_LERP_FAST;
    (void)IMU_DEAD_ZONE;

    // Keep the IMU path available, but decay any legacy offset back to zero.
    imu_off_x_ += (0.0f - imu_off_x_) * IMU_LERP_RETURN;
    imu_off_y_ += (0.0f - imu_off_y_) * IMU_LERP_RETURN;
    imu_target_x_ = 0.0f;
    imu_target_y_ = 0.0f;
}

void EyeAnimation::SyncBasePoseTargets() {
    l_tr_ = static_cast<float>(l_base_red_);
    l_tg_ = static_cast<float>(l_base_green_);
    l_tb_ = static_cast<float>(l_base_blue_);
    r_tr_ = static_cast<float>(r_base_red_);
    r_tg_ = static_cast<float>(r_base_green_);
    r_tb_ = static_cast<float>(r_base_blue_);
}

EyeAnimation::EyePose EyeAnimation::GetBaseLeftPose() const {
    return {
        eye_l_w_default_,
        eye_l_h_default_,
        eye_l_r_default_,
        l_base_red_,
        l_base_green_,
        l_base_blue_,
    };
}

EyeAnimation::EyePose EyeAnimation::GetBaseRightPose() const {
    return {
        eye_r_w_default_,
        eye_r_h_default_,
        eye_r_r_default_,
        r_base_red_,
        r_base_green_,
        r_base_blue_,
    };
}

void EyeAnimation::ClampPose(EyePose* pose) const {
    if (pose == nullptr) {
        return;
    }

    pose->w = std::max(mischief_config_.min_w, std::min(mischief_config_.max_w, pose->w));
    pose->h = std::max(mischief_config_.min_h, std::min(mischief_config_.max_h, pose->h));
    pose->radius = std::max(mischief_config_.min_radius, std::min(mischief_config_.max_radius, pose->radius));
    pose->radius = std::min(pose->radius, pose->w / 2);
    pose->radius = std::min(pose->radius, pose->h / 2);
    pose->red = static_cast<uint8_t>(std::max<int>(mischief_config_.min_r, std::min<int>(mischief_config_.max_r, pose->red)));
    pose->green = static_cast<uint8_t>(std::max<int>(mischief_config_.min_g, std::min<int>(mischief_config_.max_g, pose->green)));
    pose->blue = static_cast<uint8_t>(std::max<int>(mischief_config_.min_b, std::min<int>(mischief_config_.max_b, pose->blue)));
}

void EyeAnimation::ClampShape(EyeShape* shape) const {
    if (shape == nullptr) {
        return;
    }

    shape->w = std::max(48, std::min(110, shape->w));
    shape->h = std::max(24, std::min(110, shape->h));
    shape->radius = std::max(0, std::min(48, shape->radius));
    shape->radius = std::min(shape->radius, shape->w / 2);
    shape->radius = std::min(shape->radius, shape->h / 2);
}

EyeAnimation::EyePose EyeAnimation::MakeRandomPoseFromBase(const EyePose& base_pose) const {
    auto pose_is_too_close = [](const EyePose& a, const EyePose& b) {
        int color_delta = std::abs(static_cast<int>(a.red) - static_cast<int>(b.red)) +
                          std::abs(static_cast<int>(a.green) - static_cast<int>(b.green)) +
                          std::abs(static_cast<int>(a.blue) - static_cast<int>(b.blue));
        return std::abs(a.w - b.w) < 8 &&
               std::abs(a.h - b.h) < 8 &&
               std::abs(a.radius - b.radius) < 4 &&
               color_delta < 28;
    };

    struct MischiefColor {
        uint8_t red;
        uint8_t green;
        uint8_t blue;
    };

    static constexpr MischiefColor kPlayfulPalette[] = {
        {255,  92, 122},  // hot coral
        {255, 162,  65},  // mango orange
        {255, 214,  64},  // bright yellow
        {118, 255,  88},  // electric lime
        { 72, 250, 210},  // aqua mint
        { 75, 190, 255},  // bright sky
        {164, 110, 255},  // vivid violet
        {255,  98, 230},  // bubblegum magenta
    };
    static constexpr int kPlayfulPaletteCount =
        static_cast<int>(sizeof(kPlayfulPalette) / sizeof(kPlayfulPalette[0]));

    auto varied_channel = [this](uint8_t base, uint8_t min_value, uint8_t max_value) {
        int value = static_cast<int>(base) + RandomInt(-18, 18);
        value = std::max<int>(min_value, std::min<int>(max_value, value));
        return static_cast<uint8_t>(value);
    };

    EyePose pose = base_pose;
    for (int attempt = 0; attempt < 5; ++attempt) {
        pose.w = RandomInt(mischief_config_.min_w, mischief_config_.max_w);
        pose.h = RandomInt(mischief_config_.min_h, mischief_config_.max_h);
        pose.radius = RandomInt(mischief_config_.min_radius, mischief_config_.max_radius);

        const auto& palette_color = kPlayfulPalette[RandomInt(0, kPlayfulPaletteCount - 1)];
        pose.red = varied_channel(palette_color.red, mischief_config_.min_r, mischief_config_.max_r);
        pose.green = varied_channel(palette_color.green, mischief_config_.min_g, mischief_config_.max_g);
        pose.blue = varied_channel(palette_color.blue, mischief_config_.min_b, mischief_config_.max_b);

        ClampPose(&pose);
        if (!pose_is_too_close(pose, base_pose)) {
            break;
        }
    }
    return pose;
}

EyeAnimation::EyePose EyeAnimation::LerpPose(const EyePose& from, const EyePose& to, float t) const {
    EyePose pose;
    pose.w = RoundToInt(Lerp(static_cast<float>(from.w), static_cast<float>(to.w), t));
    pose.h = RoundToInt(Lerp(static_cast<float>(from.h), static_cast<float>(to.h), t));
    pose.radius = RoundToInt(Lerp(static_cast<float>(from.radius), static_cast<float>(to.radius), t));
    pose.red = static_cast<uint8_t>(RoundToInt(Lerp(static_cast<float>(from.red), static_cast<float>(to.red), t)));
    pose.green = static_cast<uint8_t>(RoundToInt(Lerp(static_cast<float>(from.green), static_cast<float>(to.green), t)));
    pose.blue = static_cast<uint8_t>(RoundToInt(Lerp(static_cast<float>(from.blue), static_cast<float>(to.blue), t)));
    ClampPose(&pose);
    return pose;
}

void EyeAnimation::ApplyMischiefPose(const EyePose& left_pose, const EyePose& right_pose) {
    eye_l_w_current_ = left_pose.w;
    eye_l_h_current_ = left_pose.h;
    eye_l_r_current_ = left_pose.radius;
    eye_r_w_current_ = right_pose.w;
    eye_r_h_current_ = right_pose.h;
    eye_r_r_current_ = right_pose.radius;

    l_cr_ = static_cast<float>(left_pose.red);
    l_cg_ = static_cast<float>(left_pose.green);
    l_cb_ = static_cast<float>(left_pose.blue);
    r_cr_ = static_cast<float>(right_pose.red);
    r_cg_ = static_cast<float>(right_pose.green);
    r_cb_ = static_cast<float>(right_pose.blue);
}

void EyeAnimation::FinishMischiefCycle() {
    mischief_phase_ = MischiefPhase::Waiting;
    mischief_phase_start_ms_ = 0;
    mischief_phase_duration_ms_ = 0;
    mischief_started_ = false;

    eye_l_w_current_ = eye_l_w_default_;
    eye_l_h_current_ = eye_l_h_default_;
    eye_l_r_current_ = eye_l_r_default_;
    eye_r_w_current_ = eye_r_w_default_;
    eye_r_h_current_ = eye_r_h_default_;
    eye_r_r_current_ = eye_r_r_default_;
    l_cr_ = static_cast<float>(l_base_red_);
    l_cg_ = static_cast<float>(l_base_green_);
    l_cb_ = static_cast<float>(l_base_blue_);
    r_cr_ = static_cast<float>(r_base_red_);
    r_cg_ = static_cast<float>(r_base_green_);
    r_cb_ = static_cast<float>(r_base_blue_);
    SyncBasePoseTargets();
}

void EyeAnimation::StartMischiefCycle(uint32_t now_ms) {
    auto left_base = GetBaseLeftPose();
    auto right_base = GetBaseRightPose();

    mischief_from_left_ = left_base;
    mischief_from_right_ = right_base;

    int roll = RandomInt(1, std::max(1,
        static_cast<int>(mischief_config_.both_sync_chance) +
        static_cast<int>(mischief_config_.both_independent_chance) +
        static_cast<int>(mischief_config_.left_only_chance) +
        static_cast<int>(mischief_config_.right_only_chance)));

    int threshold = mischief_config_.both_sync_chance;
    if (roll <= threshold) {
        auto pose = MakeRandomPoseFromBase(left_base);
        mischief_to_left_ = pose;
        mischief_to_right_ = pose;
    } else {
        threshold += mischief_config_.both_independent_chance;
        if (roll <= threshold) {
            mischief_to_left_ = MakeRandomPoseFromBase(left_base);
            mischief_to_right_ = MakeRandomPoseFromBase(right_base);
        } else {
            threshold += mischief_config_.left_only_chance;
            if (roll <= threshold) {
                mischief_to_left_ = MakeRandomPoseFromBase(left_base);
                mischief_to_right_ = right_base;
            } else {
                mischief_to_left_ = left_base;
                mischief_to_right_ = MakeRandomPoseFromBase(right_base);
            }
        }
    }

    mischief_phase_ = MischiefPhase::Changing;
    mischief_phase_start_ms_ = now_ms;
    mischief_phase_duration_ms_ = mischief_config_.change_ms;
    mischief_started_ = true;
    EmitInteractionEvent(InteractionEvent::Mischief);
}

void EyeAnimation::UpdateMischief(uint32_t now_ms) {
    // Timed Mischief is idle-only/menu-closed, but a manual eye tap should still
    // be allowed to run a single cycle even while the timed engine is gated off.
    if (!mischief_enabled_ && mischief_phase_ == MischiefPhase::Waiting) {
        return;
    }

    if (mischief_enabled_ && !mischief_started_) {
        mischief_next_cycle_ms_ = now_ms + RandomInt(
            static_cast<int>(mischief_config_.min_stay_ms),
            static_cast<int>(mischief_config_.max_stay_ms));
        mischief_started_ = true;
    }

    switch (mischief_phase_) {
        case MischiefPhase::Waiting:
            if (now_ms >= mischief_next_cycle_ms_) {
                StartMischiefCycle(now_ms);
            }
            return;

        case MischiefPhase::Changing: {
            float t = static_cast<float>(now_ms - mischief_phase_start_ms_) /
                      static_cast<float>(mischief_phase_duration_ms_);
            if (t >= 1.0f) {
                ApplyMischiefPose(mischief_to_left_, mischief_to_right_);
                mischief_phase_ = MischiefPhase::Holding;
                mischief_phase_start_ms_ = now_ms;
                mischief_phase_duration_ms_ = RandomInt(
                    static_cast<int>(mischief_config_.min_stay_ms),
                    static_cast<int>(mischief_config_.max_stay_ms));
                return;
            }
            float eased = EaseInOutCubic(ClampFloat(t, 0.0f, 1.0f));
            ApplyMischiefPose(
                LerpPose(mischief_from_left_, mischief_to_left_, eased),
                LerpPose(mischief_from_right_, mischief_to_right_, eased));
            return;
        }

        case MischiefPhase::Holding:
            ApplyMischiefPose(mischief_to_left_, mischief_to_right_);
            if (now_ms - mischief_phase_start_ms_ >= mischief_phase_duration_ms_) {
                mischief_from_left_ = mischief_to_left_;
                mischief_from_right_ = mischief_to_right_;
                mischief_to_left_ = GetBaseLeftPose();
                mischief_to_right_ = GetBaseRightPose();
                mischief_phase_ = MischiefPhase::Retreating;
                mischief_phase_start_ms_ = now_ms;
                mischief_phase_duration_ms_ = mischief_config_.retreat_ms;
            }
            return;

        case MischiefPhase::Retreating: {
            float t = static_cast<float>(now_ms - mischief_phase_start_ms_) /
                      static_cast<float>(mischief_phase_duration_ms_);
            if (t >= 1.0f) {
                FinishMischiefCycle();
                if (mischief_enabled_) {
                    mischief_next_cycle_ms_ = now_ms + RandomInt(
                        static_cast<int>(mischief_config_.min_stay_ms),
                        static_cast<int>(mischief_config_.max_stay_ms));
                }
                return;
            }
            float eased = EaseInOutCubic(ClampFloat(t, 0.0f, 1.0f));
            ApplyMischiefPose(
                LerpPose(mischief_from_left_, mischief_to_left_, eased),
                LerpPose(mischief_from_right_, mischief_to_right_, eased));
            return;
        }
    }
}

// ---------------------------------------------------------------------------
// RenderFrame — full RoboEyes rendering via LVGL
// ---------------------------------------------------------------------------
void EyeAnimation::RenderFrame() {
    if (!canvas_ || !canvas_buf_) return;

    lv_canvas_fill_bg(canvas_, lv_color_black(), LV_OPA_COVER);

    lv_color_t left_eye_color = lv_color_make(
        static_cast<uint8_t>(ClampFloat(l_cr_, 0.0f, 255.0f)),
        static_cast<uint8_t>(ClampFloat(l_cg_, 0.0f, 255.0f)),
        static_cast<uint8_t>(ClampFloat(l_cb_, 0.0f, 255.0f)));
    lv_color_t right_eye_color = lv_color_make(
        static_cast<uint8_t>(ClampFloat(r_cr_, 0.0f, 255.0f)),
        static_cast<uint8_t>(ClampFloat(r_cg_, 0.0f, 255.0f)),
        static_cast<uint8_t>(ClampFloat(r_cb_, 0.0f, 255.0f)));

    int left_eye_w = static_cast<int>(static_cast<float>(eye_l_w_current_) * eye_scale_);
    int right_eye_w = static_cast<int>(static_cast<float>(eye_r_w_current_) * eye_scale_);
    if (left_eye_w < eye_l_w_current_) left_eye_w = eye_l_w_current_;
    if (right_eye_w < eye_r_w_current_) right_eye_w = eye_r_w_current_;

    int left_eye_h = eye_l_h_current_;
    int right_eye_h = eye_r_h_current_;

    // Eye center (all offsets combined)
    int cx = screen_w_ / 2 + RoundToInt(off_x_ + touch_off_x_ + imu_off_x_) + flicker_off_x_;
    int cy = screen_h_ / 2 + RoundToInt(off_y_ + bounce_y_ + touch_off_y_ + imu_off_y_ + angry_bounce_off_y_) + flicker_off_y_;

    // Eye Y positions (blink shifts top edge down)
    int left_base_top    = cy - left_eye_h / 2;
    int left_base_bottom = left_base_top + left_eye_h;
    int right_base_top    = cy - right_eye_h / 2;
    int right_base_bottom = right_base_top + right_eye_h;

    // Clamp blink offset
    int left_top_y  = left_base_top + (blink_left_  ? static_cast<int>(top_offset_) : 0);
    int right_top_y = right_base_top + (blink_right_ ? static_cast<int>(top_offset_) : 0);
    if (left_top_y  > left_base_bottom - CLOSED_HEIGHT) left_top_y  = left_base_bottom - CLOSED_HEIGHT;
    if (right_top_y > right_base_bottom - CLOSED_HEIGHT) right_top_y = right_base_bottom - CLOSED_HEIGHT;

    int left_h  = left_base_bottom - left_top_y;
    int right_h = right_base_bottom - right_top_y;

    // X positions
    int gap   = cyclops_ ? 0 : gap_current_;
    int left_x  = cx - gap / 2 - left_eye_w;
    int right_x = cx + gap / 2;

    // Cyclops: collapse right eye
    if (cyclops_) {
        // Center single eye
        left_x = cx - left_eye_w / 2;
    }

    // Curious height boost for outer eye
    int l_h = left_h  + eye_l_h_offset_;
    int r_h = right_h + eye_r_h_offset_;
    int l_top = left_top_y  - eye_l_h_offset_ / 2;
    int r_top = right_top_y - eye_r_h_offset_ / 2;

    // Calculate radii
    auto calcRadius = [](int h, int r_default) -> int {
        int r = r_default;
        if (r > h / 2) r = h / 2;
        if (r < 0)     r = 0;
        return r;
    };
    int l_radius = calcRadius(l_h, eye_l_r_current_);
    int r_radius = calcRadius(r_h, eye_r_r_current_);

    // Store hit-test boxes
    left_eye_box_  = { static_cast<int16_t>(left_x),  static_cast<int16_t>(l_top),
                       static_cast<int16_t>(left_eye_w),    static_cast<int16_t>(l_h) };
    right_eye_box_ = { static_cast<int16_t>(right_x), static_cast<int16_t>(r_top),
                       static_cast<int16_t>(right_eye_w),    static_cast<int16_t>(r_h) };
    left_touch_box_  = { static_cast<int16_t>(left_x  - TOUCH_PAD_X),
                         static_cast<int16_t>(l_top   - TOUCH_PAD_Y),
                         static_cast<int16_t>(left_eye_w   + TOUCH_PAD_X * 2),
                         static_cast<int16_t>(l_h     + TOUCH_PAD_Y * 2) };
    right_touch_box_ = { static_cast<int16_t>(right_x - TOUCH_PAD_X),
                         static_cast<int16_t>(r_top   - TOUCH_PAD_Y),
                         static_cast<int16_t>(right_eye_w   + TOUCH_PAD_X * 2),
                         static_cast<int16_t>(r_h     + TOUCH_PAD_Y * 2) };

    lv_layer_t layer;
    lv_canvas_init_layer(canvas_, &layer);

    // ---- Draw eye rectangles ----
    lv_draw_rect_dsc_t rect;
    lv_draw_rect_dsc_init(&rect);
    rect.bg_opa    = LV_OPA_COVER;
    rect.border_opa= LV_OPA_TRANSP;

    // Left eye
    rect.bg_color = left_eye_color;
    rect.radius = l_radius;
    lv_area_t la = { static_cast<int16_t>(left_x), static_cast<int16_t>(l_top),
                     static_cast<int16_t>(left_x + left_eye_w - 1), static_cast<int16_t>(l_top + l_h - 1) };
    lv_draw_rect(&layer, &rect, &la);

    // Right eye (skip in cyclops mode)
    if (!cyclops_) {
        rect.bg_color = right_eye_color;
        rect.radius = r_radius;
        lv_area_t ra = { static_cast<int16_t>(right_x), static_cast<int16_t>(r_top),
                         static_cast<int16_t>(right_x + right_eye_w - 1), static_cast<int16_t>(r_top + r_h - 1) };
        lv_draw_rect(&layer, &rect, &ra);
    }

    // ---- Eyelid overlays (black triangles/rects on top of eyes) ----
    lv_draw_triangle_dsc_t tri;
    lv_draw_triangle_dsc_init(&tri);
    tri.color = lv_color_black();
    tri.opa   = LV_OPA_COVER;

    int th_tired = static_cast<int>(eyelids_tired_h_);
    int th_angry = static_cast<int>(eyelids_angry_h_);
    int hoff_bot = static_cast<int>(eyelids_happy_off_);
    int th_skeptic = static_cast<int>(eyelids_skeptic_h_);

    // TIRED eyelids: top-left corner droop on each eye
    if (th_tired > 0) {
        // Left eye: triangle from top-left corner drooping down-left
        tri.p[0].x = static_cast<int16_t>(left_x);        tri.p[0].y = static_cast<int16_t>(l_top - 1);
        tri.p[1].x = static_cast<int16_t>(left_x + left_eye_w); tri.p[1].y = static_cast<int16_t>(l_top - 1);
        tri.p[2].x = static_cast<int16_t>(left_x);         tri.p[2].y = static_cast<int16_t>(l_top + th_tired - 1);
        lv_draw_triangle(&layer, &tri);

        if (!cyclops_) {
            // Right eye: top-right corner droop
            tri.p[0].x = static_cast<int16_t>(right_x);        tri.p[0].y = static_cast<int16_t>(r_top - 1);
            tri.p[1].x = static_cast<int16_t>(right_x + right_eye_w); tri.p[1].y = static_cast<int16_t>(r_top - 1);
            tri.p[2].x = static_cast<int16_t>(right_x + right_eye_w); tri.p[2].y = static_cast<int16_t>(r_top + th_tired - 1);
            lv_draw_triangle(&layer, &tri);
        }
    }

    // ANGRY eyelids: mirror of tired — top-right droop on left eye, top-left on right eye
    if (th_angry > 0) {
        // Left eye: top-right corner droop
        tri.p[0].x = static_cast<int16_t>(left_x);         tri.p[0].y = static_cast<int16_t>(l_top - 1);
        tri.p[1].x = static_cast<int16_t>(left_x + left_eye_w);  tri.p[1].y = static_cast<int16_t>(l_top - 1);
        tri.p[2].x = static_cast<int16_t>(left_x + left_eye_w);  tri.p[2].y = static_cast<int16_t>(l_top + th_angry - 1);
        lv_draw_triangle(&layer, &tri);

        if (!cyclops_) {
            // Right eye: top-left corner droop
            tri.p[0].x = static_cast<int16_t>(right_x);        tri.p[0].y = static_cast<int16_t>(r_top - 1);
            tri.p[1].x = static_cast<int16_t>(right_x + right_eye_w); tri.p[1].y = static_cast<int16_t>(r_top - 1);
            tri.p[2].x = static_cast<int16_t>(right_x);         tri.p[2].y = static_cast<int16_t>(r_top + th_angry - 1);
            lv_draw_triangle(&layer, &tri);
        }
    }

    // HAPPY bottom eyelids: black rounded rect rising from bottom of each eye
    if (hoff_bot > 0) {
        lv_draw_rect_dsc_t mask;
        lv_draw_rect_dsc_init(&mask);
        mask.bg_color  = lv_color_black();
        mask.bg_opa    = LV_OPA_COVER;
        mask.border_opa= LV_OPA_TRANSP;
        mask.radius    = l_radius;

        // Left eye bottom mask
        int l_mask_top = l_top + l_h - hoff_bot + 1;
        lv_area_t lm = {
            static_cast<int16_t>(left_x - 1),
            static_cast<int16_t>(l_mask_top),
            static_cast<int16_t>(left_x + left_eye_w),
            static_cast<int16_t>(l_mask_top + l_h)
        };
        lv_draw_rect(&layer, &mask, &lm);

        if (!cyclops_) {
            mask.radius = r_radius;
            int r_mask_top = r_top + r_h - hoff_bot + 1;
            lv_area_t rm = {
                static_cast<int16_t>(right_x - 1),
                static_cast<int16_t>(r_mask_top),
                static_cast<int16_t>(right_x + right_eye_w),
                static_cast<int16_t>(r_mask_top + r_h)
            };
            lv_draw_rect(&layer, &mask, &rm);
        }
    }

    // SKEPTIC: asymmetrical single-eye inner-corner top droop.
    if (th_skeptic > 0) {
        if (!cyclops_) {
            if (skeptic_left_eye_) {
                // Left eye inner corner = top-right
                tri.p[0].x = static_cast<int16_t>(left_x);                tri.p[0].y = static_cast<int16_t>(l_top - 1);
                tri.p[1].x = static_cast<int16_t>(left_x + left_eye_w);   tri.p[1].y = static_cast<int16_t>(l_top - 1);
                tri.p[2].x = static_cast<int16_t>(left_x + left_eye_w);   tri.p[2].y = static_cast<int16_t>(l_top + th_skeptic - 1);
                lv_draw_triangle(&layer, &tri);
            } else {
                // Right eye inner corner = top-left
                tri.p[0].x = static_cast<int16_t>(right_x);               tri.p[0].y = static_cast<int16_t>(r_top - 1);
                tri.p[1].x = static_cast<int16_t>(right_x + right_eye_w); tri.p[1].y = static_cast<int16_t>(r_top - 1);
                tri.p[2].x = static_cast<int16_t>(right_x);               tri.p[2].y = static_cast<int16_t>(r_top + th_skeptic - 1);
                lv_draw_triangle(&layer, &tri);
            }
        }
    }

    // ---- Sweat drops ----
    if (sweat_active_) {
        DrawSweatDrops(&layer);
    }

    lv_canvas_finish_layer(canvas_, &layer);
}

// ---------------------------------------------------------------------------
// Sweat drops (RoboEyes pattern: 8 drops grow then shrink as they fall)
// ---------------------------------------------------------------------------
void EyeAnimation::DrawSweatDrops(lv_layer_t* layer) {
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_color   = lv_color_make(143, 203, 255);  // light blue
    d.bg_opa     = LV_OPA_COVER;
    d.border_opa = LV_OPA_TRANSP;
    d.radius     = 3;

    for (auto& drop : sweat_drops_) {
        // Fall
        if (drop.y <= drop.y_max) {
            drop.y += 0.5f;
        } else {
            // Reset
            drop.x_initial = static_cast<float>(RandomInt(10, screen_w_ - 10));
            drop.y         = 12.0f;
            drop.y_max     = static_cast<float>(RandomInt(20, 35));
            drop.w         = 1.0f;
            drop.h         = 2.0f;
        }
        // Grow in first half, shrink in second half
        if (drop.y <= drop.y_max / 2.0f) {
            drop.w += 0.5f; drop.h += 0.5f;
        } else {
            drop.w -= 0.1f; drop.h -= 0.5f;
            if (drop.w < 0.5f) drop.w = 0.5f;
            if (drop.h < 0.5f) drop.h = 0.5f;
        }
        drop.x = drop.x_initial - drop.w / 2.0f;

        int dx = static_cast<int>(drop.x);
        int dy = static_cast<int>(drop.y);
        int dw = std::max(1, static_cast<int>(drop.w));
        int dh = std::max(1, static_cast<int>(drop.h));

        lv_area_t a = {
            static_cast<int16_t>(dx),
            static_cast<int16_t>(dy),
            static_cast<int16_t>(dx + dw - 1),
            static_cast<int16_t>(dy + dh - 1)
        };
        lv_draw_rect(layer, &d, &a);
    }
}
