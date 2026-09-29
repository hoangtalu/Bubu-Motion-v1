#include "wifi_board.h"
#include <inttypes.h>
#include "audio/codecs/no_audio_codec.h"
#include "display/eye_display.h"
#include "display/menu_system.h"
#include "screen_manager.h"
#include "message_board.h"
#include "application.h"
#include "config.h"
#include "i2c_device.h"
#include "qmi8658.h"
#include "adc_battery_monitor.h"
#include "reminder_system.h"
#include "settings.h"
#include "assets/lang_config.h"
#include <esp_log.h>
#include <esp_vfs_fat.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_gc9a01.h>
#include <sdmmc_cmd.h>
#include <driver/i2c_master.h>
#include <driver/gpio.h>
#include <driver/sdspi_host.h>
#include <driver/spi_common.h>
#include <esp_io_expander_tca9554.h>
#include <esp_timer.h>
#include <iot_button.h>

#include <algorithm>
#include <cctype>
#include <dirent.h>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

// ── Touch interrupt flag (set by GPIO ISR, cleared in timer) ─────────────────
static volatile bool s_touch_int_flag = false;

static void IRAM_ATTR touch_gpio_isr(void*) {
    s_touch_int_flag = true;
}

#define TAG "Esp32S3RoundI80Board"

// ── CST816D touch controller ─────────────────────────────────────────────────
// Mirrors bubu_clean/src/touch_system.cpp:
//   • Reads chip ID (0xA7) and logs it
//   • Configures EnChange interrupt (0xFA = 0x20)
//   • Sets long-press time to ~0.5 s (0xEB = 50)
//   • Reads gesture register 0x01 on every poll
// ─────────────────────────────────────────────────────────────────────────────
class Cst816d : public I2cDevice {
public:
    // Hardware gesture IDs from CST816 datasheet (same as bubu_clean)
    static constexpr uint8_t HW_GESTURE_NONE         = 0x00;
    static constexpr uint8_t HW_GESTURE_SWIPE_UP     = 0x01;
    static constexpr uint8_t HW_GESTURE_SWIPE_DOWN   = 0x02;
    static constexpr uint8_t HW_GESTURE_SWIPE_LEFT   = 0x03;
    static constexpr uint8_t HW_GESTURE_SWIPE_RIGHT  = 0x04;
    static constexpr uint8_t HW_GESTURE_SINGLE_CLICK = 0x05;
    static constexpr uint8_t HW_GESTURE_DOUBLE_CLICK = 0x0B;
    static constexpr uint8_t HW_GESTURE_LONG_PRESS   = 0x0C;

    struct TouchPoint_t {
        int     num     = 0;
        int     x       = -1;
        int     y       = -1;
        uint8_t gesture = HW_GESTURE_NONE;
    };

    Cst816d(i2c_master_bus_handle_t i2c_bus, uint8_t addr) : I2cDevice(i2c_bus, addr) {}

    // Called once after hardware reset (mirrors TouchSystem::begin() chip setup)
    void Initialize() {
        // Read and log chip ID — register 0xA7
        uint8_t chip_id = ReadReg(0xA7);
        const char* chip_name = "Unknown";
        if      (chip_id == 0xB4) chip_name = "CST816S";
        else if (chip_id == 0xB5) chip_name = "CST816T";
        else if (chip_id == 0xB6) chip_name = "CST816D";
        ESP_LOGI(TAG, "CST816 chip ID: 0x%02X (%s)", chip_id, chip_name);

        // Enable touch-change interrupt: register 0xFA, EnChange = bit 5
        WriteReg(0xFA, 0x20);
        vTaskDelay(pdMS_TO_TICKS(10));

        // Long-press time: register 0xEB = 50 (~0.5 s, same as bubu_clean)
        WriteReg(0xEB, 50);
        vTaskDelay(pdMS_TO_TICKS(10));

        ESP_LOGI(TAG, "CST816 configured: EnChange INT enabled, long-press = 50 (~0.5 s)");
    }

    // Read 6 bytes from register 0x01: [gestureID, fingerCount, xH, xL, yH, yL]
    // Mirrors bubu_clean readTouchData() register sequence.
    void UpdateTouchPoint() {
        uint8_t buf[6] = {0};
        esp_err_t ret = TryReadRegs(0x01, buf, 6);
        if (ret != ESP_OK) {
            tp_.num = 0;
            return;
        }
        tp_.gesture = buf[0];
        uint8_t finger_count = buf[1] & 0x0F;
        // Fix 3: reject invalid finger count (bubu_clean touch_system.cpp:110-112)
        if (finger_count > 2) {
            tp_.num = 0;
            return;
        }
        int raw_x = ((buf[2] & 0x0F) << 8) | buf[3];
        int raw_y = ((buf[4] & 0x0F) << 8) | buf[5];
        // Fix 2: reject bogus coordinates from I2C noise (bubu_clean touch_system.cpp:153-155)
        if (raw_x > 500 || raw_y > 500) {
            tp_.num = 0;
            return;
        }
        tp_.num = finger_count;
        tp_.x   = raw_x;
        tp_.y   = raw_y;
    }

    const TouchPoint_t& GetTouchPoint() const { return tp_; }

private:
    TouchPoint_t tp_;
};

class CustomLcdDisplay : public SpiLcdDisplay {
public:
    CustomLcdDisplay(esp_lcd_panel_io_handle_t io_handle,
                    esp_lcd_panel_handle_t panel_handle,
                    int width,
                    int height,
                    int offset_x,
                    int offset_y,
                    bool mirror_x,
                    bool mirror_y,
                    bool swap_xy)
        : SpiLcdDisplay(io_handle, panel_handle, width, height, offset_x, offset_y, mirror_x, mirror_y, swap_xy) {}

    virtual void SetupUI() override {
        SpiLcdDisplay::SetupUI();
        DisplayLockGuard lock(this);
        lv_obj_set_style_pad_left(status_bar_, LV_HOR_RES * 0.2, 0);
        lv_obj_set_style_pad_right(status_bar_, LV_HOR_RES * 0.2, 0);
    }
};

class Esp32S3RoundI80Board : public WifiBoard {
private:
    i2c_master_bus_handle_t i2c_bus_ = nullptr;
    esp_io_expander_handle_t io_expander_ = nullptr;
    LcdDisplay* display_ = nullptr;
    EyeDisplay* eye_display_ = nullptr;  // typed pointer for IMU→eye fast path
    Cst816d* touch_ = nullptr;
    Qmi8658* imu_ = nullptr;
    esp_timer_handle_t touch_timer_ = nullptr;
    esp_timer_handle_t wifi_power_timer_ = nullptr;
    AdcBatteryMonitor* battery_monitor_ = nullptr;
    sdmmc_card_t* sd_card_ = nullptr;
    bool sd_spi_initialized_ = false;
    bool sd_card_mounted_ = false;
    bool speaker_test_running_ = false;
    size_t speaker_test_file_index_ = 0;
    std::mutex speaker_test_mutex_;
    std::mutex sim_battery_mutex_;
    // Touch gesture state (bubu_ota-style TAP/LONG_PRESS classifier)
    bool touch_active_ = false;
    int64_t touch_down_ms_ = 0;
    int touch_down_x_ = -1;
    int touch_down_y_ = -1;
    int touch_cur_x_ = -1;
    int touch_cur_y_ = -1;
    bool long_press_fired_ = false;
    int64_t touch_last_read_ms_ = 0;  // Fix 1: release timeout tracking

    int scrub_last_x_ = -1;
    int scrub_last_y_ = -1;

