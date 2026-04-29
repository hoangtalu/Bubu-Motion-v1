#pragma once
#include "i2c_device.h"

// QMI8658 6-axis IMU driver — accel-only, ported from the Waveshare FullFunctionTest sample.
// Adapted to the ESP-IDF I2cDevice base class (replaces Arduino Wire calls).
// Board: ESP32S3 1.28" round I80, IMU at I2C address 0x6B on shared bus IO8/IO9.
class Qmi8658 : public I2cDevice {
public:
    explicit Qmi8658(i2c_master_bus_handle_t bus);  // address 0x6B hardcoded

    // Returns false if chip is absent or WHO_AM_I mismatch — caller should null this out.
    bool Init();

    // Read accelerometer values in g. Keeps last valid values on I2C error.
    void ReadAccel(float& ax, float& ay, float& az);

private:
    // ±4g range → acc_lsb_div = 1 << 13 = 8192 (from sample QMI8658_config_acc)
    static constexpr float SCALE = 1.0f / 8192.0f;

    float last_ax_ = 0.0f;
    float last_ay_ = 0.0f;
    float last_az_ = 0.0f;
};
