#include "touch_system.h"

#include <Arduino.h>
#include <Wire.h>
#include <lvgl.h>

#include "board_pins.h"
#include "logger.h"
DEFINE_MODULE_LOGGER_DISABLED(TouchLog)  // Set to DEFINE_MODULE_LOGGER to enable

// Forward declarations for functions used in implementation
namespace TouchSystem {
  // These need to be in the header if used externally
  bool isTcaActive();
  bool hasRecentSample(uint32_t windowMs);
  uint32_t lastSampleTimestamp();
}

namespace {

constexpr uint8_t CST816_ADDR = 0x15;
constexpr uint8_t TCA6408_ADDR = 0x20;

// Hardware gesture IDs from CST816S datasheet
constexpr uint8_t HW_GESTURE_NONE = 0x00;
constexpr uint8_t HW_GESTURE_SWIPE_UP = 0x01;
constexpr uint8_t HW_GESTURE_SWIPE_DOWN = 0x02;
constexpr uint8_t HW_GESTURE_SWIPE_LEFT = 0x03;
constexpr uint8_t HW_GESTURE_SWIPE_RIGHT = 0x04;
constexpr uint8_t HW_GESTURE_SINGLE_CLICK = 0x05;
constexpr uint8_t HW_GESTURE_DOUBLE_CLICK = 0x0B;
constexpr uint8_t HW_GESTURE_LONG_PRESS = 0x0C;

volatile bool touchInterruptFlag = false;
volatile bool tcaInterruptFlag = false;

TouchPoint pendingEvent;
bool eventAvailable = false;
lv_indev_t* lvglTouchIndev = nullptr;

// Touch state tracking
struct TouchState {
  bool isDown = false;
  uint32_t downTime = 0;
  uint16_t downX = 0;
  uint16_t downY = 0;
  uint16_t currentX = 0;
  uint16_t currentY = 0;
  uint32_t lastReadTime = 0;
  uint8_t fingerCount = 0;
  bool longPressFired = false;
} touch;

// Gesture thresholds (tuned for 240x240 screen)
constexpr uint32_t LONG_PRESS_MS = 400;      
constexpr uint16_t TAP_MAX_DRIFT_PX = 35;    // Increased for easier taps
constexpr uint32_t RELEASE_TIMEOUT_MS = 120; // Tighter timeout
constexpr uint32_t DEBOUNCE_MS = 20;         // Very short debounce

void IRAM_ATTR touchISR() {
  touchInterruptFlag = true;
  tcaInterruptFlag = true;
}

void lvglTouchRead(lv_indev_t* indev, lv_indev_data_t* data) {
  (void)indev;
  data->state = touch.isDown ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
  if (touch.isDown) {
    // Freeze LVGL pointer on touch-down to prevent drag/scroll gestures.
    data->point.x = static_cast<int32_t>(touch.downX);
    data->point.y = static_cast<int32_t>(touch.downY);
  } else {
    data->point.x = static_cast<int32_t>(touch.currentX);
    data->point.y = static_cast<int32_t>(touch.currentY);
  }
  data->continue_reading = false;
}

void resetCST816() {
  pinMode(PIN_TOUCH_RST, OUTPUT);
  digitalWrite(PIN_TOUCH_RST, LOW);
  delay(10);
  digitalWrite(PIN_TOUCH_RST, HIGH);
  delay(50);
}

bool readTouchData(uint8_t& gestureID, uint8_t& fingerCount, uint16_t& x, uint16_t& y) {
  // Batch read registers 0x01-0x06 in a single I2C transaction (more reliable)
  Wire.beginTransmission(CST816_ADDR);
  Wire.write(0x01);
  if (Wire.endTransmission(false) != 0) {
    return false;
  }

  if (Wire.requestFrom(CST816_ADDR, static_cast<size_t>(6), true) != 6) {
    return false;
  }
  gestureID  = Wire.read();           // 0x01
  fingerCount = Wire.read() & 0x0F;   // 0x02
  uint8_t x_h = Wire.read();          // 0x03
  uint8_t x_l = Wire.read();          // 0x04
  uint8_t y_h = Wire.read();          // 0x05
  uint8_t y_l = Wire.read();          // 0x06

  // Validate finger count
  if (fingerCount == 0 || fingerCount > 2) {
    static uint32_t lastNoFingerLog = 0;
    uint32_t now = millis();
    if (now - lastNoFingerLog > 500) {
      // Serial.printf("[Touch] no finger (fc=%u, gest=0x%02X)\n", fingerCount, gestureID);
      lastNoFingerLog = now;
    }
    return false;
  }

  // Reconstruct 12-bit coordinates (mask out event/ID bits)
  int raw_x = ((x_h & 0x0F) << 8) | x_l;
  int raw_y = ((y_h & 0x0F) << 8) | y_l;

  // Sanity check
  if (raw_x > 500 || raw_y > 500) {
    return false;
  }

  // Map to screen coordinates: invert X (0xFF offset matches hardware calibration)
  int mapped_x = 0xFF - raw_x;
  int mapped_y = raw_y;

  // Apply display rotation=1 (90°) to align touch with screen orientation
  int rot_x = mapped_y;
  int rot_y = 239 - mapped_x;

  // Boundary check
  if (rot_x < 0 || rot_x >= 240 || rot_y < 0 || rot_y >= 240) {
    return false;
  }

  x = static_cast<uint16_t>(rot_x);
  y = static_cast<uint16_t>(rot_y);

  return true;
}

uint16_t calculateDistance(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2) {
  int16_t dx = static_cast<int16_t>(x2) - static_cast<int16_t>(x1);
  int16_t dy = static_cast<int16_t>(y2) - static_cast<int16_t>(y1);
  // Manhattan distance (faster than sqrt, good enough for gesture detection)
  return static_cast<uint16_t>(abs(static_cast<int>(dx)) + abs(static_cast<int>(dy)));
}

void emitGesture(TouchGesture gesture, uint16_t x, uint16_t y, uint32_t duration) {
  pendingEvent.gesture = gesture;
  pendingEvent.x = x;
  pendingEvent.y = y;
  pendingEvent.duration = duration;
  eventAvailable = true;

  const char* gestureName[] = {
    "NONE", "TAP", "LONG_PRESS", "LONG", "SWIPE_UP", "SWIPE_DOWN", "SWIPE_LEFT", "SWIPE_RIGHT"
  };
  TouchLog::printf("[Touch] %s at (%d,%d) dur=%lums\n", 
                gestureName[gesture], x, y, duration);
}

void handleTouchDown(uint16_t x, uint16_t y, uint32_t now) {
  touch.isDown = true;
  touch.downTime = now;
  touch.downX = x;
  touch.downY = y;
  touch.currentX = x;
  touch.currentY = y;
  touch.lastReadTime = now;
  touch.longPressFired = false;
  
  TouchLog::printf("[Touch] DOWN at (%d,%d)\n", x, y);
}

void handleTouchMove(uint16_t x, uint16_t y, uint32_t now) {
  touch.currentX = x;
  touch.currentY = y;
  touch.lastReadTime = now;
}

void checkLongPress(uint32_t now) {
  if (touch.longPressFired || !touch.isDown) {
    return;
  }

  uint32_t heldDuration = now - touch.downTime;
  if (heldDuration < LONG_PRESS_MS) {
    return;
  }

  // Check if finger stayed relatively still
  uint16_t drift = calculateDistance(touch.downX, touch.downY, 
                                     touch.currentX, touch.currentY);
  
  TouchLog::printf("[Touch] Long press check: held=%lums, drift=%upx (max=%u)\n", 
                heldDuration, drift, TAP_MAX_DRIFT_PX);
  
  if (drift <= TAP_MAX_DRIFT_PX) {
    touch.longPressFired = true;
    emitGesture(TOUCH_LONG_PRESS, touch.downX, touch.downY, heldDuration);
    TouchLog::println("[Touch] *** LONG PRESS FIRED ***");
  } else {
    TouchLog::printf("[Touch] Long press rejected - too much drift (%u > %u)\n", 
                  drift, TAP_MAX_DRIFT_PX);
  }
}

void handleTouchRelease(uint32_t now) {
  uint32_t duration = now - touch.downTime;
  
  // If long press already fired, don't emit another gesture
  if (touch.longPressFired) {
    TouchLog::printf("[Touch] RELEASE after long press (dur=%lums)\n", duration);
    touch.isDown = false;
    return;
  }

  // Ignore very short taps (debounce)
  if (duration < DEBOUNCE_MS) {
    TouchLog::printf("[Touch] IGNORED - too short (%lums)\n", duration);
    touch.isDown = false;
    return;
  }

  // Calculate motion
  int16_t deltaX = static_cast<int16_t>(touch.currentX) - static_cast<int16_t>(touch.downX);
  int16_t deltaY = static_cast<int16_t>(touch.currentY) - static_cast<int16_t>(touch.downY);
  uint16_t totalDrift = static_cast<uint16_t>(abs(static_cast<int>(deltaX)) + abs(static_cast<int>(deltaY)));

  TouchLog::printf("[Touch] Release analysis: dur=%lums, drift=%upx, dx=%d, dy=%d\n", 
                duration, totalDrift, deltaX, deltaY);

  if (totalDrift > TAP_MAX_DRIFT_PX) {
    TouchLog::printf("[Touch] IGNORED - drag (drift %u > %u)\n",
                     totalDrift, TAP_MAX_DRIFT_PX);
    touch.isDown = false;
    return;
  }

  TouchLog::printf("[Touch] -> Classified as TAP (drift %u <= %u)\n",
                   totalDrift, TAP_MAX_DRIFT_PX);
  emitGesture(TOUCH_TAP, touch.downX, touch.downY, duration);
  touch.isDown = false;
}

}  // anonymous namespace

