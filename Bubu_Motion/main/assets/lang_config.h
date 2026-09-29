// Auto-generated language config
// Language: vi-VN with en-US fallback
#pragma once

#include <string_view>

#ifndef vi_vn
    #define vi_vn  // 預設語言
#endif

namespace Lang {
    // 语言元数据
    constexpr const char* CODE = "vi-VN";

    // 字符串资源 (en-US as fallback for missing keys)
    namespace Strings {
        constexpr const char* ACCESS_VIA_BROWSER = " Config URL: ";
        constexpr const char* ACTIVATION = "Kích hoạt";
        constexpr const char* BATTERY_CHARGING = "Charging";
        constexpr const char* BATTERY_FULL = "Battery full";
        constexpr const char* BATTERY_LOW = "Low battery";
        constexpr const char* BATTERY_NEED_CHARGE = "Low battery, please charge";
        constexpr const char* CHECKING_NEW_VERSION = "Tìm bản mới";
        constexpr const char* CHECK_NEW_VERSION_FAILED = "Check for new version failed, will retry in %d seconds: %s";
        constexpr const char* CONNECTED_TO = "Connected to ";
        constexpr const char* CONNECTING = "Đang kết nối";
        constexpr const char* CONNECTION_SUCCESSFUL = "Connection Successful";
        constexpr const char* CONNECT_TO = "Connect to ";
        constexpr const char* CONNECT_TO_HOTSPOT = "Hotspot: ";
        constexpr const char* DETECTING_MODULE = "Dò thiết bị";
        constexpr const char* DOWNLOAD_ASSETS_FAILED = "Failed to download assets";
        constexpr const char* ENTERING_WIFI_CONFIG_MODE = "Entering Wi-Fi configuration mode...";
        constexpr const char* ERROR = "Lỗi";
        constexpr const char* FLIGHT_MODE_OFF = "Flight mode is off";
        constexpr const char* FLIGHT_MODE_ON = "Flight mode is on";
        constexpr const char* FOUND_NEW_ASSETS = "Found new assets: %s";
        constexpr const char* HELLO_MY_FRIEND = "Hello, my friend!";
        constexpr const char* INFO = "Information";
        constexpr const char* INITIALIZING = "Khởi động";
        constexpr const char* LISTENING = "Đang nghe";
        constexpr const char* LOADING_ASSETS = "Loading assets...";
        constexpr const char* LOADING_PROTOCOL = "Đăng nhập";
        constexpr const char* MAX_VOLUME = "Max volume";
        constexpr const char* MODEM_INIT_ERROR = "Modem initialization failed";
        constexpr const char* MUTED = "Tắt tiếng";
        constexpr const char* NEW_VERSION = "New version ";
        constexpr const char* OTA_UPGRADE = "OTA Upgrade";
        constexpr const char* PIN_ERROR = "Please insert SIM card";
        constexpr const char* PLEASE_WAIT = "Vui lòng đợi";
        constexpr const char* REGISTERING_NETWORK = "Chờ mạng";
        constexpr const char* REG_ERROR = "Unable to access network, please check SIM card status";
        constexpr const char* RTC_MODE_OFF = "AEC Off";
        constexpr const char* RTC_MODE_ON = "AEC On";
        constexpr const char* SCANNING_WIFI = "Scanning Wi-Fi...";
        constexpr const char* SERVER_ERROR = "Sending failed, please check the network";
        constexpr const char* SERVER_NOT_CONNECTED = "Unable to connect to service, please try again later";
        constexpr const char* SERVER_NOT_FOUND = "Looking for available service";
        constexpr const char* SERVER_TIMEOUT = "Waiting for response timeout";
        constexpr const char* SPEAKING = "Đang nói";
        constexpr const char* STANDBY = "Sẵn sàng";
        constexpr const char* SWITCH_TO_4G_NETWORK = "Switching to 4G...";
        constexpr const char* SWITCH_TO_WIFI_NETWORK = "Switching to Wi-Fi...";
        constexpr const char* UPGRADE_FAILED = "Upgrade failed";
        constexpr const char* UPGRADING = "Nâng cấp";
        constexpr const char* VERSION = "Ver ";
        constexpr const char* VOLUME = "Volume ";
        constexpr const char* WARNING = "Warning";
        constexpr const char* WIFI_CONFIG_MODE = "Wi-Fi Configuration Mode";
    }