    static constexpr int64_t TOUCH_LONG_PRESS_MS = 400;
    static constexpr int     TOUCH_TAP_MAX_DRIFT  = 35;
    // A release that drifted too far to be a tap becomes a swipe when one axis
    // clearly dominates and it happened quickly. Both thresholds sit above the
    // tap drift limit on purpose, so nothing that used to be a tap becomes a
    // swipe -- this only gives a meaning to releases that were dropped before.
    static constexpr int     TOUCH_SWIPE_MIN_PX   = 28;
    static constexpr int64_t TOUCH_SWIPE_MAX_MS   = 700;
    static constexpr int     TOUCH_SCRUB_MIN_DELTA = 4;   // px before a scrub sample is sent
    static constexpr int64_t TOUCH_TAP_MIN_MS     = 20;
    static constexpr int TOUCH_MAX_X = DISPLAY_WIDTH - 1;
    static constexpr int TOUCH_MAX_Y = DISPLAY_HEIGHT - 1;
    static constexpr size_t SPEAKER_TEST_MAX_FILE_BYTES = 2 * 1024 * 1024;
    static constexpr int SIM_BATTERY_DEFAULT_RUNTIME_MIN = 9 * 60;
    static constexpr int SIM_BATTERY_MIN_RUNTIME_MIN = 30;
    static constexpr int64_t SIM_BATTERY_PERSIST_INTERVAL_US = 60LL * 1000000LL;
    static constexpr int64_t SIM_BATTERY_REPORT_INTERVAL_US = 30LL * 1000000LL;
    // Use main-board USB detect for charging/discharging status reporting.
    static constexpr bool USE_MAINBOARD_USB_DET_CHARGE_STATE = true;
    static constexpr int BATTERY_VALID_MIN_MV_1S = 2800;
    static constexpr int BATTERY_VALID_MAX_MV_1S = 4350;
    // Prefer adc_battery_estimation's own capacity (outlier-rejecting ADC filter +
    // EMA low-pass on the percent + charge/discharge monotonicity, see
    // adc_battery_estimation.c get_capacity()) over EstimateSingleCellPercentFromMv's
    // bare instantaneous OCV lookup below. Flip to false to revert to the old
    // raw-mV-only behavior if this misbehaves on hardware — no other code path
    // changes, it only changes which already-computed value GetBatteryLevel() uses.
    static constexpr bool USE_LIBRARY_BATTERY_CAPACITY_ESTIMATE = true;

    int sim_battery_runtime_min_ = SIM_BATTERY_DEFAULT_RUNTIME_MIN;
    float sim_battery_level_ = 100.0f;
    int sim_battery_last_saved_level_ = 100;
    int battery_last_valid_adc_level_ = -1;
    int64_t sim_battery_last_update_us_ = 0;
    int64_t sim_battery_last_persist_us_ = 0;
    int sim_battery_last_reported_level_ = -1;
    bool sim_battery_last_reported_charging_ = false;
    bool sim_battery_last_reported_discharging_ = false;
    bool sim_battery_last_reported_fallback_ = true;
    esp_err_t sim_battery_last_reported_capacity_err_ = ESP_FAIL;
    esp_err_t sim_battery_last_reported_voltage_err_ = ESP_FAIL;
    int64_t sim_battery_last_report_us_ = 0;

    static bool HasAudioExtension(const std::string& path) {
        auto dot = path.find_last_of('.');
        if (dot == std::string::npos) {
            return false;
        }
        std::string ext = path.substr(dot);
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return ext == ".ogg" || ext == ".oga";
    }

    static std::string BaseName(const std::string& path) {
        auto pos = path.find_last_of('/');
        return pos == std::string::npos ? path : path.substr(pos + 1);
    }

    static int ClampPercent(int value) {
        return std::max(0, std::min(100, value));
    }

    static bool IsValidSingleCellBatteryMv(int battery_mv) {
        return battery_mv >= BATTERY_VALID_MIN_MV_1S && battery_mv <= BATTERY_VALID_MAX_MV_1S;
    }

    static int EstimateSingleCellPercentFromMv(int battery_mv) {
        struct OcvPoint {
            int mv;
            int pct;
        };
        static constexpr OcvPoint kOcvPoints[] = {
            {4160, 100},
            {4070, 90},
            {3990, 80},
            {3900, 70},
            {3820, 60},
            {3720, 50},
            {3610, 40},
            {3530, 30},
            {3380, 20},
            {3200, 10},
            {2850, 0},
        };

        if (battery_mv >= kOcvPoints[0].mv) {
            return 100;
        }
        constexpr size_t kPointsCount = sizeof(kOcvPoints) / sizeof(kOcvPoints[0]);
        if (battery_mv <= kOcvPoints[kPointsCount - 1].mv) {
            return 0;
        }

        for (size_t i = 0; i + 1 < kPointsCount; ++i) {
            const OcvPoint& hi = kOcvPoints[i];
            const OcvPoint& lo = kOcvPoints[i + 1];
            if (battery_mv <= hi.mv && battery_mv >= lo.mv) {
                const int dv = hi.mv - lo.mv;
                if (dv <= 0) {
                    return ClampPercent(lo.pct);
                }
                const int dp = hi.pct - lo.pct;
                const int num = (battery_mv - lo.mv) * dp;
                return ClampPercent(lo.pct + (num + (dv / 2)) / dv);
            }
        }
        return 0;
    }

    void InitializeSimulatedBattery() {
        Settings settings("battery", true);
        sim_battery_runtime_min_ = settings.GetInt("sim_runtime_min", SIM_BATTERY_DEFAULT_RUNTIME_MIN);
        if (sim_battery_runtime_min_ < SIM_BATTERY_MIN_RUNTIME_MIN) {
            sim_battery_runtime_min_ = SIM_BATTERY_MIN_RUNTIME_MIN;
        }

        sim_battery_last_saved_level_ = ClampPercent(settings.GetInt("sim_level", 100));
        sim_battery_level_ = static_cast<float>(sim_battery_last_saved_level_);
        sim_battery_last_update_us_ = esp_timer_get_time();
        sim_battery_last_persist_us_ = sim_battery_last_update_us_;

        ESP_LOGI(TAG, "Sim battery init: level=%d%% runtime=%d min",
                 sim_battery_last_saved_level_, sim_battery_runtime_min_);
    }

    void SaveSimulatedBatteryLevelIfNeeded(int level, int64_t now_us) {
        if (level == sim_battery_last_saved_level_) {
            return;
        }
        if ((now_us - sim_battery_last_persist_us_) < SIM_BATTERY_PERSIST_INTERVAL_US) {
            return;
        }
        Settings settings("battery", true);
        settings.SetInt("sim_level", level);
        sim_battery_last_saved_level_ = level;
        sim_battery_last_persist_us_ = now_us;
    }

    void UpdateSimulatedBatteryLocked() {
        const int64_t now_us = esp_timer_get_time();
        if (sim_battery_last_update_us_ <= 0) {
            sim_battery_last_update_us_ = now_us;
            return;
        }

        const int64_t elapsed_us = now_us - sim_battery_last_update_us_;
        if (elapsed_us <= 0) {
            return;
        }

        const double runtime_us = static_cast<double>(sim_battery_runtime_min_) * 60.0 * 1000000.0;
        const double drain = (100.0 * static_cast<double>(elapsed_us)) / runtime_us;
        sim_battery_level_ = static_cast<float>(std::max(0.0, static_cast<double>(sim_battery_level_) - drain));
        sim_battery_last_update_us_ = now_us;

        const int level_int = ClampPercent(static_cast<int>(sim_battery_level_ + 0.5f));
        SaveSimulatedBatteryLevelIfNeeded(level_int, now_us);
    }