namespace TouchSystem {

void begin() {
  TouchLog::println("[TouchSystem] Initializing...");
  
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  Wire.setClock(400000);
  delay(10);
  
  // Initialize TCA6408 (IO expander) - all inputs
  Wire.beginTransmission(TCA6408_ADDR);
  Wire.write(0x03);  // Configuration register
  Wire.write(0xFF);  // All inputs
  Wire.endTransmission(true);
  delay(10);
  
  // Reset touch controller
  resetCST816();
  
  // Read chip ID
  Wire.beginTransmission(CST816_ADDR);
  Wire.write(0xA7);
  Wire.endTransmission(false);
  Wire.requestFrom(CST816_ADDR, static_cast<size_t>(1), true);
  uint8_t chipID = Wire.read();
  
  TouchLog::printf("[TouchSystem] Chip ID: 0x%02X", chipID);
  if (chipID == 0xB4) {
    TouchLog::println(" (CST816S) ✓");
  } else if (chipID == 0xB5) {
    TouchLog::println(" (CST816T) ✓");
  } else if (chipID == 0xB6) {
    TouchLog::println(" (CST816D) ✓");
  } else {
    TouchLog::println(" (Unknown)");
  }
  
  // Disable auto-sleep so the chip stays awake and responsive to touches
  Wire.beginTransmission(CST816_ADDR);
  Wire.write(0xFE);  // DisAutoSleep register
  Wire.write(0x01);  // 1 = disable auto-sleep
  Wire.endTransmission(true);
  delay(10);

  // Configure interrupt control (register 0xFA)
  // EnTouch + EnChange = respond to all touch events
  Wire.beginTransmission(CST816_ADDR);
  Wire.write(0xFA);
  Wire.write(0x60);  // EnTouch(bit6) + EnChange(bit5)
  Wire.endTransmission(true);
  delay(10);

  // Configure long press time if needed (register 0xEB)
  // Default is 100 (~1 second), we want 50 (~0.5 seconds)
  Wire.beginTransmission(CST816_ADDR);
  Wire.write(0xEB);
  Wire.write(50);
  Wire.endTransmission(true);
  delay(10);
  
  // Setup interrupt pin
  pinMode(PIN_TCA_INT, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PIN_TCA_INT), touchISR, FALLING);
  