    // 音效资源 (en-US as fallback for missing audio files)
    namespace Sounds {

        extern const char ogg_activation_start[] asm("_binary_activation_ogg_start");
        extern const char ogg_activation_end[] asm("_binary_activation_ogg_end");
        static const std::string_view OGG_ACTIVATION {
        static_cast<const char*>(ogg_activation_start),
        static_cast<size_t>(ogg_activation_end - ogg_activation_start)
        };

        extern const char ogg_bed_reminder_a_start[] asm("_binary_bed_reminder_a_ogg_start");
        extern const char ogg_bed_reminder_a_end[] asm("_binary_bed_reminder_a_ogg_end");
        static const std::string_view OGG_BED_REMINDER_A {
        static_cast<const char*>(ogg_bed_reminder_a_start),
        static_cast<size_t>(ogg_bed_reminder_a_end - ogg_bed_reminder_a_start)
        };

        extern const char ogg_bed_reminder_b_start[] asm("_binary_bed_reminder_b_ogg_start");
        extern const char ogg_bed_reminder_b_end[] asm("_binary_bed_reminder_b_ogg_end");
        static const std::string_view OGG_BED_REMINDER_B {
        static_cast<const char*>(ogg_bed_reminder_b_start),
        static_cast<size_t>(ogg_bed_reminder_b_end - ogg_bed_reminder_b_start)
        };

        extern const char ogg_bed_talk_blocked_a_start[] asm("_binary_bed_talk_blocked_a_ogg_start");
        extern const char ogg_bed_talk_blocked_a_end[] asm("_binary_bed_talk_blocked_a_ogg_end");
        static const std::string_view OGG_BED_TALK_BLOCKED_A {
        static_cast<const char*>(ogg_bed_talk_blocked_a_start),
        static_cast<size_t>(ogg_bed_talk_blocked_a_end - ogg_bed_talk_blocked_a_start)
        };

        extern const char ogg_bed_talk_blocked_b_start[] asm("_binary_bed_talk_blocked_b_ogg_start");
        extern const char ogg_bed_talk_blocked_b_end[] asm("_binary_bed_talk_blocked_b_ogg_end");
        static const std::string_view OGG_BED_TALK_BLOCKED_B {
        static_cast<const char*>(ogg_bed_talk_blocked_b_start),
        static_cast<size_t>(ogg_bed_talk_blocked_b_end - ogg_bed_talk_blocked_b_start)
        };

        extern const char ogg_err_pin_start[] asm("_binary_err_pin_ogg_start");
        extern const char ogg_err_pin_end[] asm("_binary_err_pin_ogg_end");
        static const std::string_view OGG_ERR_PIN {
        static_cast<const char*>(ogg_err_pin_start),
        static_cast<size_t>(ogg_err_pin_end - ogg_err_pin_start)
        };

        extern const char ogg_err_reg_start[] asm("_binary_err_reg_ogg_start");
        extern const char ogg_err_reg_end[] asm("_binary_err_reg_ogg_end");
        static const std::string_view OGG_ERR_REG {
        static_cast<const char*>(ogg_err_reg_start),
        static_cast<size_t>(ogg_err_reg_end - ogg_err_reg_start)
        };

        extern const char ogg_exclamation_start[] asm("_binary_exclamation_ogg_start");
        extern const char ogg_exclamation_end[] asm("_binary_exclamation_ogg_end");
        static const std::string_view OGG_EXCLAMATION {
        static_cast<const char*>(ogg_exclamation_start),
        static_cast<size_t>(ogg_exclamation_end - ogg_exclamation_start)
        };

        extern const char ogg_low_battery_start[] asm("_binary_low_battery_ogg_start");
        extern const char ogg_low_battery_end[] asm("_binary_low_battery_ogg_end");
        static const std::string_view OGG_LOW_BATTERY {
        static_cast<const char*>(ogg_low_battery_start),
        static_cast<size_t>(ogg_low_battery_end - ogg_low_battery_start)
        };

        extern const char ogg_notification_start[] asm("_binary_notification_ogg_start");
        extern const char ogg_notification_end[] asm("_binary_notification_ogg_end");
        static const std::string_view OGG_NOTIFICATION {
        static_cast<const char*>(ogg_notification_start),
        static_cast<size_t>(ogg_notification_end - ogg_notification_start)
        };

