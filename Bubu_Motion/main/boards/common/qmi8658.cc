#include "qmi8658.h"
#include <esp_log.h>

#define TAG "Qmi8658"

// Register addresses — confirmed from Waveshare FullFunctionTest QMI8658 sample driver
static constexpr uint8_t REG_WHO_AM_I = 0x00;  // QMI8658Register_WhoAmI
static constexpr uint8_t REG_CTRL1    = 0x02;  // QMI8658Register_Ctrl1
static constexpr uint8_t REG_CTRL2    = 0x03;  // QMI8658Register_Ctrl2 (accel config)
static constexpr uint8_t REG_CTRL5    = 0x06;  // QMI8658Register_Ctrl5 (LPF config)
static constexpr uint8_t REG_CTRL7    = 0x08;  // QMI8658Register_Ctrl7 (enable flags)
static constexpr uint8_t REG_AX_L     = 0x35;  // QMI8658Register_Ax_L (accel X low byte)

static constexpr uint8_t WHO_AM_I_EXPECTED = 0x05;

Qmi8658::Qmi8658(i2c_master_bus_handle_t bus) : I2cDevice(bus, 0x6B) {}

bool Qmi8658::Init() {
    uint8_t who_am_i = 0;
    if (TryReadRegs(REG_WHO_AM_I, &who_am_i, 1) != ESP_OK || who_am_i != WHO_AM_I_EXPECTED) {
        ESP_LOGW(TAG, "QMI8658 not found (WHO_AM_I=0x%02X, expected 0x%02X)", who_am_i, WHO_AM_I_EXPECTED);
        return false;
    }
    ESP_LOGI(TAG, "QMI8658 found (WHO_AM_I=0x%02X)", who_am_i);

    // Ported from sample QMI8658_init():
    WriteReg(REG_CTRL1, 0x60);  // address auto-increment on (SPI/I2C interface config)
    WriteReg(REG_CTRL2, 0x17);  // ±4g (0x10) + 62.5 Hz ODR (0x07)
    WriteReg(REG_CTRL5, 0x00);  // LPF disabled — we smooth in EyeAnimation lerp
    WriteReg(REG_CTRL7, 0x01);  // accel-only enable (QMI8658_CTRL7_ACC_ENABLE)
    return true;
}

void Qmi8658::ReadAccel(float& ax, float& ay, float& az) {
    uint8_t buf[6] = {};
    if (TryReadRegs(REG_AX_L, buf, 6) != ESP_OK) {
        // Keep last valid values on I2C error — same safe pattern as Cst816d::UpdateTouchPoint
        ax = last_ax_;
        ay = last_ay_;
        az = last_az_;
        return;
    }
    // Little-endian 16-bit signed — same conversion as sample QMI8658_read_acc_xyz
    last_ax_ = static_cast<float>(static_cast<int16_t>((buf[1] << 8) | buf[0])) * SCALE;
    last_ay_ = static_cast<float>(static_cast<int16_t>((buf[3] << 8) | buf[2])) * SCALE;
    last_az_ = static_cast<float>(static_cast<int16_t>((buf[5] << 8) | buf[4])) * SCALE;
    ax = last_ax_;
    ay = last_ay_;
    az = last_az_;
}
