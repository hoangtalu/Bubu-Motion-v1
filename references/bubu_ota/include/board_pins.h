#pragma once

// TFT 8-bit I80 bus to GC9A01
constexpr int PIN_LCD_D0 = 10;
constexpr int PIN_LCD_D1 = 11;
constexpr int PIN_LCD_D2 = 12;
constexpr int PIN_LCD_D3 = 13;
constexpr int PIN_LCD_D4 = 14;
constexpr int PIN_LCD_D5 = 15;
constexpr int PIN_LCD_D6 = 16;
constexpr int PIN_LCD_D7 = 17;

constexpr int PIN_LCD_DC  = 18;
constexpr int PIN_LCD_CS  = 2;
constexpr int PIN_LCD_WR  = 3;
constexpr int PIN_LCD_RD  = -1;   // not used
constexpr int PIN_LCD_RST = 21;
constexpr int PIN_LCD_BL  = 42;   // backlight PWM

// USB pins just for reference (no code yet):
// D- = 19, D+ = 20

// I2C bus
constexpr int PIN_I2C_SDA = 8;
constexpr int PIN_I2C_SCL = 9;

// Touch and IO expander
constexpr int PIN_TOUCH_RST = 0;
constexpr int PIN_TCA_INT   = 45;

// I2S Microphone (INMP441 or compatible) - I2S_NUM_0 RX only
constexpr int PIN_MIC_SCK  = 5;   // I2S Bit Clock (shared line with mic)
constexpr int PIN_MIC_WS   = 4;   // I2S Word Select / LR Clock
constexpr int PIN_MIC_SD   = 6;   // I2S Data In (from microphone)

// I2S Speaker (MAX98357A) - I2S_NUM_1 TX only via ESP32-audioI2S
// NOTE: Verify GPIO 40/41 are routed on your PCB. Fallbacks: GPIO 47/48
constexpr int PIN_SPK_BCLK = 40;  // I2S Bit Clock (speaker dedicated)
constexpr int PIN_SPK_LRCK = 41;  // I2S Word Select / LR Clock (speaker dedicated)
constexpr int PIN_SPK_DOUT = 7;   // I2S Data Out (to MAX98357A DIN)