    void ScheduleNotification(std::string message, int duration_ms = 2500) {
        Application::GetInstance().Schedule([this, message = std::move(message), duration_ms]() {
            if (display_ != nullptr) {
                display_->ShowNotification(message, duration_ms);
            }
        });
    }

    bool EnsureSdCardMounted() {
        if (sd_card_mounted_) {
            return true;
        }

        if (!sd_spi_initialized_) {
            spi_bus_config_t bus_config = {
                .mosi_io_num = SD_SPI_MOSI,
                .miso_io_num = SD_SPI_MISO,
                .sclk_io_num = SD_SPI_CLK,
                .quadwp_io_num = GPIO_NUM_NC,
                .quadhd_io_num = GPIO_NUM_NC,
                .max_transfer_sz = 16 * 1024,
            };
            esp_err_t err = spi_bus_initialize(SD_SPI_HOST, &bus_config, SPI_DMA_CH_AUTO);
            if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
                ESP_LOGE(TAG, "Failed to initialize SD SPI bus: %s", esp_err_to_name(err));
                return false;
            }
            sd_spi_initialized_ = true;
        }

        sdmmc_host_t host = SDSPI_HOST_DEFAULT();
        host.slot = SD_SPI_HOST;

        sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
        slot_config.host_id = SD_SPI_HOST;
        slot_config.gpio_cs = SD_SPI_CS;
        slot_config.gpio_cd = SDSPI_SLOT_NO_CD;
        slot_config.gpio_wp = GPIO_NUM_NC;
        slot_config.gpio_int = GPIO_NUM_NC;

        esp_vfs_fat_sdmmc_mount_config_t mount_config = {
            .format_if_mount_failed = false,
            .max_files = 5,
            .allocation_unit_size = 16 * 1024,
            .disk_status_check_enable = false,
            .use_one_fat = false,
        };