  TouchLog::println("[TouchSystem] Ready!");
}

void update() {
  uint32_t now = millis();
  static uint32_t lastUpdate = 0;

  // Limit update rate to reduce I2C bus congestion
  if ((now - lastUpdate) < 5) {
    return;
  }
  lastUpdate = now;

  // Periodic diagnostic heartbeat (every 3s)
  static uint32_t lastDiag = 0;
  if (now - lastDiag >= 3000) {
    lastDiag = now;

    // Probe CST816 by reading chip ID
    Wire.beginTransmission(CST816_ADDR);
    Wire.write(0xA7);
    uint8_t i2cErr = Wire.endTransmission(false);
    uint8_t chipId = 0;
    if (i2cErr == 0 && Wire.requestFrom(CST816_ADDR, static_cast<size_t>(1), true) == 1) {
      chipId = Wire.read();
    }

    // Try reading TCA6408 input register
    Wire.beginTransmission(TCA6408_ADDR);
    Wire.write(0x00);
    uint8_t tcaErr = Wire.endTransmission(false);
    uint8_t tcaVal = 0xFF;
    if (tcaErr == 0 && Wire.requestFrom(TCA6408_ADDR, static_cast<size_t>(1), true) == 1) {
      tcaVal = Wire.read();
    }

    // Serial.printf("[Touch] diag: cst=%s(0x%02X) tca=%s(0x%02X) intFlag=%d isDown=%d\n",
    //               i2cErr == 0 ? "OK" : "FAIL", chipId,
    //               tcaErr == 0 ? "OK" : "FAIL", tcaVal,
    //               (int)touchInterruptFlag, (int)touch.isDown);

    // Auto-recover: if CST816 stopped responding, reset and reconfigure it
    if (chipId == 0x00 || i2cErr != 0) {
      // CST816 unresponsive — reset silently
      resetCST816();

      // Re-disable auto-sleep
      Wire.beginTransmission(CST816_ADDR);
      Wire.write(0xFE);
      Wire.write(0x01);
      Wire.endTransmission(true);
      delay(5);

      // Re-enable interrupts (EnTouch + EnChange)
      Wire.beginTransmission(CST816_ADDR);
      Wire.write(0xFA);
      Wire.write(0x60);
      Wire.endTransmission(true);
      delay(5);
    }
  }

  // Poll TCA6408 for touch interrupt status
  Wire.beginTransmission(TCA6408_ADDR);
  Wire.write(0x00);  // Input port register
  Wire.endTransmission(false);
  if (Wire.requestFrom(TCA6408_ADDR, static_cast<size_t>(1), true) == 1) {
    uint8_t tcaInput = Wire.read();
    // Bit 0 is touch interrupt (active low = touch present)
    if ((tcaInput & 0x01) == 0x00) {
      touchInterruptFlag = true;
    }
  }

  // If touch is down, always try to read new coordinates
  if (touch.isDown) {
    checkLongPress(now);
    
    // Try to read new coordinates
    uint8_t gestureID, fingerCount;
    uint16_t x, y;
    
    if (readTouchData(gestureID, fingerCount, x, y)) {
      if (fingerCount > 0) {
        // Update position (only log if significant movement)
        uint16_t dist = calculateDistance(touch.currentX, touch.currentY, x, y);
        if (dist > 5) {
          TouchLog::printf("[Touch] MOVE to (%d,%d), delta=(%d,%d)\n", 
                        x, y, 
                        (int)x - (int)touch.downX, 
                        (int)y - (int)touch.downY);
        }
        handleTouchMove(x, y, now);
      } else {
        // Finger lifted
        TouchLog::println("[Touch] Finger lifted -> RELEASE");
        handleTouchRelease(now);
        return;
      }
    } else {
      // Can't read data - check timeout
      if ((now - touch.lastReadTime) >= RELEASE_TIMEOUT_MS) {
        TouchLog::println("[Touch] Read timeout -> RELEASE");
        handleTouchRelease(now);
        return;
      }
    }
  }
  
  // Check for new touch down
  if (!touch.isDown && touchInterruptFlag) {
    touchInterruptFlag = false;

    uint8_t gestureID, fingerCount;
    uint16_t x, y;

    if (readTouchData(gestureID, fingerCount, x, y)) {
      if (fingerCount > 0) {
        handleTouchDown(x, y, now);
        // Serial.printf("[Touch] DOWN (%d,%d)\n", x, y);
      }
    } else {
      static uint32_t lastReadFail = 0;
      if (now - lastReadFail > 1000) {
        // readTouchData failed on interrupt (silenced)
        lastReadFail = now;
      }
    }
  }
}

