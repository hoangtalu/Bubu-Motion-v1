#ifndef ADC_BATTERY_MONITOR_H
#define ADC_BATTERY_MONITOR_H

#include <functional>
#include <driver/gpio.h>
#include <adc_battery_estimation.h>
#include <esp_timer.h>
#include <esp_err.h>
#include <esp_adc/adc_oneshot.h>
#include <esp_adc/adc_cali.h>

class AdcBatteryMonitor {
public:
    struct Diagnostics {
        int level_percent = 100;
        bool level_is_fallback = true;
        esp_err_t capacity_err = ESP_FAIL;

        int battery_voltage_mv = -1;
        int adc_raw = -1;
        esp_err_t voltage_err = ESP_FAIL;

        bool charging_state = false;
        esp_err_t charging_err = ESP_FAIL;
    };

    AdcBatteryMonitor(adc_unit_t adc_unit, adc_channel_t adc_channel, float upper_resistor, float lower_resistor, gpio_num_t charging_pin = GPIO_NUM_NC);
    ~AdcBatteryMonitor();

    bool IsCharging();
    bool IsDischarging();
    uint8_t GetBatteryLevel();
    bool GetDiagnostics(Diagnostics& out);

    void OnChargingStatusChanged(std::function<void(bool)> callback);

private:
    gpio_num_t charging_pin_;
    adc_unit_t adc_unit_;
    adc_channel_t adc_channel_;
    adc_bitwidth_t adc_bitwidth_ = ADC_BITWIDTH_DEFAULT;
    adc_atten_t adc_atten_ = ADC_ATTEN_DB_12;
    float upper_resistor_ = 0.0f;
    float lower_resistor_ = 0.0f;
    float voltage_divider_ratio_ = 1.0f;

    adc_oneshot_unit_handle_t debug_adc_handle_ = nullptr;
    adc_cali_handle_t debug_adc_cali_handle_ = nullptr;
    bool owns_debug_adc_handles_ = false;

    adc_battery_estimation_handle_t adc_battery_estimation_handle_ = nullptr;
    esp_timer_handle_t timer_handle_ = nullptr;
    bool is_charging_ = false;
    std::function<void(bool)> on_charging_status_changed_;

    bool InitializeDebugAdcHandles();
    void DestroyDebugAdcHandles();
    bool ReadBatteryVoltageMv(int& battery_mv, int& adc_raw, esp_err_t& err);
    void CheckBatteryStatus();
};

#endif // ADC_BATTERY_MONITOR_H