        esp_err_t err = esp_vfs_fat_sdspi_mount(SD_MOUNT_POINT, &host, &slot_config, &mount_config, &sd_card_);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to mount SD card at %s: %s", SD_MOUNT_POINT, esp_err_to_name(err));
            return false;
        }

        sd_card_mounted_ = true;
        ESP_LOGI(TAG, "SD card mounted at %s", SD_MOUNT_POINT);
        return true;
    }

    std::vector<std::string> FindSpeakerTestFiles() {
        std::vector<std::string> files;
        const std::vector<std::string> search_dirs = {
            SD_MOUNT_POINT,
            std::string(SD_MOUNT_POINT) + "/speaker_test",
        };

        for (const auto& dir_path : search_dirs) {
            DIR* dir = opendir(dir_path.c_str());
            if (dir == nullptr) {
                continue;
            }

            while (auto* entry = readdir(dir)) {
                if (entry->d_name[0] == '.') {
                    continue;
                }
                std::string full_path = dir_path + "/" + entry->d_name;
                if (HasAudioExtension(full_path)) {
                    files.push_back(full_path);
                }
            }
            closedir(dir);
        }

        std::sort(files.begin(), files.end());
        return files;
    }

    bool LoadFile(const std::string& path, std::string& data_out) {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            ESP_LOGE(TAG, "Failed to open %s", path.c_str());
            return false;
        }

        const std::streamsize size = file.tellg();
        if (size <= 0 || static_cast<size_t>(size) > SPEAKER_TEST_MAX_FILE_BYTES) {
            ESP_LOGE(TAG, "Speaker test file %s has invalid size: %lld", path.c_str(), static_cast<long long>(size));
            return false;
        }

        data_out.resize(static_cast<size_t>(size));
        file.seekg(0, std::ios::beg);
        if (!file.read(data_out.data(), size)) {
            ESP_LOGE(TAG, "Failed to read %s", path.c_str());
            data_out.clear();
            return false;
        }

        return true;
    }

    void FinishSpeakerTest() {
        std::lock_guard<std::mutex> lock(speaker_test_mutex_);
        speaker_test_running_ = false;
    }

    void RunSpeakerTestTask() {
        if (!EnsureSdCardMounted()) {
            ScheduleNotification("SD mount failed", 3000);
            FinishSpeakerTest();
            vTaskDelete(nullptr);
            return;
        }

        auto files = FindSpeakerTestFiles();
        if (files.empty()) {
            ScheduleNotification("Put .ogg files on SD", 3000);
            FinishSpeakerTest();
            vTaskDelete(nullptr);
            return;
        }

        const std::string path = files[speaker_test_file_index_ % files.size()];
        speaker_test_file_index_ = (speaker_test_file_index_ + 1) % files.size();

        std::string audio_data;
        if (!LoadFile(path, audio_data)) {
            ScheduleNotification("Audio file read failed", 3000);
            FinishSpeakerTest();
            vTaskDelete(nullptr);
            return;
        }

        const std::string name = BaseName(path);
        ScheduleNotification("Play " + name, 2500);
        Application::GetInstance().Schedule([audio = std::move(audio_data)]() mutable {
            Application::GetInstance().PlaySound(audio);
        });

        FinishSpeakerTest();
        vTaskDelete(nullptr);
    }

    Cst816d::TouchPoint_t TransformTouchPoint(const Cst816d::TouchPoint_t& raw_tp) const {
        Cst816d::TouchPoint_t display_tp = raw_tp;
        int x = raw_tp.x;
        int y = raw_tp.y;

        if (DISPLAY_SWAP_XY) {
            int tmp = x;
            x = y;
            y = tmp;
        }
        if (DISPLAY_MIRROR_X) {
            x = TOUCH_MAX_X - x;
        }
        if (DISPLAY_MIRROR_Y) {
            y = TOUCH_MAX_Y - y;
        }

        if (x < 0) {
            x = 0;
        } else if (x > TOUCH_MAX_X) {
            x = TOUCH_MAX_X;
        }
        if (y < 0) {
            y = 0;
        } else if (y > TOUCH_MAX_Y) {
            y = TOUCH_MAX_Y;
        }

        display_tp.x = x;
        display_tp.y = y;
        return display_tp;
    }

    bool HandleConversationTrigger() {
        if (eye_display_) {
            eye_display_->NotifyUserInteraction();
            if (eye_display_->IsSleepModeActive()) {
                eye_display_->StopSleepMode();
            }
        }
        auto& app = Application::GetInstance();
        auto state = app.GetDeviceState();
        if (state == kDeviceStateListening || state == kDeviceStateAudioTesting) {
            app.StopListening();
            return true;
        }
        app.StartListening();
        return false;
    }

    // Dispatched on TAP (quick touch & release, drift ≤ 35px, duration ≥ 20ms)
    void DispatchTap(int x, int y) {
        auto display = GetDisplay();
        auto eye_display = dynamic_cast<EyeDisplay*>(display);

        int32_t dismiss_reminder_id = 0;
        if (MessageBoard::HandleTap(static_cast<uint16_t>(x), static_cast<uint16_t>(y),
                                    &dismiss_reminder_id)) {
            if (dismiss_reminder_id > 0) {
                std::string error;
                if (!ReminderSystem::Dismiss(dismiss_reminder_id, &error)) {
                    ESP_LOGW(TAG, "Message board dismiss failed for reminder id=%d: %s",
                             static_cast<int>(dismiss_reminder_id), error.c_str());
                } else {
                    ESP_LOGI(TAG, "Message board dismissed reminder id=%d",
                             static_cast<int>(dismiss_reminder_id));
                }
            }
            ESP_LOGI(TAG, "Tap consumed by message board");
            return;
        }

        using ScreenId = ScreenManager::ScreenId;
        const ScreenId screen = ScreenManager::Current();

        // Scripted care sequences own the canvas. They get the tap before the
        // audio interrupt below, because a chomp or a scrub is interaction with
        // the pet, not a request to stop what it is saying.
        switch (screen) {
        case ScreenId::Feeding:
        case ScreenId::Bathing:
        case ScreenId::Celebration:
            if (MenuSystem::HandleCareAnimationTap()) {
                ESP_LOGI(TAG, "Tap consumed by care animation");
                return;
            }
            break;
        default:
            break;
        }

        Application::GetInstance().InterruptAudioPlaybackForUserInput();

        // Screens whose whole job is to be dismissed by the next touch.
        switch (screen) {
        case ScreenId::Clock:
            if (eye_display) eye_display->DismissClockScreensaver();
            ESP_LOGI(TAG, "Touch dismissed clock screensaver");
            return;
        case ScreenId::Sleep:
            if (eye_display) eye_display->StopSleepMode();
            ESP_LOGI(TAG, "Touch exited sleep mode");
            return;
        case ScreenId::Hatching:
            if (eye_display) eye_display->HandleHatchingTap(x, y);
            ESP_LOGI(TAG, "Touch consumed by hatching animation");
            return;
        default:
            break;
        }

        // The pet's own tap reactions fight a game. MẮT XANH and Snake are
        // here because a tap IS their control -- a tap voice on every eye or
        // every turn would talk over the whole round, and MẮT XANH plays its
        // own hit/miss sounds. (QuickTapGame is deliberately left off this
        // list, unchanged from before.)
        const bool game_active =
            screen == ScreenId::GreenEyeGame || screen == ScreenId::CheckerGame ||
            screen == ScreenId::SnakeGame || screen == ScreenId::TiltMazeGame ||
            screen == ScreenId::TrafficRunnerGame;
        if (!game_active && eye_display) {
            eye_display->PlayTapVoice();
            eye_display->NotifyUserInteraction();
        }

        switch (screen) {
        case ScreenId::Boot:
        case ScreenId::Main:
        case ScreenId::Feeding:
        case ScreenId::Bathing:
        case ScreenId::Celebration: {
            // The eyes own the screen: tap them to talk, tap around them for the menu.
            const bool on_eyes = eye_display && eye_display->IsTouchOnEyes(x, y);
            if (on_eyes) {
                const bool stop_requested = HandleConversationTrigger();
                // Keep Wi-Fi in performance while waiting server-side loading/turn finalization.
                SetPowerSaveLevel(stop_requested ? PowerSaveLevel::PERFORMANCE
                                                 : PowerSaveLevel::BALANCED);
                ResetWifiPowerTimer();
                ESP_LOGI(TAG, "Eye tap -> toggle conversation");
            } else {
                MenuSystem::Open();
                ESP_LOGI(TAG, "Touch outside eyes → open menu");
            }
            return;
        }
        default:
            // Every remaining screen is a menu panel.
            if (MenuSystem::HandleTap(x, y)) {
                ESP_LOGI(TAG, "Tap consumed by menu");
            }
            return;
        }
    }

    // Dispatched on LONG_PRESS (held ≥ 400ms, drift ≤ 35px)
    void DispatchLongPress(int x, int y) {
        Application::GetInstance().InterruptAudioPlaybackForUserInput();

        using ScreenId = ScreenManager::ScreenId;
        const ScreenId screen = ScreenManager::Current();

        switch (screen) {
        case ScreenId::Clock:
            if (eye_display_) eye_display_->DismissClockScreensaver();
            ESP_LOGI(TAG, "Long press dismissed clock screensaver");
            return;
        case ScreenId::Hatching:
            // Hatching only answers taps.
            ESP_LOGI(TAG, "Long press ignored during hatching");
            return;
        case ScreenId::Sleep:
            if (eye_display_) eye_display_->StopSleepMode();
            ESP_LOGI(TAG, "Long press exited sleep mode");
            return;
        default:
            break;
        }

        if (eye_display_) {
            eye_display_->NotifyUserInteraction();
        }

        switch (screen) {
        case ScreenId::Boot:
        case ScreenId::Main:
        case ScreenId::Feeding:
        case ScreenId::Bathing:
        case ScreenId::Celebration:
            // Long press on the eyes: no-op for now.
            return;
        default:
            if (MenuSystem::HandleLongPress(static_cast<uint16_t>(x),
                                            static_cast<uint16_t>(y))) {
                ESP_LOGI(TAG, "Long press consumed by menu");
            }
            return;
        }
    }

    // IO expander button handles & drivers
    button_handle_t btn_power_ = nullptr;
    button_handle_t btn_up_ = nullptr;
    button_handle_t btn_down_ = nullptr;
    button_driver_t* btn_power_driver_ = nullptr;
    button_driver_t* btn_up_driver_ = nullptr;
    button_driver_t* btn_down_driver_ = nullptr;

    static Esp32S3RoundI80Board* instance_;

    void InitializeI2c() {
        i2c_master_bus_config_t i2c_bus_cfg = {
            .i2c_port = I2C_NUM_0,
            .sda_io_num = AUDIO_CODEC_I2C_SDA_PIN,
            .scl_io_num = AUDIO_CODEC_I2C_SCL_PIN,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags = { .enable_internal_pullup = 1 },
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_cfg, &i2c_bus_));
    }

    void InitializeIoExpander() {
        esp_err_t ret = esp_io_expander_new_i2c_tca9554(i2c_bus_, 0x20, &io_expander_);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize TCA9554 IO expander at 0x20");
            return;
        }
        // Set side buttons as input
        esp_io_expander_set_dir(io_expander_, (1 << XIO_KEY_UP) | (1 << XIO_KEY_DOWN) | (1 << XIO_KEY_POWER), IO_EXPANDER_INPUT);
    }

    void InitializeDisplay() {
        esp_lcd_i80_bus_handle_t i80_bus = NULL;
        esp_lcd_i80_bus_config_t bus_config = {
            .dc_gpio_num = LCD_PIN_DC,
            .wr_gpio_num = LCD_PIN_WR,
            .clk_src = LCD_CLK_SRC_DEFAULT,
            .data_gpio_nums = {
                LCD_PIN_D0, LCD_PIN_D1, LCD_PIN_D2, LCD_PIN_D3,
                LCD_PIN_D4, LCD_PIN_D5, LCD_PIN_D6, LCD_PIN_D7,
            },
            .bus_width = 8,
            .max_transfer_bytes = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t),
            .psram_trans_align = 64,
            .sram_trans_align = 4,
        };
        ESP_ERROR_CHECK(esp_lcd_new_i80_bus(&bus_config, &i80_bus));

        esp_lcd_panel_io_handle_t io_handle = NULL;
        esp_lcd_panel_io_i80_config_t io_config = {
            .cs_gpio_num = LCD_PIN_CS,
            .pclk_hz = 20 * 1000 * 1000,
            .trans_queue_depth = 10,
            .lcd_cmd_bits = 8,
            .lcd_param_bits = 8,
            .dc_levels = {
                .dc_idle_level = 0,
                .dc_cmd_level = 0,
                .dc_dummy_level = 0,
                .dc_data_level = 1,
            },
        };
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_i80(i80_bus, &io_config, &io_handle));

        esp_lcd_panel_handle_t panel_handle = NULL;
        esp_lcd_panel_dev_config_t panel_config = {
            .reset_gpio_num = LCD_PIN_RST,
            .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
            .bits_per_pixel = 16,
        };
        ESP_ERROR_CHECK(esp_lcd_new_panel_gc9a01(io_handle, &panel_config, &panel_handle));
        
        ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
        ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
        ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_handle, true));
        ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel_handle, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y));
        ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));
        
        eye_display_ = new EyeDisplay(io_handle, panel_handle,
                                    DISPLAY_WIDTH, DISPLAY_HEIGHT,
                                    DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y,
                                    DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY);
        display_ = eye_display_;
    }

    // ── InitializeTouch ──────────────────────────────────────────────────────
    // Mirrors bubu_clean TouchSystem::begin():
    //   1. Hardware reset (LOW 10 ms → HIGH 50 ms)
    //   2. Cst816d::Initialize()  — chip ID, EnChange INT, long-press time
    //   3. GPIO 45 (TCA INT output) → falling-edge ISR sets s_touch_int_flag
    //   4. 20 ms periodic timer polls TCA bit 0 AND runs the TAP/LONG_PRESS
    //      state machine (identical thresholds: 400 ms, 35 px drift, 20 ms min)
    // ─────────────────────────────────────────────────────────────────────────
    void InitializeTouch() {
        // ── 1. Hardware reset ────────────────────────────────────────────────
        gpio_config_t rst_cfg = {
            .pin_bit_mask  = (1ULL << TOUCH_RST_PIN),
            .mode          = GPIO_MODE_OUTPUT,
            .pull_up_en    = GPIO_PULLUP_DISABLE,
            .pull_down_en  = GPIO_PULLDOWN_DISABLE,
            .intr_type     = GPIO_INTR_DISABLE,
        };
        gpio_config(&rst_cfg);
        gpio_set_level((gpio_num_t)TOUCH_RST_PIN, 0);
        vTaskDelay(pdMS_TO_TICKS(10));
        gpio_set_level((gpio_num_t)TOUCH_RST_PIN, 1);
        vTaskDelay(pdMS_TO_TICKS(50));

        // ── 2. Create driver and configure chip registers ────────────────────
        touch_ = new Cst816d(i2c_bus_, 0x15);
        touch_->Initialize();   // reads chip ID, sets 0xFA (EnChange) + 0xEB (LPTime)

        // ── 3. GPIO 45 — TCA INT output → falling-edge ISR ──────────────────
        // Mirrors: attachInterrupt(digitalPinToInterrupt(PIN_TCA_INT), touchISR, FALLING)
        gpio_config_t int_cfg = {
            .pin_bit_mask  = (1ULL << TOUCH_INT_PIN),
            .mode          = GPIO_MODE_INPUT,
            .pull_up_en    = GPIO_PULLUP_ENABLE,
            .pull_down_en  = GPIO_PULLDOWN_DISABLE,
            .intr_type     = GPIO_INTR_NEGEDGE,
        };
        gpio_config(&int_cfg);
        // gpio_install_isr_service may already be installed by iot_button — ignore ESP_ERR_INVALID_STATE
        esp_err_t isr_ret = gpio_install_isr_service(0);
        if (isr_ret != ESP_OK && isr_ret != ESP_ERR_INVALID_STATE) {
            ESP_LOGW(TAG, "gpio_install_isr_service: %s", esp_err_to_name(isr_ret));
        }
        gpio_isr_handler_add((gpio_num_t)TOUCH_INT_PIN, touch_gpio_isr, nullptr);
        ESP_LOGI(TAG, "Touch INT GPIO %d configured (falling-edge ISR)", TOUCH_INT_PIN);

        // ── 4. 20 ms periodic timer — TCA poll + TAP/LONG_PRESS classifier ──
        esp_timer_create_args_t timer_args = {
            .callback = [](void* arg) {
                auto self = (Esp32S3RoundI80Board*)arg;

                // IMU accel read — its one live consumer is the shake gesture
                // that fires AnimConfused, so it is only worth polling on
                // screens that claim it. The timer itself must keep running:
                // it also drives the TAP/LONG_PRESS classifier below.
                //
                // The read stays on this timer task; the write into
                // EyeAnimation's shared state is handed to the main task
                // (same as every other input path here) so it can't race
                // with the LVGL animation timer that reads it back.
                if (self->imu_ && ScreenManager::Policy().imu) {
                    float ax, ay, az;
                    self->imu_->ReadAccel(ax, ay, az);
                    auto eye_display = self->eye_display_;
                    const auto screen = ScreenManager::Current();
                    Application::GetInstance().Schedule([eye_display, screen, ax, ay, az]() {
                        if (screen == ScreenManager::ScreenId::TiltMazeGame ||
                            screen == ScreenManager::ScreenId::TrafficRunnerGame) {
                            MenuSystem::HandleImuAccel(ax, ay, az);
                        } else if (eye_display != nullptr) {
                            eye_display->SetImuAccel(ax, ay);
                        }
                    });
                }

                // ── TCA bit 0 poll (mirrors bubu_clean update() TCA read) ───
                // XIO_TOUCH_INT (bit 0) is active-low when touch is present.
                // This is the secondary interrupt source — catches any edge
                // the GPIO ISR may have missed (same pattern as bubu_clean).
                if (self->io_expander_) {
                    uint32_t pin_val = 0;
                    esp_io_expander_get_level(self->io_expander_,
                                             1 << XIO_TOUCH_INT, &pin_val);
                    if ((pin_val & (1 << XIO_TOUCH_INT)) == 0) {
                        // TCA reports touch INT active → flag it
                        s_touch_int_flag = true;
                    }
                }

                // ── Read touch data ──────────────────────────────────────────
                // Always read when touch is active (for move/release tracking).
                // On new touch, only read when interrupt flag is set (ISR or TCA).
                const bool should_read = self->touch_active_ || s_touch_int_flag;
                if (should_read) {
                    s_touch_int_flag = false;
                    self->touch_->UpdateTouchPoint();
                }

                const auto& tp = self->touch_->GetTouchPoint();
                int64_t now_ms = esp_timer_get_time() / 1000;

                // Fix 1: track last successful read for release timeout
                if (tp.num > 0) {
                    self->touch_last_read_ms_ = now_ms;
                }
                // Fix 1: force release if touch is active but reads have failed for >120ms
                // (mirrors bubu_clean touch_system.cpp:392-397)
                if (self->touch_active_ && (now_ms - self->touch_last_read_ms_) > 120) {
                    ESP_LOGW(TAG, "[Touch] read timeout → force RELEASE");
                    self->touch_active_     = false;
                    self->long_press_fired_ = false;
                }

                if (tp.num > 0) {
                    auto display_tp = self->TransformTouchPoint(tp);
                    self->touch_cur_x_ = display_tp.x;
                    self->touch_cur_y_ = display_tp.y;

                    if (!self->touch_active_) {
                        // ── Touch DOWN ────────────────────────────────────────
                        self->touch_active_     = true;
                        self->touch_down_ms_    = now_ms;
                        self->touch_down_x_     = display_tp.x;
                        self->touch_down_y_     = display_tp.y;
                        self->long_press_fired_ = false;
                        self->scrub_last_x_     = display_tp.x;
                        self->scrub_last_y_     = display_tp.y;
                        ESP_LOGD(TAG, "[Touch] DOWN at (%d,%d) gesture=0x%02X",
                                 display_tp.x, display_tp.y, tp.gesture);
                    } else {
                        // ── Scrub (drag) ──────────────────────────────────────
                        // Throttled by distance: a per-poll Schedule() would
                        // allocate a lambda for every touch sample.
                        int moved = abs(self->touch_cur_x_ - self->scrub_last_x_)
                                  + abs(self->touch_cur_y_ - self->scrub_last_y_);
                        // Only bathing consumes a scrub, and this runs on the
                        // touch timer -- an atomic screen read beats the
                        // dynamic_cast the old care-animation check did here.
                        if (moved >= TOUCH_SCRUB_MIN_DELTA &&
                            ScreenManager::Current() == ScreenManager::ScreenId::Bathing) {
                            self->scrub_last_x_ = self->touch_cur_x_;
                            self->scrub_last_y_ = self->touch_cur_y_;
                            const int scrub_x = self->touch_cur_x_;
                            const int scrub_y = self->touch_cur_y_;
                            Application::GetInstance().Schedule([scrub_x, scrub_y]() {
                                MenuSystem::HandleCareAnimationScrub(scrub_x, scrub_y);
                            });
                        }
                    }
                    if (!self->long_press_fired_) {
                        // ── Check LONG_PRESS while held ───────────────────────
                        // Mirrors bubu_clean checkLongPress():
                        //   held ≥ 400 ms && drift ≤ 35 px → fire once
                        int64_t held  = now_ms - self->touch_down_ms_;
                        int     drift = abs(self->touch_cur_x_ - self->touch_down_x_)
                                      + abs(self->touch_cur_y_ - self->touch_down_y_);
                        if (held >= TOUCH_LONG_PRESS_MS && drift <= TOUCH_TAP_MAX_DRIFT) {
                            self->long_press_fired_ = true;
                            ESP_LOGI(TAG, "[Touch] LONG_PRESS at (%d,%d) held=%ldms drift=%d",
                                     self->touch_down_x_, self->touch_down_y_, static_cast<long>(held), drift);
                            const int long_press_x = self->touch_down_x_;
                            const int long_press_y = self->touch_down_y_;
                            Application::GetInstance().Schedule([self, long_press_x, long_press_y]() {
                                self->DispatchLongPress(long_press_x, long_press_y);
                            });
                        }
                    }
                } else {
                    if (self->touch_active_) {
                        // ── Touch UP ──────────────────────────────────────────
                        // Mirrors bubu_clean handleTouchRelease():
                        //   skip if long press already fired
                        //   skip if duration < 20 ms (debounce)
                        //   classify as TAP if drift ≤ 35 px
                        self->touch_active_ = false;
                        if (!self->long_press_fired_) {
                            int64_t dur   = now_ms - self->touch_down_ms_;
                            int     drift = abs(self->touch_cur_x_ - self->touch_down_x_)
                                          + abs(self->touch_cur_y_ - self->touch_down_y_);
                            ESP_LOGD(TAG, "[Touch] RELEASE dur=%ldms drift=%d", static_cast<long>(dur), drift);
                            if (dur >= TOUCH_TAP_MIN_MS && drift <= TOUCH_TAP_MAX_DRIFT) {
                                ESP_LOGI(TAG, "[Touch] TAP at (%d,%d) dur=%ldms drift=%d",
                                         self->touch_down_x_, self->touch_down_y_, static_cast<long>(dur), drift);
                                const int tap_x = self->touch_down_x_;
                                const int tap_y = self->touch_down_y_;
                                Application::GetInstance().Schedule([self, tap_x, tap_y]() {
                                    self->DispatchTap(tap_x, tap_y);
                                });
                            } else {
                                // Not a tap. The only other thing that reads a
                                // drag is the Bathing scrub, and that fires
                                // during the drag rather than on release, so
                                // classifying the release as a swipe adds a
                                // signal without altering an existing one.
                                // MenuSystem returns false on every screen that
                                // does not want it.
                                const int sdx = self->touch_cur_x_ - self->touch_down_x_;
                                const int sdy = self->touch_cur_y_ - self->touch_down_y_;
                                const int adx = abs(sdx);
                                const int ady = abs(sdy);
                                const int dominant = adx > ady ? adx : ady;
                                if (dur >= TOUCH_TAP_MIN_MS && dur <= TOUCH_SWIPE_MAX_MS &&
                                    dominant >= TOUCH_SWIPE_MIN_PX) {
                                    MenuSystem::SwipeDirection dir;
                                    if (adx > ady) {
                                        dir = sdx < 0 ? MenuSystem::SwipeDirection::kLeft
                                                      : MenuSystem::SwipeDirection::kRight;
                                    } else {
                                        dir = sdy < 0 ? MenuSystem::SwipeDirection::kUp
                                                      : MenuSystem::SwipeDirection::kDown;
                                    }
                                    ESP_LOGI(TAG, "[Touch] SWIPE dx=%d dy=%d dur=%ldms",
                                             sdx, sdy, static_cast<long>(dur));
                                    Application::GetInstance().Schedule([dir]() {
                                        // An open step card owns left/right:
                                        // the child is paging through the steps,
                                        // not changing the screen behind it.
                                        const bool horizontal =
                                            dir == MenuSystem::SwipeDirection::kLeft ||
                                            dir == MenuSystem::SwipeDirection::kRight;
                                        if (horizontal &&
                                            MessageBoard::HandleSwipe(
                                                dir == MenuSystem::SwipeDirection::kLeft)) {
                                            return;
                                        }
                                        MenuSystem::HandleSwipe(dir);
                                    });
                                } else {
                                    ESP_LOGD(TAG, "[Touch] IGNORED dur=%ldms drift=%d",
                                             static_cast<long>(dur), drift);
                                }
                            }
                        }
                        self->long_press_fired_ = false;
                    }
                }
            },
            .arg              = this,
            .dispatch_method  = ESP_TIMER_TASK,
            .name             = "touch_timer",
            .skip_unhandled_events = true,
        };
        esp_timer_create(&timer_args, &touch_timer_);
        esp_timer_start_periodic(touch_timer_, 20000);  // 20 ms, same as bubu_clean
        ESP_LOGI(TAG, "Touch initialized: CST816D + TCA INT + 20 ms poll");
    }

    uint8_t IoExpanderGetKeyLevel(uint32_t pin_bit) {
        if (!io_expander_) return 0;
        uint32_t pin_val = 0;
        uint32_t pin_mask = (1 << XIO_KEY_UP) | (1 << XIO_KEY_POWER) | (1 << XIO_KEY_DOWN);
        esp_io_expander_get_level(io_expander_, pin_mask, &pin_val);
        return (uint8_t)((pin_val & (1 << pin_bit)) ? 1 : 0);
    }

    void InitializeButtons() {
        instance_ = this;

        // --- POWER button (IO expander pin XIO_KEY_POWER) ---
        button_config_t pwr_cfg = {
            .long_press_time = 2000,
            .short_press_time = 0,
        };
        btn_power_driver_ = (button_driver_t*)calloc(1, sizeof(button_driver_t));
        btn_power_driver_->enable_power_save = false;
        btn_power_driver_->get_key_level = [](button_driver_t* drv) -> uint8_t {
            return !instance_->IoExpanderGetKeyLevel(XIO_KEY_POWER);
        };
        ESP_ERROR_CHECK(iot_button_create(&pwr_cfg, btn_power_driver_, &btn_power_));

        // Single click: WiFi config during startup, menu activate if open, otherwise toggle chat
        iot_button_register_cb(btn_power_, BUTTON_SINGLE_CLICK, nullptr,
            [](void* handle, void* usr) {
                auto self = static_cast<Esp32S3RoundI80Board*>(usr);
                Application::GetInstance().Schedule([self]() {
                    if (self->eye_display_) {
                        self->eye_display_->NotifyUserInteraction();
                    }
                    auto& app = Application::GetInstance();
                    if (app.GetDeviceState() == kDeviceStateStarting) {
                        self->EnterWifiConfigMode();
                        return;
                    }
                    if (MenuSystem::IsAnyOpen()) {
                        MenuSystem::HandleActivate();
                        return;
                    }
                    self->HandleConversationTrigger();
                });
            }, this);

        // Long press: enter WiFi config from any state
        iot_button_register_cb(btn_power_, BUTTON_LONG_PRESS_START, nullptr,
            [](void* handle, void* usr) {
                auto self = static_cast<Esp32S3RoundI80Board*>(usr);
                Application::GetInstance().Schedule([self]() {
                    if (self->eye_display_) {
                        self->eye_display_->NotifyUserInteraction();
                    }
                    // Screens that treat a long press as "back" handle it
                    // themselves; anything else falls through to Wi-Fi config,
                    // which must stay reachable from any state.
                    if (MenuSystem::IsAnyOpen() &&
                        MenuSystem::HandleLongPress(0, 0, /*close_by_default=*/false)) {
                        ESP_LOGI(TAG, "Power long press consumed by menu");
                        return;
                    }
                    ESP_LOGI(TAG, "Power long press -> EnterWifiConfigMode");
                    self->EnterWifiConfigMode();
                });
            }, this);

        // --- UP button (IO expander pin XIO_KEY_UP) ---
        button_config_t up_cfg = {
            .long_press_time = 2000,
            .short_press_time = 0,
        };
        btn_up_driver_ = (button_driver_t*)calloc(1, sizeof(button_driver_t));
        btn_up_driver_->enable_power_save = false;
        btn_up_driver_->get_key_level = [](button_driver_t* drv) -> uint8_t {
            return !instance_->IoExpanderGetKeyLevel(XIO_KEY_UP);
        };
        ESP_ERROR_CHECK(iot_button_create(&up_cfg, btn_up_driver_, &btn_up_));

        // Click: menu navigate up if open, otherwise volume up +10
        iot_button_register_cb(btn_up_, BUTTON_SINGLE_CLICK, nullptr,
            [](void* handle, void* usr) {
                auto self = static_cast<Esp32S3RoundI80Board*>(usr);
                Application::GetInstance().Schedule([self]() {
                    if (self->eye_display_) {
                        self->eye_display_->NotifyUserInteraction();
                    }
                    ESP_LOGI(TAG, "UP button pressed, menu open: %d", MenuSystem::IsAnyOpen());
                    if (MenuSystem::IsAnyOpen()) {
                        MenuSystem::HandleNavigate(/*forward=*/false);
                        return;
                    }
                    auto codec = self->GetAudioCodec();
                    if (codec == nullptr) {
                        return;
                    }
                    auto volume = codec->output_volume() + 10;
                    if (volume > 100) volume = 100;
                    codec->SetOutputVolume(volume);
                    if (auto* display = self->GetDisplay(); display != nullptr) {
                        display->ShowNotification(Lang::Strings::VOLUME + std::to_string(volume));
                    }
                });
            }, this);

        // Long press: reboot system
        iot_button_register_cb(btn_up_, BUTTON_LONG_PRESS_START, nullptr,
            [](void* handle, void* usr) {
                auto self = static_cast<Esp32S3RoundI80Board*>(usr);
                if (self->eye_display_) {
                    self->eye_display_->NotifyUserInteraction();
                }
                ESP_LOGI(TAG, "UP long press -> reboot");
                Application::GetInstance().Schedule([self]() {
                    if (auto* display = self->GetDisplay(); display != nullptr) {
                        display->ShowNotification("Rebooting...");
                    }
                    Application::GetInstance().Reboot();
                });
            }, this);

        // --- DOWN button (IO expander pin XIO_KEY_DOWN) ---
        button_config_t down_cfg = {
            .long_press_time = 1000,
            .short_press_time = 0,
        };
        btn_down_driver_ = (button_driver_t*)calloc(1, sizeof(button_driver_t));
        btn_down_driver_->enable_power_save = false;
        btn_down_driver_->get_key_level = [](button_driver_t* drv) -> uint8_t {
            return !instance_->IoExpanderGetKeyLevel(XIO_KEY_DOWN);
        };
        ESP_ERROR_CHECK(iot_button_create(&down_cfg, btn_down_driver_, &btn_down_));

        // Click: menu navigate down if open, otherwise volume down -10
        iot_button_register_cb(btn_down_, BUTTON_SINGLE_CLICK, nullptr,
            [](void* handle, void* usr) {
                auto self = static_cast<Esp32S3RoundI80Board*>(usr);
                Application::GetInstance().Schedule([self]() {
                    if (self->eye_display_) {
                        self->eye_display_->NotifyUserInteraction();
                    }
                    ESP_LOGI(TAG, "DOWN button pressed, menu open: %d", MenuSystem::IsAnyOpen());
                    if (MenuSystem::IsAnyOpen()) {
                        MenuSystem::HandleNavigate(/*forward=*/true);
                        return;
                    }
                    auto codec = self->GetAudioCodec();
                    if (codec == nullptr) {
                        return;
                    }
                    auto volume = codec->output_volume() - 10;
                    if (volume < 0) volume = 0;
                    codec->SetOutputVolume(volume);
                    if (auto* display = self->GetDisplay(); display != nullptr) {
                        display->ShowNotification(Lang::Strings::VOLUME + std::to_string(volume));
                    }
                });
            }, this);

        ESP_LOGI(TAG, "Buttons initialized: POWER(WiFi/Chat), UP(Vol+), DOWN(Vol-)");
    }

    void InitializeWifiPowerTimer() {
        esp_timer_create_args_t args = {
            .callback = [](void* arg) {
                auto self = static_cast<Esp32S3RoundI80Board*>(arg);
                ESP_LOGI(TAG, "WiFi idle timeout → LOW_POWER");
                self->SetPowerSaveLevel(PowerSaveLevel::LOW_POWER);
            },
            .arg = this,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "wifi_power_timer",
            .skip_unhandled_events = true,
        };
        esp_timer_create(&args, &wifi_power_timer_);
        esp_timer_start_once(wifi_power_timer_, 60ULL * 1000000ULL);
    }

    void ResetWifiPowerTimer() {
        esp_timer_stop(wifi_power_timer_);
        esp_timer_start_once(wifi_power_timer_, 60ULL * 1000000ULL);
    }

    void InitializeImu() {
        imu_ = new Qmi8658(i2c_bus_);
        if (!imu_->Init()) {
            delete imu_;
            imu_ = nullptr;
            ESP_LOGW(TAG, "QMI8658 IMU not available — eye IMU movement disabled");
        } else {
            ESP_LOGI(TAG, "QMI8658 IMU ready — eye tilt tracking enabled");
        }
    }

    void InitializeBatteryMonitor() {
#if CONFIG_SOC_ADC_SUPPORTED
        // Board manual: battery divider is 2x100k to ADC IO1 (ADC1 channel 0).
        battery_monitor_ = new AdcBatteryMonitor(ADC_UNIT_1, ADC_CHANNEL_0, 100000.0f, 100000.0f, GPIO_NUM_NC);
        ESP_LOGI(TAG, "Battery ADC monitor ready on GPIO1 (ADC1_CH0)");
#else
        ESP_LOGW(TAG, "ADC not supported on this target; battery level falls back to simulated value");
#endif
    }

