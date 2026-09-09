#include "adc_battery_monitor.h"

#include <esp_log.h>

#include <algorithm>

namespace {
constexpr const char* TAG = "AdcBatteryMonitor";
constexpr int kVoltageSamples = 8;
}

AdcBatteryMonitor::AdcBatteryMonitor(adc_unit_t adc_unit, adc_channel_t adc_channel,
                                     float upper_resistor, float lower_resistor,
                                     gpio_num_t charging_pin)
    : charging_pin_(charging_pin),
      adc_unit_(adc_unit),
      adc_channel_(adc_channel),
      upper_resistor_(upper_resistor),
      lower_resistor_(lower_resistor) {

    if (charging_pin_ != GPIO_NUM_NC) {
        gpio_config_t gpio_cfg = {
            .pin_bit_mask = 1ULL << charging_pin_,
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        ESP_ERROR_CHECK(gpio_config(&gpio_cfg));
    }

    const float total_resistance = upper_resistor_ + lower_resistor_;
    if (total_resistance > 0.0f) {
        voltage_divider_ratio_ = lower_resistor_ / total_resistance;
    }

    const bool has_debug_adc = InitializeDebugAdcHandles();

    adc_battery_estimation_t adc_cfg = {
        .adc_channel = adc_channel_,
        .upper_resistor = upper_resistor_,
        .lower_resistor = lower_resistor_,
    };

    if (has_debug_adc) {
        adc_cfg.external.adc_handle = debug_adc_handle_;
        adc_cfg.external.adc_cali_handle = debug_adc_cali_handle_;
    } else {
        adc_cfg.internal.adc_unit = adc_unit_;
        adc_cfg.internal.adc_bitwidth = adc_bitwidth_;
        adc_cfg.internal.adc_atten = adc_atten_;
    }

    if (charging_pin_ != GPIO_NUM_NC) {
        adc_cfg.charging_detect_cb = [](void *user_data) -> bool {
            AdcBatteryMonitor *self = static_cast<AdcBatteryMonitor *>(user_data);
            return gpio_get_level(self->charging_pin_) == 1;
        };
        adc_cfg.charging_detect_user_data = this;
    } else {
        adc_cfg.charging_detect_cb = nullptr;
        adc_cfg.charging_detect_user_data = nullptr;
    }

    adc_battery_estimation_handle_ = adc_battery_estimation_create(&adc_cfg);
    if (adc_battery_estimation_handle_ == nullptr) {
        ESP_LOGW(TAG, "Failed to create adc_battery_estimation handle");
    }

    esp_timer_create_args_t timer_cfg = {
        .callback = [](void *arg) {
            AdcBatteryMonitor *self = static_cast<AdcBatteryMonitor *>(arg);
            self->CheckBatteryStatus();
        },
        .arg = this,
        .name = "adc_battery_monitor",
    };
    ESP_ERROR_CHECK(esp_timer_create(&timer_cfg, &timer_handle_));
    ESP_ERROR_CHECK(esp_timer_start_periodic(timer_handle_, 1000000));
}

AdcBatteryMonitor::~AdcBatteryMonitor() {
    if (adc_battery_estimation_handle_) {
        ESP_ERROR_CHECK(adc_battery_estimation_destroy(adc_battery_estimation_handle_));
    }

    DestroyDebugAdcHandles();

    if (timer_handle_) {
        esp_timer_stop(timer_handle_);
        esp_timer_delete(timer_handle_);
    }
}

bool AdcBatteryMonitor::IsCharging() {
    if (adc_battery_estimation_handle_ != nullptr) {
        bool is_charging = false;
        esp_err_t err = adc_battery_estimation_get_charging_state(adc_battery_estimation_handle_, &is_charging);
        if (err == ESP_OK) {
            return is_charging;
        }
    }

    if (charging_pin_ != GPIO_NUM_NC) {
        return gpio_get_level(charging_pin_) == 1;
    }

    return false;
}

bool AdcBatteryMonitor::IsDischarging() {
    return !IsCharging();
}

uint8_t AdcBatteryMonitor::GetBatteryLevel() {
    Diagnostics diagnostics;
    if (!GetDiagnostics(diagnostics)) {
        return 100;
    }
    return static_cast<uint8_t>(diagnostics.level_percent);
}

bool AdcBatteryMonitor::GetDiagnostics(Diagnostics& out) {
    out = Diagnostics{};

    bool ok = false;
    if (adc_battery_estimation_handle_ != nullptr) {
        float capacity = 0.0f;
        out.capacity_err = adc_battery_estimation_get_capacity(adc_battery_estimation_handle_, &capacity);
        if (out.capacity_err == ESP_OK) {
            int rounded = static_cast<int>(capacity + 0.5f);
            out.level_percent = std::max(0, std::min(100, rounded));
            out.level_is_fallback = false;
            ok = true;
        }

        out.charging_err = adc_battery_estimation_get_charging_state(adc_battery_estimation_handle_, &out.charging_state);
        if (out.charging_err != ESP_OK) {
            out.charging_state = false;
        }
    }

    int battery_mv = -1;
    int adc_raw = -1;
    esp_err_t voltage_err = ESP_FAIL;
    if (ReadBatteryVoltageMv(battery_mv, adc_raw, voltage_err)) {
        out.battery_voltage_mv = battery_mv;
        out.adc_raw = adc_raw;
        out.voltage_err = ESP_OK;
        ok = true;
    } else {
        out.voltage_err = voltage_err;
    }

    return ok;
}

void AdcBatteryMonitor::OnChargingStatusChanged(std::function<void(bool)> callback) {
    on_charging_status_changed_ = callback;
}

void AdcBatteryMonitor::CheckBatteryStatus() {
    bool new_charging_status = IsCharging();
    if (new_charging_status != is_charging_) {
        is_charging_ = new_charging_status;
        if (on_charging_status_changed_) {
            on_charging_status_changed_(is_charging_);
        }
    }
}

bool AdcBatteryMonitor::InitializeDebugAdcHandles() {
    adc_oneshot_unit_init_cfg_t init_cfg = {
        .unit_id = adc_unit_,
    };
    esp_err_t err = adc_oneshot_new_unit(&init_cfg, &debug_adc_handle_);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "adc_oneshot_new_unit failed: %s", esp_err_to_name(err));
        return false;
    }

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = adc_atten_,
        .bitwidth = adc_bitwidth_,
    };
    err = adc_oneshot_config_channel(debug_adc_handle_, adc_channel_, &chan_cfg);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "adc_oneshot_config_channel failed: %s", esp_err_to_name(err));
        DestroyDebugAdcHandles();
        return false;
    }

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_curve_fitting_config_t cali_config = {
        .unit_id = adc_unit_,
        .chan = adc_channel_,
        .atten = adc_atten_,
        .bitwidth = adc_bitwidth_,
    };
    err = adc_cali_create_scheme_curve_fitting(&cali_config, &debug_adc_cali_handle_);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "adc_cali_create_scheme_curve_fitting failed: %s", esp_err_to_name(err));
        DestroyDebugAdcHandles();
        return false;
    }
