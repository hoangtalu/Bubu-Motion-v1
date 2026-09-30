#include "eye_animation.h"

#include <cmath>
#include <cstring>
#include <cstdio>
#include <string>
#include <algorithm>
#include <array>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <utility>

#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_random.h>

#define TAG "EyeAnimation"

extern const lv_font_t lv_font_montserrat_vn_28;

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
float HatchClamp01(float v) { return ClampFloat(v, 0.0f, 1.0f); }
float HatchSmoothstep(float t) {
    t = HatchClamp01(t);
    return t * t * (3.0f - 2.0f * t);
}

// RoboEyes-style half-step lerp: cur = (cur + next) / 2
inline void HalfStep(float& cur, float next) { cur = (cur + next) / 2.0f; }
inline void HalfStepI(int& cur, int next) { cur = (cur + next) / 2; }

void DrawFilledCircle(lv_layer_t* layer, int cx, int cy, int radius, lv_color_t color,
                      lv_opa_t opa = LV_OPA_COVER) {
    if (layer == nullptr || radius <= 0) {
        return;
    }
    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_color = color;
    dsc.bg_opa = opa;
    dsc.border_opa = LV_OPA_TRANSP;
    dsc.radius = radius;
    lv_area_t area = {
        static_cast<int16_t>(cx - radius),
        static_cast<int16_t>(cy - radius),
        static_cast<int16_t>(cx + radius),
        static_cast<int16_t>(cy + radius),
    };
    lv_draw_rect(layer, &dsc, &area);
}

void CanvasPutPixel565(uint16_t* buf, int width, int height, int x, int y, uint16_t c);

void DrawLegacyHeartShape(lv_layer_t* layer, lv_color_t* canvas_buf,
                          int screen_w, int screen_h,
                          int cx, int cy, int w, int h,
                          float tilt_deg, lv_color_t color) {
    (void)layer;
    if (canvas_buf == nullptr || w <= 0 || h <= 0) {
        return;
    }

    const uint16_t color565 = static_cast<uint16_t>(((color.red & 0xF8U) << 8U) |
                                                     ((color.green & 0xFCU) << 3U) |
                                                     (color.blue >> 3U));

    auto draw_rotated_filled_ellipse = [&](int ex, int ey, int ew, int eh, float angle_deg) {
        const float angle = angle_deg * static_cast<float>(M_PI) / 180.0f;
        const float ca = std::cosf(angle);
        const float sa = std::sinf(angle);
        for (int dy = -eh; dy <= eh; ++dy) {
            const float y_ratio = static_cast<float>(dy) / static_cast<float>(eh);
            const float span_f = std::sqrtf(std::max(0.0f, 1.0f - y_ratio * y_ratio)) *
                                 static_cast<float>(ew);
            const int x0 = static_cast<int>(std::floor(-span_f));
            const int x1 = static_cast<int>(std::ceil(span_f));
            for (int x = x0; x <= x1; ++x) {
                const float xf = static_cast<float>(x);
                const float xr = xf * ca - static_cast<float>(dy) * sa;
                const float yr = xf * sa + static_cast<float>(dy) * ca;
                const int px = static_cast<int>(std::floor(static_cast<float>(ex) + xr + 0.5f));
                const int py = static_cast<int>(std::floor(static_cast<float>(ey) + yr + 0.5f));
                CanvasPutPixel565(reinterpret_cast<uint16_t*>(canvas_buf), screen_w, screen_h, px, py, color565);
            }
        }
    };

    draw_rotated_filled_ellipse(cx - 10, cy, w, h, -tilt_deg);
    draw_rotated_filled_ellipse(cx + 10, cy, w, h, tilt_deg);
}

constexpr int kLegacySeedCenter = 120;
constexpr int kLegacySeedEyeDistance = 50;
constexpr int kLegacySeedEyeRadius = 35;
constexpr int kLegacySeedEyeWidth = 70;
constexpr int kLegacySeedEyeHeight = 70;
constexpr int kLegacySeedEyeCorner = 20;

uint16_t Color565FromRgb(uint8_t r, uint8_t g, uint8_t b) {
    return static_cast<uint16_t>(((r & 0xF8U) << 8U) | ((g & 0xFCU) << 3U) | (b >> 3U));
}

lv_color_t LvColorFrom565(uint16_t c) {
    const uint8_t r5 = static_cast<uint8_t>((c >> 11) & 0x1F);
    const uint8_t g6 = static_cast<uint8_t>((c >> 5) & 0x3F);
    const uint8_t b5 = static_cast<uint8_t>(c & 0x1F);
    return lv_color_make(static_cast<uint8_t>((r5 << 3) | (r5 >> 2)),
                         static_cast<uint8_t>((g6 << 2) | (g6 >> 4)),
                         static_cast<uint8_t>((b5 << 3) | (b5 >> 2)));
}

uint16_t LerpColor565(uint16_t c1, uint16_t c2, float t) {
    t = ClampFloat(t, 0.0f, 1.0f);
    const int r1 = (c1 >> 11) & 0x1F;
    const int g1 = (c1 >> 5) & 0x3F;
    const int b1 = c1 & 0x1F;
    const int r2 = (c2 >> 11) & 0x1F;
    const int g2 = (c2 >> 5) & 0x3F;
    const int b2 = c2 & 0x1F;
    const int r = r1 + static_cast<int>((r2 - r1) * t);
    const int g = g1 + static_cast<int>((g2 - g1) * t);
    const int b = b1 + static_cast<int>((b2 - b1) * t);
    return static_cast<uint16_t>((r << 11) | (g << 5) | b);
}

void CanvasPutPixel565(uint16_t* buf, int width, int height, int x, int y, uint16_t c) {
    if (!buf) return;
    if (x < 0 || y < 0 || x >= width || y >= height) return;
    buf[y * width + x] = c;
}

uint16_t CanvasGetPixel565(const uint16_t* buf, int width, int height, int x, int y) {
    if (!buf) return 0;
    if (x < 0 || y < 0 || x >= width || y >= height) return 0;
    return buf[y * width + x];
}

void CanvasBlendPixel565(uint16_t* buf, int width, int height, int x, int y,
                         uint16_t color, uint8_t alpha) {
    if (!buf) return;
    if (x < 0 || y < 0 || x >= width || y >= height) return;
    const uint16_t bg = CanvasGetPixel565(buf, width, height, x, y);
    const uint8_t r1 = static_cast<uint8_t>(((color >> 11) & 0x1F) << 3);
    const uint8_t g1 = static_cast<uint8_t>(((color >> 5) & 0x3F) << 2);
    const uint8_t b1 = static_cast<uint8_t>((color & 0x1F) << 3);
    const uint8_t r0 = static_cast<uint8_t>(((bg >> 11) & 0x1F) << 3);
    const uint8_t g0 = static_cast<uint8_t>(((bg >> 5) & 0x3F) << 2);
    const uint8_t b0 = static_cast<uint8_t>((bg & 0x1F) << 3);
    const uint8_t rm = static_cast<uint8_t>((r0 * (255 - alpha) + r1 * alpha) / 255);
    const uint8_t gm = static_cast<uint8_t>((g0 * (255 - alpha) + g1 * alpha) / 255);
    const uint8_t bm = static_cast<uint8_t>((b0 * (255 - alpha) + b1 * alpha) / 255);
    CanvasPutPixel565(buf, width, height, x, y, Color565FromRgb(rm, gm, bm));
}

struct LegacySeedRuntime {
    const EyeAnimation* owner = nullptr;
    EyeAnimation::LegacyEmotionMode mode = EyeAnimation::LegacyEmotionMode::None;
    uint32_t mode_start_ms = 0;

    bool confuse_initialized = false;
    bool confuse_swap_state = false;
    uint32_t confuse_last_switch_ms = 0;
    std::array<float, 25> confuse_angle{};
    std::array<float, 25> confuse_radius{};
    std::array<int, 25> confuse_size{};
    std::array<uint16_t, 25> confuse_color{};

    struct BanhSparkle {
        int x = 0;
        int y = 0;
        int size = 0;
        uint32_t offset = 0;
        uint32_t period = 0;
    };
    std::array<BanhSparkle, 10> banh_sparkles{};
    uint32_t banh_cycle_id = std::numeric_limits<uint32_t>::max();

    struct DPDot {
        int x = 0;
        int y = 0;
        int r = 1;
    };
    std::array<DPDot, 240> deadpool_dots{};
    int deadpool_dots_placed = 0;
    bool deadpool_dots_active = false;
    int deadpool_prev_phase = 2; // start in OUT so first IN clears
    uint32_t deadpool_seed = 0xDEAD901Fu;
};

LegacySeedRuntime& LegacySeedRuntimeStorage() {
    static LegacySeedRuntime rt;
    return rt;
}

LegacySeedRuntime& GetLegacySeedRuntime(const EyeAnimation* owner,
                                        EyeAnimation::LegacyEmotionMode mode,
                                        uint32_t now_ms,
                                        bool* entered_mode) {
    LegacySeedRuntime& rt = LegacySeedRuntimeStorage();
    if (entered_mode) *entered_mode = false;

    if (rt.owner != owner) {
        rt = LegacySeedRuntime{};
        rt.owner = owner;
    }
    if (rt.mode != mode) {
        rt.mode = mode;
        rt.mode_start_ms = now_ms;
        if (entered_mode) *entered_mode = true;

        if (mode == EyeAnimation::LegacyEmotionMode::Confuse) {
            rt.confuse_initialized = false;
            rt.confuse_swap_state = false;
            rt.confuse_last_switch_ms = now_ms;
        } else if (mode == EyeAnimation::LegacyEmotionMode::BanhChung) {
            rt.banh_cycle_id = std::numeric_limits<uint32_t>::max();
        } else if (mode == EyeAnimation::LegacyEmotionMode::Deadpool) {
            rt.deadpool_dots_placed = 0;
            rt.deadpool_dots_active = false;
            rt.deadpool_prev_phase = 2;
            rt.deadpool_seed ^= static_cast<uint32_t>(reinterpret_cast<uintptr_t>(owner));
            rt.deadpool_seed ^= now_ms * 2654435761u;
        }
    }
    return rt;
}

void MarkLegacySeedRuntimeNone(const EyeAnimation* owner) {
    LegacySeedRuntime& rt = LegacySeedRuntimeStorage();
    if (rt.owner == owner) {
        rt.mode = EyeAnimation::LegacyEmotionMode::None;
    }
}

} // namespace

// ---------------------------------------------------------------------------
// Emotion slots (Eye Lab, 2026-09-29)
//
// The product firmware accepts 38 names, but on the screen they reduce to 21
// looks. The 21 are the ACTIVE emotions below. The other 17 names were
// aliases that drew the same eyes as an active one; they are kept as EMPTY
// slots so the names stay reserved (Gemini, MCP and old callers still send
// them) and can each be given their own look later. An empty slot draws
// plain neutral eyes and says so in the log.
// ---------------------------------------------------------------------------
const char* const kEyeActiveEmotions[] = {
    "neutral", "happy", "surprised", "sad", "worried", "embarrassed",
    "nervous", "angry", "annoyed", "skeptic", "doubt", "confused", "sleepy",
    "legacy_emo_love", "legacy_emo_cyclop", "legacy_emo_drunk", "legacy_emo_confuse",
    "legacy_emo_angry", "legacy_emo_furious", "legacy_emo_banh_chung",
    "legacy_emo_deadpool", "legacy_emo_cry",
};
const int kEyeActiveEmotionCount = sizeof(kEyeActiveEmotions) / sizeof(kEyeActiveEmotions[0]);

// Empty slot -> the active emotion it used to copy (for reference only).
const char* const kEyeEmptySlots[][2] = {
    {"relaxed", "neutral"},   {"cool", "neutral"},
    {"funny", "happy"},       {"laughing", "happy"},   {"confident", "happy"},
    {"loving", "happy"},      {"kissy", "happy"},      {"delicious", "happy"},
    {"shocked", "happy"},
    {"crying", "sad"},
    {"anxious", "nervous"},
    {"thinking", "sad"},      {"winking", "sad"},      {"silly", "sad"},
    {"skeptical", "skeptic"}, {"doubtful", "doubt"},
};
const int kEyeEmptySlotCount = sizeof(kEyeEmptySlots) / sizeof(kEyeEmptySlots[0]);