public:
    Esp32S3RoundI80Board() {
        ESP_LOGI(TAG, "Initializing Esp32S3RoundI80Board (V2 - Fixed I2C & IO Expander)");
        InitializeI2c();
        InitializeIoExpander();
        InitializeButtons();
        InitializeDisplay();
        InitializeImu();
        InitializeTouch();
        InitializeBatteryMonitor();
        InitializeSimulatedBattery();
        InitializeWifiPowerTimer();
        // WiFi power save: PERFORMANCE when active, BALANCED on tap, LOW_POWER after 1 min idle.
        //
        // Upgrading and Activating must hold the timer off. Both are long network
        // operations with no audio, so the idle timer would otherwise expire
        // mid-transfer and drop the radio to LOW_POWER underneath them. That was
        // observed to cut an assets download from ~74 KB/s to ~13 KB/s and then
        // stall it outright: the download fails, "applied_url" is never written
        // (see Application::CheckAssetsVersion), and the device retries the same
        // 4.5 MB download on every boot forever without ever reaching Idle.
        // The same stall produced SSL read resets during activation.
        Application::GetInstance().AddStateChangeListener([this](DeviceState, DeviceState to) {
            if (to == kDeviceStateListening || to == kDeviceStateConnecting ||
                to == kDeviceStateUpgrading || to == kDeviceStateActivating) {
                esp_timer_stop(wifi_power_timer_);
                SetPowerSaveLevel(PowerSaveLevel::PERFORMANCE);
            } else if (to == kDeviceStateIdle) {
                ResetWifiPowerTimer();
            }
        });
        // Seed the default startup volume without overwriting a saved NVS value.
        GetAudioCodec()->PreviewOutputVolume(12);
        GetAudioCodec()->SetInputGain(3.0f);
        GetBacklight()->RestoreBrightness();
}

    virtual AudioCodec* GetAudioCodec() override {
        static NoAudioCodecDuplex audio_codec(
            AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_GPIO_BCLK, AUDIO_I2S_GPIO_WS, AUDIO_I2S_GPIO_DOUT, AUDIO_I2S_GPIO_DIN);
        return &audio_codec;
    }

    virtual bool StartSpeakerTest() override {
        auto state = Application::GetInstance().GetDeviceState();
        if (state != kDeviceStateIdle && state != kDeviceStateAudioTesting) {
            ScheduleNotification("Speaker busy", 2000);
            return false;
        }

        {
            std::lock_guard<std::mutex> lock(speaker_test_mutex_);
            if (speaker_test_running_) {
                ScheduleNotification("Speaker test busy", 2000);
                return false;
            }
            speaker_test_running_ = true;
        }

        SetPowerSaveLevel(PowerSaveLevel::PERFORMANCE);
        ResetWifiPowerTimer();

        BaseType_t ok = xTaskCreate(
            [](void* arg) {
                static_cast<Esp32S3RoundI80Board*>(arg)->RunSpeakerTestTask();
            },
            "speaker_test",
            6144,
            this,
            4,
            nullptr);
        if (ok != pdPASS) {
            FinishSpeakerTest();
            ScheduleNotification("Speaker task failed", 2500);
            return false;
        }

        return true;
    }

    virtual Display* GetDisplay() override {
        return display_;
    }

    virtual bool GetBatteryLevel(int &level, bool& charging, bool& discharging) override {
        std::lock_guard<std::mutex> lock(sim_battery_mutex_);
        bool level_from_adc = false;
        bool adc_level_fallback = true;
        int battery_mv = -1;
        esp_err_t adc_capacity_err = ESP_FAIL;
        esp_err_t adc_voltage_err = ESP_FAIL;
        bool adc_voltage_valid = false;
        if (battery_monitor_ != nullptr) {
            AdcBatteryMonitor::Diagnostics diagnostics;
            if (battery_monitor_->GetDiagnostics(diagnostics)) {
                adc_voltage_valid = diagnostics.voltage_err == ESP_OK &&
                                    IsValidSingleCellBatteryMv(diagnostics.battery_voltage_mv);
                if (USE_LIBRARY_BATTERY_CAPACITY_ESTIMATE &&
                    diagnostics.capacity_err == ESP_OK && !diagnostics.level_is_fallback) {
                    level = diagnostics.level_percent;
                    level_from_adc = true;
                    adc_level_fallback = false;
                    battery_last_valid_adc_level_ = level;
                } else if (adc_voltage_valid) {
                    level = EstimateSingleCellPercentFromMv(diagnostics.battery_voltage_mv);
                    level_from_adc = true;
                    adc_level_fallback = false;
                    battery_last_valid_adc_level_ = level;
                } else {
                    adc_level_fallback = diagnostics.level_is_fallback;
                }
                battery_mv = diagnostics.battery_voltage_mv;
                adc_capacity_err = diagnostics.capacity_err;
                adc_voltage_err = diagnostics.voltage_err;
            }
        }
        if (!level_from_adc) {
            if (battery_last_valid_adc_level_ >= 0) {
                level = battery_last_valid_adc_level_;
            } else if (adc_voltage_err == ESP_OK && battery_mv > 0) {
                // ADC is alive but voltage is out of valid 1S range.
                // Keep UI stable and avoid stale simulated NVS value jumps.
                level = sim_battery_last_reported_level_ >= 0 ? sim_battery_last_reported_level_ : 100;
            } else {
                UpdateSimulatedBatteryLocked();
                level = ClampPercent(static_cast<int>(sim_battery_level_ + 0.5f));
            }
        }
        charging = false;
        bool charging_known = false;
        if (USE_MAINBOARD_USB_DET_CHARGE_STATE && io_expander_ != nullptr) {
            uint32_t pin_val = 0;
            esp_err_t err = esp_io_expander_get_level(io_expander_, 1 << XIO_USB_DET, &pin_val);
            if (err == ESP_OK) {
                // TCA6408 input lines are active-low on this board.
                charging = (pin_val & (1 << XIO_USB_DET)) == 0;
                charging_known = true;
            } else {
                ESP_LOGW(TAG, "USB detect read failed: %s", esp_err_to_name(err));
            }
        }
        discharging = charging_known ? !charging : false;
        const int64_t now_us = esp_timer_get_time();
        if ((sim_battery_last_reported_level_ < 0 ||
             charging != sim_battery_last_reported_charging_ ||
             discharging != sim_battery_last_reported_discharging_ ||
             adc_level_fallback != sim_battery_last_reported_fallback_ ||
             adc_capacity_err != sim_battery_last_reported_capacity_err_ ||
             adc_voltage_err != sim_battery_last_reported_voltage_err_ ||
             level != sim_battery_last_reported_level_ ||
             (now_us - sim_battery_last_report_us_) >= SIM_BATTERY_REPORT_INTERVAL_US)) {
            sim_battery_last_reported_level_ = level;
            sim_battery_last_reported_charging_ = charging;
            sim_battery_last_reported_discharging_ = discharging;
            sim_battery_last_reported_fallback_ = adc_level_fallback;
            sim_battery_last_reported_capacity_err_ = adc_capacity_err;
            sim_battery_last_reported_voltage_err_ = adc_voltage_err;
            sim_battery_last_report_us_ = now_us;
        }
        return true;
    }

    virtual Backlight* GetBacklight() override {
        static PwmBacklight backlight(DISPLAY_BACKLIGHT_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
        return &backlight;
    }
};

Esp32S3RoundI80Board* Esp32S3RoundI80Board::instance_ = nullptr;

DECLARE_BOARD(Esp32S3RoundI80Board);