#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    adc_cali_line_fitting_config_t cali_config = {
        .unit_id = adc_unit_,
        .atten = adc_atten_,
        .bitwidth = adc_bitwidth_,
    };
    err = adc_cali_create_scheme_line_fitting(&cali_config, &debug_adc_cali_handle_);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "adc_cali_create_scheme_line_fitting failed: %s", esp_err_to_name(err));
        DestroyDebugAdcHandles();
        return false;
    }
#else
    ESP_LOGW(TAG, "No ADC calibration scheme available");
    DestroyDebugAdcHandles();
    return false;
#endif

    owns_debug_adc_handles_ = true;
    return true;
}

void AdcBatteryMonitor::DestroyDebugAdcHandles() {
    if (!owns_debug_adc_handles_) {
        return;
    }

    if (debug_adc_cali_handle_ != nullptr) {
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
        adc_cali_delete_scheme_curve_fitting(debug_adc_cali_handle_);
#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
        adc_cali_delete_scheme_line_fitting(debug_adc_cali_handle_);
#endif
        debug_adc_cali_handle_ = nullptr;
    }

    if (debug_adc_handle_ != nullptr) {
        adc_oneshot_del_unit(debug_adc_handle_);
        debug_adc_handle_ = nullptr;
    }
    owns_debug_adc_handles_ = false;
}

bool AdcBatteryMonitor::ReadBatteryVoltageMv(int& battery_mv, int& adc_raw, esp_err_t& err) {
    battery_mv = -1;
    adc_raw = -1;
    err = ESP_FAIL;

    if (debug_adc_handle_ == nullptr || debug_adc_cali_handle_ == nullptr || voltage_divider_ratio_ <= 0.0f) {
        err = ESP_ERR_INVALID_STATE;
        return false;
    }

    int total_mv = 0;
    int total_raw = 0;
    for (int i = 0; i < kVoltageSamples; ++i) {
        int sample_raw = 0;
        err = adc_oneshot_read(debug_adc_handle_, adc_channel_, &sample_raw);
        if (err != ESP_OK) {
            return false;
        }

        int sample_mv = 0;
        err = adc_cali_raw_to_voltage(debug_adc_cali_handle_, sample_raw, &sample_mv);
        if (err != ESP_OK) {
            return false;
        }

        total_raw += sample_raw;
        total_mv += sample_mv;
    }

    const int avg_raw = total_raw / kVoltageSamples;
    const int avg_mv = total_mv / kVoltageSamples;
    adc_raw = avg_raw;
    battery_mv = static_cast<int>((static_cast<float>(avg_mv) / voltage_divider_ratio_) + 0.5f);
    err = ESP_OK;
    return true;
}