        extern const char ogg_popup_start[] asm("_binary_popup_ogg_start");
        extern const char ogg_popup_end[] asm("_binary_popup_ogg_end");
        static const std::string_view OGG_POPUP {
        static_cast<const char*>(ogg_popup_start),
        static_cast<size_t>(ogg_popup_end - ogg_popup_start)
        };

        extern const char ogg_success_start[] asm("_binary_success_ogg_start");
        extern const char ogg_success_end[] asm("_binary_success_ogg_end");
        static const std::string_view OGG_SUCCESS {
        static_cast<const char*>(ogg_success_start),
        static_cast<size_t>(ogg_success_end - ogg_success_start)
        };

        extern const char ogg_upgrade_start[] asm("_binary_upgrade_ogg_start");
        extern const char ogg_upgrade_end[] asm("_binary_upgrade_ogg_end");
        static const std::string_view OGG_UPGRADE {
        static_cast<const char*>(ogg_upgrade_start),
        static_cast<size_t>(ogg_upgrade_end - ogg_upgrade_start)
        };

        extern const char ogg_vibration_start[] asm("_binary_vibration_ogg_start");
        extern const char ogg_vibration_end[] asm("_binary_vibration_ogg_end");
        static const std::string_view OGG_VIBRATION {
        static_cast<const char*>(ogg_vibration_start),
        static_cast<size_t>(ogg_vibration_end - ogg_vibration_start)
        };

        extern const char ogg_vox_annoyed_1_a_start[] asm("_binary_vox_annoyed_1_a_ogg_start");
        extern const char ogg_vox_annoyed_1_a_end[] asm("_binary_vox_annoyed_1_a_ogg_end");
        static const std::string_view OGG_VOX_ANNOYED_1_A {
        static_cast<const char*>(ogg_vox_annoyed_1_a_start),
        static_cast<size_t>(ogg_vox_annoyed_1_a_end - ogg_vox_annoyed_1_a_start)
        };

        extern const char ogg_vox_annoyed_1_b_start[] asm("_binary_vox_annoyed_1_b_ogg_start");
        extern const char ogg_vox_annoyed_1_b_end[] asm("_binary_vox_annoyed_1_b_ogg_end");
        static const std::string_view OGG_VOX_ANNOYED_1_B {
        static_cast<const char*>(ogg_vox_annoyed_1_b_start),
        static_cast<size_t>(ogg_vox_annoyed_1_b_end - ogg_vox_annoyed_1_b_start)
        };

        extern const char ogg_vox_annoyed_2_a_start[] asm("_binary_vox_annoyed_2_a_ogg_start");
        extern const char ogg_vox_annoyed_2_a_end[] asm("_binary_vox_annoyed_2_a_ogg_end");
        static const std::string_view OGG_VOX_ANNOYED_2_A {
        static_cast<const char*>(ogg_vox_annoyed_2_a_start),
        static_cast<size_t>(ogg_vox_annoyed_2_a_end - ogg_vox_annoyed_2_a_start)
        };

        extern const char ogg_vox_annoyed_2_b_start[] asm("_binary_vox_annoyed_2_b_ogg_start");
        extern const char ogg_vox_annoyed_2_b_end[] asm("_binary_vox_annoyed_2_b_ogg_end");
        static const std::string_view OGG_VOX_ANNOYED_2_B {
        static_cast<const char*>(ogg_vox_annoyed_2_b_start),
        static_cast<size_t>(ogg_vox_annoyed_2_b_end - ogg_vox_annoyed_2_b_start)
        };

        extern const char ogg_vox_happy_1_a_start[] asm("_binary_vox_happy_1_a_ogg_start");
        extern const char ogg_vox_happy_1_a_end[] asm("_binary_vox_happy_1_a_ogg_end");
        static const std::string_view OGG_VOX_HAPPY_1_A {
        static_cast<const char*>(ogg_vox_happy_1_a_start),
        static_cast<size_t>(ogg_vox_happy_1_a_end - ogg_vox_happy_1_a_start)
        };

        extern const char ogg_vox_happy_1_b_start[] asm("_binary_vox_happy_1_b_ogg_start");
        extern const char ogg_vox_happy_1_b_end[] asm("_binary_vox_happy_1_b_ogg_end");
        static const std::string_view OGG_VOX_HAPPY_1_B {
        static_cast<const char*>(ogg_vox_happy_1_b_start),
        static_cast<size_t>(ogg_vox_happy_1_b_end - ogg_vox_happy_1_b_start)
        };