bool EyeEmotion_IsEmptySlot(const char* emotion) {
    if (!emotion) return false;
    for (int i = 0; i < kEyeEmptySlotCount; ++i) {
        if (strcmp(emotion, kEyeEmptySlots[i][0]) == 0) return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// EyeEmotion_Apply
// ---------------------------------------------------------------------------
void EyeEmotion_Apply(const char* emotion, EyeAnimation* anim) {
    if (!emotion || !anim) return;

    // Clear everything a previous emotion may have set, lids included, so no
    // look inherits the one before it (confused used to keep sleepy's lids).
    anim->SetSweat(false);
    anim->SetSurprised(false);
    anim->SetSkeptic(false);
    anim->SetCyclops(false);
    anim->SetLegacyEmotionMode(EyeAnimation::LegacyEmotionMode::None);
    anim->SetTiredLidStrength(0.5f);
    anim->SetAngryLidStrength(0.5f);
    anim->SetSkepticLidStrength(0.35f);
    anim->SetMood(false, false, false);
    anim->SetCurious(false);
    anim->StopDoze();
    anim->CancelSurprised();

    auto is = [emotion](const char* name) { return strcmp(emotion, name) == 0; };

    if (EyeEmotion_IsEmptySlot(emotion)) {
        ESP_LOGI("EyeEmotion", "'%s' is an empty slot: neutral eyes", emotion);
        return;
    }
    if (is("neutral")) {
        return;
    }
    if (is("happy")) {
        anim->SetMood(false, false, true);
        return;
    }
    if (is("surprised")) {
        // Bounce once, then settle rounder (r24 -> r40), see PlaySurprised.
        anim->PlaySurprised();
        return;
    }
    if (is("sad")) {
        anim->SetMood(true, false, false);
        anim->SetTiredLidStrength(0.55f);
        return;
    }
    if (is("worried")) {
        anim->SetMood(true, false, false);
        anim->SetTiredLidStrength(0.30f);
        return;
    }
    if (is("embarrassed")) {
        anim->SetMood(true, false, false);
        anim->SetSweat(true);
        return;
    }
    if (is("nervous")) {
        anim->SetSweat(true);
        return;
    }
    if (is("angry")) {
        anim->SetMood(false, true, false);
        anim->SetAngryLidStrength(0.55f);
        return;
    }
    if (is("annoyed")) {
        anim->SetMood(false, true, false);
        anim->SetAngryLidStrength(0.30f);
        return;
    }
    if (is("skeptic")) {
        // Asymmetrical single-eye inner-corner top lid, on a random side.
        anim->SetSkeptic(true, RandomInt(0, 1) == 0);
        anim->SetSkepticLidStrength(0.30f);
        return;
    }
    if (is("doubt")) {
        anim->SetSkeptic(true, RandomInt(0, 1) == 0);
        anim->SetSkepticLidStrength(0.55f);
        return;
    }
    if (is("confused")) {
        anim->AnimConfused();
        return;
    }
    if (is("sleepy")) {
        // Dozing off (gù gật), see UpdateDoze.
        anim->StartDoze();
        return;
    }

    // Legacy scenes accept the short names (legacy_love) too.
    struct LegacyName { const char* full; const char* short_name; EyeAnimation::LegacyEmotionMode mode; };
    static const LegacyName kLegacy[] = {
        {"legacy_emo_love", "legacy_love", EyeAnimation::LegacyEmotionMode::Love},
        {"legacy_emo_cyclop", "legacy_cyclop", EyeAnimation::LegacyEmotionMode::Cyclop},
        {"legacy_emo_drunk", "legacy_drunk", EyeAnimation::LegacyEmotionMode::Drunk},
        {"legacy_emo_confuse", "legacy_confuse", EyeAnimation::LegacyEmotionMode::Confuse},
        {"legacy_emo_angry", "legacy_angry", EyeAnimation::LegacyEmotionMode::Angry},
        {"legacy_emo_furious", "legacy_furious", EyeAnimation::LegacyEmotionMode::Furious},
        {"legacy_emo_banh_chung", "legacy_banh_chung", EyeAnimation::LegacyEmotionMode::BanhChung},
        {"legacy_emo_deadpool", "legacy_deadpool", EyeAnimation::LegacyEmotionMode::Deadpool},
        {"legacy_emo_cry", "legacy_cry", EyeAnimation::LegacyEmotionMode::Cry},
    };
    for (const auto& l : kLegacy) {
        if (is(l.full) || is(l.short_name)) {
            if (l.mode == EyeAnimation::LegacyEmotionMode::Cyclop) anim->SetCyclops(true);
            anim->SetLegacyEmotionMode(l.mode);
            return;
        }
    }

    ESP_LOGW("EyeEmotion", "unknown emotion '%s': neutral eyes", emotion);
}

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------
EyeAnimation::EyeAnimation() {
    for (auto& d : sweat_drops_) {
        d = {0.0f, 0.0f, 2.0f, 3.0f, 1.0f};
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

    // Seed rain drops across the full screen.
    for (int i = 0; i < kSweatDropCount; i++) {
        auto& drop = sweat_drops_[i];
        drop.w = static_cast<float>(RandomInt(2, 3));  // 2-3 px wide
        drop.h = static_cast<float>(RandomInt(3, 6));  // 3-6 px long
        const int max_x = std::max(0, sw - static_cast<int>(drop.w));
        drop.x = static_cast<float>(RandomInt(0, max_x));
        drop.y = static_cast<float>(RandomInt(0, sh - 1));
        drop.speed = static_cast<float>(RandomInt(6, 16)) / 10.0f;  // 0.6..1.6 px/frame
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

void EyeAnimation::SetRenderPolicy(uint8_t fps, bool static_pose) {
    if (fps == render_fps_ && static_pose == static_pose_) {
        return;
    }

    const uint8_t previous_fps = render_fps_;
    render_fps_  = fps;
    static_pose_ = static_pose;

    if (!anim_timer_) {
        return;
    }

    if (fps == 0) {
        // Hiding the container is what actually stops the flush: it is the only
        // thing lv_obj_area_is_visible() checks, and the canvas invalidation
        // otherwise forces a full-screen refresh even under an opaque panel.
        // No final frame is drawn -- every 0 fps screen fully covers the eyes,
        // and the resume path repaints before revealing them again.
        SetVisible(false);
        lv_timer_pause(anim_timer_);
        return;
    }

    lv_timer_set_period(anim_timer_, 1000 / fps);

    if (previous_fps == 0) {
        // The canvas still holds the frame from before we suspended. Refresh it
        // before revealing the container, otherwise resuming shows a flash of
        // the previous screen's eyes.
        lv_timer_resume(anim_timer_);
        Update(lv_tick_get());
        RenderFrame();
        SetVisible(true);
    }
}

void EyeAnimation::StartHatching(uint32_t now_ms) {
    if (feed_.active) { FeedFinish(); }
    hatch_.active = true;
    hatch_.start_ms = now_ms;
    hatch_.pos_x = static_cast<float>(screen_w_ / 2);
    hatch_.pos_y = static_cast<float>(screen_h_ / 2);
    hatch_.moving = false;
    hatch_.move_start_ms = 0;
    hatch_.move_duration_ms = 0;
    hatch_.stop_until_ms = 0;
    hatch_.blink_started = false;
    hatch_.blink_start_ms = 0;
    HatchEnterPhase(1, now_ms);
}

bool EyeAnimation::IsHatchingActive() const { return hatch_.active; }

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

void EyeAnimation::SetMoodColorAutoEnabled(bool enabled) {
    mood_color_auto_enabled_ = enabled;
    if (mood_color_auto_enabled_) {
        UpdateMoodColor();
    }
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

void EyeAnimation::ExtendMischiefHold(uint32_t min_hold_ms) {
    if (min_hold_ms == 0) {
        return;
    }
    if (mischief_phase_ == MischiefPhase::Holding) {
        // Already holding this cycle's pose - stretch the remaining time in
        // place instead of letting it queue for a future cycle.
        uint32_t now_ms = lv_tick_get();
        uint32_t elapsed = now_ms - mischief_phase_start_ms_;
        mischief_phase_duration_ms_ = std::max(mischief_phase_duration_ms_, elapsed + min_hold_ms);
    } else if (mischief_phase_ == MischiefPhase::Changing) {
        // Holding's own duration hasn't been picked yet (see UpdateMischief's
        // Changing -> Holding transition) - queue the extension for then.
        mischief_min_hold_extension_ms_ = std::max(mischief_min_hold_extension_ms_, min_hold_ms);
    }
    // Waiting/Retreating: the voice arrived outside this cycle's pose window
    // (event consumption is expected to land well within the ~160ms Changing
    // phase); nothing sensible to attach it to.
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
void EyeAnimation::SetLegacyEmotionMode(LegacyEmotionMode mode) {
    legacy_emotion_mode_ = mode;
}

void EyeAnimation::SetSleepMode(bool active) {
    if (sleep_mode_ == active) {
        return;
    }

    sleep_mode_ = active;
    if (sleep_mode_) {
        if (feed_.active) { FeedFinish(); }
        blink_active_ = false;
        top_offset_ = 0;
        look_active_ = false;
        touch_end_ms_ = 0;
        touch_off_x_ = 0.0f;
        touch_off_y_ = 0.0f;
        off_x_ = 0.0f;
        off_y_ = 0.0f;
        target_off_x_ = 0.0f;
        target_off_y_ = 0.0f;
        SetMood(false, false, false);
        SetCurious(false);
        SetCyclops(false);
        SetSurprised(false);
        SetSkeptic(false);
        SetSweat(false);
        // Sleep owns the canvas. Clear legacy modes explicitly because their
        // renderer returns before the normal closed-eye + Z animation is drawn.
        // EyeDisplay remembers the prior emotion and reapplies it on wake.
        SetLegacyEmotionMode(LegacyEmotionMode::None);
        CancelBathing();
        FinishMischiefCycle();
        SetEyeColor(255, 255, 255);
        l_cr_ = l_cg_ = l_cb_ = 255.0f;
        r_cr_ = r_cg_ = r_cb_ = 255.0f;
        for (int i = 0; i < NUM_Z_PARTICLES; i++) {
            z_particles_[i].active = false;
        }
        next_z_spawn_ms_ = lv_tick_get();
        return;
    }

    angry_bounce_off_y_ = 0.0f;
    for (int i = 0; i < NUM_Z_PARTICLES; i++) {
        z_particles_[i].active = false;
    }

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

// ---- Dozing (gù gật) ----
// The top edge of both eyes sinks slowly (ease-in, like fighting sleep) to a
// depth picked fresh each cycle between doze_min_px_ and doze_max_px_, holds,
// rises again (ease-out), stays open, and sinks again. Each phase's time
// varies by +-25% so it never looks mechanical.
void EyeAnimation::StartDoze() {
    if (doze_active_) return;
    doze_active_ = true;
    doze_capped_ = false;  // the cap starts once the first droop has sunk past it
    doze_close_ = 0.0f;
    doze_from_ = 0.0f;
    // First droop starts from fully open and sinks at least to the resting
    // droop, so switching the max-open cap on afterwards causes no jump.
    doze_phase_ = DozePhase::Droop;
    doze_phase_start_ms_ = lv_tick_get();
    doze_phase_ms_ = doze_droop_ms_;
    doze_target_px_ = std::max(DozeRestPx(), static_cast<float>(RandomInt(doze_min_px_, std::max(doze_min_px_, doze_max_px_))));
}

void EyeAnimation::StopDoze() {
    doze_active_ = false;
    doze_close_ = 0.0f;
}

void EyeAnimation::UpdateDoze(uint32_t now_ms) {
    if (!doze_active_) return;
    const float t = ClampFloat(static_cast<float>(now_ms - doze_phase_start_ms_) /
                               static_cast<float>(std::max<uint32_t>(1, doze_phase_ms_)), 0.0f, 1.0f);
    const float depth = doze_target_px_;
    auto vary = [](uint32_t ms) {
        return static_cast<uint32_t>(RandomInt(static_cast<int>(ms * 3 / 4), static_cast<int>(ms * 5 / 4)));
    };
    switch (doze_phase_) {
        case DozePhase::Droop:
            doze_close_ = Lerp(doze_from_, depth, t * t * t);  // slow start, gives in at the end
            break;
        case DozePhase::Hold:
            doze_close_ = depth;
            break;
        case DozePhase::Snap: {
            const float u = 1.0f - t;
            doze_close_ = Lerp(doze_from_, DozeRestPx(), 1.0f - u * u * u);  // fast, then settles
            break;
        }
        case DozePhase::Open:
            doze_close_ = DozeRestPx();
            break;
    }
    if (t < 1.0f) return;

    doze_phase_start_ms_ = now_ms;
    doze_from_ = doze_close_;
    doze_capped_ = true;
    switch (doze_phase_) {
        case DozePhase::Open:
            doze_phase_ = DozePhase::Droop;
            doze_phase_ms_ = vary(doze_droop_ms_);
            doze_target_px_ = std::max(DozeRestPx(), static_cast<float>(RandomInt(doze_min_px_, std::max(doze_min_px_, doze_max_px_))));
            break;
        case DozePhase::Droop:
            doze_phase_ = DozePhase::Hold;
            doze_phase_ms_ = vary(doze_hold_ms_);
            break;
        case DozePhase::Hold:
            doze_phase_ = DozePhase::Snap;
            doze_phase_ms_ = vary(doze_snap_ms_);
            break;
        case DozePhase::Snap:
            doze_phase_ = DozePhase::Open;
            doze_phase_ms_ = vary(doze_open_ms_);
            break;
    }
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
    if (hatch_.active) {
        HatchHandleTap(lv_tick_get());
        return;
    }
    if (sleep_mode_) {
        return;
    }
    if (feed_.active) {
        HandleFeedTap(lv_tick_get());
        return;
    }
    if (bath_.active) {
        return;
    }

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
    if (hatch_.active) {
        return false;
    }
    auto hit = [&](const EyeBounds& b) {
        return x >= b.x && x <= b.x + b.w && y >= b.y && y <= b.y + b.h;
    };
    return hit(left_touch_box_) || hit(right_touch_box_);
}

// ---------------------------------------------------------------------------
// Timer callback
// ---------------------------------------------------------------------------
void EyeAnimation::RefrStartCb(lv_event_t* e) {
    auto* self = static_cast<EyeAnimation*>(lv_event_get_user_data(e));
    if (!self || self->render_fps_ == 0) return;
    self->Update(lv_tick_get());
    self->RenderFrame();
}

void EyeAnimation::SyncToDisplayRefresh(lv_display_t* disp, bool on) {
    if (synced_disp_) {
        lv_display_remove_event_cb_with_user_data(synced_disp_, RefrStartCb, this);
        synced_disp_ = nullptr;
    }
    if (on && disp) {
        synced_disp_ = disp;
        lv_display_add_event_cb(disp, RefrStartCb, LV_EVENT_REFR_START, this);
        // The timer keeps running, but in sync mode it only marks the canvas
        // dirty: LVGL 9 pauses its refresh timer when nothing is invalid, so
        // without this kick the refresh (and the render inside it) stops.
    }
}

void EyeAnimation::TimerCallback(lv_timer_t* timer) {
    auto* self = static_cast<EyeAnimation*>(lv_timer_get_user_data(timer));
    if (!self) return;
    if (self->synced_disp_) {
        if (self->canvas_) lv_obj_invalidate(self->canvas_);
        return;
    }
    self->Update(lv_tick_get());
    self->RenderFrame();
}

// ---------------------------------------------------------------------------
// Update pipeline
// ---------------------------------------------------------------------------
void EyeAnimation::Update(uint32_t now_ms) {
    if (hatch_.active) {
        UpdateHatching(now_ms);
        return;
    }

    UpdateMoodColor();
    UpdateGeometryLerp();
    UpdateEyeColor(now_ms);
    if (sleep_mode_) {
        UpdateSleepMode(now_ms);
    } else if (feed_.active) {
        UpdateFeeding(now_ms);
        UpdateEyelidLerp();
        UpdateAutoblinker(now_ms);
        UpdateBlink(now_ms);
    } else if (bath_.active) {
        UpdateBathing(now_ms);
        UpdateEyelidLerp();
        UpdateAutoblinker(now_ms);
        UpdateBlink(now_ms);
    } else {
        UpdateMischief(now_ms);
        UpdateEyelidLerp();
        UpdateAutoblinker(now_ms);
        UpdateBlink(now_ms);
        UpdateIdleLook(now_ms);
        UpdateCuriousMode();
        UpdateDoze(now_ms);
    }
    UpdateAngryBounce(now_ms);
    UpdateFlicker();
    if (!sleep_mode_) {
        UpdateConfused(now_ms);
        UpdateLaugh(now_ms);
        if (!feed_.active && !bath_.active) {
            UpdateTouchReaction(now_ms);
        }
    }
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
    // Surprised grows both eyes to SURPRISED_SIZE (user, 2026-09-29: 90x90).
    // Listening and speaking use a smaller eye (user, 2026-09-29: 60x60 r20).
    const bool session = listening_active_ || speaking_active_;
    const int lw = surprised_ ? SURPRISED_SIZE : session ? SESSION_SIZE : eye_l_w_next_;
    const int lh = surprised_ ? SURPRISED_SIZE : session ? SESSION_SIZE : eye_l_h_next_;
    const int rw = surprised_ ? SURPRISED_SIZE : session ? SESSION_SIZE : eye_r_w_next_;
    const int rh = surprised_ ? SURPRISED_SIZE : session ? SESSION_SIZE : eye_r_h_next_;
    const int lr = session ? SESSION_RADIUS : eye_l_r_next_;
    const int rr = session ? SESSION_RADIUS : eye_r_r_next_;
    HalfStepI(eye_l_w_current_, lw);
    HalfStepI(eye_l_h_current_, lh);
    HalfStepI(eye_l_r_current_, lr);
    HalfStepI(eye_r_w_current_, rw);
    HalfStepI(eye_r_h_current_, rh);
    HalfStepI(eye_r_r_current_, rr);
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

    // Connecting and speaking play at the centre: pull the look back, pause it.
    if (connecting_active_ || speaking_active_ || listening_active_) {
        look_active_ = false;
        HalfStep(off_x_, 0.0f);
        HalfStep(off_y_, 0.0f);
        return;
    }

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
        MaybeIdleGesture(now_ms);
        MaybeIdleTilt();
        MaybeIdleGaze();
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

// Idle tilt timing: each step holds 1.5-4 s, then picks the next state.
void EyeAnimation::UpdateIdleTilt(uint32_t now_ms) {
    if (head_shake_active_ || now_ms < next_look_ms_) return;
    // Leave a running mischief cycle alone; only tilts own the phases here.
    if (tilt_ == Tilt::Off && mischief_phase_ != MischiefPhase::Waiting) return;

    constexpr int kHoldMinMs = 1500;
    constexpr int kHoldMaxMs = 4000;
    next_look_ms_ = now_ms + static_cast<uint32_t>(RandomInt(kHoldMinMs, kHoldMaxMs));

    // From the centre: tilt to a random side with idle_tilt_chance_ %.
    // From a tilt: always come back to the centre first, so the eyes never
    // swing straight from one side to the other while idling.
    Tilt next = Tilt::Off;
    if (tilt_ == Tilt::Off && RandomInt(1, 100) <= static_cast<int>(idle_tilt_chance_)) {
        next = RandomInt(0, 1) == 0 ? Tilt::Left : Tilt::Right;
    }
    if (next != tilt_) SetTilt(next);
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
    if (static_pose_) {
        // Sleep holds the eyes still; only the Z particles move.
        angry_bounce_off_y_ = 0.0f;
        return;
    }

    if (sleep_mode_) {
        float phase = static_cast<float>(now_ms % SLEEP_BOUNCE_PERIOD_MS) /
                      static_cast<float>(SLEEP_BOUNCE_PERIOD_MS);
        float triangle_01 = (phase < 0.5f) ? (phase * 2.0f) : (2.0f - phase * 2.0f);
        float signed_wave = triangle_01 * 2.0f - 1.0f;
        angry_bounce_off_y_ = signed_wave * 2.0f;
        return;
    }

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
    if (static_pose_) {
        return;
    }
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

void EyeAnimation::UpdateSleepMode(uint32_t now_ms) {
    if (now_ms >= next_z_spawn_ms_) {
        // Find an inactive particle
        for (int i = 0; i < NUM_Z_PARTICLES; i++) {
            if (!z_particles_[i].active) {
                z_particles_[i].active = true;
                z_particles_[i].start_ms = now_ms;
                z_particles_[i].duration_ms = static_cast<uint32_t>(RandomInt(1500, 2500));
                
                // Start near the middle of the eyes
                z_particles_[i].start_x = static_cast<float>(screen_w_ / 2 + RandomInt(-15, 15));
                z_particles_[i].start_y = 0.0f; // Y offset from anchor_y
                
                z_particles_[i].x = z_particles_[i].start_x;
                z_particles_[i].y = z_particles_[i].start_y;
                z_particles_[i].x_drift = static_cast<float>(RandomInt(-15, 15));
                
                break; // Spawn one at a time
            }
        }
        next_z_spawn_ms_ = now_ms + static_cast<uint32_t>(RandomInt(400, 800));
    }
    
    // Update active particles
    for (int i = 0; i < NUM_Z_PARTICLES; i++) {
        if (z_particles_[i].active) {
            uint32_t elapsed = now_ms - z_particles_[i].start_ms;
            if (elapsed >= z_particles_[i].duration_ms) {
                z_particles_[i].active = false;
            } else {
                float t = static_cast<float>(elapsed) / static_cast<float>(z_particles_[i].duration_ms);
                z_particles_[i].y = z_particles_[i].start_y - t * 50.0f; // Rise up 50px
                z_particles_[i].x = z_particles_[i].start_x + t * z_particles_[i].x_drift;
            }
        }
    }


    eyelids_tired_h_ = 0.0f;
    eyelids_angry_h_ = 0.0f;
    eyelids_happy_off_ = 0.0f;
    eyelids_skeptic_h_ = 0.0f;
    eye_l_h_offset_ = 0;
    eye_r_h_offset_ = 0;
}

void EyeAnimation::HatchEnterPhase(uint8_t phase, uint32_t now_ms) {
    hatch_.phase = phase;
    hatch_.phase_start_ms = now_ms;
    hatch_.tap_bob_start_ms = 0;
    hatch_.tap_bob_duration_ms = 0;
    hatch_.tap_bob_amp = 0.0f;
    hatch_.phase2_boost_until_ms = 0;
    hatch_.twitch_start_ms = 0;
    hatch_.twitch_duration_ms = 0;
    hatch_.twitch_x = 0.0f;
    hatch_.twitch_y = 0.0f;
    if (phase == 3) {
        hatch_.moving = false;
        hatch_.stop_until_ms = now_ms + static_cast<uint32_t>(RandomInt(600, 1201));
    }
    if (phase == 4) {
        hatch_.blink_started = false;
        hatch_.blink_start_ms = 0;
    }
}

void EyeAnimation::HatchHandleTap(uint32_t now_ms) {
    if (hatch_.phase == 1) {
        hatch_.tap_bob_start_ms = now_ms;
        hatch_.tap_bob_duration_ms = 600;
        hatch_.tap_bob_amp = 4.0f;
    } else if (hatch_.phase == 2) {
        hatch_.tap_bob_start_ms = now_ms;
        hatch_.tap_bob_duration_ms = 650;
        hatch_.tap_bob_amp = 5.5f;
        hatch_.phase2_boost_until_ms = now_ms + 3500;
    } else if (hatch_.phase == 3) {
        hatch_.tap_bob_start_ms = now_ms;
        hatch_.tap_bob_duration_ms = 500;
        hatch_.tap_bob_amp = 6.0f;
        hatch_.twitch_start_ms = now_ms;
        hatch_.twitch_duration_ms = 280;
        hatch_.twitch_x = static_cast<float>(RandomInt(-50, 50)) / 10.0f;
        hatch_.twitch_y = static_cast<float>(RandomInt(-40, 40)) / 10.0f;
    }
}

void EyeAnimation::HatchUpdatePhase3(uint32_t now_ms) {
    const float base_x = static_cast<float>(screen_w_ / 2);
    const float base_y = static_cast<float>(screen_h_ / 2);
    if (hatch_.moving) {
        uint32_t elapsed = now_ms - hatch_.move_start_ms;
        float t = hatch_.move_duration_ms > 0
                      ? static_cast<float>(elapsed) /
                            static_cast<float>(hatch_.move_duration_ms)
                      : 1.0f;
        if (t >= 1.0f) {
            hatch_.pos_x = hatch_.move_target_x;
            hatch_.pos_y = hatch_.move_target_y;
            hatch_.moving = false;
            hatch_.stop_until_ms = now_ms + static_cast<uint32_t>(RandomInt(700, 1301));
        } else {
            float eased = HatchSmoothstep(t);
            hatch_.pos_x = Lerp(hatch_.move_start_x, hatch_.move_target_x, eased);
            hatch_.pos_y = Lerp(hatch_.move_start_y, hatch_.move_target_y, eased);
        }
    } else if (now_ms >= hatch_.stop_until_ms) {
        float angle =
            static_cast<float>(RandomInt(0, 62831)) / 10000.0f;
        float radius = static_cast<float>(RandomInt(200, 300)) / 10.0f;
        hatch_.move_start_x = hatch_.pos_x;
        hatch_.move_start_y = hatch_.pos_y;
        hatch_.move_target_x = base_x + std::cos(angle) * radius;
        hatch_.move_target_y = base_y + std::sin(angle) * radius;
        hatch_.move_start_ms = now_ms;
        hatch_.move_duration_ms = static_cast<uint32_t>(RandomInt(650, 1101));
        hatch_.moving = true;
    }
}

void EyeAnimation::HatchFinish(uint32_t now_ms) {
    (void)now_ms;
    hatch_.active = false;
    left_touch_box_ = {};
    right_touch_box_ = {};
}

void EyeAnimation::UpdateHatching(uint32_t now_ms) {
    uint32_t elapsed = now_ms - hatch_.start_ms;
    if (elapsed >= HATCH_TOTAL_MS) {
        HatchFinish(now_ms);
        return;
    }

    uint32_t p1_end = HATCH_PHASE1_MS;
    uint32_t p2_end = p1_end + HATCH_PHASE2_MS;
    uint32_t p3_end = p2_end + HATCH_PHASE3_MS;
    uint8_t next_phase = 4;
    if (elapsed < p1_end) {
        next_phase = 1;
    } else if (elapsed < p2_end) {
        next_phase = 2;
    } else if (elapsed < p3_end) {
        next_phase = 3;
    }

    if (next_phase != hatch_.phase) {
        HatchEnterPhase(next_phase, now_ms);
    }

    if (hatch_.phase == 1 || hatch_.phase == 2) {
        hatch_.pos_x = static_cast<float>(screen_w_ / 2);
        hatch_.pos_y = static_cast<float>(screen_h_ / 2);
    } else if (hatch_.phase == 3) {
        HatchUpdatePhase3(now_ms);
    } else if (hatch_.phase == 4) {
        hatch_.pos_x = static_cast<float>(screen_w_ / 2);
        hatch_.pos_y = static_cast<float>(screen_h_ / 2);
    }
}



// ---------------------------------------------------------------------------
// Bathing — scrub-to-clean
//
// Drawn on the main eye canvas (the old bath-rain effect used a second opaque
// full-screen canvas moved to the foreground, which hid the eyes for its whole
// duration). Foam bubbles are two concentric circles -- a blue rim around a pale
// core -- so they stay legible over the white eyes as well as the black ground.
// ---------------------------------------------------------------------------
namespace {

constexpr uint8_t kShowerR = 64,  kShowerG = 220, kShowerB = 227;   // icon teal
constexpr uint8_t kWaterR  = 77,  kWaterG  = 166, kWaterB  = 255;   // lifted for black bg
constexpr uint8_t kFoamRimR = 150, kFoamRimG = 197, kFoamRimB = 230;
constexpr uint8_t kFoamCoreR = 240, kFoamCoreG = 249, kFoamCoreB = 255;
constexpr uint8_t kSmudgeR = 90, kSmudgeG = 70, kSmudgeB = 52;

// Fixed grime spots so smudges don't jitter frame to frame. Offsets are from the
// centre of the eye they sit on; index parity picks the eye.
struct SmudgeSpot { int8_t dx; int8_t dy; int8_t r; };
constexpr SmudgeSpot kSmudgeSpots[6] = {
    {-26, -18, 4}, {14, 10, 3}, {-8, 20, 3}, {22, -22, 4}, {-20, 6, 3}, {4, -6, 3},
};

}  // namespace

void EyeAnimation::SetDirtyLevel(int cleanliness) {
    uint8_t count = 0;
    if (cleanliness < kDirtyHeavy)        count = 6;
    else if (cleanliness < kDirtyMedium)  count = 4;
    else if (cleanliness < kDirtyLight)   count = 2;
    dirty_count_ = count;
}

void EyeAnimation::StartBathing(uint32_t now_ms) {
    if (hatch_.active || sleep_mode_ || feed_.active) {
        return;
    }

    bath_.active = true;
    bath_.start_ms = now_ms;
    bath_.last_scrub_ms = now_ms;
    bath_.scrub_progress = 0.0f;
    bath_.foam_debt = 0.0f;
    bath_.has_last = false;
    bath_.smudges_hidden = 0;
    bath_.completed_pending = false;
    bath_.head_from_y = -30.0f;
    bath_.head_to_y = static_cast<float>(kBathHeadY);
    bath_.head_y = bath_.head_from_y;

    for (auto& blob : bath_foam_) {
        blob.active = false;
    }
    for (auto& drop : bath_droplets_) {
        drop.x = static_cast<float>(screen_w_ / 2 + RandomInt(-48, 48));
        drop.y = static_cast<float>(RandomInt(-140, 0));
        drop.speed = static_cast<float>(RandomInt(24, 52)) / 10.0f;
        drop.len = static_cast<uint8_t>(RandomInt(5, 9));
    }

    SetMood(false, false, false);
    SetCurious(false);
    SetSurprised(false);
    SetSkeptic(false);
    SetSweat(false);
    SetLegacyEmotionMode(LegacyEmotionMode::None);
    FinishMischiefCycle();
    look_active_ = false;
    idle_active_ = false;
    target_off_x_ = 0.0f;
    off_x_ = 0.0f;
    touch_end_ms_ = 0;
    touch_off_x_ = 0.0f;
    touch_off_y_ = 0.0f;

    BathEnterPhase(BathPhase::Enter, now_ms);
    ESP_LOGI(TAG, "Bathing started (smudges=%u)", static_cast<unsigned>(dirty_count_));
}

void EyeAnimation::BathEnterPhase(BathPhase phase, uint32_t now_ms) {
    bath_.phase = phase;
    bath_.phase_start_ms = now_ms;
    switch (phase) {
        case BathPhase::Enter:
            bath_.phase_duration_ms = kBathEnterMs;
            break;
        case BathPhase::Scrub:
            bath_.phase_duration_ms = 0;   // interactive
            bath_.last_scrub_ms = now_ms;
            break;
        case BathPhase::Rinse:
            bath_.phase_duration_ms = kBathRinseMs;
            for (auto& blob : bath_foam_) {
                if (blob.active) {
                    blob.vy = static_cast<float>(RandomInt(18, 40)) / 10.0f;
                }
            }
            break;
        case BathPhase::Clean:
            bath_.phase_duration_ms = kBathCleanMs;
            bath_.smudges_hidden = kSmudgeMaxCount;   // fully clean
            bath_.completed_pending = true;
            SetMood(false, false, true);
            break;
        case BathPhase::Exit:
            bath_.phase_duration_ms = kBathExitMs;
            break;
    }
}

bool EyeAnimation::HandleBathTap() {
    // A short scrub is classified as a TAP on release; swallow it so it cannot
    // fall through to menu-open or the conversation trigger.
    return bath_.active;
}

bool EyeAnimation::HandleBathScrub(int x, int y) {
    if (!bath_.active) {
        return false;
    }
    if (bath_.phase != BathPhase::Scrub && bath_.phase != BathPhase::Enter) {
        return true;
    }
    if (bath_.phase == BathPhase::Enter) {
        // Let an eager scrubber skip the descent.
        BathEnterPhase(BathPhase::Scrub, lv_tick_get());
    }

    const float fx = static_cast<float>(x);
    const float fy = static_cast<float>(y);
    if (bath_.has_last) {
        const float dx = fx - bath_.last_x;
        const float dy = fy - bath_.last_y;
        float dist = std::sqrt(dx * dx + dy * dy);
        if (dist > 60.0f) {
            dist = 60.0f;   // ignore teleports between separate strokes
        }
        bath_.scrub_progress += dist;
        bath_.foam_debt += dist;
        while (bath_.foam_debt >= kBathFoamEveryPx) {
            bath_.foam_debt -= kBathFoamEveryPx;
            BathSpawnFoam(fx, fy);
        }
        // Clear grime in step with progress.
        const float per_smudge = kBathScrubTarget / static_cast<float>(kSmudgeMaxCount);
        const uint8_t should_hide = static_cast<uint8_t>(
            std::min(static_cast<float>(kSmudgeMaxCount), bath_.scrub_progress / per_smudge));
        if (should_hide > bath_.smudges_hidden) {
            bath_.smudges_hidden = should_hide;
        }
    }
    bath_.last_x = fx;
    bath_.last_y = fy;
    bath_.has_last = true;
    bath_.last_scrub_ms = lv_tick_get();
    return true;
}

void EyeAnimation::BathSpawnFoam(float x, float y) {
    FoamBlob* slot = nullptr;
    for (auto& blob : bath_foam_) {
        if (!blob.active) { slot = &blob; break; }
    }
    if (slot == nullptr) {
        slot = &bath_foam_[RandomInt(0, kBathFoamCount - 1)];   // recycle oldest-ish
    }
    slot->active = true;
    slot->x = x + static_cast<float>(RandomInt(-6, 6));
    slot->y = y + static_cast<float>(RandomInt(-6, 6));
    slot->vy = 0.0f;
    slot->r = static_cast<uint8_t>(RandomInt(6, 11));
}

void EyeAnimation::BathFinish() {
    bath_.active = false;
    bath_.has_last = false;
    for (auto& blob : bath_foam_) {
        blob.active = false;
    }
    SetMood(false, false, false);
    idle_active_ = true;
    target_eye_scale_ = 1.0f;
    target_off_y_ = 0.0f;
    ESP_LOGI(TAG, "Bathing finished (scrub=%d px)", static_cast<int>(bath_.scrub_progress));
}

void EyeAnimation::CancelBathing() {
    if (bath_.active) {
        BathFinish();
    }
}

bool EyeAnimation::ConsumeBathCompleted() {
    const bool pending = bath_.completed_pending;
    bath_.completed_pending = false;
    return pending;
}

void EyeAnimation::UpdateBathing(uint32_t now_ms) {
    if (now_ms - bath_.start_ms >= kBathHardCapMs && bath_.phase != BathPhase::Exit) {
        BathEnterPhase(BathPhase::Exit, now_ms);
    }

    const uint32_t elapsed = now_ms - bath_.phase_start_ms;
    const float t = bath_.phase_duration_ms > 0
                        ? ClampFloat(static_cast<float>(elapsed) /
                                         static_cast<float>(bath_.phase_duration_ms),
                                     0.0f, 1.0f)
                        : 0.0f;

    target_eye_scale_ = 1.05f;
    target_off_y_ = -6.0f;          // looking up at the showerhead
    off_y_ += (target_off_y_ - off_y_) * 0.18f;
    feed_squint_px_ = 0.0f;

    bool water_running = true;

    switch (bath_.phase) {
        case BathPhase::Enter: {
            bath_.head_y = Lerp(bath_.head_from_y, bath_.head_to_y, EaseOutCubic(t));
            if (t >= 1.0f) {
                BathEnterPhase(BathPhase::Scrub, now_ms);
            }
            break;
        }
        case BathPhase::Scrub: {
            bath_.head_y = bath_.head_to_y;
            // Happy squint that responds to scrubbing.
            const bool rubbing = (now_ms - bath_.last_scrub_ms) < 220;
            const float want = rubbing ? 0.26f : 0.12f;
            const float avg_h = static_cast<float>(eye_l_h_current_ + eye_r_h_current_) / 2.0f;
            feed_squint_px_ = want * avg_h;
            if (bath_.scrub_progress >= kBathScrubTarget) {
                BathEnterPhase(BathPhase::Rinse, now_ms);
            } else if (now_ms - bath_.last_scrub_ms >= kBathIdleAdvanceMs) {
                ESP_LOGI(TAG, "Bath scrub idle; rinsing at %d px",
                         static_cast<int>(bath_.scrub_progress));
                BathEnterPhase(BathPhase::Rinse, now_ms);
            }
            break;
        }
        case BathPhase::Rinse: {
            bath_.head_y = bath_.head_to_y;
            const float avg_h = static_cast<float>(eye_l_h_current_ + eye_r_h_current_) / 2.0f;
            feed_squint_px_ = 0.34f * avg_h;
            // Grime that survived the scrub rinses away over this phase.
            const uint8_t rinsed = static_cast<uint8_t>(t * kSmudgeMaxCount);
            if (rinsed > bath_.smudges_hidden) {
                bath_.smudges_hidden = rinsed;
            }
            if (t >= 1.0f) {
                BathEnterPhase(BathPhase::Clean, now_ms);
            }
            break;
        }
        case BathPhase::Clean: {
            bath_.head_y = Lerp(bath_.head_to_y, bath_.head_from_y, EaseInOutCubic(t));
            water_running = false;
            if (t >= 1.0f) {
                BathEnterPhase(BathPhase::Exit, now_ms);
            }
            break;
        }
        case BathPhase::Exit: {
            bath_.head_y = bath_.head_from_y;
            water_running = false;
            target_eye_scale_ = 1.0f;
            target_off_y_ = 0.0f;
            if (t >= 1.0f) {
                BathFinish();
                return;
            }
            break;
        }
    }

    // Water: droplets stream from the head and recycle at the top.
    if (water_running) {
        const int heavy = (bath_.phase == BathPhase::Rinse) ? 2 : 1;
        for (auto& drop : bath_droplets_) {
            drop.y += drop.speed * static_cast<float>(heavy);
            if (drop.y > static_cast<float>(screen_h_)) {
                const int spread = (bath_.phase == BathPhase::Rinse) ? 48 : 34;
                drop.x = static_cast<float>(screen_w_ / 2 + RandomInt(-spread, spread));
                drop.y = bath_.head_y + static_cast<float>(RandomInt(10, 26));
                drop.speed = static_cast<float>(RandomInt(24, 52)) / 10.0f;
                drop.len = static_cast<uint8_t>(RandomInt(5, 9));
            }
        }
    }

    // Foam drifts down once rinsing starts and leaves the screen.
    for (auto& blob : bath_foam_) {
        if (!blob.active) continue;
        if (blob.vy > 0.0f) {
            blob.y += blob.vy;
            blob.vy += 0.18f;
            if (blob.y - blob.r > static_cast<float>(screen_h_)) {
                blob.active = false;
            }
        }
    }
}

void EyeAnimation::DrawBathShower(lv_layer_t* layer) const {
    if (layer == nullptr) return;
    const int cx = screen_w_ / 2;
    const int y = static_cast<int>(bath_.head_y + 0.5f);
    if (y + kBathHeadH < 0) return;

    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_color = lv_color_make(kShowerR, kShowerG, kShowerB);
    dsc.bg_opa = LV_OPA_COVER;
    dsc.border_opa = LV_OPA_TRANSP;

    dsc.radius = 3;
    lv_area_t stem = {static_cast<int16_t>(cx - 5), static_cast<int16_t>(y - 16),
                      static_cast<int16_t>(cx + 4), static_cast<int16_t>(y - 5)};
    lv_draw_rect(layer, &dsc, &stem);

    dsc.radius = 6;
    lv_area_t head = {static_cast<int16_t>(cx - kBathHeadW / 2), static_cast<int16_t>(y - 4),
                      static_cast<int16_t>(cx + kBathHeadW / 2 - 1),
                      static_cast<int16_t>(y - 4 + kBathHeadH - 1)};
    lv_draw_rect(layer, &dsc, &head);
}

void EyeAnimation::DrawBathWater(lv_layer_t* layer) const {
    if (layer == nullptr) return;
    if (bath_.phase == BathPhase::Clean || bath_.phase == BathPhase::Exit) return;

    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_color = lv_color_make(kWaterR, kWaterG, kWaterB);
    dsc.bg_opa = LV_OPA_COVER;
    dsc.border_opa = LV_OPA_TRANSP;
    dsc.radius = 1;

    for (const auto& drop : bath_droplets_) {
        if (drop.y < 0.0f) continue;
        const int16_t x = static_cast<int16_t>(drop.x);
        const int16_t y = static_cast<int16_t>(drop.y);
        lv_area_t a = {x, y, static_cast<int16_t>(x + 2),
                       static_cast<int16_t>(y + drop.len)};
        lv_draw_rect(layer, &dsc, &a);
    }
}

void EyeAnimation::DrawBathFoam(lv_layer_t* layer) const {
    if (layer == nullptr) return;
    const lv_color_t rim = lv_color_make(kFoamRimR, kFoamRimG, kFoamRimB);
    const lv_color_t core = lv_color_make(kFoamCoreR, kFoamCoreG, kFoamCoreB);
    for (const auto& blob : bath_foam_) {
        if (!blob.active) continue;
        const int x = static_cast<int>(blob.x);
        const int y = static_cast<int>(blob.y);
        DrawFilledCircle(layer, x, y, blob.r, rim);
        DrawFilledCircle(layer, x, y, blob.r - 2, core);
    }
}

void EyeAnimation::DrawSmudges(lv_layer_t* layer) const {
    if (layer == nullptr) return;
    int visible = static_cast<int>(dirty_count_);
    if (bath_.active) {
        visible -= static_cast<int>(bath_.smudges_hidden);
    }
    if (visible <= 0) return;
    if (visible > kSmudgeMaxCount) visible = kSmudgeMaxCount;

    const lv_color_t color = lv_color_make(kSmudgeR, kSmudgeG, kSmudgeB);
    for (int i = 0; i < visible; ++i) {
        const SmudgeSpot& spot = kSmudgeSpots[i];
        const EyeBounds& eye = (i % 2 == 0) ? left_eye_box_ : right_eye_box_;
        if (eye.w <= 0 || eye.h <= 0) continue;
        const int cx = eye.x + eye.w / 2 + spot.dx;
        const int cy = eye.y + eye.h / 2 + spot.dy;
        // Keep the whole blob inside the eye so it never floats on the black bg.
        if (cx - spot.r < eye.x || cx + spot.r > eye.x + eye.w) continue;
        if (cy - spot.r < eye.y || cy + spot.r > eye.y + eye.h) continue;
        DrawFilledCircle(layer, cx, cy, spot.r, color);
    }
}

// ---------------------------------------------------------------------------
// Feeding — tap-to-chomp
//
// Drawn with the same rounded-rect vocabulary as everything else: the dish is a
// single 62x20 rect and each kibble is a circle (rect with radius = r). Kibble
// are listed crown-first so a bite peels the mound from the top down.
// ---------------------------------------------------------------------------
namespace {

struct FeedKibble { int8_t dx; int8_t dy; int8_t r; };

constexpr FeedKibble kFeedKibbleLayout[6] = {
    { 0, -22, 6},                 // crown       — gone after bite 1
    { 9, -14, 7}, {-9, -14, 7},   // second row  — gone after bite 2
    {17,  -3, 7}, { 0,  -5, 7}, {-17, -3, 7},   // base row — gone after bite 3
};

constexpr uint8_t kFeedDishR = 253, kFeedDishG = 206, kFeedDishB = 20;
constexpr uint8_t kFeedKibbleR = 252, kFeedKibbleG = 107, kFeedKibbleB = 1;

}  // namespace

void EyeAnimation::StartFeeding(uint32_t now_ms, int hunger) {
    if (hatch_.active || sleep_mode_ || bath_.active) {
        return;
    }

    // Remember what to put back when the sequence ends.
    feed_.prev_tired = tired_;
    feed_.prev_angry = angry_;
    feed_.prev_happy = happy_;
    feed_.prev_idle = idle_active_;
    feed_.prev_gap = gap_default_;

    feed_.active = true;
    feed_.start_ms = now_ms;
    feed_.kibble_left = kFeedKibbleCount;
    feed_.bites_pending = 0;
    feed_.squash = 1.0f;
    feed_.refuse = hunger >= kFeedFullThreshold;

    if (hunger < kFeedStarvingThreshold) {
        feed_.approach_ms = 350;      // starving: eager, quick, big reactions
        feed_.auto_chomp_ms = 1200;
        feed_.shake_px = 5.0f;
        feed_.squint = 0.70f;
    } else {
        feed_.approach_ms = 500;
        feed_.auto_chomp_ms = 2200;
        feed_.shake_px = 3.0f;
        feed_.squint = 0.55f;
    }

    for (auto& crumb : feed_crumbs_) {
        crumb.active = false;
    }
    feed_squint_px_ = 0.0f;
    feed_shake_y_ = 0.0f;

    // Clear anything that would fight the sequence.
    SetMood(false, false, false);
    SetCurious(false);
    SetSurprised(false);
    SetSkeptic(false);
    SetSweat(false);
    CancelBathing();
    SetLegacyEmotionMode(LegacyEmotionMode::None);
    FinishMischiefCycle();
    look_active_ = false;
    idle_active_ = false;
    target_off_x_ = 0.0f;
    off_x_ = 0.0f;
    touch_end_ms_ = 0;
    touch_off_x_ = 0.0f;
    touch_off_y_ = 0.0f;

    feed_.food_from_y = static_cast<float>(screen_h_ + 40);
    feed_.food_to_y = static_cast<float>(kFeedDishY);
    feed_.food_y = feed_.food_from_y;
    FeedEnterPhase(FeedPhase::Approach, now_ms);
    ESP_LOGI(TAG, "Feeding started (hunger=%d, refuse=%d)", hunger, feed_.refuse ? 1 : 0);
}

void EyeAnimation::FeedEnterPhase(FeedPhase phase, uint32_t now_ms) {
    feed_.phase = phase;
    feed_.phase_start_ms = now_ms;
    switch (phase) {
        case FeedPhase::Approach:
            feed_.phase_duration_ms = feed_.approach_ms;
            break;
        case FeedPhase::Waiting:
            feed_.phase_duration_ms = 0;   // waits for a tap (or the auto-chomp)
            break;
        case FeedPhase::Chomp:
            feed_.phase_duration_ms = kFeedChompMs;
            break;
        case FeedPhase::Refuse:
            feed_.phase_duration_ms = kFeedRefuseMs;
            AnimConfused();                 // head-shake "no"
            SetSkeptic(true, RandomInt(0, 1) == 0);
            SetSkepticLidStrength(0.45f);
            break;
        case FeedPhase::Savor:
            feed_.phase_duration_ms = kFeedSavorMs;
            SetMood(false, false, true);    // happy arcs
            break;
        case FeedPhase::Exit:
            feed_.phase_duration_ms = kFeedExitMs;
            break;
    }
}

bool EyeAnimation::HandleFeedTap(uint32_t now_ms) {
    if (!feed_.active) {
        return false;
    }
    if (feed_.phase == FeedPhase::Waiting) {
        FeedTakeBite(now_ms);
    }
    // Taps during any other phase are swallowed so they can't fall through to
    // the "tap eyes -> start conversation" gesture mid-animation.
    return true;
}

void EyeAnimation::FeedTakeBite(uint32_t now_ms) {
    const int per_bite = kFeedKibbleCount / kFeedBiteCount;
    feed_.kibble_left = static_cast<uint8_t>(
        feed_.kibble_left > per_bite ? feed_.kibble_left - per_bite : 0);
    if (feed_.bites_pending < 255) {
        feed_.bites_pending++;
    }
    FeedSpawnCrumbs(now_ms);
    FeedEnterPhase(FeedPhase::Chomp, now_ms);
}

void EyeAnimation::FeedSpawnCrumbs(uint32_t now_ms) {
    const float origin_y = feed_.food_y - 24.0f;
    int spawned = 0;
    for (auto& crumb : feed_crumbs_) {
        if (spawned >= 6) break;
        if (crumb.active) continue;
        crumb.active = true;
        crumb.x = static_cast<float>(screen_w_ / 2 + RandomInt(-18, 18));
        crumb.y = origin_y + static_cast<float>(RandomInt(-6, 6));
        crumb.vx = static_cast<float>(RandomInt(-14, 14)) / 10.0f;
        crumb.vy = static_cast<float>(RandomInt(-26, -14)) / 10.0f;
        crumb.size = static_cast<uint8_t>(RandomInt(2, 3));
        crumb.start_ms = now_ms;
        spawned++;
    }
}

void EyeAnimation::FeedFinish() {
    feed_.active = false;
    feed_squint_px_ = 0.0f;
    feed_shake_y_ = 0.0f;
    for (auto& crumb : feed_crumbs_) {
        crumb.active = false;
    }
    SetSkeptic(false);
    SetSkepticLidStrength(0.35f);
    SetMood(feed_.prev_tired, feed_.prev_angry, feed_.prev_happy);
    idle_active_ = feed_.prev_idle;
    gap_next_ = feed_.prev_gap;
    target_eye_scale_ = 1.0f;
    target_off_y_ = 0.0f;
    ESP_LOGI(TAG, "Feeding finished");
}

int EyeAnimation::ConsumeFeedBites() {
    const int bites = feed_.bites_pending;
    feed_.bites_pending = 0;
    return bites;
}

void EyeAnimation::UpdateFeeding(uint32_t now_ms) {
    // Hard cap so a walked-away-from feeding can never wedge the display.
    if (now_ms - feed_.start_ms >= kFeedHardCapMs && feed_.phase != FeedPhase::Exit) {
        FeedEnterPhase(FeedPhase::Exit, now_ms);
    }

    const uint32_t elapsed = now_ms - feed_.phase_start_ms;
    const float t = feed_.phase_duration_ms > 0
                        ? ClampFloat(static_cast<float>(elapsed) /
                                         static_cast<float>(feed_.phase_duration_ms),
                                     0.0f, 1.0f)
                        : 0.0f;

    // Eyes converge on the dish and grow slightly for the whole sequence.
    gap_next_ = kFeedConvergedGap;
    target_eye_scale_ = kFeedEyeScale;
    target_off_y_ = 6.0f;
    off_y_ += (target_off_y_ - off_y_) * 0.18f;
    feed_squint_px_ = 0.0f;
    feed_shake_y_ = 0.0f;

    switch (feed_.phase) {
        case FeedPhase::Approach: {
            feed_.food_y = Lerp(feed_.food_from_y, feed_.food_to_y, EaseOutCubic(t));
            if (t >= 1.0f) {
                FeedEnterPhase(feed_.refuse ? FeedPhase::Refuse : FeedPhase::Waiting, now_ms);
            }
            break;
        }
        case FeedPhase::Waiting: {
            // Gentle bob to invite the tap.
            const float phase = static_cast<float>((now_ms - feed_.phase_start_ms) % 1400U) /
                                1400.0f * 2.0f * static_cast<float>(M_PI);
            feed_.food_y = feed_.food_to_y + std::sin(phase) * 3.0f;
            if (feed_.auto_chomp_ms > 0 && elapsed >= feed_.auto_chomp_ms) {
                FeedTakeBite(now_ms);   // don't hang if the user walks away
            }
            break;
        }
        case FeedPhase::Chomp: {
            // Squash the mound and shake the eyes on the bite, then release.
            const float pulse = std::sin(t * static_cast<float>(M_PI));
            feed_.squash = 1.0f - 0.14f * pulse;
            feed_squint_px_ = feed_.squint * pulse *
                              static_cast<float>(eye_l_h_current_ + eye_r_h_current_) / 2.0f;
            feed_shake_y_ = std::sin(t * 6.0f * static_cast<float>(M_PI)) *
                            feed_.shake_px * (1.0f - t);
            feed_.food_y = feed_.food_to_y + 2.0f * pulse;
            if (t >= 1.0f) {
                feed_.squash = 1.0f;
                FeedEnterPhase(feed_.kibble_left > 0 ? FeedPhase::Waiting : FeedPhase::Savor,
                               now_ms);
            }
            break;
        }
        case FeedPhase::Refuse: {
            // Shove the dish back down; the head-shake comes from AnimConfused.
            feed_.food_y = Lerp(feed_.food_to_y, feed_.food_from_y, EaseInOutCubic(t));
            if (t >= 1.0f) {
                FeedEnterPhase(FeedPhase::Exit, now_ms);
            }
            break;
        }
        case FeedPhase::Savor: {
            feed_.food_y = feed_.food_to_y;
            if (t >= 1.0f) {
                FeedEnterPhase(FeedPhase::Exit, now_ms);
            }
            break;
        }
        case FeedPhase::Exit: {
            feed_.food_y = Lerp(feed_.food_to_y, feed_.food_from_y, EaseInOutCubic(t));
            gap_next_ = feed_.prev_gap;
            target_eye_scale_ = 1.0f;
            target_off_y_ = 0.0f;
            if (t >= 1.0f) {
                FeedFinish();
                return;
            }
            break;
        }
    }

    // Crumb physics (gravity, fixed 30fps step).
    for (auto& crumb : feed_crumbs_) {
        if (!crumb.active) continue;
        if (now_ms - crumb.start_ms >= kFeedCrumbLifeMs) {
            crumb.active = false;
            continue;
        }
        crumb.x += crumb.vx;
        crumb.y += crumb.vy;
        crumb.vy += 0.35f;
    }
}

void EyeAnimation::DrawFeedFood(lv_layer_t* layer) const {
    if (layer == nullptr) {
        return;
    }
    const int cx = screen_w_ / 2;
    const int dish_top = static_cast<int>(feed_.food_y + 0.5f);
    const int mound_anchor = dish_top - 11;

    const lv_color_t kibble_color =
        lv_color_make(kFeedKibbleR, kFeedKibbleG, kFeedKibbleB);

    // Kibble first so the dish rim overlaps and visually contains the mound.
    for (int i = kFeedKibbleCount - feed_.kibble_left; i < kFeedKibbleCount; ++i) {
        const FeedKibble& k = kFeedKibbleLayout[i];
        const int ky = mound_anchor + static_cast<int>(static_cast<float>(k.dy) * feed_.squash);
        DrawFilledCircle(layer, cx + k.dx, ky, k.r, kibble_color);
    }

    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_color = lv_color_make(kFeedDishR, kFeedDishG, kFeedDishB);
    dsc.bg_opa = LV_OPA_COVER;
    dsc.border_opa = LV_OPA_TRANSP;
    dsc.radius = kFeedDishRadius;
    lv_area_t dish = {
        static_cast<int16_t>(cx - kFeedDishW / 2),
        static_cast<int16_t>(mound_anchor),
        static_cast<int16_t>(cx + kFeedDishW / 2 - 1),
        static_cast<int16_t>(mound_anchor + kFeedDishH - 1),
    };
    lv_draw_rect(layer, &dsc, &dish);
}

void EyeAnimation::DrawFeedCrumbs(lv_layer_t* layer, uint32_t now_ms) const {
    if (layer == nullptr) {
        return;
    }
    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_color = lv_color_make(kFeedKibbleR, kFeedKibbleG, kFeedKibbleB);
    dsc.border_opa = LV_OPA_TRANSP;
    dsc.radius = 0;

    for (const auto& crumb : feed_crumbs_) {
        if (!crumb.active) continue;
        const uint32_t age = now_ms - crumb.start_ms;
        if (age >= kFeedCrumbLifeMs) continue;
        const float fade = 1.0f - static_cast<float>(age) / static_cast<float>(kFeedCrumbLifeMs);
        dsc.bg_opa = static_cast<lv_opa_t>(ClampFloat(fade * 255.0f, 0.0f, 255.0f));
        const int16_t x = static_cast<int16_t>(crumb.x);
        const int16_t y = static_cast<int16_t>(crumb.y);
        lv_area_t area = {
            x, y,
            static_cast<int16_t>(x + crumb.size),
            static_cast<int16_t>(y + crumb.size),
        };
        lv_draw_rect(layer, &dsc, &area);
    }
}

void EyeAnimation::DrawFeedSparkles(lv_layer_t* layer, uint32_t now_ms) const {
    if (layer == nullptr || feed_.phase != FeedPhase::Savor) {
        return;
    }
    const uint32_t age = now_ms - feed_.phase_start_ms;
    if (age >= kFeedSavorMs) {
        return;
    }
    const float t = static_cast<float>(age) / static_cast<float>(kFeedSavorMs);
    // Fade in over the first third, out over the rest.
    const float fade = t < 0.33f ? t / 0.33f : 1.0f - (t - 0.33f) / 0.67f;
    const lv_opa_t opa = static_cast<lv_opa_t>(ClampFloat(fade * 255.0f, 0.0f, 255.0f));
    if (opa == 0) {
        return;
    }

    // Sparkles live in the empty band above the eyes -- inside the eye rects they
    // would be white-on-white and invisible.
    struct Sparkle { int16_t x; int16_t y; int8_t s; };
    static constexpr Sparkle kSparkles[5] = {
        {84, 58, 4}, {156, 52, 5}, {118, 42, 3}, {60, 86, 3}, {182, 90, 4},
    };

    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_color = lv_color_white();
    dsc.bg_opa = opa;
    dsc.border_opa = LV_OPA_TRANSP;
    dsc.radius = 0;

    for (const auto& sp : kSparkles) {
        const int16_t rise = static_cast<int16_t>(t * 6.0f);
        const int16_t x = sp.x;
        const int16_t y = static_cast<int16_t>(sp.y - rise);
        lv_area_t v = {static_cast<int16_t>(x - 1), static_cast<int16_t>(y - sp.s),
                       static_cast<int16_t>(x + 1), static_cast<int16_t>(y + sp.s)};
        lv_area_t h = {static_cast<int16_t>(x - sp.s), static_cast<int16_t>(y - 1),
                       static_cast<int16_t>(x + sp.s), static_cast<int16_t>(y + 1)};
        lv_draw_rect(layer, &dsc, &v);
        lv_draw_rect(layer, &dsc, &h);
    }
}

// ---------------------------------------------------------------------------
// Care thought bubble
//
// What it shows is decided by EyeDisplay::UpdateCareExpression; this only
// draws it, over everything else, in the same art as the feeding and bath
// scenes so a child recognises what tapping it will do.
// ---------------------------------------------------------------------------
namespace {
constexpr uint32_t kCareBubbleFill = 0x16202C;
constexpr uint32_t kCareBubbleEdge = 0xCFD8E3;
}  // namespace

void EyeAnimation::SetCareBubble(CareBubble bubble) {
    if (bubble == care_bubble_) {
        return;
    }
    care_bubble_ = bubble;
    care_bubble_since_ms_ = lv_tick_get();
}

bool EyeAnimation::CareBubbleVisible() const {
    return care_bubble_ != CareBubble::None && !sleep_mode_ && !feed_.active &&
           !bath_.active && !hatch_.active && !connecting_active_ &&
           !listening_active_ && !speaking_active_ &&
           legacy_emotion_mode_ == LegacyEmotionMode::None;
}

bool EyeAnimation::IsTouchOnCareBubble(int x, int y) const {
    if (!care_bubble_drawn_ || !CareBubbleVisible()) {
        return false;
    }
    const int dx = x - kCareBubbleX;
    const int dy = y - kCareBubbleY;
    return dx * dx + dy * dy <= kCareBubbleHitR * kCareBubbleHitR;
}

void EyeAnimation::DrawCareBubble(lv_layer_t* layer, uint32_t now_ms) const {
    if (layer == nullptr) {
        return;
    }
    const float grow = ClampFloat(static_cast<float>(now_ms - care_bubble_since_ms_) /
                                  static_cast<float>(kCareBubblePopMs), 0.0f, 1.0f);
    const float pop = EaseOutCubic(grow);
    const float phase = static_cast<float>(now_ms % kCareBubbleBobMs) /
                        static_cast<float>(kCareBubbleBobMs);
    const int bob = RoundToInt(std::sin(phase * 2.0f * 3.14159265f) *
                               static_cast<float>(kCareBubbleBobPx));
    const int bx = kCareBubbleX;
    const int by = kCareBubbleY + bob;
    const lv_color_t fill = lv_color_hex(kCareBubbleFill);
    const lv_color_t edge = lv_color_hex(kCareBubbleEdge);

    // Trail from the eye's top-right corner up to the bubble.
    DrawFilledCircle(layer, 202, 77 + bob / 2, RoundToInt(3.0f * pop), edge);
    DrawFilledCircle(layer, 193, 70 + bob / 2, RoundToInt(5.0f * pop), edge);
    const int r = RoundToInt(static_cast<float>(kCareBubbleR) * pop);
    DrawFilledCircle(layer, bx, by, r, edge);
    DrawFilledCircle(layer, bx, by, r - 2, fill);
    if (grow < 1.0f) {
        return;  // the icon appears once the bubble is full size
    }

    lv_draw_rect_dsc_t rect;
    lv_draw_rect_dsc_init(&rect);
    rect.bg_opa = LV_OPA_COVER;
    rect.border_opa = LV_OPA_TRANSP;

    switch (care_bubble_) {
    case CareBubble::Food: {
        // The feeding scene's kibble and dish, in miniature.
        const lv_color_t kibble = lv_color_make(kFeedKibbleR, kFeedKibbleG, kFeedKibbleB);
        DrawFilledCircle(layer, bx - 5, by - 2, 4, kibble);
        DrawFilledCircle(layer, bx + 5, by - 2, 4, kibble);
        DrawFilledCircle(layer, bx, by - 6, 4, kibble);
        rect.bg_color = lv_color_make(kFeedDishR, kFeedDishG, kFeedDishB);
        rect.radius = 4;
        const lv_area_t dish = {bx - 12, by + 1, bx + 12, by + 8};
        lv_draw_rect(layer, &rect, &dish);
        break;
    }
    case CareBubble::Bath: {
        // The bath scene's foam.
        const lv_color_t rim = lv_color_make(kFoamRimR, kFoamRimG, kFoamRimB);
        const lv_color_t core = lv_color_make(kFoamCoreR, kFoamCoreG, kFoamCoreB);
        struct Blob { int dx, dy, r; };
        constexpr Blob kFoam[3] = {{-6, 4, 7}, {6, 2, 6}, {-1, -7, 5}};
        for (const Blob& f : kFoam) {
            DrawFilledCircle(layer, bx + f.dx, by + f.dy, f.r, rim);
            DrawFilledCircle(layer, bx + f.dx, by + f.dy, f.r - 2, core);
        }
        break;
    }
    case CareBubble::Moon:
        // A crescent: the bubble's own fill bites the moon.
        DrawFilledCircle(layer, bx - 1, by + 1, 11, lv_color_hex(0xFFE08A));
        DrawFilledCircle(layer, bx + 5, by - 4, 10, fill);
        break;
    case CareBubble::Heart: {
        const lv_color_t pink = lv_color_hex(0xFF6FA8);
        DrawFilledCircle(layer, bx - 5, by - 3, 6, pink);
        DrawFilledCircle(layer, bx + 5, by - 3, 6, pink);
        lv_draw_triangle_dsc_t tri;
        lv_draw_triangle_dsc_init(&tri);
        tri.color = pink;
        tri.opa = LV_OPA_COVER;
        tri.p[0].x = bx - 11;
        tri.p[0].y = by - 1;
        tri.p[1].x = bx + 11;
        tri.p[1].y = by - 1;
        tri.p[2].x = bx;
        tri.p[2].y = by + 11;
        lv_draw_triangle(layer, &tri);
        break;
    }
    case CareBubble::Medal: {
        // Ribbon, then the gold disc.
        rect.radius = 1;
        rect.bg_color = lv_color_hex(0x4F8CFF);
        const lv_area_t left = {bx - 8, by - 14, bx - 2, by - 2};
        lv_draw_rect(layer, &rect, &left);
        rect.bg_color = lv_color_hex(0xFF5A5A);
        const lv_area_t right = {bx + 2, by - 14, bx + 8, by - 2};
        lv_draw_rect(layer, &rect, &right);
        DrawFilledCircle(layer, bx, by + 4, 9, lv_color_hex(0xF5C542));
        DrawFilledCircle(layer, bx, by + 4, 5, lv_color_hex(0xFFE38A));
        break;
    }
    case CareBubble::None:
        break;
    }
}

// ---- Mood color targets ----
void EyeAnimation::UpdateMoodColor() {
    if (!mood_color_auto_enabled_) {
        return;
    }

    // The tint comes from EyeDisplay::UpdateCareExpression once a second, so
    // this per-frame path never waits on the care system's lock.
    if (care_tint_ == CareTint::Critical) {
        SetEyeColor(255, 120, 120);
    } else if (care_tint_ == CareTint::Needs) {
        SetEyeColor(170, 210, 255);
    } else if (angry_) {
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

Vox::Mood EyeAnimation::PickMischiefMood() const {
    // Eye Lab: `mood <name>` pins one mood; `mweight` edits the odds live.
    if (lab_forced_mood_ >= 0 && lab_forced_mood_ < 8) {
        return static_cast<Vox::Mood>(lab_forced_mood_);
    }
    int total = 0;
    for (int w : lab_mood_weights_) {
        total += std::max(0, w);
    }
    if (total <= 0) {
        return Vox::Mood::Mumble;
    }
    int roll = RandomInt(1, total);
    for (int i = 0; i < 8; ++i) {
        roll -= std::max(0, lab_mood_weights_[i]);
        if (roll <= 0) {
            return static_cast<Vox::Mood>(i);
        }
    }
    return Vox::Mood::Mumble;
}

void EyeAnimation::SetMoodWeights(const int (&weights)[8]) {
    for (int i = 0; i < 8; ++i) {
        lab_mood_weights_[i] = std::max(0, weights[i]);
    }
}

void EyeAnimation::LabSetMoodWeight(int mood, int weight) {
    if (mood >= 0 && mood < 8) lab_mood_weights_[mood] = std::max(0, weight);
}

int EyeAnimation::LabMoodWeight(int mood) const {
    return (mood >= 0 && mood < 8) ? lab_mood_weights_[mood] : 0;
}

void EyeAnimation::LabSetPose(int side, int w, int h, int r, uint8_t red, uint8_t green, uint8_t blue) {
    EyePose pose{w, h, r, red, green, blue};
    ClampPose(&pose);
    if (!lab_pose_active_) {
        lab_pose_left_ = GetBaseLeftPose();
        lab_pose_right_ = GetBaseRightPose();
        lab_pose_active_ = true;
    }
    if (side == 0 || side == 1) lab_pose_left_ = pose;
    if (side == 0 || side == 2) lab_pose_right_ = pose;
    // Live: swap the pose on screen now, or start a cycle to show it.
    if (mischief_phase_ == MischiefPhase::Holding || mischief_phase_ == MischiefPhase::Changing) {
        mischief_to_left_ = lab_pose_left_;
        mischief_to_right_ = lab_pose_right_;
        if (mischief_phase_ == MischiefPhase::Holding) {
            ApplyMischiefPose(mischief_to_left_, mischief_to_right_);
        }
    } else {
        StartMischiefCycle(lv_tick_get(), true);
    }
}

void EyeAnimation::LabSwapPoses() {
    if (!lab_pose_active_) return;
    std::swap(lab_pose_left_, lab_pose_right_);
    if (lab_shift_px_ != 0) {
        const int left_area = lab_pose_left_.w * lab_pose_left_.h;
        const int right_area = lab_pose_right_.w * lab_pose_right_.h;
        lab_shift_from_ = tilt_off_x_;
        lab_shift_y_from_ = lab_shift_y_to_ = head_off_y_;
        lab_shift_to_ = right_area > left_area ? static_cast<float>(lab_shift_px_)
                      : left_area > right_area ? -static_cast<float>(lab_shift_px_) : 0.0f;
        lab_shift_moving_ = true;
    }
    if (mischief_phase_ == MischiefPhase::Holding || mischief_phase_ == MischiefPhase::Changing) {
        // Ease from whatever is on screen now to the swapped pair.
        mischief_from_left_ = mischief_to_left_;
        mischief_from_right_ = mischief_to_right_;
        mischief_to_left_ = lab_pose_left_;
        mischief_to_right_ = lab_pose_right_;
        mischief_phase_ = MischiefPhase::Changing;
        mischief_phase_start_ms_ = lv_tick_get();
        mischief_phase_duration_ms_ = mischief_config_.change_ms;
    } else {
        StartMischiefCycle(lv_tick_get(), true);
    }
}

// Moves both eyes into a held pose, sliding them by (x, y) px, easing over
// change_ms. Rides the mischief Changing/Holding phases; holds until the next
// head pose or ReleaseHeadPose.
void EyeAnimation::ApplyHeadPose(const EyePose& left, const EyePose& right, float x, float y,
                                 uint32_t change_ms) {
    lab_shift_from_ = tilt_off_x_;
    lab_shift_y_from_ = head_off_y_;
    lab_shift_to_ = x;
    lab_shift_y_to_ = y;
    lab_shift_moving_ = true;
    mischief_from_left_ = (mischief_phase_ == MischiefPhase::Waiting) ? GetBaseLeftPose() : mischief_to_left_;
    mischief_from_right_ = (mischief_phase_ == MischiefPhase::Waiting) ? GetBaseRightPose() : mischief_to_right_;
    lab_pose_left_ = left;
    lab_pose_right_ = right;
    ClampPose(&lab_pose_left_);
    ClampPose(&lab_pose_right_);
    lab_pose_active_ = true;
    head_pose_hold_ = true;  // hold until the next head pose / release
    mischief_to_left_ = lab_pose_left_;
    mischief_to_right_ = lab_pose_right_;
    mischief_phase_ = MischiefPhase::Changing;
    mischief_phase_start_ms_ = lv_tick_get();
    mischief_phase_duration_ms_ = change_ms ? change_ms : mischief_config_.change_ms;
    mischief_started_ = true;
}

// Eases back to the resting eyes and the centre over retreat_ms.
void EyeAnimation::ReleaseHeadPose(uint32_t retreat_ms) {
    lab_pose_active_ = false;
    head_pose_hold_ = false;
    lab_shift_px_ = 0;
    lab_shift_from_ = tilt_off_x_;
    lab_shift_y_from_ = head_off_y_;
    lab_shift_to_ = 0.0f;
    lab_shift_y_to_ = 0.0f;
    if (mischief_phase_ == MischiefPhase::Waiting) {
        lab_shift_moving_ = false;
        tilt_off_x_ = 0.0f;
        head_off_y_ = 0.0f;
        return;
    }
    lab_shift_moving_ = true;
    mischief_from_left_ = mischief_to_left_;
    mischief_from_right_ = mischief_to_right_;
    mischief_to_left_ = GetBaseLeftPose();
    mischief_to_right_ = GetBaseRightPose();
    mischief_phase_ = MischiefPhase::Retreating;
    mischief_phase_start_ms_ = lv_tick_get();
    mischief_phase_duration_ms_ = retreat_ms ? retreat_ms : mischief_config_.retreat_ms;
}

void EyeAnimation::SetTilt(Tilt tilt, uint32_t change_ms, uint32_t retreat_ms) {
    // Locked values, see the header.
    static constexpr int kTiltShiftPx = 10;
    const EyePose small{65, 70, 20, 255, 250, 240};
    const EyePose big{80, 80, 24, 255, 250, 240};

    tilt_ = tilt;
    gaze_ = Gaze::Off;
    if (tilt == Tilt::Off) {
        ReleaseHeadPose(retreat_ms);
        return;
    }
    lab_shift_px_ = kTiltShiftPx;
    const bool left = tilt == Tilt::Left;
    ApplyHeadPose(left ? small : big, left ? big : small,
                  left ? static_cast<float>(kTiltShiftPx) : -static_cast<float>(kTiltShiftPx), 0.0f,
                  change_ms);
}

void EyeAnimation::SetGaze(Gaze gaze, uint32_t change_ms, uint32_t retreat_ms) {
    // Locked values, see the header.
    static constexpr int kGazeShiftPx = 60;
    const EyePose pose{70, 70, 30, 255, 250, 240};

    gaze_ = gaze;
    tilt_ = Tilt::Off;
    if (gaze == Gaze::Off) {
        ReleaseHeadPose(retreat_ms);
        return;
    }
    ApplyHeadPose(pose, pose, 0.0f,
                  gaze == Gaze::Down ? static_cast<float>(kGazeShiftPx) : -static_cast<float>(kGazeShiftPx),
                  change_ms);
}

namespace {
// Locked head-shake values, see the header.
constexpr uint32_t kHeadShakeSwingMs = 500;   // time between direction changes
constexpr uint32_t kHeadShakeEaseMs = 500;    // each swing's ease
constexpr uint32_t kHeadShakeReturnMs = 1000; // ease back when it stops
}  // namespace

// Idle tilt: each new idle look may tilt, but only toward the side the eyes
// are heading. Tilt::Left slides the eyes toward +x, so it is only allowed
// when the look target is on the +x side, and Tilt::Right only on -x. A look
// back to the centre, or to the other side, eases the tilt off first.
void EyeAnimation::MaybeIdleTilt() {
    if (connecting_active_ || speaking_active_ || listening_active_ || idle_tilt_chance_ == 0 || head_shake_active_ || nod_active_ || bounce_active_) return;
    // Leave a running mischief cycle alone; only tilts own the phases here.
    if (tilt_ == Tilt::Off && mischief_phase_ != MischiefPhase::Waiting) return;

    constexpr float kSideThresholdPx = 3.0f;
    const Tilt side = target_off_x_ >= kSideThresholdPx ? Tilt::Left
                    : target_off_x_ <= -kSideThresholdPx ? Tilt::Right
                    : Tilt::Off;
    if (tilt_ != Tilt::Off && tilt_ != side) {
        SetTilt(Tilt::Off);  // never hold a tilt that points away from the eyes
        return;
    }
    if (tilt_ == Tilt::Off && side != Tilt::Off &&
        RandomInt(1, 100) <= static_cast<int>(idle_tilt_chance_)) {
        SetTilt(side);
    }
}

void EyeAnimation::MaybeIdleGaze() {
    if (connecting_active_ || speaking_active_ || listening_active_ || idle_gaze_chance_ == 0 || head_shake_active_ || nod_active_ || bounce_active_) return;
    if (gaze_ == Gaze::Off && mischief_phase_ != MischiefPhase::Waiting) return;

    constexpr float kSideThresholdPx = 3.0f;
    const Gaze side = target_off_y_ >= kSideThresholdPx ? Gaze::Down
                    : target_off_y_ <= -kSideThresholdPx ? Gaze::Up
                    : Gaze::Off;
    if (gaze_ != Gaze::Off && gaze_ != side) {
        SetGaze(Gaze::Off);  // never hold a gaze that points away from the look
        return;
    }
    if (gaze_ == Gaze::Off && side != Gaze::Off &&
        RandomInt(1, 100) <= static_cast<int>(idle_gaze_chance_)) {
        SetGaze(side);
    }
}

// Idle gesture: now and then a whole nod or head shake, from a neutral
// start only (no tilt or gaze held, no mischief pose), with a cooldown so
// it stays an occasional beat rather than a habit.
void EyeAnimation::MaybeIdleGesture(uint32_t now_ms) {
    if (connecting_active_ || speaking_active_ || listening_active_ || idle_gesture_chance_ == 0 || head_shake_active_ || nod_active_ || bounce_active_) return;
    if (tilt_ != Tilt::Off || gaze_ != Gaze::Off) return;
    if (mischief_phase_ != MischiefPhase::Waiting) return;
    if (static_cast<int32_t>(now_ms - idle_gesture_ready_ms_) < 0) return;
    if (RandomInt(1, 100) > static_cast<int>(idle_gesture_chance_)) return;

    constexpr int kNodQuarters = 4;   // down, centre, up, centre
    constexpr int kShakeSwings = 4;   // left, right, left, right
    constexpr int kCooldownMinMs = 20000;
    constexpr int kCooldownMaxMs = 40000;
    if (RandomInt(0, 1) == 0) {
        StartNod(kNodQuarters);
    } else {
        StartHeadShake(kShakeSwings);
    }
    idle_gesture_ready_ms_ = now_ms + static_cast<uint32_t>(RandomInt(kCooldownMinMs, kCooldownMaxMs));
}

// Bounce in place (trial). One bounce = one period: the eyes leave the
// centre, rise bounce_height_px_ and land again, y = -h * sin(pi * p).
// Shape is squash & stretch driven by the same phase:
//   squash  = cos^8(pi p)          -> 1 only at the moment of landing
//   stretch = |cos(pi p)| - squash -> peaks while moving fast, 0 at the top
// Base is the resting 80x80 r24; squash/stretch add their px deltas.
void EyeAnimation::StartBounce(int count) {
    StopNod();
    StopHeadShake();
    tilt_ = Tilt::Off;
    gaze_ = Gaze::Off;
    bounce_active_ = true;
    bounce_left_ = count > 0 ? count : -1;
    bounce_start_ms_ = lv_tick_get();
    lab_pose_active_ = true;
    head_pose_hold_ = true;  // hold until the next head pose / release
    mischief_started_ = true;
}

void EyeAnimation::StartBounceStyle(BounceStyle style, int count) {
    struct Preset { int height_px; uint32_t period_ms; };
    static constexpr Preset kPresets[] = {
        {12, 500},  // LowFast
        {12, 800},  // LowSlow
        {30, 500},  // MidFast
        {30, 800},  // MidSlow
        {50, 800},  // HighSlow
    };
    const Preset& p = kPresets[static_cast<int>(style)];
    SetBounceParams(p.height_px, p.period_ms);
    StartBounce(count);
}

// Connecting: something that reads as "working on it" for a few seconds.
//   Bounce   - MidSlow bounce in place at the centre, until stopped
//   TiltHold - tilt to one side, hold 1.2-2 s, tilt to the other, ...
//   Shake    - continuous head shake
void EyeAnimation::StartConnecting(ConnectingStyle style) {
    if (style == ConnectingStyle::Random) {
        style = static_cast<ConnectingStyle>(RandomInt(1, 3));
    }
    StopConnecting();
    connecting_active_ = true;
    connecting_style_ = style;
    head_off_y_ = 0.0f;
    switch (style) {
        case ConnectingStyle::Bounce:
            StartBounceStyle(BounceStyle::MidSlow, 0);
            break;
        case ConnectingStyle::Shake:
            StartHeadShake(0);
            break;
        case ConnectingStyle::TiltHold:
        case ConnectingStyle::Random:
            connecting_style_ = ConnectingStyle::TiltHold;
            StopBounce();
            StopNod();
            StopHeadShake();
            SetTilt(RandomInt(0, 1) == 0 ? Tilt::Left : Tilt::Right);
            connecting_next_ms_ = lv_tick_get() + static_cast<uint32_t>(RandomInt(1200, 2000));
            break;
    }
}

void EyeAnimation::StopConnecting() {
    if (!connecting_active_) return;
    connecting_active_ = false;
    switch (connecting_style_) {
        case ConnectingStyle::Bounce: StopBounce(); break;
        case ConnectingStyle::Shake: StopHeadShake(); break;
        default: SetTilt(Tilt::Off); break;
    }
}

void EyeAnimation::UpdateSpeaking(uint32_t now_ms) {
    if (!speaking_active_ || bounce_active_ || bounce_settling_) return;
    if (static_cast<int32_t>(now_ms - speaking_next_bounce_ms_) < 0) return;
    StartBounceStyle(speaking_bounce_, 1);
    // Next one only after this bounce is over, plus a random gap.
    speaking_next_bounce_ms_ = now_ms + bounce_period_ms_ +
        static_cast<uint32_t>(RandomInt(kSpeakingGapMinMs, kSpeakingGapMaxMs));
}

void EyeAnimation::UpdateConnecting(uint32_t now_ms) {
    if (!connecting_active_ || connecting_style_ != ConnectingStyle::TiltHold) return;
    if (static_cast<int32_t>(now_ms - connecting_next_ms_) < 0) return;
    SetTilt(tilt_ == Tilt::Left ? Tilt::Right : Tilt::Left);
    connecting_next_ms_ = now_ms + static_cast<uint32_t>(RandomInt(1200, 2000));
}

void EyeAnimation::SetDeviceLook(DeviceLook look) {
    if (look == device_look_) return;
    // Leave the old state first.
    if (device_look_ == DeviceLook::Connecting) StopConnecting();
    if (device_look_ == DeviceLook::Speaking) {
        speaking_active_ = false;
        StopBounce();
    }
    listening_active_ = false;
    device_look_ = look;
    switch (look) {
        case DeviceLook::Connecting:
            StartConnecting(ConnectingStyle::Shake);
            break;
        case DeviceLook::Speaking:
            // Still (blinking allowed), with one LowSlow bounce now and then.
            speaking_active_ = true;
            StopNod();
            StopHeadShake();
            if (tilt_ != Tilt::Off || gaze_ != Gaze::Off) {
                ReleaseHeadPose(0);
                tilt_ = Tilt::Off;
                gaze_ = Gaze::Off;
            }
            speaking_next_bounce_ms_ = lv_tick_get() +
                static_cast<uint32_t>(RandomInt(kSpeakingGapMinMs, kSpeakingGapMaxMs));
            break;
        case DeviceLook::Listening:
            // Still and attentive: centred, no look-around, no idle extras,
            // no mischief; the autoblinker keeps running.
            listening_active_ = true;
            StopBounce();
            StopNod();
            StopHeadShake();
            if (tilt_ != Tilt::Off || gaze_ != Gaze::Off || mischief_phase_ != MischiefPhase::Waiting) {
                ReleaseHeadPose(0);
                tilt_ = Tilt::Off;
                gaze_ = Gaze::Off;
            }
            break;
        case DeviceLook::Idle:
            break;  // normal idle eyes
    }
}

void EyeAnimation::PlaySurprised() {
    surprised_after_bounce_ = true;
    StartBounceStyle(BounceStyle::MidFast, 1);
}

void EyeAnimation::CancelSurprised() {
    if (surprised_after_bounce_) {
        surprised_after_bounce_ = false;
        StopBounce();
    }
}

void EyeAnimation::StopBounce() {
    if (!bounce_active_) return;
    bounce_active_ = false;
    ReleaseHeadPose(bounce_period_ms_ / 2);
}

void EyeAnimation::UpdateBounce(uint32_t now_ms) {
    if (bounce_settling_) {
        HalfStep(head_off_y_, 0.0f);
        if (std::fabs(head_off_y_) < 0.5f) {
            head_off_y_ = 0.0f;
            bounce_settling_ = false;
        }
    }
    if (!bounce_active_) return;
    const uint32_t period = std::max<uint32_t>(50, bounce_period_ms_);
    const uint32_t elapsed = now_ms - bounce_start_ms_;
    if (bounce_left_ > 0 && elapsed >= static_cast<uint32_t>(bounce_left_) * period) {
        // Landed for the last time. Hand the squashed shape to the geometry
        // lerp instead of snapping: current = the landing pose, target = the
        // resting eye, so it springs back over a few frames.
        bounce_active_ = false;
        bounce_settling_ = true;
        lab_pose_active_ = false;
        head_pose_hold_ = false;
        lab_shift_moving_ = false;
        mischief_phase_ = MischiefPhase::Waiting;
        mischief_phase_start_ms_ = 0;
        mischief_phase_duration_ms_ = 0;
        mischief_started_ = false;
        SyncBasePoseTargets();
        if (surprised_after_bounce_) {
            surprised_after_bounce_ = false;
            SetSurprised(true);  // radius eases 24 -> 40 while it settles
        }
        return;
    }
    const float p = static_cast<float>(elapsed % period) / static_cast<float>(period);
    const float c = std::cos(3.14159265f * p);
    const float ac = std::fabs(c);
    const float c2 = c * c, c4 = c2 * c2;
    const float squash = c4 * c4;
    const float stretch = std::max(0.0f, ac - squash);

    // Speaking bounces the smaller session eye; everything else the resting one.
    EyePose pose = speaking_active_ ? EyePose{SESSION_SIZE, SESSION_SIZE, SESSION_RADIUS, 255, 250, 240}
                                    : EyePose{80, 80, 24, 255, 250, 240};
    const float base_h = static_cast<float>(pose.h);
    pose.w += RoundToInt(squash * bounce_squash_[0] + stretch * bounce_stretch_[0]);
    pose.h += RoundToInt(squash * bounce_squash_[1] + stretch * bounce_stretch_[1]);
    pose.radius += RoundToInt(squash * bounce_squash_[2] + stretch * bounce_stretch_[2]);
    ClampPose(&pose);
    // Keep the bottom edge on the floor while squashing, so it reads as
    // landing rather than shrinking towards the middle.
    const float floor_fix = (base_h - static_cast<float>(pose.h)) * 0.5f;
    head_off_y_ = -static_cast<float>(bounce_height_px_) * std::sin(3.14159265f * p) + floor_fix;
    tilt_off_x_ = 0.0f;
    lab_shift_moving_ = false;
    lab_pose_left_ = lab_pose_right_ = pose;
    mischief_to_left_ = mischief_to_right_ = pose;
    mischief_phase_ = MischiefPhase::Holding;
    mischief_phase_start_ms_ = now_ms;
    mischief_phase_duration_ms_ = 3600000u;
    ApplyMischiefPose(pose, pose);
}

void EyeAnimation::StartNod(int swings) {
    StopBounce();
    StopHeadShake();
    tilt_ = Tilt::Off;
    gaze_ = Gaze::Off;
    nod_active_ = true;
    nod_left_ = swings > 0 ? swings : -1;  // quarter nods (centre->down = 1)
    nod_start_ms_ = lv_tick_get();
    lab_pose_active_ = true;
    head_pose_hold_ = true;  // hold until the next head pose / release
    mischief_started_ = true;
}

void EyeAnimation::StopNod() {
    if (!nod_active_) return;
    nod_active_ = false;
    gaze_ = Gaze::Off;
    ReleaseHeadPose(nod_return_ms_);
}

// Nod: one continuous sine, so the eyes pass the centre at full speed and
// only slow down at the top and bottom (step-by-step easing stopped dead at
// the centre). Height y = nod_px_ * sin(phase), down first; the shape
// follows |sin|: resting 80x80 r24 at the centre, 70x70 r30 at +-nod_px_.
// A full nod (down, centre, up, centre) takes 4 * nod_swing_ms_.
void EyeAnimation::UpdateNod(uint32_t now_ms) {
    if (!nod_active_) return;
    const uint32_t elapsed = now_ms - nod_start_ms_;
    if (nod_left_ > 0 && elapsed >= static_cast<uint32_t>(nod_left_) * nod_swing_ms_) {
        StopNod();
        return;
    }
    const EyePose centre{80, 80, 24, 255, 250, 240};
    const EyePose moved{70, 70, 30, 255, 250, 240};
    const float phase = 6.2831853f * static_cast<float>(elapsed) /
                        static_cast<float>(4 * std::max<uint32_t>(1, nod_swing_ms_));
    const float s = std::sin(phase);
    EyePose pose = LerpPose(centre, moved, std::fabs(s));
    head_off_y_ = s * static_cast<float>(nod_px_);
    tilt_off_x_ = 0.0f;
    lab_shift_moving_ = false;
    lab_pose_left_ = lab_pose_right_ = pose;
    mischief_to_left_ = mischief_to_right_ = pose;
    mischief_phase_ = MischiefPhase::Holding;
    mischief_phase_start_ms_ = now_ms;
    mischief_phase_duration_ms_ = 3600000u;  // the nod, not the hold timer, ends it
    ApplyMischiefPose(pose, pose);
}

void EyeAnimation::StartHeadShake(int swings) {
    StopNod();
    StopBounce();
    head_shake_active_ = true;
    head_shake_left_ = swings > 0 ? swings : -1;
    head_shake_next_ms_ = lv_tick_get();  // first swing now
}

void EyeAnimation::StopHeadShake() {
    if (!head_shake_active_) return;
    head_shake_active_ = false;
    SetTilt(Tilt::Off, 0, kHeadShakeReturnMs);
}

void EyeAnimation::UpdateHeadShake(uint32_t now_ms) {
    if (!head_shake_active_ || now_ms < head_shake_next_ms_) return;
    if (head_shake_left_ == 0) {
        StopHeadShake();
        return;
    }
    SetTilt(tilt_ == Tilt::Left ? Tilt::Right : Tilt::Left, kHeadShakeEaseMs);
    if (head_shake_left_ > 0) --head_shake_left_;
    head_shake_next_ms_ = now_ms + kHeadShakeSwingMs;
}

void EyeAnimation::LabClearPose() {
    lab_pose_active_ = false;
}

void EyeAnimation::LabDescribe() const {
    static const char* kNames[] = {"mumble", "hum", "think", "surprise", "happy", "laugh", "annoyed", "sleepy"};
    static const char* kPhase[] = {"waiting", "changing", "holding", "retreating"};
    printf("mischief: %s, phase %s, mood %s%s\n", mischief_enabled_ ? "on" : "off",
           kPhase[static_cast<int>(mischief_phase_)], kNames[static_cast<int>(mischief_mood_)],
           lab_forced_mood_ >= 0 ? " (forced)" : "");
    printf("weights:");
    for (int i = 0; i < 8; ++i) printf(" %s=%d", kNames[i], lab_mood_weights_[i]);
    printf("\nhold: %s\n", lab_hold_ms_ ? std::to_string(lab_hold_ms_).c_str() : "per mood");
    const auto& m = mischief_config_;
    printf("cfg: stay %lu-%lu ms, change %lu ms, retreat %lu ms\n", (unsigned long)m.min_stay_ms,
           (unsigned long)m.max_stay_ms, (unsigned long)m.change_ms, (unsigned long)m.retreat_ms);
    printf("cfg limits: w %d-%d h %d-%d radius %d-%d rgb %u-%u/%u-%u/%u-%u\n", m.min_w, m.max_w, m.min_h,
           m.max_h, m.min_radius, m.max_radius, m.min_r, m.max_r, m.min_g, m.max_g, m.min_b, m.max_b);
    const EyePose& l = mischief_to_left_;
    const EyePose& r = mischief_to_right_;
    printf("pose L: %dx%d r%d rgb(%u,%u,%u)  R: %dx%d r%d rgb(%u,%u,%u)%s\n", l.w, l.h, l.radius, l.red,
           l.green, l.blue, r.w, r.h, r.radius, r.red, r.green, r.blue,
           lab_pose_active_ ? "  [custom pose]" : "");
}

void EyeAnimation::MakeMoodPoses(Vox::Mood mood, const EyePose& left_base, const EyePose& right_base,
                                 EyePose* left, EyePose* right) const {
    struct Rgb {
        uint8_t r;
        uint8_t g;
        uint8_t b;
    };
    auto tint = [](EyePose* pose, const Rgb& c) {
        pose->red = c.r;
        pose->green = c.g;
        pose->blue = c.b;
    };
    auto pick = [](std::initializer_list<Rgb> colors) {
        return *(colors.begin() + RandomInt(0, static_cast<int>(colors.size()) - 1));
    };
    constexpr Rgb kCoral{255, 92, 122};
    constexpr Rgb kMango{255, 162, 65};
    constexpr Rgb kYellow{255, 214, 64};
    constexpr Rgb kLime{118, 255, 88};
    constexpr Rgb kMint{72, 250, 210};
    constexpr Rgb kSky{75, 190, 255};
    constexpr Rgb kViolet{164, 110, 255};
    constexpr Rgb kMagenta{255, 98, 230};

    EyePose pose = left_base;
    switch (mood) {
        case Vox::Mood::Surprise:
            // Wide-open round eyes.
            pose.w = pose.h = RandomInt(90, 96);
            pose.radius = 36;
            tint(&pose, pick({kYellow, kSky}));
            break;
        case Vox::Mood::Happy:
            // Wide, soft, a little shorter - a smiling squint.
            pose.w = RandomInt(86, 96);
            pose.h = RandomInt(54, 62);
            pose.radius = RandomInt(30, 36);
            tint(&pose, pick({kLime, kYellow, kMagenta}));
            break;
        case Vox::Mood::Laugh:
            // Squeezed almost shut from laughing.
            pose.w = RandomInt(90, 96);
            pose.h = RandomInt(38, 46);
            pose.radius = RandomInt(18, 22);
            tint(&pose, pick({kYellow, kMango}));
            break;
        case Vox::Mood::Annoyed:
            // Flat, hard-edged slits.
            pose.w = RandomInt(84, 96);
            pose.h = RandomInt(34, 40);
            pose.radius = RandomInt(2, 8);
            tint(&pose, kCoral);
            break;
        case Vox::Mood::Sleepy:
            // Heavy, half-closed and narrower.
            pose.w = RandomInt(72, 84);
            pose.h = RandomInt(34, 38);
            pose.radius = RandomInt(14, 18);
            tint(&pose, kViolet);
            break;
        case Vox::Mood::Think: {
            // One eye narrows, the other stays put - a lopsided "hmm?".
            EyePose narrowed = left_base;
            narrowed.w = RandomInt(70, 78);
            narrowed.h = RandomInt(48, 56);
            narrowed.radius = RandomInt(16, 22);
            EyePose steady = right_base;
            tint(&narrowed, kMint);
            tint(&steady, kMint);
            ClampPose(&narrowed);
            ClampPose(&steady);
            if (RandomInt(0, 1) == 0) {
                *left = narrowed;
                *right = steady;
            } else {
                *left = steady;
                *right = narrowed;
            }
            return;
        }
        case Vox::Mood::Mumble:
        case Vox::Mood::Hum:
            // Talking/humming to itself: stay close to the resting shape and
            // only shift colour, so the eyes read as "busy", not as a feeling.
            pose.w = left_base.w + RandomInt(-10, 10);
            pose.h = left_base.h + RandomInt(-10, 10);
            pose.radius = left_base.radius + RandomInt(-6, 6);
            tint(&pose, mood == Vox::Mood::Hum ? pick({kMango, kMagenta}) : pick({kSky, kMint}));
            break;
    }
    ClampPose(&pose);
    *left = pose;
    *right = pose;
}

uint32_t EyeAnimation::PickMoodHoldMs(Vox::Mood mood) const {
    if (head_pose_hold_) {
        return 3600000u;  // tilt/gaze/nod/bounce: held until changed or released
    }
    if (lab_hold_ms_ > 0) {
        return lab_hold_ms_;  // console `mhold`
    }
    // A reaction only reads as one if it is brief; mumbling/humming keeps the
    // engine's long idle range. The voice can still stretch this via
    // ExtendMischiefHold so a clip never outlives its pose.
    switch (mood) {
        case Vox::Mood::Surprise:
            return RandomInt(1200, 2500);
        case Vox::Mood::Laugh:
            return RandomInt(1500, 3000);
        case Vox::Mood::Happy:
        case Vox::Mood::Annoyed:
            return RandomInt(2000, 4000);
        case Vox::Mood::Think:
            return RandomInt(2500, 5000);
        case Vox::Mood::Sleepy:
            return RandomInt(3000, 6000);
        case Vox::Mood::Mumble:
        case Vox::Mood::Hum:
            break;
    }
    return RandomInt(static_cast<int>(mischief_config_.min_stay_ms),
                     static_cast<int>(mischief_config_.max_stay_ms));
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
    head_pose_hold_ = false;
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

void EyeAnimation::StartMischiefCycle(uint32_t now_ms, bool use_lab_pose) {
    auto left_base = GetBaseLeftPose();
    auto right_base = GetBaseRightPose();

    // Ease from what is on screen, which may be a held head pose.
    const bool from_pose = mischief_phase_ != MischiefPhase::Waiting;
    mischief_from_left_ = from_pose ? mischief_to_left_ : left_base;
    mischief_from_right_ = from_pose ? mischief_to_right_ : right_base;

    if (!use_lab_pose) {
        // A mood cycle replaces any held tilt/gaze/nod/bounce: drop it and slide
        // its offsets home during the change.
        bounce_active_ = false;
        nod_active_ = false;
        head_shake_active_ = false;
        tilt_ = Tilt::Off;
        gaze_ = Gaze::Off;
        lab_pose_active_ = false;
        head_pose_hold_ = false;
        lab_shift_from_ = tilt_off_x_;
        lab_shift_y_from_ = head_off_y_;
        lab_shift_to_ = 0.0f;
        lab_shift_y_to_ = 0.0f;
        lab_shift_moving_ = tilt_off_x_ != 0.0f || head_off_y_ != 0.0f;
    }

    // Feeling first, then the pose for it; the voice layer reads
    // GetMischiefMood() so the clip it plays matches what is on screen.
    mischief_mood_ = PickMischiefMood();
    MakeMoodPoses(mischief_mood_, left_base, right_base, &mischief_to_left_, &mischief_to_right_);
    if (lab_pose_active_) {
        mischief_to_left_ = lab_pose_left_;
        mischief_to_right_ = lab_pose_right_;
    }

    mischief_phase_ = MischiefPhase::Changing;
    mischief_phase_start_ms_ = now_ms;
    mischief_phase_duration_ms_ = mischief_config_.change_ms;
    mischief_started_ = true;
    EmitInteractionEvent(InteractionEvent::Mischief);
}

void EyeAnimation::UpdateMischief(uint32_t now_ms) {
    UpdateConnecting(now_ms);
    UpdateSpeaking(now_ms);
    UpdateHeadShake(now_ms);
    UpdateNod(now_ms);
    UpdateBounce(now_ms);
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
            if (listening_active_ || speaking_active_ || connecting_active_) {
                return;  // no timed mischief while in a session state
            }
            if (now_ms >= mischief_next_cycle_ms_) {
                StartMischiefCycle(now_ms);
            }
            return;

        case MischiefPhase::Changing: {
            float t = static_cast<float>(now_ms - mischief_phase_start_ms_) /
                      static_cast<float>(mischief_phase_duration_ms_);
            if (lab_shift_moving_) {
                const float st = ClampFloat(t, 0.0f, 1.0f);
                tilt_off_x_ = Lerp(lab_shift_from_, lab_shift_to_, EaseInOutCubic(st));
                head_off_y_ = Lerp(lab_shift_y_from_, lab_shift_y_to_, EaseInOutCubic(st));
                if (st >= 1.0f) lab_shift_moving_ = false;
            }
            if (t >= 1.0f) {
                ApplyMischiefPose(mischief_to_left_, mischief_to_right_);
                mischief_phase_ = MischiefPhase::Holding;
                mischief_phase_start_ms_ = now_ms;
                mischief_phase_duration_ms_ = std::max<uint32_t>(
                    PickMoodHoldMs(mischief_mood_), mischief_min_hold_extension_ms_);
                mischief_min_hold_extension_ms_ = 0;
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
                if (lab_shift_moving_) {
                    tilt_off_x_ = lab_shift_to_;
                    head_off_y_ = lab_shift_y_to_;
                    lab_shift_moving_ = false;
                }
                FinishMischiefCycle();
                if (mischief_enabled_) {
                    mischief_next_cycle_ms_ = now_ms + RandomInt(
                        static_cast<int>(mischief_config_.min_stay_ms),
                        static_cast<int>(mischief_config_.max_stay_ms));
                }
                return;
            }
            float eased = EaseInOutCubic(ClampFloat(t, 0.0f, 1.0f));
            if (lab_shift_moving_) {
                tilt_off_x_ = Lerp(lab_shift_from_, lab_shift_to_, eased);
                head_off_y_ = Lerp(lab_shift_y_from_, lab_shift_y_to_, eased);
            }
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
    care_bubble_drawn_ = false;

    if (hatch_.active) {
        RenderHatchingFrame(lv_tick_get());
        return;
    }

    lv_color_t background = lv_color_black();
    if (legacy_emotion_mode_ == LegacyEmotionMode::Cyclop) {
        background = lv_color_make(255, 224, 0);
    }
    lv_canvas_fill_bg(canvas_, background, LV_OPA_COVER);

    if (legacy_emotion_mode_ != LegacyEmotionMode::None) {
        const int legacy_w = std::max(56, eye_l_w_default_);
        const int legacy_h = std::max(56, eye_l_h_default_);
        const int legacy_gap = std::max(10, gap_default_);
        const int cx = screen_w_ / 2;
        const int cy = screen_h_ / 2;

        if (legacy_emotion_mode_ == LegacyEmotionMode::Cyclop) {
            left_eye_box_ = {static_cast<int16_t>(cx - 96), static_cast<int16_t>(cy - 96), 192, 192};
            right_eye_box_ = {};
            left_touch_box_ = {static_cast<int16_t>(cx - 110), static_cast<int16_t>(cy - 110), 220, 220};
            right_touch_box_ = {};
        } else {
            const int left_x = cx - legacy_gap / 2 - legacy_w;
            const int right_x = cx + legacy_gap / 2;
            const int top_y = cy - legacy_h / 2;
            left_eye_box_ = {static_cast<int16_t>(left_x), static_cast<int16_t>(top_y),
                             static_cast<int16_t>(legacy_w), static_cast<int16_t>(legacy_h)};
            right_eye_box_ = {static_cast<int16_t>(right_x), static_cast<int16_t>(top_y),
                              static_cast<int16_t>(legacy_w), static_cast<int16_t>(legacy_h)};
            left_touch_box_ = {static_cast<int16_t>(left_x - TOUCH_PAD_X), static_cast<int16_t>(top_y - TOUCH_PAD_Y),
                               static_cast<int16_t>(legacy_w + TOUCH_PAD_X * 2),
                               static_cast<int16_t>(legacy_h + TOUCH_PAD_Y * 2)};
            right_touch_box_ = {static_cast<int16_t>(right_x - TOUCH_PAD_X), static_cast<int16_t>(top_y - TOUCH_PAD_Y),
                                static_cast<int16_t>(legacy_w + TOUCH_PAD_X * 2),
                                static_cast<int16_t>(legacy_h + TOUCH_PAD_Y * 2)};
        }

        lv_layer_t layer;
        lv_canvas_init_layer(canvas_, &layer);
        const uint32_t now_ms = lv_tick_get();
        if (legacy_emotion_mode_ == LegacyEmotionMode::Love) {
            DrawLegacyLove(&layer, now_ms);
        } else if (legacy_emotion_mode_ == LegacyEmotionMode::Cyclop) {
            DrawLegacyCyclop(&layer, now_ms);
        } else if (legacy_emotion_mode_ == LegacyEmotionMode::Drunk) {
            DrawLegacyDrunk(&layer, now_ms);
        } else if (legacy_emotion_mode_ == LegacyEmotionMode::Confuse) {
            DrawLegacyConfuse(&layer, now_ms);
        } else if (legacy_emotion_mode_ == LegacyEmotionMode::Angry) {
            DrawLegacyAngry(&layer, now_ms);
        } else if (legacy_emotion_mode_ == LegacyEmotionMode::Furious) {
            DrawLegacyFurious(&layer, now_ms);
        } else if (legacy_emotion_mode_ == LegacyEmotionMode::BanhChung) {
            DrawLegacyBanhChung(&layer, now_ms);
        } else if (legacy_emotion_mode_ == LegacyEmotionMode::Deadpool) {
            DrawLegacyDeadpool(&layer, now_ms);
        } else if (legacy_emotion_mode_ == LegacyEmotionMode::Cry) {
            DrawLegacyCry(&layer, now_ms);
        }
        lv_canvas_finish_layer(canvas_, &layer);
        return;
    }

    MarkLegacySeedRuntimeNone(this);

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
    if (sleep_mode_) {
        left_eye_w = eye_l_w_default_;
        right_eye_w = eye_r_w_default_;
        left_eye_h = std::max(CLOSED_HEIGHT, static_cast<int>(RoundToInt(static_cast<float>(eye_l_h_default_) * 0.10f)));
        right_eye_h = std::max(CLOSED_HEIGHT, static_cast<int>(RoundToInt(static_cast<float>(eye_r_h_default_) * 0.10f)));
    }

    // Eye center (all offsets combined)
    int cx = screen_w_ / 2 + RoundToInt(off_x_ + tilt_off_x_ + touch_off_x_ + imu_off_x_) + flicker_off_x_;
    int cy = screen_h_ / 2 + base_off_y_ + RoundToInt(head_off_y_ + off_y_ + bounce_y_ + touch_off_y_ + imu_off_y_ + angry_bounce_off_y_ + feed_shake_y_) + flicker_off_y_;

    // Eye Y positions (blink shifts top edge down)
    int left_base_top    = cy - left_eye_h / 2;
    int left_base_bottom = left_base_top + left_eye_h;
    int right_base_top    = cy - right_eye_h / 2;
    int right_base_bottom = right_base_top + right_eye_h;

    // Clamp blink offset
    int left_top_y  = left_base_top + (blink_left_  ? static_cast<int>(top_offset_) : 0);
    int right_top_y = right_base_top + (blink_right_ ? static_cast<int>(top_offset_) : 0);
    // Dozing: the top edge sinks by doze_close_ px, and while dozing the eye
    // never opens taller than doze_max_open_px_ (user, 2026-09-29: 60 of 80).
    if (doze_close_ > 0.0f) {
        left_top_y  += RoundToInt(doze_close_);
        right_top_y += RoundToInt(doze_close_);
    }
    if (doze_active_ && doze_capped_) {
        left_top_y  = std::max(left_top_y,  left_base_bottom  - doze_max_open_px_);
        right_top_y = std::max(right_top_y, right_base_bottom - doze_max_open_px_);
    }
    if (sleep_mode_) {
        left_top_y = left_base_top;
        right_top_y = right_base_top;
    }
    if (left_top_y  > left_base_bottom - CLOSED_HEIGHT) left_top_y  = left_base_bottom - CLOSED_HEIGHT;
    if (right_top_y > right_base_bottom - CLOSED_HEIGHT) right_top_y = right_base_bottom - CLOSED_HEIGHT;

    int left_h  = left_base_bottom - left_top_y;
    int right_h = right_base_bottom - right_top_y;

    // X positions
    int gap   = cyclops_ ? 0 : gap_current_;
    if (sleep_mode_) {
        gap = gap_default_;
    }
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

    // ---- Grime (under the eyelids, so a blink hides it) ----
    DrawSmudges(&layer);

    // ---- Eyelid overlays (black triangles/rects on top of eyes) ----
    lv_draw_triangle_dsc_t tri;
    lv_draw_triangle_dsc_init(&tri);
    tri.color = lv_color_black();
    tri.opa   = LV_OPA_COVER;

    int th_tired = static_cast<int>(eyelids_tired_h_);
    int th_angry = static_cast<int>(eyelids_angry_h_);
    int hoff_bot = static_cast<int>(eyelids_happy_off_ + feed_squint_px_);
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

    if (sleep_mode_) {
        const int sleep_anchor_y = std::min(l_top, r_top) - 10;
        DrawSleepZzz(&layer, lv_tick_get(), sleep_anchor_y);
    }

    if (feed_.active) {
        const uint32_t feed_now_ms = lv_tick_get();
        DrawFeedFood(&layer);
        DrawFeedCrumbs(&layer, feed_now_ms);
        DrawFeedSparkles(&layer, feed_now_ms);
    }

    if (bath_.active) {
        DrawBathWater(&layer);
        DrawBathShower(&layer);
        DrawBathFoam(&layer);
    }

    // ---- Care thought bubble, over everything ----
    // A small overlap with a taller pose is fine; an eye that reaches well up
    // into the bubble (gaze up, bounce) hides it for those frames instead.
    if (CareBubbleVisible()) {
        bool clear = true;
        for (const EyeBounds* eye : {&left_eye_box_, &right_eye_box_}) {
            if (eye == &right_eye_box_ && cyclops_) continue;
            const bool overlaps_x = eye->x < kCareBubbleX + kCareBubbleR &&
                                    eye->x + eye->w > kCareBubbleX - kCareBubbleR;
            if (eye->w > 0 && overlaps_x && eye->y < kCareBubbleY + kCareBubbleR - 10) {
                clear = false;
            }
        }
        if (clear) {
            DrawCareBubble(&layer, lv_tick_get());
            care_bubble_drawn_ = true;
        }
    }

    lv_canvas_finish_layer(canvas_, &layer);
}

void EyeAnimation::DrawHatchEgg(lv_layer_t* layer, float cx, float cy, float w, float h,
                                float radius, float lobe, bool glow) const {
    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_color = lv_color_make(245, 245, 245);
    dsc.bg_opa = LV_OPA_COVER;
    dsc.border_opa = LV_OPA_TRANSP;
    dsc.radius = static_cast<int16_t>(radius + 0.5f);

    if (glow) {
        lv_draw_rect_dsc_t glow_dsc = dsc;
        glow_dsc.bg_opa = 50;
        glow_dsc.radius = static_cast<int16_t>(radius + 6);
        int16_t glow_w = static_cast<int16_t>(w + 10);
        int16_t glow_h = static_cast<int16_t>(h + 10);
        lv_area_t glow_area = {
            static_cast<int16_t>(cx - glow_w / 2),
            static_cast<int16_t>(cy - glow_h / 2),
            static_cast<int16_t>(cx + glow_w / 2 - 1),
            static_cast<int16_t>(cy + glow_h / 2 - 1),
        };
        lv_draw_rect(layer, &glow_dsc, &glow_area);
    }

    if (lobe <= 0.01f) {
        lv_area_t area = {
            static_cast<int16_t>(cx - w / 2),
            static_cast<int16_t>(cy - h / 2),
            static_cast<int16_t>(cx + w / 2 - 1),
            static_cast<int16_t>(cy + h / 2 - 1),
        };
        lv_draw_rect(layer, &dsc, &area);
        return;
    }

    float circle_size = h;
    float circle_radius = circle_size / 2.0f;
    float offset = 8.0f + lobe * 8.0f;
    lv_area_t left = {
        static_cast<int16_t>(cx - offset - circle_radius),
        static_cast<int16_t>(cy - circle_radius),
        static_cast<int16_t>(cx - offset + circle_radius - 1),
        static_cast<int16_t>(cy + circle_radius - 1),
    };
    lv_area_t right = {
        static_cast<int16_t>(cx + offset - circle_radius),
        static_cast<int16_t>(cy - circle_radius),
        static_cast<int16_t>(cx + offset + circle_radius - 1),
        static_cast<int16_t>(cy + circle_radius - 1),
    };
    dsc.radius = static_cast<int16_t>(circle_radius);
    lv_draw_rect(layer, &dsc, &left);
    lv_draw_rect(layer, &dsc, &right);
}

void EyeAnimation::DrawHatchEyes(lv_layer_t* layer, float cx, float cy, float split_t,
                                 float base_size, float base_radius,
                                 float blink_scale) const {
    const float final_offset = (80.0f + 10.0f) * 0.5f;
    float offset = final_offset * split_t;
    float size = base_size;
    float height = base_size * blink_scale;
    if (height < static_cast<float>(CLOSED_HEIGHT)) {
        height = static_cast<float>(CLOSED_HEIGHT);
    }
    float radius = base_radius;
    if (radius > height / 2.0f) {
        radius = height / 2.0f;
    }

    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_color = lv_color_make(245, 245, 245);
    dsc.bg_opa = LV_OPA_COVER;
    dsc.border_opa = LV_OPA_TRANSP;
    dsc.radius = static_cast<int16_t>(radius + 0.5f);

    int16_t w = static_cast<int16_t>(size + 0.5f);
    int16_t h = static_cast<int16_t>(height + 0.5f);
    int16_t left_cx = static_cast<int16_t>(cx - offset + 0.5f);
    int16_t right_cx = static_cast<int16_t>(cx + offset + 0.5f);
    int16_t cy_i = static_cast<int16_t>(cy + 0.5f);

    lv_area_t left = {
        static_cast<int16_t>(left_cx - w / 2),
        static_cast<int16_t>(cy_i - h / 2),
        static_cast<int16_t>(left_cx + w / 2 - 1),
        static_cast<int16_t>(cy_i + h / 2 - 1),
    };
    lv_area_t right = {
        static_cast<int16_t>(right_cx - w / 2),
        static_cast<int16_t>(cy_i - h / 2),
        static_cast<int16_t>(right_cx + w / 2 - 1),
        static_cast<int16_t>(cy_i + h / 2 - 1),
    };
    lv_draw_rect(layer, &dsc, &left);
    lv_draw_rect(layer, &dsc, &right);
}

void EyeAnimation::RenderHatchingFrame(uint32_t now_ms) {
    lv_canvas_fill_bg(canvas_, lv_color_black(), LV_OPA_COVER);

    lv_layer_t layer;
    lv_canvas_init_layer(canvas_, &layer);

    auto hatch_tap_bob = [now_ms, this]() -> float {
        if (hatch_.tap_bob_duration_ms == 0) {
            return 0.0f;
        }
        if (now_ms < hatch_.tap_bob_start_ms) {
            return 0.0f;
        }
        uint32_t elapsed = now_ms - hatch_.tap_bob_start_ms;
        if (elapsed >= hatch_.tap_bob_duration_ms) {
            return 0.0f;
        }
        float t = static_cast<float>(elapsed) /
                  static_cast<float>(hatch_.tap_bob_duration_ms);
        return std::sin(t * 3.1415926f) * hatch_.tap_bob_amp;
    };

    float tap_bob = hatch_tap_bob();
    float center_x = hatch_.pos_x;
    float center_y = hatch_.pos_y;
    float bob_offset = 0.0f;

    uint32_t phase_elapsed = now_ms - hatch_.phase_start_ms;

    if (hatch_.phase == 1) {
        bob_offset = -tap_bob;
        DrawHatchEgg(&layer, center_x, center_y + bob_offset, HATCH_BASE_SIZE,
                     HATCH_BASE_SIZE, HATCH_BASE_SIZE / 2.0f, 0.0f, true);
    } else if (hatch_.phase == 2) {
        float period =
            (hatch_.phase2_boost_until_ms > now_ms) ? 2600.0f : 3600.0f;
        float auto_bob = std::sin(phase_elapsed * 2.0f * 3.1415926f / period) * 3.0f;
        bob_offset = auto_bob - tap_bob;
        DrawHatchEgg(&layer, center_x, center_y + bob_offset, HATCH_BASE_SIZE,
                     HATCH_BASE_SIZE, HATCH_BASE_SIZE / 2.0f, 0.0f, false);
    } else if (hatch_.phase == 3) {
        float t = HatchClamp01(static_cast<float>(phase_elapsed) /
                               static_cast<float>(HATCH_PHASE3_MS));
        float deform = HatchSmoothstep(t);
        float stretch = 1.0f + 0.12f * std::sin(now_ms * 0.002f);
        float width = HATCH_BASE_SIZE * (1.0f + 0.18f * deform) * stretch;
        float height = HATCH_BASE_SIZE * (1.0f - 0.10f * deform);
        float radius =
            Lerp(HATCH_BASE_SIZE / 2.0f, HATCH_BASE_SIZE * 0.28f, deform);
        float lobe = HatchClamp01((t - 0.65f) / 0.35f);
        float auto_bob = hatch_.moving
                             ? 0.0f
                             : std::sin(phase_elapsed * 2.0f * 3.1415926f / 3200.0f) * 3.5f;
        bob_offset = auto_bob - tap_bob;
        float twitch_x = 0.0f;
        float twitch_y = 0.0f;
        if (hatch_.twitch_duration_ms > 0 && now_ms >= hatch_.twitch_start_ms) {
            uint32_t twitch_elapsed = now_ms - hatch_.twitch_start_ms;
            if (twitch_elapsed < hatch_.twitch_duration_ms) {
                float k = 1.0f -
                          (static_cast<float>(twitch_elapsed) /
                           static_cast<float>(hatch_.twitch_duration_ms));
                twitch_x = hatch_.twitch_x * k;
                twitch_y = hatch_.twitch_y * k;
            }
        }
        DrawHatchEgg(&layer, center_x + twitch_x, center_y + bob_offset + twitch_y,
                     width, height, radius, lobe, false);
    } else if (hatch_.phase == 4) {
        float t = HatchClamp01(static_cast<float>(phase_elapsed) /
                               static_cast<float>(HATCH_PHASE4_MS));
        float split = HatchSmoothstep(t);
        float size = Lerp(static_cast<float>(HATCH_BASE_SIZE), 80.0f, split);
        float radius =
            Lerp(static_cast<float>(HATCH_BASE_SIZE / 2.0f), 24.0f, split);
        float settle = std::sin(t * 3.1415926f) * 4.0f * (1.0f - t);
        if (!hatch_.blink_started && t > 0.55f) {
            hatch_.blink_started = true;
            hatch_.blink_start_ms = now_ms;
        }
        float blink_scale = 1.0f;
        if (hatch_.blink_started) {
            uint32_t blink_elapsed = now_ms - hatch_.blink_start_ms;
            const uint32_t blink_duration = 450;
            if (blink_elapsed < blink_duration) {
                float bt = static_cast<float>(blink_elapsed) /
                           static_cast<float>(blink_duration);
                blink_scale = 1.0f - 0.9f * std::sin(bt * 3.1415926f);
            }
        }
        DrawHatchEyes(&layer, center_x, center_y + settle, split, size, radius,
                      blink_scale);
    }

    lv_canvas_finish_layer(canvas_, &layer);
}

void EyeAnimation::DrawSleepZzz(lv_layer_t* layer, uint32_t now_ms, int anchor_y) const {
    if (layer == nullptr) {
        return;
    }

    lv_draw_label_dsc_t label;
    lv_draw_label_dsc_init(&label);
    label.text = "Z";
    label.font = &lv_font_montserrat_vn_28;
    label.color = lv_color_white();
    label.text_local = 0;
    label.text_static = 1;
    label.align = LV_TEXT_ALIGN_CENTER;

    for (int i = 0; i < NUM_Z_PARTICLES; i++) {
        if (z_particles_[i].active) {
            uint32_t elapsed_ms = now_ms - z_particles_[i].start_ms;
            float t = ClampFloat(static_cast<float>(elapsed_ms) / static_cast<float>(z_particles_[i].duration_ms), 0.0f, 1.0f);
            
            label.opa = static_cast<lv_opa_t>(Lerp(255.0f, 0.0f, EaseOutCubic(t)));
            
            int y = anchor_y + static_cast<int>(z_particles_[i].y);
            int x = static_cast<int>(z_particles_[i].x);
            
            lv_area_t area = {
                static_cast<int16_t>(x - 10),
                static_cast<int16_t>(y),
                static_cast<int16_t>(x + 10),
                static_cast<int16_t>(y + 34),
            };
            lv_draw_label(layer, &label, &area);
        }
    }
}

void EyeAnimation::DrawLegacyLove(lv_layer_t* layer, uint32_t now_ms) const {
    if (layer == nullptr || canvas_buf_ == nullptr ||
        left_eye_box_.w <= 0 || right_eye_box_.w <= 0) {
        return;
    }

    const int bob = RoundToInt(std::sinf(static_cast<float>(now_ms) / 300.0f) * 4.0f);
    const lv_color_t heart_color = lv_color_make(255, 0, 160);
    const lv_color_t glow_color = lv_color_make(255, 80, 180);

    const int left_cx = left_eye_box_.x + left_eye_box_.w / 2;
    const int left_cy = left_eye_box_.y + left_eye_box_.h / 2 + bob;
    const int right_cx = right_eye_box_.x + right_eye_box_.w / 2;
    const int right_cy = right_eye_box_.y + right_eye_box_.h / 2 + bob;
    // Match bubu_seed heart construction: each eye-heart = 2 rotated ellipses.
    const float eye_scale = static_cast<float>(std::min(left_eye_box_.w, left_eye_box_.h)) / 70.0f;
    const int heart_w = std::max(8, static_cast<int>(RoundToInt(20.0f * eye_scale)));
    const int heart_h = std::max(10, static_cast<int>(RoundToInt(30.0f * eye_scale)));
    const float tilt_deg = 45.0f;

    const int cheek_y = std::min(screen_h_ - 1, left_cy + left_eye_box_.h / 2 + 14);
    DrawFilledCircle(layer, left_cx, cheek_y, 18, glow_color, LV_OPA_30);
    DrawFilledCircle(layer, left_cx, cheek_y, 11, glow_color, LV_OPA_40);
    DrawFilledCircle(layer, right_cx, cheek_y, 18, glow_color, LV_OPA_30);
    DrawFilledCircle(layer, right_cx, cheek_y, 11, glow_color, LV_OPA_40);

    DrawLegacyHeartShape(layer, canvas_buf_, screen_w_, screen_h_,
                         left_cx, left_cy, heart_w, heart_h, tilt_deg, heart_color);
    DrawLegacyHeartShape(layer, canvas_buf_, screen_w_, screen_h_,
                         right_cx, right_cy, heart_w, heart_h, tilt_deg, heart_color);
}

void EyeAnimation::DrawLegacyCyclop(lv_layer_t* layer, uint32_t now_ms) const {
    if (layer == nullptr || left_eye_box_.w <= 0 || left_eye_box_.h <= 0) {
        return;
    }

    const float t = static_cast<float>(now_ms) / 1000.0f;
    const int center_x = left_eye_box_.x + left_eye_box_.w / 2;
    const int center_y = left_eye_box_.y + left_eye_box_.h / 2;
    const int eye_r = std::max(42, std::min(left_eye_box_.w, left_eye_box_.h) / 2);
    DrawFilledCircle(layer, center_x, center_y, eye_r, lv_color_white(), LV_OPA_COVER);

    const int max_dx = std::max(10, eye_r / 2);
    const int max_dy = std::max(8, eye_r / 3);
    const int pupil_x = center_x + RoundToInt(std::sinf(t * 1.5f) * static_cast<float>(max_dx));
    const int pupil_y = center_y + RoundToInt(std::cosf(t * 1.2f) * static_cast<float>(max_dy));
    const int pupil_r = std::max(16, eye_r / 3);

    DrawFilledCircle(layer, pupil_x, pupil_y, pupil_r, lv_color_black(), LV_OPA_COVER);
}

void EyeAnimation::DrawLegacyDrunk(lv_layer_t* layer, uint32_t now_ms) const {
    if (layer == nullptr || left_eye_box_.w <= 0 || right_eye_box_.w <= 0) {
        return;
    }

    const int centers_x[2] = {
        left_eye_box_.x + left_eye_box_.w / 2,
        right_eye_box_.x + right_eye_box_.w / 2,
    };
    const int centers_y[2] = {
        left_eye_box_.y + left_eye_box_.h / 2,
        right_eye_box_.y + right_eye_box_.h / 2,
    };

    for (int eye = 0; eye < 2; ++eye) {
        const int cx = centers_x[eye];
        const int cy = centers_y[eye];
        const float direction = (eye == 0) ? 1.0f : -1.0f;
        const float phase = static_cast<float>(now_ms) / 280.0f * direction;

        for (int ring = 1; ring <= 6; ++ring) {
            const float ring_ratio = static_cast<float>(ring) / 6.0f;
            const int radius = static_cast<int>(ring_ratio * 24.0f);
            for (int seg = 0; seg < 20; ++seg) {
                const float seg_ratio = static_cast<float>(seg) / 20.0f;
                const float angle = seg_ratio * 2.0f * static_cast<float>(M_PI) + phase + ring_ratio * direction;
                const int x = cx + RoundToInt(std::cosf(angle) * static_cast<float>(radius));
                const int y = cy + RoundToInt(std::sinf(angle) * static_cast<float>(radius));
                const uint8_t c = static_cast<uint8_t>(110 + ring * 20);
                DrawFilledCircle(layer, x, y, 2, lv_color_make(c, c / 2, c), LV_OPA_80);
            }
        }
    }
}

void EyeAnimation::DrawLegacyConfuse(lv_layer_t* layer, uint32_t now_ms) const {
    if (layer == nullptr || !canvas_buf_) {
        return;
    }

    bool entered_mode = false;
    auto& rt = GetLegacySeedRuntime(this, LegacyEmotionMode::Confuse, now_ms, &entered_mode);
    if (!rt.confuse_initialized) {
        for (size_t i = 0; i < rt.confuse_angle.size(); ++i) {
            rt.confuse_angle[i] = static_cast<float>(RandomInt(0, 628)) / 100.0f;
            rt.confuse_radius[i] = static_cast<float>(RandomInt(30, 99));
            rt.confuse_size[i] = RandomInt(5, 20);
            rt.confuse_color[i] = Color565FromRgb(
                static_cast<uint8_t>(RandomInt(100, 254)),
                static_cast<uint8_t>(RandomInt(100, 254)),
                static_cast<uint8_t>(RandomInt(100, 254)));
        }
        rt.confuse_initialized = true;
        rt.confuse_swap_state = false;
        rt.confuse_last_switch_ms = now_ms;
    }
    if (entered_mode) {
        rt.confuse_last_switch_ms = now_ms;
    }

    if (now_ms - rt.confuse_last_switch_ms > 500) {
        rt.confuse_swap_state = !rt.confuse_swap_state;
        rt.confuse_last_switch_ms = now_ms;
    }

    uint16_t* buf565 = reinterpret_cast<uint16_t*>(canvas_buf_);
    const int cx = screen_w_ / 2;
    const int cy = screen_h_ / 2;

    for (size_t i = 0; i < rt.confuse_angle.size(); ++i) {
        const float swirl = 0.002f + static_cast<float>(i) * 0.0002f;
        rt.confuse_angle[i] += swirl;
        const int fog_x = cx + RoundToInt(std::cosf(rt.confuse_angle[i]) * rt.confuse_radius[i]);
        const int fog_y = cy + RoundToInt(std::sinf(rt.confuse_angle[i]) * rt.confuse_radius[i]);
        const int radius = rt.confuse_size[i];
        for (int dy = -radius; dy <= radius; ++dy) {
            for (int dx = -radius; dx <= radius; ++dx) {
                if (dx * dx + dy * dy <= radius * radius) {
                    CanvasBlendPixel565(buf565, screen_w_, screen_h_, fog_x + dx, fog_y + dy,
                                        rt.confuse_color[i], 127);
                }
            }
        }
    }

    auto draw_eye_flat = [&](int x, int y, lv_color_t c) {
        lv_draw_rect_dsc_t d;
        lv_draw_rect_dsc_init(&d);
        d.bg_color = c;
        d.bg_opa = LV_OPA_COVER;
        d.border_opa = LV_OPA_TRANSP;
        d.radius = 12;
        lv_area_t area = {static_cast<int16_t>(x - 35), static_cast<int16_t>(y - 21),
                          static_cast<int16_t>(x + 34), static_cast<int16_t>(y + 20)};
        lv_draw_rect(layer, &d, &area);
    };

    const lv_color_t yellow = lv_color_make(255, 255, 0);
    const lv_color_t blue = lv_color_make(0, 0, 255);
    if (!rt.confuse_swap_state) {
        DrawFilledCircle(layer, cx - kLegacySeedEyeDistance, cy, 35, yellow, LV_OPA_COVER);
        draw_eye_flat(cx + kLegacySeedEyeDistance, cy, blue);
    } else {
        draw_eye_flat(cx - kLegacySeedEyeDistance, cy, blue);
        DrawFilledCircle(layer, cx + kLegacySeedEyeDistance, cy, 35, yellow, LV_OPA_COVER);
    }
}

void EyeAnimation::DrawLegacyAngry(lv_layer_t* layer, uint32_t now_ms) const {
    if (layer == nullptr) {
        return;
    }

    const int center_x = screen_w_ / 2;
    const int center_y = screen_h_ / 2;
    const int h = 35;
    const int left_x = center_x + kLegacySeedEyeDistance;
    const int right_x = center_x - kLegacySeedEyeDistance;

    lv_draw_rect_dsc_t eye;
    lv_draw_rect_dsc_init(&eye);
    eye.bg_color = lv_color_white();
    eye.bg_opa = LV_OPA_COVER;
    eye.border_opa = LV_OPA_TRANSP;
    eye.radius = kLegacySeedEyeRadius;

    lv_area_t left_area = {
        static_cast<int16_t>(left_x - kLegacySeedEyeRadius),
        static_cast<int16_t>(center_y - h / 2),
        static_cast<int16_t>(left_x + kLegacySeedEyeRadius - 1),
        static_cast<int16_t>(center_y + h / 2 - 1),
    };
    lv_area_t right_area = {
        static_cast<int16_t>(right_x - kLegacySeedEyeRadius),
        static_cast<int16_t>(center_y - h / 2),
        static_cast<int16_t>(right_x + kLegacySeedEyeRadius - 1),
        static_cast<int16_t>(center_y + h / 2 - 1),
    };
    lv_draw_rect(layer, &eye, &left_area);
    lv_draw_rect(layer, &eye, &right_area);

    const uint32_t elapsed = now_ms - (now_ms / 1000U) * 1000U;
    const float pulse = (elapsed < 500U)
                            ? (static_cast<float>(elapsed) / 500.0f)
                            : (1.0f - static_cast<float>(elapsed - 500U) / 500.0f);
    const uint8_t intensity = static_cast<uint8_t>(pulse * 255.0f);
    const lv_color_t cross_color = lv_color_make(intensity, 0, 0);

    auto fill_rect = [&](int x, int y, int w, int h, lv_color_t c) {
        lv_draw_rect_dsc_t d;
        lv_draw_rect_dsc_init(&d);
        d.bg_color = c;
        d.bg_opa = LV_OPA_COVER;
        d.border_opa = LV_OPA_TRANSP;
        d.radius = 0;
        lv_area_t area = {static_cast<int16_t>(x), static_cast<int16_t>(y),
                          static_cast<int16_t>(x + w - 1), static_cast<int16_t>(y + h - 1)};
        lv_draw_rect(layer, &d, &area);
    };

    const int right_eye_x = center_x + kLegacySeedEyeDistance;
    const int cross_x = right_eye_x + kLegacySeedEyeRadius - 5;
    const int cross_y = center_y - 40;
    const int outer_length = 30;
    const int outer_thickness = 15;
    const int inner_length = 30;
    const int inner_thickness = 5;
    fill_rect(cross_x - outer_length / 2, cross_y - outer_thickness / 2,
              outer_length, outer_thickness, cross_color);
    fill_rect(cross_x - outer_thickness / 2, cross_y - outer_length / 2,
              outer_thickness, outer_length, cross_color);
    fill_rect(cross_x - inner_length / 2, cross_y - inner_thickness / 2,
              inner_length, inner_thickness, lv_color_black());
    fill_rect(cross_x - inner_thickness / 2, cross_y - inner_length / 2,
              inner_thickness, inner_length, lv_color_black());
}

void EyeAnimation::DrawLegacyFurious(lv_layer_t* layer, uint32_t now_ms) const {
    if (layer == nullptr) {
        return;
    }

    bool entered_mode = false;
    auto& rt = GetLegacySeedRuntime(this, LegacyEmotionMode::Furious, now_ms, &entered_mode);
    (void)entered_mode;
    const uint32_t elapsed = now_ms - rt.mode_start_ms;

    constexpr uint32_t kPhaseEnterMs = 1600;
    constexpr uint32_t kPhaseHoldMs = 4400;
    constexpr uint32_t kPhaseReleaseMs = 400;

    const float t_color = (elapsed < 6000U) ? (static_cast<float>(elapsed) / 6000.0f) : 1.0f;
    const lv_color_t eye_color = LvColorFrom565(LerpColor565(0xFFFF, 0xF800, t_color));
    const int cx = screen_w_ / 2;
    const int cy = screen_h_ / 2;
    const int base_w = kLegacySeedEyeWidth;
    const int base_h = kLegacySeedEyeHeight;
    const int left_cx = cx - kLegacySeedEyeDistance;
    const int right_cx = cx + kLegacySeedEyeDistance;
    const int eye_r = kLegacySeedEyeRadius;

    auto eye_box = [&](int ecx, int ecy, int w, int h) {
        lv_draw_rect_dsc_t d;
        lv_draw_rect_dsc_init(&d);
        d.bg_color = eye_color;
        d.bg_opa = LV_OPA_COVER;
        d.border_opa = LV_OPA_TRANSP;
        d.radius = eye_r;
        lv_area_t area = {static_cast<int16_t>(ecx - w / 2), static_cast<int16_t>(ecy - h / 2),
                          static_cast<int16_t>(ecx + w / 2 - 1), static_cast<int16_t>(ecy + h / 2 - 1)};
        lv_draw_rect(layer, &d, &area);
    };

    auto brows = [&](int lc, int rc, int y, int extent) {
        const int l_out_x = lc - (base_w / 2);
        const int l_in_x = lc + (base_w / 2);
        const int r_out_x = rc + (base_w / 2);
        const int r_in_x = rc - (base_w / 2);
        lv_draw_line_dsc_t d;
        lv_draw_line_dsc_init(&d);
        d.color = eye_color;
        d.opa = LV_OPA_COVER;
        d.width = 1;
        for (int t = 0; t < 7; ++t) {
            d.p1.x = static_cast<float>(l_out_x + 30);
            d.p1.y = static_cast<float>(y - extent + t);
            d.p2.x = static_cast<float>(l_in_x);
            d.p2.y = static_cast<float>(y + extent + t);
            lv_draw_line(layer, &d);
            d.p1.x = static_cast<float>(r_out_x - 30);
            d.p1.y = static_cast<float>(y - extent + t);
            d.p2.x = static_cast<float>(r_in_x);
            d.p2.y = static_cast<float>(y + extent + t);
            lv_draw_line(layer, &d);
        }
    };

    if (elapsed < kPhaseEnterMs) {
        const float t = static_cast<float>(elapsed) / static_cast<float>(kPhaseEnterMs);
        const int eh = base_h - static_cast<int>(t * 25.0f);
        const int lcx = left_cx + static_cast<int>(t * 4.0f);
        const int rcx = right_cx - static_cast<int>(t * 4.0f);
        eye_box(lcx, cy, base_w, eh);
        eye_box(rcx, cy, base_w, eh);
        return;
    }

    if (elapsed < kPhaseEnterMs + kPhaseHoldMs) {
        const uint32_t in = elapsed - kPhaseEnterMs;
        const int shake = ((in / 50U) % 2U == 0U) ? 2 : -2;
        const int eh = base_h - 25;
        eye_box(left_cx + shake, cy, base_w, eh);
        eye_box(right_cx + shake, cy, base_w, eh);
        brows(left_cx + shake, right_cx + shake, cy - base_h / 2 - 6, 10);
        return;
    }

    if (elapsed < kPhaseEnterMs + kPhaseHoldMs + kPhaseReleaseMs) {
        const uint32_t in = elapsed - (kPhaseEnterMs + kPhaseHoldMs);
        const float t = static_cast<float>(in) / static_cast<float>(kPhaseReleaseMs);
        const int eh = (base_h - 25) + static_cast<int>(t * 25.0f);
        const int lcx = left_cx + static_cast<int>((1.0f - t) * 4.0f);
        const int rcx = right_cx - static_cast<int>((1.0f - t) * 4.0f);
        eye_box(lcx, cy, base_w, eh);
        eye_box(rcx, cy, base_w, eh);
        return;
    }
}

void EyeAnimation::DrawLegacyBanhChung(lv_layer_t* layer, uint32_t now_ms) const {
    if (layer == nullptr) {
        return;
    }

    bool entered_mode = false;
    auto& rt = GetLegacySeedRuntime(this, LegacyEmotionMode::BanhChung, now_ms, &entered_mode);
    (void)entered_mode;
    const uint32_t elapsed = now_ms - rt.mode_start_ms;

    constexpr uint32_t kPhaseInMs = 500;
    constexpr uint32_t kPhaseHoldMs = 3500;
    constexpr uint32_t kPhaseOutMs = 500;
    constexpr uint32_t kTotalMs = kPhaseInMs + kPhaseHoldMs + kPhaseOutMs;
    constexpr uint16_t kColorWhite = 0xFFFF;
    constexpr uint16_t kColorLeafMid = 0x07E0;
    constexpr uint16_t kColorRibbon = 0xB4A5;
    constexpr uint16_t kColorGlint = 0xFFE0;
    const int left_x = screen_w_ / 2 - kLegacySeedEyeDistance;
    const int right_x = screen_w_ / 2 + kLegacySeedEyeDistance;
    const int eye_y = screen_h_ / 2;

    const uint32_t e = elapsed % kTotalMs;
    const uint32_t cycle_id = elapsed / kTotalMs;
    if (cycle_id != rt.banh_cycle_id) {
        for (auto& sp : rt.banh_sparkles) {
            sp.size = RandomInt(4, 7);
            const int margin = sp.size + 2;
            sp.x = RandomInt(margin, std::max(margin, screen_w_ - margin - 1));
            sp.y = RandomInt(margin, std::max(margin, screen_h_ - margin - 1));
            sp.offset = static_cast<uint32_t>(RandomInt(0, 999));
            sp.period = static_cast<uint32_t>(600 + RandomInt(0, 699));
        }
        rt.banh_cycle_id = cycle_id;
    }

    const auto ease_in_out = [](float t) {
        t = ClampFloat(t, 0.0f, 1.0f);
        return (t < 0.5f) ? (2.0f * t * t) : (-1.0f + (4.0f - 2.0f * t) * t);
    };
    const auto scale565 = [](uint16_t c, float b) {
        b = ClampFloat(b, 0.0f, 1.0f);
        if (b <= 0.0f) return static_cast<uint16_t>(0);
        if (b >= 1.0f) return c;
        const uint8_t r = static_cast<uint8_t>(((c >> 11) & 0x1F) << 3);
        const uint8_t g = static_cast<uint8_t>(((c >> 5) & 0x3F) << 2);
        const uint8_t bl = static_cast<uint8_t>((c & 0x1F) << 3);
        return Color565FromRgb(static_cast<uint8_t>(r * b), static_cast<uint8_t>(g * b),
                               static_cast<uint8_t>(bl * b));
    };

    float t_in = 0.0f;
    float t_out = 0.0f;
    if (e < kPhaseInMs) t_in = ease_in_out(static_cast<float>(e) / static_cast<float>(kPhaseInMs));
    else if (e < kPhaseInMs + kPhaseHoldMs) t_in = 1.0f;
    else {
        const uint32_t e2 = e - (kPhaseInMs + kPhaseHoldMs);
        t_out = ease_in_out(static_cast<float>(e2) / static_cast<float>(kPhaseOutMs));
    }
    const float t_morph = (t_in > 0.0f) ? t_in : (1.0f - t_out);

    const int w0 = kLegacySeedEyeWidth;
    const int h0 = kLegacySeedEyeHeight;
    const int r0 = kLegacySeedEyeCorner;
    const int w1 = kLegacySeedEyeWidth + 6;
    const int h1 = kLegacySeedEyeHeight + 6;
    const int r1 = 8;
    const int wm = w0 + RoundToInt((w1 - w0) * t_morph);
    const int hm = h0 + RoundToInt((h1 - h0) * t_morph);
    const int rm = r0 + RoundToInt((r1 - r0) * t_morph);
    const lv_color_t eye_color = LvColorFrom565(LerpColor565(kColorWhite, kColorLeafMid, t_morph));

    int bounce_y = 0;
    if (t_in == 1.0f && t_out == 0.0f) {
        const float t_sec = static_cast<float>(elapsed) / 1000.0f;
        bounce_y = RoundToInt(2.0f * std::sinf(2.0f * static_cast<float>(M_PI) * 2.2f * t_sec));
    }

    auto fill_eye_box = [&](int cx, int cy, int w, int h, int r, lv_color_t c) {
        lv_draw_rect_dsc_t d;
        lv_draw_rect_dsc_init(&d);
        d.bg_color = c;
        d.bg_opa = LV_OPA_COVER;
        d.border_opa = LV_OPA_TRANSP;
        d.radius = r;
        lv_area_t area = {static_cast<int16_t>(cx - w / 2), static_cast<int16_t>(cy - h / 2),
                          static_cast<int16_t>(cx + w / 2 - 1), static_cast<int16_t>(cy + h / 2 - 1)};
        lv_draw_rect(layer, &d, &area);
    };
    auto draw_ribbon_rect = [&](int cx, int cy, int w, int h) {
        const int band = std::max(3, w / 14);
        const int gap = band * 6;
        lv_draw_rect_dsc_t d;
        lv_draw_rect_dsc_init(&d);
        d.bg_color = LvColorFrom565(kColorRibbon);
        d.bg_opa = LV_OPA_COVER;
        d.border_opa = LV_OPA_TRANSP;
        d.radius = 0;
        lv_area_t a = {0, 0, 0, 0};
        a = {static_cast<int16_t>(cx - gap / 2 - band / 2), static_cast<int16_t>(cy - h / 2),
             static_cast<int16_t>(cx - gap / 2 + band / 2 - 1), static_cast<int16_t>(cy + h / 2 - 1)};
        lv_draw_rect(layer, &d, &a);
        a = {static_cast<int16_t>(cx + gap / 2 - band / 2), static_cast<int16_t>(cy - h / 2),
             static_cast<int16_t>(cx + gap / 2 + band / 2 - 1), static_cast<int16_t>(cy + h / 2 - 1)};
        lv_draw_rect(layer, &d, &a);
        a = {static_cast<int16_t>(cx - w / 2), static_cast<int16_t>(cy - gap / 2 - band / 2),
             static_cast<int16_t>(cx + w / 2 - 1), static_cast<int16_t>(cy - gap / 2 + band / 2 - 1)};
        lv_draw_rect(layer, &d, &a);
        a = {static_cast<int16_t>(cx - w / 2), static_cast<int16_t>(cy + gap / 2 - band / 2),
             static_cast<int16_t>(cx + w / 2 - 1), static_cast<int16_t>(cy + gap / 2 + band / 2 - 1)};
        lv_draw_rect(layer, &d, &a);
    };
    auto draw_sparkle4 = [&](int cx, int cy, lv_color_t c, int size) {
        lv_draw_line_dsc_t d;
        lv_draw_line_dsc_init(&d);
        d.color = c;
        d.opa = LV_OPA_COVER;
        d.width = 1;
        for (int i = 0; i < size; ++i) {
            const int w = size - i;
            d.p1.x = cx - w / 2;
            d.p1.y = cy - i;
            d.p2.x = cx - w / 2 + w - 1;
            d.p2.y = cy - i;
            lv_draw_line(layer, &d);
            d.p1.x = cx - w / 2;
            d.p1.y = cy + i;
            d.p2.x = cx - w / 2 + w - 1;
            d.p2.y = cy + i;
            lv_draw_line(layer, &d);
        }
        for (int i = 0; i < size; ++i) {
            const int h = size - i;
            d.p1.x = cx - i;
            d.p1.y = cy - h / 2;
            d.p2.x = cx - i;
            d.p2.y = cy - h / 2 + h - 1;
            lv_draw_line(layer, &d);
            d.p1.x = cx + i;
            d.p1.y = cy - h / 2;
            d.p2.x = cx + i;
            d.p2.y = cy - h / 2 + h - 1;
            lv_draw_line(layer, &d);
        }
    };

    fill_eye_box(left_x, eye_y + bounce_y, wm, hm, rm, eye_color);
    if (t_morph > 0.15f) draw_ribbon_rect(left_x, eye_y + bounce_y, wm, hm);
    fill_eye_box(right_x, eye_y - bounce_y, wm, hm, rm, eye_color);
    if (t_morph > 0.15f) draw_ribbon_rect(right_x, eye_y - bounce_y, wm, hm);

    if (t_in == 1.0f && t_out == 0.0f) {
        for (const auto& sp : rt.banh_sparkles) {
            const uint32_t tl = (elapsed + sp.offset) % sp.period;
            const float phase = static_cast<float>(tl) / static_cast<float>(sp.period);
            const float fade = 0.5f * (1.0f - std::cosf(2.0f * static_cast<float>(M_PI) * phase));
            draw_sparkle4(sp.x, sp.y, LvColorFrom565(scale565(kColorGlint, fade)), sp.size);
        }
    }
}

void EyeAnimation::DrawLegacyDeadpool(lv_layer_t* layer, uint32_t now_ms) const {
    if (layer == nullptr) {
        return;
    }

    bool entered_mode = false;
    auto& rt = GetLegacySeedRuntime(this, LegacyEmotionMode::Deadpool, now_ms, &entered_mode);
    (void)entered_mode;
    const uint32_t elapsed_raw = now_ms - rt.mode_start_ms;
    constexpr uint32_t kPhaseInMs = 800;
    constexpr uint32_t kPhaseHoldMs = 6400;
    constexpr uint32_t kPhaseOutMs = 800;
    constexpr uint32_t kTotalMs = kPhaseInMs + kPhaseHoldMs + kPhaseOutMs;
    constexpr int kMoveAmplPx = 18;
    constexpr int kDotsCount = 240;

    uint32_t elapsed = elapsed_raw;
    if (elapsed > kTotalMs) elapsed = kTotalMs;

    enum Phase { PH_IN = 0, PH_HOLD = 1, PH_OUT = 2 };
    int cur_phase = PH_OUT;
    int y_off = 0;
    auto ease_in_out = [](float x) {
        if (x <= 0.0f) return 0.0f;
        if (x >= 1.0f) return 1.0f;
        return (x < 0.5f) ? (2.0f * x * x) : (-1.0f + (4.0f - 2.0f * x) * x);
    };
    if (elapsed < kPhaseInMs) {
        cur_phase = PH_IN;
        const float t = static_cast<float>(elapsed) / static_cast<float>(kPhaseInMs);
        y_off = RoundToInt(ease_in_out(t) * static_cast<float>(kMoveAmplPx));
    } else if (elapsed < kPhaseInMs + kPhaseHoldMs) {
        cur_phase = PH_HOLD;
        y_off = kMoveAmplPx;
    } else {
        cur_phase = PH_OUT;
        const uint32_t e2 = elapsed - (kPhaseInMs + kPhaseHoldMs);
        const float t = static_cast<float>(e2) / static_cast<float>(kPhaseOutMs);
        y_off = RoundToInt((1.0f - ease_in_out(t)) * static_cast<float>(kMoveAmplPx));
    }

    auto frand01 = [&](uint32_t* seed) -> float {
        *seed ^= (*seed << 13);
        *seed ^= (*seed >> 17);
        *seed ^= (*seed << 5);
        return static_cast<float>(*seed % 10000U) / 10000.0f;
    };
    auto rand_biased_high = [&](uint32_t* seed, float k) -> float {
        const float r = frand01(seed);
        return 1.0f - std::pow(r, k);
    };
    auto sample_dot = [&](uint32_t* seed, int* x_out, int* y_out, int* r_out) -> bool {
        const float ux = rand_biased_high(seed, 3.0f);
        const float uy = rand_biased_high(seed, 3.0f);
        const int x = RoundToInt(ux * static_cast<float>(screen_w_ - 1));
        const int y = RoundToInt(uy * static_cast<float>(screen_h_ - 1));
        const float p = std::pow((x / static_cast<float>(screen_w_)) *
                                     (y / static_cast<float>(screen_h_)),
                                 0.5f);
        if (frand01(seed) > p) return false;
        const float rr = 1.5f + (5.0f - 1.5f) * frand01(seed);
        const int r = std::max(1, static_cast<int>(RoundToInt(rr)));
        *x_out = x;
        *y_out = y;
        *r_out = r;
        return true;
    };
    auto generate_dots = [&]() {
        rt.deadpool_dots_placed = 0;
        uint32_t seed = rt.deadpool_seed ^ elapsed_raw ^ 0xDEAD901Fu;
        const int max_tries = kDotsCount * 12;
        int tries = 0;
        while (rt.deadpool_dots_placed < kDotsCount && tries < max_tries) {
            ++tries;
            int x = 0;
            int y = 0;
            int r = 1;
            if (!sample_dot(&seed, &x, &y, &r)) continue;
            rt.deadpool_dots[rt.deadpool_dots_placed++] = {x, y, r};
        }
        rt.deadpool_dots_active = true;
        rt.deadpool_seed = seed;
    };

    if (cur_phase == PH_IN && rt.deadpool_prev_phase != PH_IN) {
        rt.deadpool_dots_active = false;
        rt.deadpool_dots_placed = 0;
    }
    if (cur_phase == PH_HOLD && rt.deadpool_prev_phase != PH_HOLD) {
        generate_dots();
    }
    rt.deadpool_prev_phase = cur_phase;

    const int cx = screen_w_ / 2;
    const int cy = screen_h_ / 2;
    const lv_color_t col_bg = lv_color_black();
    const lv_color_t col_eye = lv_color_white();
    const lv_color_t col_tri = lv_color_black();
    const lv_color_t col_red = lv_color_make(255, 0, 0);

    const int outer_r = 120;
    const int ring_thick = 16;
    DrawFilledCircle(layer, cx, cy, outer_r, col_red, LV_OPA_COVER);
    DrawFilledCircle(layer, cx, cy, std::max(0, outer_r - ring_thick), col_bg, LV_OPA_COVER);

    lv_draw_rect_dsc_t eye;
    lv_draw_rect_dsc_init(&eye);
    eye.bg_color = col_eye;
    eye.bg_opa = LV_OPA_COVER;
    eye.border_opa = LV_OPA_TRANSP;
    eye.radius = 20;
    lv_area_t le = {static_cast<int16_t>(cx - 50 - 35), static_cast<int16_t>(cy - 20),
                    static_cast<int16_t>(cx - 50 + 35 - 1), static_cast<int16_t>(cy + 20 - 1)};
    lv_area_t re = {static_cast<int16_t>(cx + 50 - 35), static_cast<int16_t>(cy - 20),
                    static_cast<int16_t>(cx + 50 + 35 - 1), static_cast<int16_t>(cy + 20 - 1)};
    lv_draw_rect(layer, &eye, &le);
    lv_draw_rect(layer, &eye, &re);

    lv_draw_triangle_dsc_t brow;
    lv_draw_triangle_dsc_init(&brow);
    brow.color = col_tri;
    brow.opa = LV_OPA_COVER;
    const int base_y = cy - 40 / 2 + y_off - 25;
    const int apex_y = cy + 40 / 2 + y_off - 25;
    brow.p[0] = {static_cast<int16_t>(cx - 140 / 2), static_cast<int16_t>(base_y)};
    brow.p[1] = {static_cast<int16_t>(cx + 140 / 2), static_cast<int16_t>(base_y)};
    brow.p[2] = {static_cast<int16_t>(cx), static_cast<int16_t>(apex_y)};
    lv_draw_triangle(layer, &brow);

    if (rt.deadpool_dots_active && rt.deadpool_dots_placed > 0) {
        for (int i = 0; i < rt.deadpool_dots_placed; ++i) {
            const auto& d = rt.deadpool_dots[static_cast<size_t>(i)];
            DrawFilledCircle(layer, d.x, d.y, d.r, col_red, LV_OPA_COVER);
        }
    }

    lv_draw_rect_dsc_t divider;
    lv_draw_rect_dsc_init(&divider);
    divider.bg_color = col_red;
    divider.bg_opa = LV_OPA_COVER;
    divider.border_opa = LV_OPA_TRANSP;
    divider.radius = 0;
    lv_area_t div_area = {static_cast<int16_t>(cx - 18 / 2), 0,
                          static_cast<int16_t>(cx + 18 / 2 - 1),
                          static_cast<int16_t>(screen_h_ - 1)};
    lv_draw_rect(layer, &divider, &div_area);
}

void EyeAnimation::DrawLegacyCry(lv_layer_t* layer, uint32_t now_ms) const {
    if (layer == nullptr) {
        return;
    }

    bool entered_mode = false;
    auto& rt = GetLegacySeedRuntime(this, LegacyEmotionMode::Cry, now_ms, &entered_mode);
    (void)entered_mode;
    const uint32_t elapsed_raw = now_ms - rt.mode_start_ms;

    constexpr uint32_t kIntroMs = 400;
    constexpr uint32_t kInMs = 1000;
    constexpr uint32_t kHoldMs = 5000;
    constexpr uint32_t kOutMs = 1000;
    constexpr uint32_t kTotalMs = kIntroMs + kInMs + kHoldMs + kOutMs;
    uint32_t e = elapsed_raw;
    if (e > kTotalMs) e = kTotalMs;

    auto ease_in_out = [](float t) {
        t = ClampFloat(t, 0.0f, 1.0f);
        return (t < 0.5f) ? (2.0f * t * t) : (-1.0f + (4.0f - 2.0f * t) * t);
    };

    float p_tears = 0.0f;
    float wobble_a = 0.0f;
    if (e <= kIntroMs) {
        p_tears = 0.0f;
        wobble_a = 0.0f;
    } else if (e <= kIntroMs + kInMs) {
        const float t = static_cast<float>(e - kIntroMs) / static_cast<float>(kInMs);
        const float k = ease_in_out(t);
        p_tears = k;
        wobble_a = 10.0f * k;
    } else if (e <= kIntroMs + kInMs + kHoldMs) {
        p_tears = 1.0f;
        wobble_a = 10.0f;
    } else {
        const uint32_t base = kIntroMs + kInMs + kHoldMs;
        const float t = static_cast<float>(e - base) / static_cast<float>(kOutMs);
        const float k = ease_in_out(t);
        p_tears = 1.0f - k;
        wobble_a = 10.0f * (1.0f - k);
    }

    const float t_sec = static_cast<float>(e) / 1000.0f;
    const float phase = 2.0f * static_cast<float>(M_PI) * 1.6f * t_sec;
    const int wob_x = RoundToInt(wobble_a * std::sinf(phase));
    const int wob_y = RoundToInt((wobble_a * 0.6f) * std::sinf(phase + 1.3f));
    const int left_x = screen_w_ / 2 - kLegacySeedEyeDistance + wob_x;
    const int right_x = screen_w_ / 2 + kLegacySeedEyeDistance + wob_x;
    const int center_y = screen_h_ / 2 + wob_y;
    const lv_color_t tear_left = LvColorFrom565(0xC618);
    const lv_color_t tear_right = LvColorFrom565(0xD67A);

    auto fill_round_strip = [&](int x, int y, int w, int h, int r, lv_color_t c) {
        if (h <= 0 || w <= 0) return;
        lv_draw_rect_dsc_t d;
        lv_draw_rect_dsc_init(&d);
        d.bg_color = c;
        d.bg_opa = LV_OPA_COVER;
        d.border_opa = LV_OPA_TRANSP;
        d.radius = r;
        lv_area_t area = {static_cast<int16_t>(x), static_cast<int16_t>(y),
                          static_cast<int16_t>(x + w - 1), static_cast<int16_t>(y + h - 1)};
        lv_draw_rect(layer, &d, &area);
    };

    if (p_tears > 0.0f) {
        const int base_y = center_y + kLegacySeedEyeHeight / 2 - 6;
        fill_round_strip(left_x - 20, base_y, 10, RoundToInt(34.0f * p_tears), 5, tear_left);
        fill_round_strip(left_x - 11, base_y, 8, RoundToInt(26.0f * p_tears), 4, tear_left);
        fill_round_strip(left_x - 5, base_y, 12, RoundToInt(44.0f * p_tears), 6, tear_left);
        fill_round_strip(left_x + 6, base_y, 8, RoundToInt(24.0f * p_tears), 4, tear_left);

        fill_round_strip(right_x - 10, base_y, 20, RoundToInt(30.0f * p_tears), 5, tear_right);
        fill_round_strip(right_x - 2, base_y, 12, RoundToInt(40.0f * p_tears), 6, tear_right);
        fill_round_strip(right_x + 10, base_y, 10, RoundToInt(22.0f * p_tears), 4, tear_right);
    }

    lv_draw_rect_dsc_t eye;
    lv_draw_rect_dsc_init(&eye);
    eye.bg_color = lv_color_white();
    eye.bg_opa = LV_OPA_COVER;
    eye.border_opa = LV_OPA_TRANSP;
    eye.radius = kLegacySeedEyeCorner;
    lv_area_t le = {static_cast<int16_t>(left_x - kLegacySeedEyeWidth / 2),
                    static_cast<int16_t>(center_y - kLegacySeedEyeHeight / 2),
                    static_cast<int16_t>(left_x + kLegacySeedEyeWidth / 2 - 1),
                    static_cast<int16_t>(center_y + kLegacySeedEyeHeight / 2 - 1)};
    lv_area_t re = {static_cast<int16_t>(right_x - kLegacySeedEyeWidth / 2),
                    static_cast<int16_t>(center_y - kLegacySeedEyeHeight / 2),
                    static_cast<int16_t>(right_x + kLegacySeedEyeWidth / 2 - 1),
                    static_cast<int16_t>(center_y + kLegacySeedEyeHeight / 2 - 1)};
    lv_draw_rect(layer, &eye, &le);
    lv_draw_rect(layer, &eye, &re);
}

// ---------------------------------------------------------------------------
// Sweat drops (rain background: 30 drops with randomized size and speed)
// ---------------------------------------------------------------------------
void EyeAnimation::DrawSweatDrops(lv_layer_t* layer) {
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_color   = lv_color_make(143, 203, 255);  // light blue
    d.bg_opa     = LV_OPA_COVER;
    d.border_opa = LV_OPA_TRANSP;
    d.radius     = 2;

    for (auto& drop : sweat_drops_) {
        drop.y += drop.speed;
        if (drop.y > static_cast<float>(screen_h_ + static_cast<int>(drop.h))) {
            drop.w = static_cast<float>(RandomInt(2, 3));  // 2-3 px wide
            drop.h = static_cast<float>(RandomInt(3, 6));  // 3-6 px long
            const int max_x = std::max(0, screen_w_ - static_cast<int>(drop.w));
            drop.x = static_cast<float>(RandomInt(0, max_x));
            drop.y = static_cast<float>(-RandomInt(3, 20));
            drop.speed = static_cast<float>(RandomInt(6, 16)) / 10.0f;  // 0.6..1.6 px/frame
        }

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
