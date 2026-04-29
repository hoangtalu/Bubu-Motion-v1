#include "legacy_emotions.h"
#include "lgfx_setup.hpp"
#include <esp_random.h>
#include <math.h>

// External reference to the main GFX object (defined in display_system.cpp)
extern LGFX gfx;

namespace LegacyEmotions {

// =====================================================
// Constants
// =====================================================
static constexpr int SCREEN_W = 240;
static constexpr int SCREEN_H = 240;
static constexpr int CENTER_X = 120;
static constexpr int CENTER_Y = 120;
static constexpr int EYE_DISTANCE = 50;

// LOVE constants
static constexpr int HEART_W = 20;
static constexpr int HEART_H = 30;
static constexpr float HEART_TILT_DEG = 45.0f;
static constexpr int HEART_BOB_AMPLITUDE = 4;
static constexpr float HEART_BOB_SPEED_MS = 300.0f;
static constexpr int CHEEK_RINGS = 8;
static constexpr float CHEEK_HZ = 0.55f;
static constexpr int CHEEK_BASE_RADIUS = 35;
static constexpr int CHEEK_PULSE_AMP = 6;
static constexpr int CHEEK_Y_OFFSET = 60;
static constexpr int CHEEK_X_OFFSET = 130;

// CYCLOP constants
static constexpr int CYCLOP_EYE_RADIUS = 100;
static constexpr int CYCLOP_PUPIL_RADIUS = 30;

// =====================================================
// State
// =====================================================
static bool s_active = false;
static EmotionType s_currentEmotion = LEGACY_LOVE;
static uint32_t s_emotionStartTime = 0;
static uint32_t s_emotionDuration = 0;

// LOVE state
static uint16_t s_loveColor = 0;
static uint16_t s_cheekInnerColor = 0;
static uint16_t s_cheekOuterColor = 0;

// CYCLOP state
static float s_cyclopScale = 1.0f;
static float s_scalePhase = 0.0f;
static float s_pupilX = 0.0f;
static float s_pupilY = 0.0f;
static float s_targetPX = 0.0f;
static float s_targetPY = 0.0f;
static uint32_t s_lastCyclopMove = 0;
static bool s_cyclopPaused = false;

// DRUNK state
static float s_phaseLeft = 0.0f;
static float s_phaseRight = 0.0f;

// =====================================================
// Helper Functions
// =====================================================

static inline uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

static inline void putPixelSafe(lgfx::LGFX_Sprite* sprite, int x, int y, uint16_t c) {
  if ((unsigned)x < SCREEN_W && (unsigned)y < SCREEN_H) {
    sprite->drawPixel(x, y, c);
  }
}

// =====================================================
// LOVE Emotion Functions
// =====================================================

static void drawLoveHeartShape(lgfx::LGFX_Sprite* sprite, int cx, int cy, int w, int h,
                                float tiltDeg, uint16_t col) {
  float angle = tiltDeg * DEG_TO_RAD;
  float ca = cosf(angle);
  float sa = sinf(angle);

  // Helper lambda to draw a rotated ellipse
  auto drawEllipse = [&](int ex, int ey, int w, int h, float angleDeg, uint16_t col) {
    float ang = angleDeg * DEG_TO_RAD;
    float cosA = cosf(ang);
    float sinA = sinf(ang);

    for (int dy = -h; dy <= h; dy++) {
      float yRatio = (float)dy / (float)h;
      float span = sqrtf(fmaxf(0.0f, 1.0f - yRatio * yRatio)) * (float)w;
      int x0 = (int)floorf(-span);
      int x1 = (int)ceilf(span);

      for (int i = x0; i <= x1; i++) {
        float xf = (float)i;
        float xr = xf * cosA - (float)dy * sinA;
        float yr = xf * sinA + (float)dy * cosA;
        int px = (int)floorf(ex + xr + 0.5f);
        int py = (int)floorf(ey + yr + 0.5f);
        putPixelSafe(sprite, px, py, col);
      }
    }
  };

  // Draw two ellipse lobes to form a heart
  drawEllipse(cx - 10, cy, w, h, -tiltDeg, col);
  drawEllipse(cx + 10, cy, w, h, +tiltDeg, col);
}

static inline void makeColorRamp(uint16_t outer, uint16_t inner, uint16_t ramp[], int rings) {
  uint8_t ir = ((inner >> 11) & 0x1F) << 3;
  uint8_t ig = ((inner >> 5) & 0x3F) << 2;
  uint8_t ib = (inner & 0x1F) << 3;

  uint8_t or_ = ((outer >> 11) & 0x1F) << 3;
  uint8_t og = ((outer >> 5) & 0x3F) << 2;
  uint8_t ob = (outer & 0x1F) << 3;

  for (int i = 0; i < rings; ++i) {
    float t = (float)i / (rings - 1);
    t = t * t * (3 - 2 * t);  // smoothstep easing
    uint8_t r = or_ + (uint8_t)((ir - or_) * t);
    uint8_t g = og + (uint8_t)((ig - og) * t);
    uint8_t b = ob + (uint8_t)((ib - ob) * t);
    ramp[i] = rgb565(r, g, b);
  }
}

static inline void drawGradientCircleFast(lgfx::LGFX_Sprite* sprite, int cx, int cy,
                                           int r, uint16_t inner, uint16_t outer, int rings) {
  static uint16_t ramp[16];
  if (rings > 16) rings = 16;
  makeColorRamp(outer, inner, ramp, rings);

  for (int i = 0; i < rings; ++i) {
    float t = (float)i / (rings - 1);
    t = t * t * (3 - 2 * t);
    int rr = (int)(r * (1.0f - t) + 0.5f);
    if (rr <= 0) rr = 1;
    sprite->fillCircle(cx, cy, rr, ramp[i]);
  }
}

static void drawCheekGlows(lgfx::LGFX_Sprite* sprite, uint32_t now) {
  float phase = (now * 0.001f) * (2.0f * PI * CHEEK_HZ);
  float s = (sinf(phase) + 1.0f) * 0.5f;  // 0..1
  int r = CHEEK_BASE_RADIUS + (int)((s * 2.0f - 1.0f) * CHEEK_PULSE_AMP);
  if (r < 6) r = 6;

  int leftCX = CENTER_X - EYE_DISTANCE + CHEEK_X_OFFSET;
  int rightCX = CENTER_X + EYE_DISTANCE - CHEEK_X_OFFSET;
  int cy = CENTER_Y + CHEEK_Y_OFFSET;

  drawGradientCircleFast(sprite, leftCX, cy, r, s_cheekInnerColor, s_cheekOuterColor, CHEEK_RINGS);
  drawGradientCircleFast(sprite, rightCX, cy, r, s_cheekInnerColor, s_cheekOuterColor, CHEEK_RINGS);
}

static void renderLove(lgfx::LGFX_Sprite* sprite, uint32_t now) {
  // Draw cheeks first (background layer)
  drawCheekGlows(sprite, now);

  // Heart eyes with subtle vertical bobbing
  float offsetY = sinf(now / HEART_BOB_SPEED_MS) * HEART_BOB_AMPLITUDE;
  drawLoveHeartShape(sprite, CENTER_X - EYE_DISTANCE, CENTER_Y + offsetY,
                     HEART_W, HEART_H, HEART_TILT_DEG, s_loveColor);
  drawLoveHeartShape(sprite, CENTER_X + EYE_DISTANCE, CENTER_Y + offsetY,
                     HEART_W, HEART_H, HEART_TILT_DEG, s_loveColor);
}

// =====================================================
// CYCLOP Emotion Functions
// =====================================================

static void renderCyclop(lgfx::LGFX_Sprite* sprite, uint32_t now) {
  // Breathing scale effect
  s_scalePhase += 0.03f;
  s_cyclopScale = 1.0f + 0.05f * sinf(s_scalePhase);

  // Pupil movement logic
  if (!s_cyclopPaused && now - s_lastCyclopMove > 1000) {
    s_targetPX = random(-50, 51);
    s_targetPY = random(-50, 51);
    s_lastCyclopMove = now;
    s_cyclopPaused = true;
  } else if (s_cyclopPaused && now - s_lastCyclopMove > 500) {
    s_cyclopPaused = false;
  }

  if (!s_cyclopPaused) {
    s_pupilX += (s_targetPX - s_pupilX) * 0.1f;
    s_pupilY += (s_targetPY - s_pupilY) * 0.1f;
  }

  // Draw cyclops eye (yellow background, white eye, black pupil)
  sprite->fillScreen(0xFFE0);  // Yellow
  int r = CYCLOP_EYE_RADIUS * s_cyclopScale;
  sprite->fillCircle(CENTER_X, CENTER_Y, r, 0xFFFF);  // White
  sprite->fillCircle(CENTER_X + s_pupilX * s_cyclopScale,
                     CENTER_Y + s_pupilY * s_cyclopScale,
                     CYCLOP_PUPIL_RADIUS, 0x0000);  // Black
}

// =====================================================
// DRUNK Emotion Functions
// =====================================================

static void drawDrunkWhirlpool(lgfx::LGFX_Sprite* sprite, int cx, int cy,
                                int radius, bool clockwise, float phase) {
  // Draw a spiral/whirlpool effect
  int numRings = 8;
  int numSegments = 32;

  for (int ring = 0; ring < numRings; ring++) {
    float ringRatio = (float)ring / numRings;
    int r = (int)(radius * ringRatio);
    if (r < 2) continue;

    for (int seg = 0; seg < numSegments; seg++) {
      float angle = (TWO_PI * seg / numSegments) + phase + (clockwise ? ringRatio : -ringRatio) * TWO_PI;
      int x = cx + (int)(r * cosf(angle));
      int y = cy + (int)(r * sinf(angle));

      // Create color variation based on position
      uint8_t brightness = 100 + (uint8_t)(155 * ringRatio);
      uint16_t color = rgb565(brightness, brightness / 2, brightness);

      sprite->fillCircle(x, y, 2, color);
    }
  }
}

static void renderDrunk(lgfx::LGFX_Sprite* sprite, uint32_t now) {
  s_phaseLeft += 0.1f;
  s_phaseRight -= 0.1f;

  drawDrunkWhirlpool(sprite, CENTER_X - EYE_DISTANCE, CENTER_Y, 35, true, s_phaseLeft);
  drawDrunkWhirlpool(sprite, CENTER_X + EYE_DISTANCE, CENTER_Y, 35, false, s_phaseRight);
}

// =====================================================
// Public API Implementation
// =====================================================

void begin() {
  // Initialize colors for LOVE emotion
  s_loveColor = rgb565(255, 0, 160);       // Pink eyes
  s_cheekInnerColor = rgb565(255, 60, 170);  // Soft pink core
  s_cheekOuterColor = rgb565(40, 0, 35);     // Darker rim

  // Reset state
  s_active = false;
  s_emotionStartTime = 0;
  s_emotionDuration = 0;
}

void update(uint32_t nowMs) {
  if (!s_active) return;

  // Check if emotion duration has expired
  if (nowMs - s_emotionStartTime >= s_emotionDuration) {
    s_active = false;
  }
}

void trigger(EmotionType emotion, uint32_t durationMs) {
  s_active = true;
  s_currentEmotion = emotion;
  s_emotionStartTime = millis();
  s_emotionDuration = durationMs;

  // Reset emotion-specific state
  switch (emotion) {
    case LEGACY_CYCLOP:
      s_scalePhase = 0.0f;
      s_cyclopScale = 1.0f;
      s_pupilX = 0.0f;
      s_pupilY = 0.0f;
      s_targetPX = 0.0f;
      s_targetPY = 0.0f;
      s_lastCyclopMove = millis();
      s_cyclopPaused = false;
      break;
    case LEGACY_DRUNK:
      s_phaseLeft = 0.0f;
      s_phaseRight = 0.0f;
      break;
    default:
      break;
  }
}

bool isActive() {
  return s_active;
}

EmotionType getCurrentEmotion() {
  return s_currentEmotion;
}

bool renderFrame(void* spritePtr, uint32_t nowMs) {
  if (!s_active) return false;

  lgfx::LGFX_Sprite* sprite = static_cast<lgfx::LGFX_Sprite*>(spritePtr);

  // Clear background (black by default)
  if (s_currentEmotion != LEGACY_CYCLOP) {
    sprite->fillScreen(0x0000);  // Black
  }

  // Render the appropriate emotion
  switch (s_currentEmotion) {
    case LEGACY_LOVE:
      renderLove(sprite, nowMs);
      break;
    case LEGACY_CYCLOP:
      renderCyclop(sprite, nowMs);
      break;
    case LEGACY_DRUNK:
      renderDrunk(sprite, nowMs);
      break;
  }

  return s_active;
}

} // namespace LegacyEmotions