        extern const char ogg_vox_happy_2_a_start[] asm("_binary_vox_happy_2_a_ogg_start");
        extern const char ogg_vox_happy_2_a_end[] asm("_binary_vox_happy_2_a_ogg_end");
        static const std::string_view OGG_VOX_HAPPY_2_A {
        static_cast<const char*>(ogg_vox_happy_2_a_start),
        static_cast<size_t>(ogg_vox_happy_2_a_end - ogg_vox_happy_2_a_start)
        };

        extern const char ogg_vox_happy_2_b_start[] asm("_binary_vox_happy_2_b_ogg_start");
        extern const char ogg_vox_happy_2_b_end[] asm("_binary_vox_happy_2_b_ogg_end");
        static const std::string_view OGG_VOX_HAPPY_2_B {
        static_cast<const char*>(ogg_vox_happy_2_b_start),
        static_cast<size_t>(ogg_vox_happy_2_b_end - ogg_vox_happy_2_b_start)
        };

        extern const char ogg_vox_hum_1_a_start[] asm("_binary_vox_hum_1_a_ogg_start");
        extern const char ogg_vox_hum_1_a_end[] asm("_binary_vox_hum_1_a_ogg_end");
        static const std::string_view OGG_VOX_HUM_1_A {
        static_cast<const char*>(ogg_vox_hum_1_a_start),
        static_cast<size_t>(ogg_vox_hum_1_a_end - ogg_vox_hum_1_a_start)
        };

        extern const char ogg_vox_hum_1_b_start[] asm("_binary_vox_hum_1_b_ogg_start");
        extern const char ogg_vox_hum_1_b_end[] asm("_binary_vox_hum_1_b_ogg_end");
        static const std::string_view OGG_VOX_HUM_1_B {
        static_cast<const char*>(ogg_vox_hum_1_b_start),
        static_cast<size_t>(ogg_vox_hum_1_b_end - ogg_vox_hum_1_b_start)
        };

        extern const char ogg_vox_hum_2_a_start[] asm("_binary_vox_hum_2_a_ogg_start");
        extern const char ogg_vox_hum_2_a_end[] asm("_binary_vox_hum_2_a_ogg_end");
        static const std::string_view OGG_VOX_HUM_2_A {
        static_cast<const char*>(ogg_vox_hum_2_a_start),
        static_cast<size_t>(ogg_vox_hum_2_a_end - ogg_vox_hum_2_a_start)
        };

        extern const char ogg_vox_hum_2_b_start[] asm("_binary_vox_hum_2_b_ogg_start");
        extern const char ogg_vox_hum_2_b_end[] asm("_binary_vox_hum_2_b_ogg_end");
        static const std::string_view OGG_VOX_HUM_2_B {
        static_cast<const char*>(ogg_vox_hum_2_b_start),
        static_cast<size_t>(ogg_vox_hum_2_b_end - ogg_vox_hum_2_b_start)
        };

        extern const char ogg_vox_hum_3_a_start[] asm("_binary_vox_hum_3_a_ogg_start");
        extern const char ogg_vox_hum_3_a_end[] asm("_binary_vox_hum_3_a_ogg_end");
        static const std::string_view OGG_VOX_HUM_3_A {
        static_cast<const char*>(ogg_vox_hum_3_a_start),
        static_cast<size_t>(ogg_vox_hum_3_a_end - ogg_vox_hum_3_a_start)
        };

        extern const char ogg_vox_hum_3_b_start[] asm("_binary_vox_hum_3_b_ogg_start");
        extern const char ogg_vox_hum_3_b_end[] asm("_binary_vox_hum_3_b_ogg_end");
        static const std::string_view OGG_VOX_HUM_3_B {
        static_cast<const char*>(ogg_vox_hum_3_b_start),
        static_cast<size_t>(ogg_vox_hum_3_b_end - ogg_vox_hum_3_b_start)
        };

        extern const char ogg_vox_hum_4_a_start[] asm("_binary_vox_hum_4_a_ogg_start");
        extern const char ogg_vox_hum_4_a_end[] asm("_binary_vox_hum_4_a_ogg_end");
        static const std::string_view OGG_VOX_HUM_4_A {
        static_cast<const char*>(ogg_vox_hum_4_a_start),
        static_cast<size_t>(ogg_vox_hum_4_a_end - ogg_vox_hum_4_a_start)
        };