bool available() {
  return eventAvailable;
}

TouchPoint get() {
  eventAvailable = false;
  return pendingEvent;
}

void lvgl_init() {
  if (lvglTouchIndev) return;
  lvglTouchIndev = lv_indev_create();
  if (!lvglTouchIndev) {
    TouchLog::println("[Touch] LVGL indev create failed");
    return;
  }
  lv_indev_set_type(lvglTouchIndev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(lvglTouchIndev, lvglTouchRead);
  lv_display_t* disp = lv_display_get_default();
  if (disp) {
    lv_indev_set_display(lvglTouchIndev, disp);
  }
  TouchLog::println("[Touch] LVGL input registered");
}

// Compatibility functions
bool isTcaActive() {
  return false;
}

bool hasRecentSample(uint32_t windowMs) {
  return (millis() - touch.lastReadTime) < windowMs;
}

TouchPoint getLastPoint() {
  return pendingEvent;
}

uint32_t lastSampleTimestamp() {
  return touch.lastReadTime;
}

uint32_t getTouchDownCount() {
  static uint32_t count = 0;
  if (eventAvailable && pendingEvent.gesture != TOUCH_NONE) count++;
  return count;
}

uint32_t getFinalEventCount() {
  return getTouchDownCount();
}

bool isTouchPressed() {
  return touch.isDown;
}

bool consumeTcaInterrupt() {
  bool fired = false;
  noInterrupts();
  fired = tcaInterruptFlag;
  tcaInterruptFlag = false;
  interrupts();
  return fired;
}

}  // namespace TouchSystem