        extern const char ogg_vox_hum_4_b_start[] asm("_binary_vox_hum_4_b_ogg_start");
        extern const char ogg_vox_hum_4_b_end[] asm("_binary_vox_hum_4_b_ogg_end");
        static const std::string_view OGG_VOX_HUM_4_B {
        static_cast<const char*>(ogg_vox_hum_4_b_start),
        static_cast<size_t>(ogg_vox_hum_4_b_end - ogg_vox_hum_4_b_start)
        };

        extern const char ogg_vox_laugh_1_a_start[] asm("_binary_vox_laugh_1_a_ogg_start");
        extern const char ogg_vox_laugh_1_a_end[] asm("_binary_vox_laugh_1_a_ogg_end");
        static const std::string_view OGG_VOX_LAUGH_1_A {
        static_cast<const char*>(ogg_vox_laugh_1_a_start),
        static_cast<size_t>(ogg_vox_laugh_1_a_end - ogg_vox_laugh_1_a_start)
        };

        extern const char ogg_vox_laugh_1_b_start[] asm("_binary_vox_laugh_1_b_ogg_start");
        extern const char ogg_vox_laugh_1_b_end[] asm("_binary_vox_laugh_1_b_ogg_end");
        static const std::string_view OGG_VOX_LAUGH_1_B {
        static_cast<const char*>(ogg_vox_laugh_1_b_start),
        static_cast<size_t>(ogg_vox_laugh_1_b_end - ogg_vox_laugh_1_b_start)
        };

        extern const char ogg_vox_mumble_1_a_start[] asm("_binary_vox_mumble_1_a_ogg_start");
        extern const char ogg_vox_mumble_1_a_end[] asm("_binary_vox_mumble_1_a_ogg_end");
        static const std::string_view OGG_VOX_MUMBLE_1_A {
        static_cast<const char*>(ogg_vox_mumble_1_a_start),
        static_cast<size_t>(ogg_vox_mumble_1_a_end - ogg_vox_mumble_1_a_start)
        };

        extern const char ogg_vox_mumble_1_b_start[] asm("_binary_vox_mumble_1_b_ogg_start");
        extern const char ogg_vox_mumble_1_b_end[] asm("_binary_vox_mumble_1_b_ogg_end");
        static const std::string_view OGG_VOX_MUMBLE_1_B {
        static_cast<const char*>(ogg_vox_mumble_1_b_start),
        static_cast<size_t>(ogg_vox_mumble_1_b_end - ogg_vox_mumble_1_b_start)
        };

        extern const char ogg_vox_mumble_2_a_start[] asm("_binary_vox_mumble_2_a_ogg_start");
        extern const char ogg_vox_mumble_2_a_end[] asm("_binary_vox_mumble_2_a_ogg_end");
        static const std::string_view OGG_VOX_MUMBLE_2_A {
        static_cast<const char*>(ogg_vox_mumble_2_a_start),
        static_cast<size_t>(ogg_vox_mumble_2_a_end - ogg_vox_mumble_2_a_start)
        };

        extern const char ogg_vox_mumble_2_b_start[] asm("_binary_vox_mumble_2_b_ogg_start");
        extern const char ogg_vox_mumble_2_b_end[] asm("_binary_vox_mumble_2_b_ogg_end");
        static const std::string_view OGG_VOX_MUMBLE_2_B {
        static_cast<const char*>(ogg_vox_mumble_2_b_start),
        static_cast<size_t>(ogg_vox_mumble_2_b_end - ogg_vox_mumble_2_b_start)
        };

        extern const char ogg_vox_mumble_3_a_start[] asm("_binary_vox_mumble_3_a_ogg_start");
        extern const char ogg_vox_mumble_3_a_end[] asm("_binary_vox_mumble_3_a_ogg_end");
        static const std::string_view OGG_VOX_MUMBLE_3_A {
        static_cast<const char*>(ogg_vox_mumble_3_a_start),
        static_cast<size_t>(ogg_vox_mumble_3_a_end - ogg_vox_mumble_3_a_start)
        };

        extern const char ogg_vox_mumble_3_b_start[] asm("_binary_vox_mumble_3_b_ogg_start");
        extern const char ogg_vox_mumble_3_b_end[] asm("_binary_vox_mumble_3_b_ogg_end");
        static const std::string_view OGG_VOX_MUMBLE_3_B {
        static_cast<const char*>(ogg_vox_mumble_3_b_start),
        static_cast<size_t>(ogg_vox_mumble_3_b_end - ogg_vox_mumble_3_b_start)
        };

        extern const char ogg_vox_mumble_4_a_start[] asm("_binary_vox_mumble_4_a_ogg_start");
        extern const char ogg_vox_mumble_4_a_end[] asm("_binary_vox_mumble_4_a_ogg_end");
        static const std::string_view OGG_VOX_MUMBLE_4_A {
        static_cast<const char*>(ogg_vox_mumble_4_a_start),
        static_cast<size_t>(ogg_vox_mumble_4_a_end - ogg_vox_mumble_4_a_start)
        };

        extern const char ogg_vox_mumble_4_b_start[] asm("_binary_vox_mumble_4_b_ogg_start");
        extern const char ogg_vox_mumble_4_b_end[] asm("_binary_vox_mumble_4_b_ogg_end");
        static const std::string_view OGG_VOX_MUMBLE_4_B {
        static_cast<const char*>(ogg_vox_mumble_4_b_start),
        static_cast<size_t>(ogg_vox_mumble_4_b_end - ogg_vox_mumble_4_b_start)
        };

        extern const char ogg_vox_sad_1_a_start[] asm("_binary_vox_sad_1_a_ogg_start");
        extern const char ogg_vox_sad_1_a_end[] asm("_binary_vox_sad_1_a_ogg_end");
        static const std::string_view OGG_VOX_SAD_1_A {
        static_cast<const char*>(ogg_vox_sad_1_a_start),
        static_cast<size_t>(ogg_vox_sad_1_a_end - ogg_vox_sad_1_a_start)
        };

        extern const char ogg_vox_sad_1_b_start[] asm("_binary_vox_sad_1_b_ogg_start");
        extern const char ogg_vox_sad_1_b_end[] asm("_binary_vox_sad_1_b_ogg_end");
        static const std::string_view OGG_VOX_SAD_1_B {
        static_cast<const char*>(ogg_vox_sad_1_b_start),
        static_cast<size_t>(ogg_vox_sad_1_b_end - ogg_vox_sad_1_b_start)
        };

        extern const char ogg_vox_sad_2_a_start[] asm("_binary_vox_sad_2_a_ogg_start");
        extern const char ogg_vox_sad_2_a_end[] asm("_binary_vox_sad_2_a_ogg_end");
        static const std::string_view OGG_VOX_SAD_2_A {
        static_cast<const char*>(ogg_vox_sad_2_a_start),
        static_cast<size_t>(ogg_vox_sad_2_a_end - ogg_vox_sad_2_a_start)
        };

        extern const char ogg_vox_sad_2_b_start[] asm("_binary_vox_sad_2_b_ogg_start");
        extern const char ogg_vox_sad_2_b_end[] asm("_binary_vox_sad_2_b_ogg_end");
        static const std::string_view OGG_VOX_SAD_2_B {
        static_cast<const char*>(ogg_vox_sad_2_b_start),
        static_cast<size_t>(ogg_vox_sad_2_b_end - ogg_vox_sad_2_b_start)
        };

        extern const char ogg_vox_surprise_1_a_start[] asm("_binary_vox_surprise_1_a_ogg_start");
        extern const char ogg_vox_surprise_1_a_end[] asm("_binary_vox_surprise_1_a_ogg_end");
        static const std::string_view OGG_VOX_SURPRISE_1_A {
        static_cast<const char*>(ogg_vox_surprise_1_a_start),
        static_cast<size_t>(ogg_vox_surprise_1_a_end - ogg_vox_surprise_1_a_start)
        };

        extern const char ogg_vox_surprise_1_b_start[] asm("_binary_vox_surprise_1_b_ogg_start");
        extern const char ogg_vox_surprise_1_b_end[] asm("_binary_vox_surprise_1_b_ogg_end");
        static const std::string_view OGG_VOX_SURPRISE_1_B {
        static_cast<const char*>(ogg_vox_surprise_1_b_start),
        static_cast<size_t>(ogg_vox_surprise_1_b_end - ogg_vox_surprise_1_b_start)
        };

        extern const char ogg_vox_surprise_2_a_start[] asm("_binary_vox_surprise_2_a_ogg_start");
        extern const char ogg_vox_surprise_2_a_end[] asm("_binary_vox_surprise_2_a_ogg_end");
        static const std::string_view OGG_VOX_SURPRISE_2_A {
        static_cast<const char*>(ogg_vox_surprise_2_a_start),
        static_cast<size_t>(ogg_vox_surprise_2_a_end - ogg_vox_surprise_2_a_start)
        };

        extern const char ogg_vox_surprise_2_b_start[] asm("_binary_vox_surprise_2_b_ogg_start");
        extern const char ogg_vox_surprise_2_b_end[] asm("_binary_vox_surprise_2_b_ogg_end");
        static const std::string_view OGG_VOX_SURPRISE_2_B {
        static_cast<const char*>(ogg_vox_surprise_2_b_start),
        static_cast<size_t>(ogg_vox_surprise_2_b_end - ogg_vox_surprise_2_b_start)
        };

        extern const char ogg_vox_think_1_a_start[] asm("_binary_vox_think_1_a_ogg_start");
        extern const char ogg_vox_think_1_a_end[] asm("_binary_vox_think_1_a_ogg_end");
        static const std::string_view OGG_VOX_THINK_1_A {
        static_cast<const char*>(ogg_vox_think_1_a_start),
        static_cast<size_t>(ogg_vox_think_1_a_end - ogg_vox_think_1_a_start)
        };

        extern const char ogg_vox_think_1_b_start[] asm("_binary_vox_think_1_b_ogg_start");
        extern const char ogg_vox_think_1_b_end[] asm("_binary_vox_think_1_b_ogg_end");
        static const std::string_view OGG_VOX_THINK_1_B {
        static_cast<const char*>(ogg_vox_think_1_b_start),
        static_cast<size_t>(ogg_vox_think_1_b_end - ogg_vox_think_1_b_start)
        };

        extern const char ogg_vox_think_2_a_start[] asm("_binary_vox_think_2_a_ogg_start");
        extern const char ogg_vox_think_2_a_end[] asm("_binary_vox_think_2_a_ogg_end");
        static const std::string_view OGG_VOX_THINK_2_A {
        static_cast<const char*>(ogg_vox_think_2_a_start),
        static_cast<size_t>(ogg_vox_think_2_a_end - ogg_vox_think_2_a_start)
        };

        extern const char ogg_vox_think_2_b_start[] asm("_binary_vox_think_2_b_ogg_start");
        extern const char ogg_vox_think_2_b_end[] asm("_binary_vox_think_2_b_ogg_end");
        static const std::string_view OGG_VOX_THINK_2_B {
        static_cast<const char*>(ogg_vox_think_2_b_start),
        static_cast<size_t>(ogg_vox_think_2_b_end - ogg_vox_think_2_b_start)
        };

        extern const char ogg_vox_yawn_1_a_start[] asm("_binary_vox_yawn_1_a_ogg_start");
        extern const char ogg_vox_yawn_1_a_end[] asm("_binary_vox_yawn_1_a_ogg_end");
        static const std::string_view OGG_VOX_YAWN_1_A {
        static_cast<const char*>(ogg_vox_yawn_1_a_start),
        static_cast<size_t>(ogg_vox_yawn_1_a_end - ogg_vox_yawn_1_a_start)
        };

        extern const char ogg_vox_yawn_1_b_start[] asm("_binary_vox_yawn_1_b_ogg_start");
        extern const char ogg_vox_yawn_1_b_end[] asm("_binary_vox_yawn_1_b_ogg_end");
        static const std::string_view OGG_VOX_YAWN_1_B {
        static_cast<const char*>(ogg_vox_yawn_1_b_start),
        static_cast<size_t>(ogg_vox_yawn_1_b_end - ogg_vox_yawn_1_b_start)
        };

        extern const char ogg_welcome_start[] asm("_binary_welcome_ogg_start");
        extern const char ogg_welcome_end[] asm("_binary_welcome_ogg_end");
        static const std::string_view OGG_WELCOME {
        static_cast<const char*>(ogg_welcome_start),
        static_cast<size_t>(ogg_welcome_end - ogg_welcome_start)
        };

        extern const char ogg_wificonfig_start[] asm("_binary_wificonfig_ogg_start");
        extern const char ogg_wificonfig_end[] asm("_binary_wificonfig_ogg_end");
        static const std::string_view OGG_WIFICONFIG {
        static_cast<const char*>(ogg_wificonfig_start),
        static_cast<size_t>(ogg_wificonfig_end - ogg_wificonfig_start)
        };
    }
}
