// Menu system ported from bubu_ota in stages.
// This pass ports the CARE menu and the STATS arc, backed by xiaozhi CareSystem.

#include "menu_system.h"

#include "care_system.h"
#include "behavior_scheduler.h"
#include "audio/audio_codec.h"
#include "assets.h"
#include "board.h"
#include "boards/common/wifi_board.h"
#include "boards/common/wifi_connect_service.h"
#include "eye_display.h"
#include "level_system.h"
#include "notes_system.h"
#include "reminder_system.h"
#include "lvgl_display/gif/lvgl_gif.h"
#include "lvgl_display/lvgl_image.h"
#include "lvgl_display/lvgl_theme.h"
#include "settings.h"
#include "display/display.h"
#include "application.h"
#include <ssid_manager.h>

#include <array>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

#include <esp_log.h>
#include <esp_err.h>
#include <esp_timer.h>
#include <lvgl.h>

#define TAG "MenuSystem"

extern const lv_font_t lv_font_montserrat_vn_20;
extern const lv_font_t lv_font_montserrat_vn_22;
extern const lv_font_t lv_font_montserrat_vn_28;

namespace {

enum CareItem {
    CARE_FEED,
    CARE_PLAY,
    CARE_CLEAN,
    CARE_SLEEP,
    CARE_STATS,
    CARE_LEVEL,
    CARE_ITEM_COUNT
};

enum ConnectItem {
    CONNECT_PHONE,
    CONNECT_DEVICE,
    CONNECT_ITEM_COUNT
};

enum ConnectView {
    CONNECT_VIEW_METHODS,
    CONNECT_VIEW_WIFI_LIST,
    CONNECT_VIEW_PHONE_HELP
};

enum SettingsItem {
    SETTINGS_VOLUME,
    SETTINGS_ITEM_COUNT
};

enum EyeEditorMode {
    EYE_EDITOR_WIDTH,
    EYE_EDITOR_HEIGHT,
    EYE_EDITOR_RADIUS,
    EYE_EDITOR_MODE_COUNT
};

enum EyeEditorSelectedEye {
    EYE_EDITOR_LEFT,
    EYE_EDITOR_RIGHT
};

constexpr size_t STAT_COUNT = 4;

MenuState currentState = MENU_CLOSED;
MenuItem selectedItem = MENU_CARE;
CareItem selectedCareItem = CARE_FEED;
ConnectItem selectedConnectItem = CONNECT_PHONE;
ConnectView connectView = CONNECT_VIEW_METHODS;
SettingsItem selectedSettingsItem = SETTINGS_VOLUME;
size_t selectedWifiIndex = 0;
size_t statIndex = 0;
bool statsOpenedFromCare = false;
Display* displayHandle = nullptr;

constexpr uint32_t COLOR_BACKGROUND = 0x050812;
constexpr uint32_t COLOR_MINT = 0x58F5C9;
constexpr uint32_t COLOR_TEXT = 0xFFFFFF;

lv_obj_t* menuPanel = nullptr;
lv_obj_t* menuList = nullptr;
lv_obj_t* menuItems[MENU_ITEM_COUNT] = {nullptr};
lv_obj_t* upButton = nullptr;
lv_obj_t* downButton = nullptr;

lv_obj_t* carePanel = nullptr;
lv_obj_t* careList = nullptr;
lv_obj_t* careItems[CARE_ITEM_COUNT] = {nullptr};
lv_obj_t* careUpButton = nullptr;
lv_obj_t* careDownButton = nullptr;

lv_obj_t* connectPanel = nullptr;
lv_obj_t* connectList = nullptr;
lv_obj_t* connectTitle = nullptr;
lv_obj_t* connectHint = nullptr;
lv_obj_t* connectItems[CONNECT_ITEM_COUNT] = {nullptr};
lv_obj_t* connectUpButton = nullptr;
lv_obj_t* connectDownButton = nullptr;
lv_obj_t* connectWifiList = nullptr;

lv_obj_t* keyboardPanel = nullptr;
lv_obj_t* keyboardTitle = nullptr;
lv_obj_t* keyboardSsid = nullptr;
lv_obj_t* keyboardValue = nullptr;
lv_obj_t* keyboardGrid = nullptr;
std::array<lv_obj_t*, 15> keyboardButtons = {};

lv_obj_t* settingsPanel = nullptr;
lv_obj_t* settingsTitle = nullptr;
lv_obj_t* settingsItems[SETTINGS_ITEM_COUNT] = {nullptr};

lv_obj_t* notesPanel = nullptr;
lv_obj_t* notesTitle = nullptr;
lv_obj_t* notesList = nullptr;
lv_obj_t* notesUpButton = nullptr;
lv_obj_t* notesDownButton = nullptr;
std::vector<lv_obj_t*> notesItems;
std::vector<NotesSystem::NoteEntry> notesEntries;
size_t selectedNoteIndex = 0;

lv_obj_t* noteDetailPanel = nullptr;
lv_obj_t* noteDetailTitle = nullptr;
lv_obj_t* noteDetailValue = nullptr;
lv_obj_t* noteDetailHint = nullptr;

lv_obj_t* remindersPanel = nullptr;
lv_obj_t* remindersTitle = nullptr;
lv_obj_t* remindersList = nullptr;
lv_obj_t* remindersUpButton = nullptr;
lv_obj_t* remindersDownButton = nullptr;
std::vector<lv_obj_t*> remindersItems;
std::vector<ReminderSystem::Reminder> remindersEntries;
size_t selectedReminderIndex = 0;

lv_obj_t* reminderDetailPanel = nullptr;
lv_obj_t* reminderDetailTitle = nullptr;
lv_obj_t* reminderDetailValue = nullptr;
lv_obj_t* reminderDetailHint = nullptr;

lv_obj_t* volumePanel = nullptr;
lv_obj_t* volumeTitle = nullptr;
lv_obj_t* volumeValueLabel = nullptr;
lv_obj_t* volumeMinusButton = nullptr;
lv_obj_t* volumePlusButton = nullptr;
lv_obj_t* volumeBackButton = nullptr;
int volumeWorkingValue = 0;
bool volumeDirty = false;

lv_obj_t* eyeEditorPanel = nullptr;
lv_obj_t* eyeEditorModeButtons[EYE_EDITOR_MODE_COUNT] = {nullptr};
lv_obj_t* eyeEditorModeLabels[EYE_EDITOR_MODE_COUNT] = {nullptr};
lv_obj_t* eyeEditorSelectionOutline = nullptr;
lv_obj_t* eyeEditorMinusButton = nullptr;
lv_obj_t* eyeEditorPlusButton = nullptr;
lv_obj_t* eyeEditorValueLabel = nullptr;
lv_obj_t* eyeEditorSaveButton = nullptr;
lv_obj_t* eyeEditorResetButton = nullptr;
lv_obj_t* eyeEditorBackButton = nullptr;

lv_obj_t* statsPanel = nullptr;
lv_obj_t* statsArc = nullptr;
lv_obj_t* statsTitle = nullptr;
lv_obj_t* statsLeftBtn = nullptr;
lv_obj_t* statsRightBtn = nullptr;
lv_obj_t* statsActionZone = nullptr;
lv_obj_t* feedingOverlay = nullptr;
lv_obj_t* feedingImage = nullptr;
std::unique_ptr<LvglGif> feedingGifController = nullptr;
std::unique_ptr<LvglRawImage> feedingRawImage = nullptr;
esp_timer_handle_t feedingTimer = nullptr;
bool feedingAnimationActive = false;

constexpr uint32_t FEEDING_ANIMATION_DURATION_MS = 2000;
constexpr const char* kFeedingEmojiName = "feeding";
constexpr const char* kFeedingGifAssetFile = "feeding.gif";
constexpr const char* kFeedingGifMissingNotice = "Feeding GIF missing";

const char* menuItemLabelTexts[MENU_ITEM_COUNT] = {
    "CHĂM SÓC",
    "KẾT NỐI",
    "NHẮC NHỞ",
    "GHI CHÚ",
    "CÀI ĐẶT",
};

const char* careItemLabelTexts[CARE_ITEM_COUNT] = {
    "CHO ĂN",
    "GIẢI TRÍ",
    "TẮM RỬA",
    "NGỦ",
    "TRẠNG THÁI",
    "CẤP ĐỘ",
};

const char* connectItemLabelTexts[CONNECT_ITEM_COUNT] = {
    "BẰNG ĐIỆN THOẠI",
    "TRÊN THIẾT BỊ",
};

const std::array<const char*, 15> keyboardButtonTexts = {
    "1!@", "2 abc", "3 def",
    "4 ghi", "5 jkl", "6 mno",
    "7 pqrs", "8 tuv", "9 wxyz",
    "*", "0", "#",
    LV_SYMBOL_UP, "OK", LV_SYMBOL_LEFT,
};

std::vector<lv_obj_t*> connectWifiItems;
uint32_t lastWifiScanVersion = 0;
bool lastWifiScanActive = false;
std::string selectedWifiSsid;
char keyboardText[65] = {0};
size_t keyboardLen = 0;
char keyboardLastKey = '\0';
size_t keyboardLastIndex = 0;
uint32_t keyboardLastTapMs = 0;
bool keyboardCaps = false;

EyeDisplay::EyeShape eyeEditorSavedLeftShape = {};
EyeDisplay::EyeShape eyeEditorSavedRightShape = {};
EyeDisplay::EyeShape eyeEditorWorkingLeftShape = {};
EyeDisplay::EyeShape eyeEditorWorkingRightShape = {};
EyeEditorMode eyeEditorMode = EYE_EDITOR_WIDTH;
EyeEditorSelectedEye eyeEditorSelectedEye = EYE_EDITOR_LEFT;
bool eyeEditorDirty = false;

constexpr uint32_t T9_TAP_TIMEOUT_MS = 1100;
constexpr uint32_t MENU_INACTIVITY_TIMEOUT_MS = 30000;

uint32_t lastMenuActivityMs = 0;

const char* statNames[STAT_COUNT] = {
    "CÁI BỤNG",
    "CẢM XÚC",
    "NĂNG LƯỢNG",
    "SẠCH SẼ",
};

const uint32_t statColors[STAT_COUNT] = {
    0xFF7F50,
    0x70C1FF,
    0xFFD23F,
    0x58F5C9,
};

bool IsPointInside(lv_obj_t* obj, uint16_t x, uint16_t y) {
    if (obj == nullptr) {
        return false;
    }
    lv_area_t area;
    lv_obj_get_coords(obj, &area);
    return x >= area.x1 && x <= area.x2 && y >= area.y1 && y <= area.y2;
}

void MarkMenuActivity() {
    lastMenuActivityMs = lv_tick_get();
    BehaviorScheduler::SetLastInteractionTime();
}

EyeDisplay* GetEyeDisplay() {
    return dynamic_cast<EyeDisplay*>(displayHandle);
}

EyeDisplay::EyeShape GetFactoryEyeShape() {
    return {80, 80, 24};
}

void StopFeedingAnimationLocked() {
    if (!feedingAnimationActive) {
        return;
    }

    feedingAnimationActive = false;
    if (feedingTimer != nullptr) {
        esp_timer_stop(feedingTimer);
    }

    if (feedingGifController) {
        feedingGifController->Stop();
        feedingGifController.reset();
    }

    if (feedingImage != nullptr) {
        lv_image_set_src(feedingImage, nullptr);
        lv_obj_add_flag(feedingImage, LV_OBJ_FLAG_HIDDEN);
    }
    if (feedingOverlay != nullptr) {
        lv_obj_add_flag(feedingOverlay, LV_OBJ_FLAG_HIDDEN);
    }
}

void EnsureFeedingOverlayLocked() {
    if (feedingOverlay != nullptr) {
        return;
    }

    lv_obj_t* overlay_parent = lv_layer_top();
    if (overlay_parent == nullptr) {
        overlay_parent = lv_screen_active();
    }
    if (overlay_parent == nullptr) {
        return;
    }

    feedingOverlay = lv_obj_create(overlay_parent);
    lv_obj_set_size(feedingOverlay, LV_HOR_RES, LV_VER_RES);
    lv_obj_center(feedingOverlay);
    lv_obj_set_style_bg_color(feedingOverlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(feedingOverlay, LV_OPA_70, 0);
    lv_obj_set_style_border_width(feedingOverlay, 0, 0);
    lv_obj_set_style_pad_all(feedingOverlay, 0, 0);
    lv_obj_set_style_radius(feedingOverlay, 0, 0);
    lv_obj_clear_flag(feedingOverlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(feedingOverlay, LV_OBJ_FLAG_HIDDEN);

    feedingImage = lv_img_create(feedingOverlay);
    lv_obj_center(feedingImage);
    lv_obj_add_flag(feedingImage, LV_OBJ_FLAG_HIDDEN);
}

void EnsureFeedingTimerCreated() {
    if (feedingTimer != nullptr) {
        return;
    }

    esp_timer_create_args_t timer_args = {
        .callback = [](void*) {
            Application::GetInstance().Schedule([]() {
                if (!feedingAnimationActive || displayHandle == nullptr) {
                    return;
                }
                DisplayLockGuard lock(displayHandle);
                StopFeedingAnimationLocked();
            });
        },
        .arg = nullptr,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "feeding_gif",
        .skip_unhandled_events = true,
    };

    esp_err_t err = esp_timer_create(&timer_args, &feedingTimer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create feeding timer: %s", esp_err_to_name(err));
        feedingTimer = nullptr;
    }
}

void RestartFeedingTimer() {
    if (feedingTimer == nullptr) {
        return;
    }
    esp_timer_stop(feedingTimer);
    esp_err_t err = esp_timer_start_once(
        feedingTimer, static_cast<uint64_t>(FEEDING_ANIMATION_DURATION_MS) * 1000ULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start feeding timer: %s", esp_err_to_name(err));
    }
}

const LvglImage* ResolveFeedingImage() {
    feedingRawImage.reset();

    if (displayHandle != nullptr) {
        auto* lvgl_theme = dynamic_cast<LvglTheme*>(displayHandle->GetTheme());
        auto emoji_collection = lvgl_theme != nullptr ? lvgl_theme->emoji_collection() : nullptr;
        if (emoji_collection != nullptr) {
            const LvglImage* by_name = emoji_collection->GetEmojiImage(kFeedingEmojiName);
            if (by_name != nullptr) {
                return by_name;
            }
        }
    }

    void* asset_ptr = nullptr;
    size_t asset_size = 0;
    auto& assets = Assets::GetInstance();
    if (!assets.partition_valid()) {
        return nullptr;
    }
    if (!assets.GetAssetData(kFeedingGifAssetFile, asset_ptr, asset_size) || asset_ptr == nullptr || asset_size == 0) {
        return nullptr;
    }

    feedingRawImage = std::make_unique<LvglRawImage>(asset_ptr, asset_size);
    return feedingRawImage.get();
}

bool StartFeedingAnimation() {
    if (displayHandle == nullptr) {
        return false;
    }

    const LvglImage* feeding_image = ResolveFeedingImage();
    if (feeding_image == nullptr) {
        ESP_LOGW(TAG, "Feeding image not found (name: %s, file: %s)", kFeedingEmojiName, kFeedingGifAssetFile);
        return false;
    }

    EnsureFeedingTimerCreated();
    if (feedingTimer == nullptr) {
        return false;
    }

    DisplayLockGuard lock(displayHandle);
    EnsureFeedingOverlayLocked();
    if (feedingOverlay == nullptr || feedingImage == nullptr) {
        return false;
    }

    if (feedingAnimationActive) {
        RestartFeedingTimer();
        return true;
    }

    feedingGifController.reset();
    lv_image_set_src(feedingImage, nullptr);

    if (feeding_image->IsGif()) {
        feedingGifController = std::make_unique<LvglGif>(feeding_image->image_dsc());
        if (!feedingGifController->IsLoaded()) {
            ESP_LOGW(TAG, "Feeding GIF failed to load");
            feedingGifController.reset();
            return false;
        }
        feedingGifController->SetFrameCallback([]() {
            if (feedingImage != nullptr && feedingGifController != nullptr) {
                lv_image_set_src(feedingImage, feedingGifController->image_dsc());
            }
        });
        lv_image_set_src(feedingImage, feedingGifController->image_dsc());
        feedingGifController->Start();
    } else {
        lv_image_set_src(feedingImage, feeding_image->image_dsc());
    }

    feedingAnimationActive = true;
    lv_obj_clear_flag(feedingImage, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(feedingOverlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(feedingOverlay);
    lv_obj_move_foreground(feedingImage);
    RestartFeedingTimer();
    ESP_LOGI(TAG, "Feeding animation started");
    return true;
}

void TriggerFeedingAnimationOrNotify() {
    if (!StartFeedingAnimation() && displayHandle != nullptr) {
        displayHandle->ShowNotification(kFeedingGifMissingNotice, 1200);
    }
}

int* GetSelectedEyeValue(EyeDisplay::EyeShape* shape, EyeEditorMode mode) {
    if (shape == nullptr) {
        return nullptr;
    }
    switch (mode) {
        case EYE_EDITOR_WIDTH:
            return &shape->w;
        case EYE_EDITOR_HEIGHT:
            return &shape->h;
        case EYE_EDITOR_RADIUS:
            return &shape->radius;
        case EYE_EDITOR_MODE_COUNT:
            return nullptr;
    }
    return nullptr;
}

const EyeDisplay::EyeShape& GetCurrentWorkingShape(EyeEditorSelectedEye selected_eye) {
    return selected_eye == EYE_EDITOR_LEFT ? eyeEditorWorkingLeftShape : eyeEditorWorkingRightShape;
}

EyeDisplay::EyeShape* GetMutableWorkingShape(EyeEditorSelectedEye selected_eye) {
    return selected_eye == EYE_EDITOR_LEFT ? &eyeEditorWorkingLeftShape : &eyeEditorWorkingRightShape;
}

int GetEyeEditorValue(const EyeDisplay::EyeShape& shape, EyeEditorMode mode) {
    switch (mode) {
        case EYE_EDITOR_WIDTH:
            return shape.w;
        case EYE_EDITOR_HEIGHT:
            return shape.h;
        case EYE_EDITOR_RADIUS:
            return shape.radius;
        case EYE_EDITOR_MODE_COUNT:
            return 0;
    }
    return 0;
}

lv_obj_t* CreateNavButton(lv_obj_t* parent, lv_align_t align, lv_coord_t x_ofs, lv_coord_t y_ofs,
                          const char* symbol, lv_align_t label_align, lv_coord_t label_x_ofs,
                          lv_coord_t label_y_ofs, lv_event_cb_t callback) {
    lv_obj_t* button = lv_btn_create(parent);
    lv_obj_set_size(button, 100, 100);
    lv_obj_align(button, align, x_ofs, y_ofs);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x303030), 0);
    lv_obj_set_style_radius(button, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_shadow_width(button, 0, 0);

    lv_obj_t* label = lv_label_create(button);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
    lv_label_set_text(label, symbol);
    lv_obj_align(label, label_align, label_x_ofs, label_y_ofs);

    if (callback != nullptr) {
        lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, nullptr);
    }
    return button;
}

void UpdateMenuItemStyles() {
    if (menuList == nullptr) {
        return;
    }

    for (int i = 0; i < MENU_ITEM_COUNT; ++i) {
        int diff = i - static_cast<int>(selectedItem);
        int adiff = std::abs(diff);
        int wrap_diff = MENU_ITEM_COUNT - adiff;
        int dist = wrap_diff < adiff ? wrap_diff : adiff;

        lv_obj_t* item = menuItems[i];
        if (item == nullptr) {
            continue;
        }

        const lv_font_t* font = &lv_font_montserrat_vn_20;
        lv_opa_t opa = LV_OPA_50;
        if (dist == 0) {
            font = &lv_font_montserrat_vn_22;
            opa = LV_OPA_COVER;
        } else if (dist == 1) {
            opa = 200;
        } else if (dist > 2) {
            opa = LV_OPA_40;
        }

        lv_obj_set_style_text_font(item, font, 0);
        lv_obj_set_style_text_opa(item, opa, 0);
        lv_obj_set_style_text_color(item, lv_color_hex(COLOR_TEXT), 0);
        lv_obj_set_style_text_align(item, LV_TEXT_ALIGN_CENTER, 0);
    }
}

void UpdateCareItemStyles() {
    if (careList == nullptr) {
        return;
    }

    for (int i = 0; i < CARE_ITEM_COUNT; ++i) {
        int dist = std::abs(i - static_cast<int>(selectedCareItem));
        lv_obj_t* item = careItems[i];
        if (item == nullptr) {
            continue;
        }

        const lv_font_t* font = dist == 0 ? &lv_font_montserrat_vn_22 : &lv_font_montserrat_vn_20;
        lv_opa_t opa = dist == 0 ? LV_OPA_COVER : (dist == 1 ? 200 : LV_OPA_40);

        lv_obj_set_style_text_font(item, font, 0);
        lv_obj_set_style_text_opa(item, opa, 0);
        lv_obj_set_style_text_color(item, lv_color_hex(COLOR_TEXT), 0);
        lv_obj_set_style_text_align(item, LV_TEXT_ALIGN_CENTER, 0);
    }
}

void UpdateConnectItemStyles() {
    if (connectList == nullptr) {
        return;
    }

    for (int i = 0; i < CONNECT_ITEM_COUNT; ++i) {
        int dist = std::abs(i - static_cast<int>(selectedConnectItem));
        lv_obj_t* item = connectItems[i];
        if (item == nullptr) {
            continue;
        }

        const lv_font_t* font = dist == 0 ? &lv_font_montserrat_vn_22 : &lv_font_montserrat_vn_20;
        lv_opa_t opa = dist == 0 ? LV_OPA_COVER : (dist == 1 ? 200 : LV_OPA_40);

        lv_obj_set_style_text_font(item, font, 0);
        lv_obj_set_style_text_opa(item, opa, 0);
        lv_obj_set_style_text_color(item, lv_color_hex(COLOR_TEXT), 0);
        lv_obj_set_style_text_align(item, LV_TEXT_ALIGN_CENTER, 0);
    }
}

void UpdateWifiItemStyles() {
    for (size_t i = 0; i < connectWifiItems.size(); ++i) {
        auto* item = connectWifiItems[i];
        if (item == nullptr) {
            continue;
        }

        int dist = std::abs(static_cast<int>(i) - static_cast<int>(selectedWifiIndex));
        const lv_font_t* font = dist == 0 ? &lv_font_montserrat_vn_22 : &lv_font_montserrat_vn_20;
        lv_opa_t opa = dist == 0 ? LV_OPA_COVER : (dist == 1 ? 200 : LV_OPA_40);

        lv_obj_set_style_text_font(item, font, 0);
        lv_obj_set_style_text_opa(item, opa, 0);
        lv_obj_set_style_text_color(item, lv_color_hex(COLOR_TEXT), 0);
        lv_obj_set_style_text_align(item, LV_TEXT_ALIGN_CENTER, 0);
    }
}

void SetConnectHint(const char* text) {
    if (connectHint == nullptr) {
        return;
    }
    lv_label_set_text(connectHint, text);
}

void ApplyConnectView() {
    if (connectPanel == nullptr) {
        return;
    }

    lv_label_set_text(connectTitle, "WIFI");

    switch (connectView) {
        case CONNECT_VIEW_METHODS:
            lv_obj_clear_flag(connectList, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(connectWifiList, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(connectHint, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(connectUpButton, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(connectDownButton, LV_OBJ_FLAG_HIDDEN);
            break;
        case CONNECT_VIEW_WIFI_LIST:
            lv_obj_add_flag(connectList, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(connectWifiList, LV_OBJ_FLAG_HIDDEN);
            if (lv_label_get_text(connectHint)[0] == '\0') {
                lv_obj_add_flag(connectHint, LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_clear_flag(connectHint, LV_OBJ_FLAG_HIDDEN);
            }
            lv_obj_clear_flag(connectUpButton, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(connectDownButton, LV_OBJ_FLAG_HIDDEN);
            break;
        case CONNECT_VIEW_PHONE_HELP:
            lv_obj_add_flag(connectList, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(connectWifiList, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(connectHint, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(connectUpButton, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(connectDownButton, LV_OBJ_FLAG_HIDDEN);
            break;
    }
}

void ResetKeyboardState(const char* initial_text = "") {
    keyboardCaps = false;
    keyboardLastKey = '\0';
    keyboardLastIndex = 0;
    keyboardLastTapMs = 0;
    std::strncpy(keyboardText, initial_text, sizeof(keyboardText) - 1);
    keyboardText[sizeof(keyboardText) - 1] = '\0';
    keyboardLen = std::strlen(keyboardText);
}

void UpdateKeyboardValue() {
    if (keyboardValue != nullptr) {
        lv_label_set_text(keyboardValue, keyboardText);
    }
}

const char* T9CharsForKey(char key) {
    switch (key) {
        case '1': return "1!@";
        case '2': return keyboardCaps ? "2ABC" : "2abc";
        case '3': return keyboardCaps ? "3DEF" : "3def";
        case '4': return keyboardCaps ? "4GHI" : "4ghi";
        case '5': return keyboardCaps ? "5JKL" : "5jkl";
        case '6': return keyboardCaps ? "6MNO" : "6mno";
        case '7': return keyboardCaps ? "7PQRS" : "7pqrs";
        case '8': return keyboardCaps ? "8TUV" : "8tuv";
        case '9': return keyboardCaps ? "9WXYZ" : "9wxyz";
        case '0': return "0";
        case '*': return "*";
        case '#': return "#";
        default: return "";
    }
}

void HandleKeyboardBackspace() {
    if (keyboardLen > 0) {
        --keyboardLen;
        keyboardText[keyboardLen] = '\0';
    }
    keyboardLastKey = '\0';
    keyboardLastIndex = 0;
    keyboardLastTapMs = 0;
    UpdateKeyboardValue();
}

void HandleT9Key(char key) {
    const uint32_t now = lv_tick_get();
    const char* chars = T9CharsForKey(key);
    const size_t chars_len = std::strlen(chars);
    if (chars_len == 0) {
        return;
    }

    if (key == keyboardLastKey && (now - keyboardLastTapMs) < T9_TAP_TIMEOUT_MS && keyboardLen > 0) {
        keyboardLastIndex = (keyboardLastIndex + 1) % chars_len;
        keyboardText[keyboardLen - 1] = chars[keyboardLastIndex];
    } else if (keyboardLen + 1 < sizeof(keyboardText)) {
        keyboardLastIndex = 0;
        keyboardText[keyboardLen++] = chars[keyboardLastIndex];
        keyboardText[keyboardLen] = '\0';
    }

    keyboardLastKey = key;
    keyboardLastTapMs = now;
    UpdateKeyboardValue();
    if (keyboardCaps) {
        keyboardCaps = false;
    }
}

void ScrollMenuToIndex(uint8_t idx, lv_anim_enable_t anim) {
    if (menuList == nullptr || idx >= MENU_ITEM_COUNT) {
        return;
    }
    selectedItem = static_cast<MenuItem>(idx);
    UpdateMenuItemStyles();
    if (menuItems[idx] != nullptr) {
        lv_obj_scroll_to_view(menuItems[idx], anim);
    }
}

void ScrollCareToIndex(uint8_t idx, lv_anim_enable_t anim) {
    if (careList == nullptr || idx >= CARE_ITEM_COUNT) {
        return;
    }
    selectedCareItem = static_cast<CareItem>(idx);
    UpdateCareItemStyles();
    if (careItems[idx] != nullptr) {
        lv_obj_scroll_to_view(careItems[idx], anim);
    }
}

void ScrollConnectToIndex(uint8_t idx, lv_anim_enable_t anim) {
    if (connectList == nullptr || idx >= CONNECT_ITEM_COUNT) {
        return;
    }
    selectedConnectItem = static_cast<ConnectItem>(idx);
    UpdateConnectItemStyles();
    if (connectItems[idx] != nullptr) {
        lv_obj_scroll_to_view(connectItems[idx], anim);
    }
}

void ScrollWifiToIndex(size_t idx, lv_anim_enable_t anim) {
    if (connectWifiItems.empty() || idx >= connectWifiItems.size()) {
        return;
    }
    selectedWifiIndex = idx;
    UpdateWifiItemStyles();
    if (connectWifiItems[idx] != nullptr) {
        lv_obj_scroll_to_view(connectWifiItems[idx], anim);
    }
}

void UpdateStatsUI() {
    if (statsPanel == nullptr) {
        return;
    }

    lv_label_set_text(statsTitle, statNames[statIndex]);

    int value = 0;
    switch (statIndex) {
        case 0:
            value = CareSystem::GetHunger();
            break;
        case 1:
            value = CareSystem::GetMood();
            break;
        case 2:
            value = CareSystem::GetEnergy();
            break;
        case 3:
            value = CareSystem::GetCleanliness();
            break;
        default:
            break;
    }

    if (value < 0) {
        value = 0;
    } else if (value > 100) {
        value = 100;
    }

    lv_arc_set_value(statsArc, value);
    lv_obj_set_style_arc_color(statsArc, lv_color_hex(statColors[statIndex]), LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(statsArc, lv_color_hex(0x202020), LV_PART_MAIN);
}

void ApplyCurrentStatAction() {
    switch (statIndex) {
        case 0:
            CareSystem::AddHunger(CareSystem::kSandwichBoost);
            TriggerFeedingAnimationOrNotify();
            break;
        case 1:
            CareSystem::AddMood(CareSystem::kGamesBoost);
            break;
        case 2:
            CareSystem::AddEnergy(CareSystem::kSleepBoost);
            break;
        case 3:
            CareSystem::AddCleanliness(CareSystem::kBathBoost);
            break;
        default:
            break;
    }
}

void HidePanel(lv_obj_t* panel) {
    if (panel != nullptr) {
        lv_obj_add_flag(panel, LV_OBJ_FLAG_HIDDEN);
    }
}

void ShowPanel(lv_obj_t* panel) {
    if (panel != nullptr) {
        lv_obj_clear_flag(panel, LV_OBJ_FLAG_HIDDEN);
    }
}

void UpdateNotesItemStyles() {
    if (notesList == nullptr) {
        return;
    }

    for (size_t i = 0; i < notesItems.size(); ++i) {
        int dist = std::abs(static_cast<int>(i) - static_cast<int>(selectedNoteIndex));
        lv_obj_t* item = notesItems[i];
        if (item == nullptr) {
            continue;
        }

        const lv_font_t* font = dist == 0 ? &lv_font_montserrat_vn_22 : &lv_font_montserrat_vn_20;
        lv_opa_t opa = dist == 0 ? LV_OPA_COVER : (dist == 1 ? 200 : LV_OPA_40);
        lv_obj_set_style_text_font(item, font, 0);
        lv_obj_set_style_text_opa(item, opa, 0);
        lv_obj_set_style_text_color(item, lv_color_hex(COLOR_TEXT), 0);
        lv_obj_set_style_text_align(item, LV_TEXT_ALIGN_CENTER, 0);
    }
}

void ScrollNotesToIndex(size_t idx, lv_anim_enable_t anim) {
    if (notesItems.empty() || idx >= notesItems.size()) {
        return;
    }
    selectedNoteIndex = idx;
    UpdateNotesItemStyles();
    if (notesItems[idx] != nullptr) {
        lv_obj_scroll_to_view(notesItems[idx], anim);
    }
}

void RebuildNotesList() {
    if (notesList == nullptr) {
        return;
    }

    while (lv_obj_get_child_cnt(notesList) > 0) {
        lv_obj_delete(lv_obj_get_child(notesList, 0));
    }
    notesItems.clear();

    notesEntries = NotesSystem::List();
    if (notesEntries.empty()) {
        auto* label = lv_label_create(notesList);
        lv_label_set_text(label, "NO NOTES");
        lv_obj_set_width(label, lv_pct(100));
        lv_obj_set_style_pad_all(label, 8, 0);
        lv_obj_set_style_min_height(label, 28, 0);
        notesItems.push_back(label);
        selectedNoteIndex = 0;
        UpdateNotesItemStyles();
        return;
    }

    for (size_t i = 0; i < notesEntries.size(); ++i) {
        auto* label = lv_label_create(notesList);
        std::string row_text = std::to_string(i + 1) + ". " + notesEntries[i].key;
        lv_label_set_text(label, row_text.c_str());
        lv_obj_set_width(label, lv_pct(100));
        lv_obj_set_style_pad_all(label, 8, 0);
        lv_obj_set_style_min_height(label, 28, 0);
        notesItems.push_back(label);
    }

    if (selectedNoteIndex >= notesItems.size()) {
        selectedNoteIndex = 0;
    }
    ScrollNotesToIndex(selectedNoteIndex, LV_ANIM_OFF);
}

void ShowNoteDetailForCurrentSelection() {
    if (noteDetailTitle == nullptr || noteDetailValue == nullptr || noteDetailHint == nullptr) {
        return;
    }

    if (notesEntries.empty()) {
        lv_label_set_text(noteDetailTitle, "NO NOTES");
        lv_label_set_text(noteDetailValue, "Use remember(key, value) via MCP to store notes.");
        lv_label_set_text(noteDetailHint, "TAP/POWER: BACK");
        return;
    }

    if (selectedNoteIndex >= notesEntries.size()) {
        selectedNoteIndex = 0;
    }
    const auto& note = notesEntries[selectedNoteIndex];
    lv_label_set_text(noteDetailTitle, note.key.c_str());
    lv_label_set_text(noteDetailValue, note.value.c_str());
    lv_label_set_text(noteDetailHint, "UP/DOWN: NEXT NOTE  TAP/POWER: BACK");
}

const char* ReminderStateText(ReminderSystem::ReminderState state) {
    switch (state) {
        case ReminderSystem::ReminderState::kPending:
            return "PENDING";
        case ReminderSystem::ReminderState::kFiring:
            return "FIRING";
        case ReminderSystem::ReminderState::kSnoozed:
            return "SNOOZED";
        case ReminderSystem::ReminderState::kConfirmed:
            return "CONFIRMED";
        case ReminderSystem::ReminderState::kCancelled:
            return "CANCELLED";
        default:
            return "UNKNOWN";
    }
}

void UpdateRemindersItemStyles() {
    if (remindersList == nullptr) {
        return;
    }

    for (size_t i = 0; i < remindersItems.size(); ++i) {
        int dist = std::abs(static_cast<int>(i) - static_cast<int>(selectedReminderIndex));
        lv_obj_t* item = remindersItems[i];
        if (item == nullptr) {
            continue;
        }

        const lv_font_t* font = dist == 0 ? &lv_font_montserrat_vn_22 : &lv_font_montserrat_vn_20;
        lv_opa_t opa = dist == 0 ? LV_OPA_COVER : (dist == 1 ? 200 : LV_OPA_40);
        lv_obj_set_style_text_font(item, font, 0);
        lv_obj_set_style_text_opa(item, opa, 0);
        lv_obj_set_style_text_color(item, lv_color_hex(COLOR_TEXT), 0);
        lv_obj_set_style_text_align(item, LV_TEXT_ALIGN_CENTER, 0);
    }
}

void ScrollRemindersToIndex(size_t idx, lv_anim_enable_t anim) {
    if (remindersItems.empty() || idx >= remindersItems.size()) {
        return;
    }
    selectedReminderIndex = idx;
    UpdateRemindersItemStyles();
    if (remindersItems[idx] != nullptr) {
        lv_obj_scroll_to_view(remindersItems[idx], anim);
    }
}

void RebuildRemindersList() {
    if (remindersList == nullptr) {
        return;
    }

    while (lv_obj_get_child_cnt(remindersList) > 0) {
        lv_obj_delete(lv_obj_get_child(remindersList, 0));
    }
    remindersItems.clear();

    remindersEntries = ReminderSystem::List();
    if (remindersEntries.empty()) {
        auto* label = lv_label_create(remindersList);
        lv_label_set_text(label, "NO REMINDERS");
        lv_obj_set_width(label, lv_pct(100));
        lv_obj_set_style_pad_all(label, 8, 0);
        lv_obj_set_style_min_height(label, 28, 0);
        remindersItems.push_back(label);
        selectedReminderIndex = 0;
        UpdateRemindersItemStyles();
        return;
    }

    for (size_t i = 0; i < remindersEntries.size(); ++i) {
        auto* label = lv_label_create(remindersList);
        char time_text[8];
        std::snprintf(time_text, sizeof(time_text), "%02d:%02d",
                      remindersEntries[i].target_hour, remindersEntries[i].target_minute);
        std::string row_text = std::to_string(i + 1) + ". ";
        row_text += time_text;
        row_text += " ";
        row_text += remindersEntries[i].message;
        lv_label_set_text(label, row_text.c_str());
        lv_obj_set_width(label, lv_pct(100));
        lv_obj_set_style_pad_all(label, 8, 0);
        lv_obj_set_style_min_height(label, 28, 0);
        remindersItems.push_back(label);
    }

    if (selectedReminderIndex >= remindersItems.size()) {
        selectedReminderIndex = 0;
    }
    ScrollRemindersToIndex(selectedReminderIndex, LV_ANIM_OFF);
}

void ShowReminderDetailForCurrentSelection() {
    if (reminderDetailTitle == nullptr || reminderDetailValue == nullptr || reminderDetailHint == nullptr) {
        return;
    }

    if (remindersEntries.empty()) {
        lv_label_set_text(reminderDetailTitle, "NO REMINDERS");
        lv_label_set_text(reminderDetailValue, "Create reminders via AI.");
        lv_label_set_text(reminderDetailHint, "TAP/POWER: BACK");
        return;
    }

    if (selectedReminderIndex >= remindersEntries.size()) {
        selectedReminderIndex = 0;
    }
    const auto& reminder = remindersEntries[selectedReminderIndex];
    char title[32];
    std::snprintf(title, sizeof(title), "#%d %02d:%02d", static_cast<int>(reminder.id),
                  reminder.target_hour, reminder.target_minute);
    lv_label_set_text(reminderDetailTitle, title);

    std::string detail = reminder.message;
    detail += "\nState: ";
    detail += ReminderStateText(reminder.state);
    detail += "\nSnooze: ";
    detail += std::to_string(reminder.snooze_interval_min);
    detail += "m";
    if (reminder.snooze_count > 0) {
        detail += " (";
        detail += std::to_string(reminder.snooze_count);
        detail += ")";
    }
    lv_label_set_text(reminderDetailValue, detail.c_str());
    lv_label_set_text(reminderDetailHint, "UP/DOWN: NEXT  TAP/POWER: BACK");
}

lv_obj_t* CreateTextButton(lv_obj_t* parent, const char* text, lv_coord_t w, lv_coord_t h, const lv_font_t* font = &lv_font_montserrat_14) {
    auto* button = lv_btn_create(parent);
    lv_obj_set_size(button, w, h);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x101722), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(button, 1, 0);
    lv_obj_set_style_border_color(button, lv_color_hex(0x284552), 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_set_style_radius(button, 14, 0);

    auto* label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_center(label);
    return button;
}

void UpdateSettingsItemStyles() {
    for (int i = 0; i < SETTINGS_ITEM_COUNT; ++i) {
        if (settingsItems[i] == nullptr) {
            continue;
        }

        const bool selected = static_cast<int>(selectedSettingsItem) == i;
        lv_obj_set_style_text_color(settingsItems[i],
                                    selected ? lv_color_hex(COLOR_MINT) : lv_color_hex(COLOR_TEXT), 0);
        lv_obj_set_style_text_font(settingsItems[i],
                                   selected ? &lv_font_montserrat_vn_28 : &lv_font_montserrat_vn_22, 0);
        lv_obj_set_style_text_opa(settingsItems[i], selected ? LV_OPA_COVER : LV_OPA_80, 0);
    }
}

int GetOutputVolume() {
    auto* codec = Board::GetInstance().GetAudioCodec();
    return codec != nullptr ? codec->output_volume() : 0;
}

void UpdateVolumeValueLabel() {
    if (volumeValueLabel == nullptr) {
        return;
    }

    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "%d", volumeWorkingValue);
    if (displayHandle == nullptr) {
        return;
    }
    DisplayLockGuard lock(displayHandle);
    lv_label_set_text(volumeValueLabel, buffer);
}

void PreviewOutputVolumeClamped(int volume) {
    auto* codec = Board::GetInstance().GetAudioCodec();
    if (codec == nullptr) {
        return;
    }

    const int clamped = std::clamp(volume, 0, 100);
    if (clamped == volumeWorkingValue) {
        return;
    }

    codec->PreviewOutputVolume(clamped);
    volumeWorkingValue = clamped;
    volumeDirty = true;
    UpdateVolumeValueLabel();
}

void CommitVolumeIfPending() {
    if (!volumeDirty) {
        return;
    }

    auto* codec = Board::GetInstance().GetAudioCodec();
    if (codec == nullptr) {
        volumeDirty = false;
        return;
    }

    codec->PersistOutputVolume();
    volumeDirty = false;
}

void OpenVolumePanel() {
    if (volumePanel == nullptr) {
        return;
    }

    volumeWorkingValue = GetOutputVolume();
    volumeDirty = false;
    UpdateVolumeValueLabel();
    DisplayLockGuard lock(displayHandle);
    HidePanel(settingsPanel);
    ShowPanel(volumePanel);
    currentState = MENU_VOLUME_OPEN;
}

void ApplyEyeEditorPreview() {
    auto* eye_display = GetEyeDisplay();
    if (eye_display == nullptr) {
        return;
    }

    eye_display->PreviewLeftEyeShape(eyeEditorWorkingLeftShape);
    eye_display->PreviewRightEyeShape(eyeEditorWorkingRightShape);
}

void UpdateEyeEditorValueLabel() {
    if (eyeEditorValueLabel == nullptr) {
        return;
    }

    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "%d", GetEyeEditorValue(GetCurrentWorkingShape(eyeEditorSelectedEye), eyeEditorMode));
    lv_label_set_text(eyeEditorValueLabel, buffer);
}

void UpdateEyeEditorModeStyles() {
    for (int i = 0; i < EYE_EDITOR_MODE_COUNT; ++i) {
        if (eyeEditorModeButtons[i] == nullptr || eyeEditorModeLabels[i] == nullptr) {
            continue;
        }

        const bool selected = static_cast<int>(eyeEditorMode) == i;
        lv_obj_set_style_bg_opa(eyeEditorModeButtons[i], selected ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_color(eyeEditorModeButtons[i], selected ? lv_color_hex(COLOR_MINT) : lv_color_hex(0x101722), 0);
        lv_obj_set_style_border_color(eyeEditorModeButtons[i], selected ? lv_color_hex(COLOR_MINT) : lv_color_hex(0x466170), 0);
        lv_obj_set_style_border_width(eyeEditorModeButtons[i], selected ? 2 : 1, 0);
        lv_obj_set_style_text_color(eyeEditorModeLabels[i], selected ? lv_color_hex(0x04110C) : lv_color_hex(0xC5D5DA), 0);
    }
}

void UpdateEyeEditorSelectionOutline() {
    auto* eye_display = GetEyeDisplay();
    if (eye_display == nullptr || eyeEditorSelectionOutline == nullptr) {
        return;
    }

    auto bounds = eyeEditorSelectedEye == EYE_EDITOR_LEFT ?
        eye_display->GetLeftEyeBounds() :
        eye_display->GetRightEyeBounds();

    if (bounds.w <= 0 || bounds.h <= 0) {
        lv_obj_add_flag(eyeEditorSelectionOutline, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    lv_obj_clear_flag(eyeEditorSelectionOutline, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_size(eyeEditorSelectionOutline, bounds.w + 12, bounds.h + 12);
    lv_obj_set_pos(eyeEditorSelectionOutline, bounds.x - 6, bounds.y - 6);
    lv_obj_move_foreground(eyeEditorSelectionOutline);
}

void SaveEyeEditorShapes() {
    auto* eye_display = GetEyeDisplay();
    if (eye_display == nullptr) {
        return;
    }

    Settings display_settings("display", true);
    display_settings.SetInt("eye_l_w", eyeEditorWorkingLeftShape.w);
    display_settings.SetInt("eye_l_h", eyeEditorWorkingLeftShape.h);
    display_settings.SetInt("eye_l_r", eyeEditorWorkingLeftShape.radius);
    display_settings.SetInt("eye_r_w", eyeEditorWorkingRightShape.w);
    display_settings.SetInt("eye_r_h", eyeEditorWorkingRightShape.h);
    display_settings.SetInt("eye_r_r", eyeEditorWorkingRightShape.radius);

    eye_display->SetBaseLeftEyeShape(eyeEditorWorkingLeftShape);
    eye_display->SetBaseRightEyeShape(eyeEditorWorkingRightShape);
    eyeEditorSavedLeftShape = eyeEditorWorkingLeftShape;
    eyeEditorSavedRightShape = eyeEditorWorkingRightShape;
    eyeEditorDirty = false;
    eye_display->ShowNotification("Saved eyes", 1200);
}

void ResetEyeEditorWorkingShapes() {
    eyeEditorWorkingLeftShape = GetFactoryEyeShape();
    eyeEditorWorkingRightShape = GetFactoryEyeShape();
    eyeEditorDirty = true;
    ApplyEyeEditorPreview();
    UpdateEyeEditorValueLabel();
    UpdateEyeEditorSelectionOutline();
}

void RestoreEyeEditorSavedShapes() {
    auto* eye_display = GetEyeDisplay();
    if (eye_display == nullptr) {
        return;
    }

    eyeEditorWorkingLeftShape = eyeEditorSavedLeftShape;
    eyeEditorWorkingRightShape = eyeEditorSavedRightShape;
    eyeEditorDirty = false;
    eye_display->ClearEyePreviewToBase();
    UpdateEyeEditorValueLabel();
    UpdateEyeEditorSelectionOutline();
}

void OpenEyeEditor() {
    auto* eye_display = GetEyeDisplay();
    if (eye_display == nullptr || eyeEditorPanel == nullptr) {
        return;
    }

    eyeEditorSavedLeftShape = eye_display->GetBaseLeftEyeShape();
    eyeEditorSavedRightShape = eye_display->GetBaseRightEyeShape();
    eyeEditorWorkingLeftShape = eyeEditorSavedLeftShape;
    eyeEditorWorkingRightShape = eyeEditorSavedRightShape;
    eyeEditorSelectedEye = EYE_EDITOR_LEFT;
    eyeEditorMode = EYE_EDITOR_WIDTH;
    eyeEditorDirty = false;
    eye_display->SetEyeMischiefEnabled(false);
    ApplyEyeEditorPreview();
    UpdateEyeEditorModeStyles();
    UpdateEyeEditorValueLabel();
    {
        DisplayLockGuard lock(displayHandle);
        HidePanel(settingsPanel);
        ShowPanel(eyeEditorPanel);
        currentState = MENU_EYE_EDITOR_OPEN;
    }
    UpdateEyeEditorSelectionOutline();
}

void HideAllPanels() {
    HidePanel(menuPanel);
    HidePanel(carePanel);
    HidePanel(connectPanel);
    HidePanel(keyboardPanel);
    HidePanel(notesPanel);
    HidePanel(noteDetailPanel);
    HidePanel(remindersPanel);
    HidePanel(reminderDetailPanel);
    HidePanel(statsPanel);
    HidePanel(settingsPanel);
    HidePanel(volumePanel);
    HidePanel(eyeEditorPanel);
}

void CreateCircularPanel() {
    if (menuPanel != nullptr) {
        return;
    }

    menuPanel = lv_obj_create(lv_screen_active());
    lv_obj_set_size(menuPanel, 240, 240);
    lv_obj_center(menuPanel);
    lv_obj_set_style_radius(menuPanel, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(menuPanel, lv_color_hex(COLOR_BACKGROUND), 0);
    lv_obj_set_style_bg_opa(menuPanel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(menuPanel, 12, 0);
    lv_obj_set_style_border_color(menuPanel, lv_color_hex(COLOR_MINT), 0);
    lv_obj_set_style_border_opa(menuPanel, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(menuPanel, 0, 0);
    lv_obj_clear_flag(menuPanel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(menuPanel, LV_OBJ_FLAG_HIDDEN);
}

void CreateMenuRoller() {
    if (menuList != nullptr) {
        return;
    }

    menuList = lv_obj_create(menuPanel);
    lv_obj_set_size(menuList, 200, 160);
    lv_obj_center(menuList);
    lv_obj_set_scroll_dir(menuList, LV_DIR_VER);
    lv_obj_set_scroll_snap_y(menuList, LV_SCROLL_SNAP_CENTER);
    lv_obj_set_scrollbar_mode(menuList, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(menuList, LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_pad_all(menuList, 0, 0);
    lv_obj_set_style_pad_row(menuList, 6, 0);
    lv_obj_set_style_bg_opa(menuList, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(menuList, 0, 0);
    lv_obj_set_flex_flow(menuList, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(menuList, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);

    for (int i = 0; i < MENU_ITEM_COUNT; ++i) {
        lv_obj_t* label = lv_label_create(menuList);
        menuItems[i] = label;
        lv_label_set_text(label, menuItemLabelTexts[i]);
        lv_obj_set_width(label, lv_pct(100));
        lv_obj_set_style_pad_all(label, 8, 0);
        lv_obj_set_style_min_height(label, 28, 0);
    }

    ScrollMenuToIndex(0, LV_ANIM_OFF);

    upButton = CreateNavButton(menuPanel, LV_ALIGN_TOP_MID, 0, -65, LV_SYMBOL_UP,
                               LV_ALIGN_CENTER, 0, 30,
                               [](lv_event_t*) { MenuSystem::SelectPrev(); });
    downButton = CreateNavButton(menuPanel, LV_ALIGN_BOTTOM_MID, 0, 65, LV_SYMBOL_DOWN,
                                 LV_ALIGN_CENTER, 0, -30,
                                 [](lv_event_t*) { MenuSystem::SelectNext(); });
}

void CreateCarePanel() {
    if (carePanel != nullptr) {
        return;
    }

    carePanel = lv_obj_create(lv_screen_active());
    lv_obj_set_size(carePanel, 240, 240);
    lv_obj_center(carePanel);
    lv_obj_set_style_radius(carePanel, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(carePanel, lv_color_hex(COLOR_BACKGROUND), 0);
    lv_obj_set_style_bg_opa(carePanel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(carePanel, 12, 0);
    lv_obj_set_style_border_color(carePanel, lv_color_hex(COLOR_MINT), 0);
    lv_obj_set_style_border_opa(carePanel, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(carePanel, 0, 0);
    lv_obj_clear_flag(carePanel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(carePanel, LV_OBJ_FLAG_HIDDEN);

    careList = lv_obj_create(carePanel);
    lv_obj_set_size(careList, 200, 150);
    lv_obj_align(careList, LV_ALIGN_CENTER, 0, 12);
    lv_obj_set_scroll_dir(careList, LV_DIR_VER);
    lv_obj_set_scroll_snap_y(careList, LV_SCROLL_SNAP_CENTER);
    lv_obj_set_scrollbar_mode(careList, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(careList, LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_pad_all(careList, 0, 0);
    lv_obj_set_style_pad_row(careList, 6, 0);
    lv_obj_set_style_bg_opa(careList, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(careList, 0, 0);
    lv_obj_set_flex_flow(careList, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(careList, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);

    for (int i = 0; i < CARE_ITEM_COUNT; ++i) {
        lv_obj_t* label = lv_label_create(careList);
        careItems[i] = label;
        lv_label_set_text(label, careItemLabelTexts[i]);
        lv_obj_set_width(label, lv_pct(100));
        lv_obj_set_style_pad_all(label, 8, 0);
        lv_obj_set_style_min_height(label, 28, 0);
    }

    ScrollCareToIndex(static_cast<uint8_t>(selectedCareItem), LV_ANIM_OFF);

    careUpButton = CreateNavButton(carePanel, LV_ALIGN_TOP_MID, 0, -65, LV_SYMBOL_UP,
                                   LV_ALIGN_CENTER, 0, 30,
                                   [](lv_event_t*) { MenuSystem::SelectCarePrev(); });
    careDownButton = CreateNavButton(carePanel, LV_ALIGN_BOTTOM_MID, 0, 65, LV_SYMBOL_DOWN,
                                     LV_ALIGN_CENTER, 0, -30,
                                     [](lv_event_t*) { MenuSystem::SelectCareNext(); });
}

void CreateConnectPanel() {
    if (connectPanel != nullptr) {
        return;
    }

    connectPanel = lv_obj_create(lv_screen_active());
    lv_obj_set_size(connectPanel, 240, 240);
    lv_obj_center(connectPanel);
    lv_obj_set_style_radius(connectPanel, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(connectPanel, lv_color_hex(COLOR_BACKGROUND), 0);
    lv_obj_set_style_bg_opa(connectPanel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(connectPanel, 12, 0);
    lv_obj_set_style_border_color(connectPanel, lv_color_hex(COLOR_MINT), 0);
    lv_obj_set_style_border_opa(connectPanel, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(connectPanel, 0, 0);
    lv_obj_clear_flag(connectPanel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(connectPanel, LV_OBJ_FLAG_HIDDEN);

    connectTitle = lv_label_create(connectPanel);
    lv_label_set_text(connectTitle, "WIFI");
    lv_obj_set_style_text_color(connectTitle, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_text_font(connectTitle, &lv_font_montserrat_vn_20, 0);
    lv_obj_align(connectTitle, LV_ALIGN_TOP_MID, 0, 26);

    connectHint = lv_label_create(connectPanel);
    lv_label_set_text(connectHint, "");
    lv_obj_set_width(connectHint, 172);
    lv_obj_set_style_text_color(connectHint, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_text_opa(connectHint, 200, 0);
    lv_obj_set_style_text_font(connectHint, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_align(connectHint, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(connectHint, LV_LABEL_LONG_WRAP);
    lv_obj_align(connectHint, LV_ALIGN_CENTER, 0, 24);
    lv_obj_add_flag(connectHint, LV_OBJ_FLAG_HIDDEN);

    connectList = lv_obj_create(connectPanel);
    lv_obj_set_size(connectList, 190, 92);
    lv_obj_align(connectList, LV_ALIGN_CENTER, 0, 20);
    lv_obj_set_scroll_dir(connectList, LV_DIR_VER);
    lv_obj_set_scroll_snap_y(connectList, LV_SCROLL_SNAP_CENTER);
    lv_obj_set_scrollbar_mode(connectList, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(connectList, LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_pad_all(connectList, 0, 0);
    lv_obj_set_style_pad_row(connectList, 10, 0);
    lv_obj_set_style_bg_opa(connectList, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(connectList, 0, 0);
    lv_obj_set_flex_flow(connectList, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(connectList, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);

    for (int i = 0; i < CONNECT_ITEM_COUNT; ++i) {
        lv_obj_t* label = lv_label_create(connectList);
        connectItems[i] = label;
        lv_label_set_text(label, connectItemLabelTexts[i]);
        lv_obj_set_width(label, lv_pct(100));
        lv_obj_set_style_pad_all(label, 8, 0);
        lv_obj_set_style_min_height(label, 30, 0);
    }

    ScrollConnectToIndex(static_cast<uint8_t>(selectedConnectItem), LV_ANIM_OFF);

    connectWifiList = lv_obj_create(connectPanel);
    lv_obj_set_size(connectWifiList, 190, 110);
    lv_obj_align(connectWifiList, LV_ALIGN_CENTER, 0, 28);
    lv_obj_set_scroll_dir(connectWifiList, LV_DIR_VER);
    lv_obj_set_scroll_snap_y(connectWifiList, LV_SCROLL_SNAP_CENTER);
    lv_obj_set_scrollbar_mode(connectWifiList, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(connectWifiList, LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_pad_all(connectWifiList, 0, 0);
    lv_obj_set_style_pad_row(connectWifiList, 6, 0);
    lv_obj_set_style_bg_opa(connectWifiList, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(connectWifiList, 0, 0);
    lv_obj_set_flex_flow(connectWifiList, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(connectWifiList, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_add_flag(connectWifiList, LV_OBJ_FLAG_HIDDEN);

    connectUpButton = CreateNavButton(connectPanel, LV_ALIGN_TOP_MID, 0, -65, LV_SYMBOL_UP,
                                      LV_ALIGN_CENTER, 0, 30,
                                      [](lv_event_t*) { MenuSystem::NavigatePrev(); });
    connectDownButton = CreateNavButton(connectPanel, LV_ALIGN_BOTTOM_MID, 0, 65, LV_SYMBOL_DOWN,
                                        LV_ALIGN_CENTER, 0, -30,
                                        [](lv_event_t*) { MenuSystem::NavigateNext(); });

    ApplyConnectView();
}

void RebuildWifiList() {
    if (connectWifiList == nullptr) {
        return;
    }

    while (lv_obj_get_child_cnt(connectWifiList) > 0) {
        lv_obj_delete(lv_obj_get_child(connectWifiList, 0));
    }
    connectWifiItems.clear();

    const auto& wifi_service = WifiConnectService::GetInstance();
    const bool scanning = wifi_service.IsScanning();
    const auto results = wifi_service.GetScanResults();

    if (scanning) {
        SetConnectHint("SCANNING....");
        lv_obj_clear_flag(connectHint, LV_OBJ_FLAG_HIDDEN);
        selectedWifiIndex = 0;
        return;
    }

    SetConnectHint("");
    lv_obj_add_flag(connectHint, LV_OBJ_FLAG_HIDDEN);

    if (results.empty()) {
        auto* label = lv_label_create(connectWifiList);
        lv_label_set_text(label, "KHÔNG CÓ WIFI");
        lv_obj_set_width(label, lv_pct(100));
        lv_obj_set_style_text_color(label, lv_color_hex(COLOR_TEXT), 0);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        connectWifiItems.push_back(label);
        selectedWifiIndex = 0;
        return;
    }

    for (const auto& result : results) {
        auto* label = lv_label_create(connectWifiList);
        std::string text = result.ssid + (result.known ? " *" : "");
        lv_label_set_text(label, text.c_str());
        lv_obj_set_width(label, lv_pct(100));
        lv_obj_set_style_pad_all(label, 8, 0);
        lv_obj_set_style_min_height(label, 28, 0);
        lv_obj_set_style_text_color(label, lv_color_hex(COLOR_TEXT), 0);
        connectWifiItems.push_back(label);
    }

    if (selectedWifiIndex >= connectWifiItems.size()) {
        selectedWifiIndex = 0;
    }
    UpdateWifiItemStyles();
    ScrollWifiToIndex(selectedWifiIndex, LV_ANIM_OFF);
}

void CreateKeyboardPanel() {
    if (keyboardPanel != nullptr) {
        return;
    }

    keyboardPanel = lv_obj_create(lv_screen_active());
    lv_obj_set_size(keyboardPanel, 240, 240);
    lv_obj_center(keyboardPanel);
    lv_obj_set_style_radius(keyboardPanel, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(keyboardPanel, lv_color_hex(COLOR_BACKGROUND), 0);
    lv_obj_set_style_bg_opa(keyboardPanel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(keyboardPanel, 12, 0);
    lv_obj_set_style_border_color(keyboardPanel, lv_color_hex(COLOR_MINT), 0);
    lv_obj_set_style_border_opa(keyboardPanel, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(keyboardPanel, 0, 0);
    lv_obj_clear_flag(keyboardPanel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(keyboardPanel, LV_OBJ_FLAG_HIDDEN);

    keyboardTitle = lv_label_create(keyboardPanel);
    lv_label_set_text(keyboardTitle, "PASSWORD");
    lv_obj_set_style_text_color(keyboardTitle, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_text_font(keyboardTitle, &lv_font_montserrat_14, 0);
    lv_obj_align(keyboardTitle, LV_ALIGN_TOP_MID, 0, 8);

    keyboardSsid = lv_label_create(keyboardPanel);
    lv_label_set_text(keyboardSsid, "");
    lv_obj_set_width(keyboardSsid, 180);
    lv_obj_set_style_text_color(keyboardSsid, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_text_align(keyboardSsid, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(keyboardSsid, &lv_font_montserrat_14, 0);
    lv_obj_align(keyboardSsid, LV_ALIGN_TOP_MID, 0, 30);

    keyboardValue = lv_label_create(keyboardPanel);
    lv_label_set_text(keyboardValue, "");
    lv_obj_set_width(keyboardValue, 180);
    lv_label_set_long_mode(keyboardValue, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(keyboardValue, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_text_align(keyboardValue, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(keyboardValue, &lv_font_montserrat_14, 0);
    lv_obj_align(keyboardValue, LV_ALIGN_TOP_MID, 0, 52);

    constexpr lv_coord_t kButtonWidth = 66;
    constexpr lv_coord_t kButtonHeight = 26;
    constexpr lv_coord_t kGapX = 6;
    constexpr lv_coord_t kGapY = 4;
    constexpr lv_coord_t kGridWidth = kButtonWidth * 3 + kGapX * 2;
    constexpr lv_coord_t kGridHeight = kButtonHeight * 5 + kGapY * 4;

    keyboardGrid = lv_obj_create(keyboardPanel);
    lv_obj_set_size(keyboardGrid, kGridWidth, kGridHeight);
    lv_obj_align(keyboardGrid, LV_ALIGN_TOP_MID, 0, 92);
    lv_obj_set_style_bg_opa(keyboardGrid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(keyboardGrid, 0, 0);
    lv_obj_set_style_pad_all(keyboardGrid, 0, 0);
    lv_obj_clear_flag(keyboardGrid, LV_OBJ_FLAG_SCROLLABLE);

    for (size_t i = 0; i < keyboardButtons.size(); ++i) {
        auto* button = lv_btn_create(keyboardGrid);
        keyboardButtons[i] = button;
        lv_obj_set_size(button, kButtonWidth, kButtonHeight);

        const int row = static_cast<int>(i / 3);
        const int col = static_cast<int>(i % 3);
        const lv_coord_t x = col * (kButtonWidth + kGapX);
        const lv_coord_t y = row * (kButtonHeight + kGapY);
        lv_obj_set_pos(button, x, y);
        lv_obj_set_style_bg_color(button, lv_color_hex(0x111111), 0);
        lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(button, 0, 0);
        lv_obj_set_style_shadow_width(button, 0, 0);
        lv_obj_set_style_radius(button, 12, 0);

        auto* label = lv_label_create(button);
        lv_label_set_text(label, keyboardButtonTexts[i]);
        lv_obj_set_style_text_color(label, lv_color_hex(COLOR_TEXT), 0);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
        lv_obj_center(label);
    }
}

void CreateSettingsPanel() {
    if (settingsPanel != nullptr) {
        return;
    }

    settingsPanel = lv_obj_create(lv_screen_active());
    lv_obj_set_size(settingsPanel, 240, 240);
    lv_obj_center(settingsPanel);
    lv_obj_set_style_radius(settingsPanel, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(settingsPanel, lv_color_hex(COLOR_BACKGROUND), 0);
    lv_obj_set_style_bg_opa(settingsPanel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(settingsPanel, 12, 0);
    lv_obj_set_style_border_color(settingsPanel, lv_color_hex(COLOR_MINT), 0);
    lv_obj_set_style_border_opa(settingsPanel, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(settingsPanel, 0, 0);
    lv_obj_clear_flag(settingsPanel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(settingsPanel, LV_OBJ_FLAG_HIDDEN);

    settingsTitle = lv_label_create(settingsPanel);
    lv_label_set_text(settingsTitle, "SETTINGS");
    lv_obj_set_style_text_color(settingsTitle, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_text_font(settingsTitle, &lv_font_montserrat_vn_20, 0);
    lv_obj_align(settingsTitle, LV_ALIGN_TOP_MID, 0, 44);

    settingsItems[SETTINGS_VOLUME] = lv_label_create(settingsPanel);
    lv_label_set_text(settingsItems[SETTINGS_VOLUME], "VOLUME");
    lv_obj_set_width(settingsItems[SETTINGS_VOLUME], 180);
    lv_obj_set_style_text_align(settingsItems[SETTINGS_VOLUME], LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(settingsItems[SETTINGS_VOLUME], LV_ALIGN_CENTER, 0, 0);

    UpdateSettingsItemStyles();
}

void CreateNotesPanel() {
    if (notesPanel != nullptr) {
        return;
    }

    notesPanel = lv_obj_create(lv_screen_active());
    lv_obj_set_size(notesPanel, 240, 240);
    lv_obj_center(notesPanel);
    lv_obj_set_style_radius(notesPanel, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(notesPanel, lv_color_hex(COLOR_BACKGROUND), 0);
    lv_obj_set_style_bg_opa(notesPanel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(notesPanel, 12, 0);
    lv_obj_set_style_border_color(notesPanel, lv_color_hex(COLOR_MINT), 0);
    lv_obj_set_style_border_opa(notesPanel, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(notesPanel, 0, 0);
    lv_obj_clear_flag(notesPanel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(notesPanel, LV_OBJ_FLAG_HIDDEN);

    notesTitle = lv_label_create(notesPanel);
    lv_label_set_text(notesTitle, "NOTES");
    lv_obj_set_style_text_color(notesTitle, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_text_font(notesTitle, &lv_font_montserrat_vn_20, 0);
    lv_obj_align(notesTitle, LV_ALIGN_TOP_MID, 0, 26);

    notesList = lv_obj_create(notesPanel);
    lv_obj_set_size(notesList, 190, 110);
    lv_obj_align(notesList, LV_ALIGN_CENTER, 0, 22);
    lv_obj_set_scroll_dir(notesList, LV_DIR_VER);
    lv_obj_set_scroll_snap_y(notesList, LV_SCROLL_SNAP_CENTER);
    lv_obj_set_scrollbar_mode(notesList, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(notesList, LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_pad_all(notesList, 0, 0);
    lv_obj_set_style_pad_row(notesList, 6, 0);
    lv_obj_set_style_bg_opa(notesList, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(notesList, 0, 0);
    lv_obj_set_flex_flow(notesList, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(notesList, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);

    notesUpButton = CreateNavButton(notesPanel, LV_ALIGN_TOP_MID, 0, -65, LV_SYMBOL_UP,
                                    LV_ALIGN_CENTER, 0, 30,
                                    [](lv_event_t*) { MenuSystem::SelectNotesPrev(); });
    notesDownButton = CreateNavButton(notesPanel, LV_ALIGN_BOTTOM_MID, 0, 65, LV_SYMBOL_DOWN,
                                      LV_ALIGN_CENTER, 0, -30,
                                      [](lv_event_t*) { MenuSystem::SelectNotesNext(); });

    noteDetailPanel = lv_obj_create(lv_screen_active());
    lv_obj_set_size(noteDetailPanel, 240, 240);
    lv_obj_center(noteDetailPanel);
    lv_obj_set_style_radius(noteDetailPanel, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(noteDetailPanel, lv_color_hex(COLOR_BACKGROUND), 0);
    lv_obj_set_style_bg_opa(noteDetailPanel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(noteDetailPanel, 12, 0);
    lv_obj_set_style_border_color(noteDetailPanel, lv_color_hex(COLOR_MINT), 0);
    lv_obj_set_style_border_opa(noteDetailPanel, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(noteDetailPanel, 0, 0);
    lv_obj_clear_flag(noteDetailPanel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(noteDetailPanel, LV_OBJ_FLAG_HIDDEN);

    noteDetailTitle = lv_label_create(noteDetailPanel);
    lv_label_set_text(noteDetailTitle, "NOTE");
    lv_obj_set_width(noteDetailTitle, 180);
    lv_obj_set_style_text_color(noteDetailTitle, lv_color_hex(COLOR_MINT), 0);
    lv_obj_set_style_text_font(noteDetailTitle, &lv_font_montserrat_vn_20, 0);
    lv_obj_set_style_text_align(noteDetailTitle, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(noteDetailTitle, LV_LABEL_LONG_DOT);
    lv_obj_align(noteDetailTitle, LV_ALIGN_TOP_MID, 0, 22);

    noteDetailValue = lv_label_create(noteDetailPanel);
    lv_label_set_text(noteDetailValue, "");
    lv_obj_set_width(noteDetailValue, 176);
    lv_obj_set_style_text_color(noteDetailValue, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_text_font(noteDetailValue, &lv_font_montserrat_vn_20, 0);
    lv_obj_set_style_text_align(noteDetailValue, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(noteDetailValue, LV_LABEL_LONG_WRAP);
    lv_obj_align(noteDetailValue, LV_ALIGN_CENTER, 0, 8);

    noteDetailHint = lv_label_create(noteDetailPanel);
    lv_label_set_text(noteDetailHint, "TAP/POWER: BACK");
    lv_obj_set_width(noteDetailHint, 186);
    lv_obj_set_style_text_color(noteDetailHint, lv_color_hex(0xA8BBC2), 0);
    lv_obj_set_style_text_font(noteDetailHint, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_align(noteDetailHint, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(noteDetailHint, LV_LABEL_LONG_WRAP);
    lv_obj_align(noteDetailHint, LV_ALIGN_BOTTOM_MID, 0, -20);

    RebuildNotesList();
}

void CreateRemindersPanel() {
    if (remindersPanel != nullptr) {
        return;
    }

    remindersPanel = lv_obj_create(lv_screen_active());
    lv_obj_set_size(remindersPanel, 240, 240);
    lv_obj_center(remindersPanel);
    lv_obj_set_style_radius(remindersPanel, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(remindersPanel, lv_color_hex(COLOR_BACKGROUND), 0);
    lv_obj_set_style_bg_opa(remindersPanel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(remindersPanel, 12, 0);
    lv_obj_set_style_border_color(remindersPanel, lv_color_hex(COLOR_MINT), 0);
    lv_obj_set_style_border_opa(remindersPanel, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(remindersPanel, 0, 0);
    lv_obj_clear_flag(remindersPanel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(remindersPanel, LV_OBJ_FLAG_HIDDEN);

    remindersTitle = lv_label_create(remindersPanel);
    lv_label_set_text(remindersTitle, "REMINDERS");
    lv_obj_set_style_text_color(remindersTitle, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_text_font(remindersTitle, &lv_font_montserrat_vn_20, 0);
    lv_obj_align(remindersTitle, LV_ALIGN_TOP_MID, 0, 26);

    remindersList = lv_obj_create(remindersPanel);
    lv_obj_set_size(remindersList, 190, 110);
    lv_obj_align(remindersList, LV_ALIGN_CENTER, 0, 22);
    lv_obj_set_scroll_dir(remindersList, LV_DIR_VER);
    lv_obj_set_scroll_snap_y(remindersList, LV_SCROLL_SNAP_CENTER);
    lv_obj_set_scrollbar_mode(remindersList, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(remindersList, LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_pad_all(remindersList, 0, 0);
    lv_obj_set_style_pad_row(remindersList, 6, 0);
    lv_obj_set_style_bg_opa(remindersList, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(remindersList, 0, 0);
    lv_obj_set_flex_flow(remindersList, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(remindersList, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);

    remindersUpButton = CreateNavButton(remindersPanel, LV_ALIGN_TOP_MID, 0, -65, LV_SYMBOL_UP,
                                        LV_ALIGN_CENTER, 0, 30,
                                        [](lv_event_t*) { MenuSystem::SelectRemindersPrev(); });
    remindersDownButton = CreateNavButton(remindersPanel, LV_ALIGN_BOTTOM_MID, 0, 65, LV_SYMBOL_DOWN,
                                          LV_ALIGN_CENTER, 0, -30,
                                          [](lv_event_t*) { MenuSystem::SelectRemindersNext(); });

    reminderDetailPanel = lv_obj_create(lv_screen_active());
    lv_obj_set_size(reminderDetailPanel, 240, 240);
    lv_obj_center(reminderDetailPanel);
    lv_obj_set_style_radius(reminderDetailPanel, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(reminderDetailPanel, lv_color_hex(COLOR_BACKGROUND), 0);
    lv_obj_set_style_bg_opa(reminderDetailPanel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(reminderDetailPanel, 12, 0);
    lv_obj_set_style_border_color(reminderDetailPanel, lv_color_hex(COLOR_MINT), 0);
    lv_obj_set_style_border_opa(reminderDetailPanel, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(reminderDetailPanel, 0, 0);
    lv_obj_clear_flag(reminderDetailPanel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(reminderDetailPanel, LV_OBJ_FLAG_HIDDEN);

    reminderDetailTitle = lv_label_create(reminderDetailPanel);
    lv_label_set_text(reminderDetailTitle, "REMINDER");
    lv_obj_set_width(reminderDetailTitle, 180);
    lv_obj_set_style_text_color(reminderDetailTitle, lv_color_hex(COLOR_MINT), 0);
    lv_obj_set_style_text_font(reminderDetailTitle, &lv_font_montserrat_vn_20, 0);
    lv_obj_set_style_text_align(reminderDetailTitle, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(reminderDetailTitle, LV_LABEL_LONG_DOT);
    lv_obj_align(reminderDetailTitle, LV_ALIGN_TOP_MID, 0, 22);

    reminderDetailValue = lv_label_create(reminderDetailPanel);
    lv_label_set_text(reminderDetailValue, "");
    lv_obj_set_width(reminderDetailValue, 176);
    lv_obj_set_style_text_color(reminderDetailValue, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_text_font(reminderDetailValue, &lv_font_montserrat_vn_20, 0);
    lv_obj_set_style_text_align(reminderDetailValue, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(reminderDetailValue, LV_LABEL_LONG_WRAP);
    lv_obj_align(reminderDetailValue, LV_ALIGN_CENTER, 0, 8);

    reminderDetailHint = lv_label_create(reminderDetailPanel);
    lv_label_set_text(reminderDetailHint, "TAP/POWER: BACK");
    lv_obj_set_width(reminderDetailHint, 186);
    lv_obj_set_style_text_color(reminderDetailHint, lv_color_hex(0xA8BBC2), 0);
    lv_obj_set_style_text_font(reminderDetailHint, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_align(reminderDetailHint, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(reminderDetailHint, LV_LABEL_LONG_WRAP);
    lv_obj_align(reminderDetailHint, LV_ALIGN_BOTTOM_MID, 0, -20);

    RebuildRemindersList();
}

void CreateVolumePanel() {
    if (volumePanel != nullptr) {
        return;
    }

    volumePanel = lv_obj_create(lv_screen_active());
    lv_obj_set_size(volumePanel, 240, 240);
    lv_obj_center(volumePanel);
    lv_obj_set_style_radius(volumePanel, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(volumePanel, lv_color_hex(COLOR_BACKGROUND), 0);
    lv_obj_set_style_bg_opa(volumePanel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(volumePanel, 12, 0);
    lv_obj_set_style_border_color(volumePanel, lv_color_hex(COLOR_MINT), 0);
    lv_obj_set_style_border_opa(volumePanel, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(volumePanel, 0, 0);
    lv_obj_clear_flag(volumePanel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(volumePanel, LV_OBJ_FLAG_HIDDEN);

    volumeTitle = lv_label_create(volumePanel);
    lv_label_set_text(volumeTitle, "VOLUME");
    lv_obj_set_style_text_color(volumeTitle, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_text_font(volumeTitle, &lv_font_montserrat_vn_20, 0);
    lv_obj_align(volumeTitle, LV_ALIGN_TOP_MID, 0, 34);

    volumeValueLabel = lv_label_create(volumePanel);
    lv_label_set_text(volumeValueLabel, "0");
    lv_obj_set_style_text_color(volumeValueLabel, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_text_font(volumeValueLabel, &lv_font_montserrat_vn_28, 0);
    lv_obj_align(volumeValueLabel, LV_ALIGN_CENTER, 0, -8);

    volumeMinusButton = CreateTextButton(volumePanel, "-", 58, 42, &lv_font_montserrat_vn_22);
    lv_obj_align(volumeMinusButton, LV_ALIGN_CENTER, -70, -6);

    volumePlusButton = CreateTextButton(volumePanel, "+", 58, 42, &lv_font_montserrat_vn_22);
    lv_obj_align(volumePlusButton, LV_ALIGN_CENTER, 70, -6);

    volumeBackButton = CreateTextButton(volumePanel, "BACK", 72, 30);
    lv_obj_align(volumeBackButton, LV_ALIGN_BOTTOM_MID, 0, -22);

    UpdateVolumeValueLabel();
}

void CreateEyeEditorPanel() {
    if (eyeEditorPanel != nullptr) {
        return;
    }

    eyeEditorPanel = lv_obj_create(lv_screen_active());
    lv_obj_set_size(eyeEditorPanel, 240, 240);
    lv_obj_center(eyeEditorPanel);
    lv_obj_set_style_bg_opa(eyeEditorPanel, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(eyeEditorPanel, 0, 0);
    lv_obj_set_style_pad_all(eyeEditorPanel, 0, 0);
    lv_obj_clear_flag(eyeEditorPanel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(eyeEditorPanel, LV_OBJ_FLAG_HIDDEN);

    static const char* kModeTexts[EYE_EDITOR_MODE_COUNT] = {
        "|---|",
        "|\n|\n|",
        "L_",
    };
    constexpr lv_coord_t kModeButtonW = 42;
    constexpr lv_coord_t kModeButtonH = 34;
    constexpr lv_coord_t kModeGap = 10;
    constexpr lv_coord_t kTopY = 10;
    const lv_coord_t total_w = kModeButtonW * EYE_EDITOR_MODE_COUNT + kModeGap * (EYE_EDITOR_MODE_COUNT - 1);
    const lv_coord_t start_x = (240 - total_w) / 2;
    for (int i = 0; i < EYE_EDITOR_MODE_COUNT; ++i) {
        auto* button = lv_obj_create(eyeEditorPanel);
        eyeEditorModeButtons[i] = button;
        lv_obj_set_size(button, kModeButtonW, kModeButtonH);
        lv_obj_set_pos(button, start_x + i * (kModeButtonW + kModeGap), kTopY);
        lv_obj_set_style_radius(button, 12, 0);
        lv_obj_set_style_pad_all(button, 0, 0);
        lv_obj_set_style_shadow_width(button, 0, 0);
        auto* label = lv_label_create(button);
        eyeEditorModeLabels[i] = label;
        lv_label_set_text(label, kModeTexts[i]);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(label);
    }

    eyeEditorSelectionOutline = lv_obj_create(eyeEditorPanel);
    lv_obj_set_style_bg_opa(eyeEditorSelectionOutline, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(eyeEditorSelectionOutline, 2, 0);
    lv_obj_set_style_border_color(eyeEditorSelectionOutline, lv_color_hex(COLOR_MINT), 0);
    lv_obj_set_style_outline_width(eyeEditorSelectionOutline, 2, 0);
    lv_obj_set_style_outline_color(eyeEditorSelectionOutline, lv_color_hex(COLOR_MINT), 0);
    lv_obj_set_style_outline_opa(eyeEditorSelectionOutline, 120, 0);
    lv_obj_set_style_radius(eyeEditorSelectionOutline, 18, 0);
    lv_obj_set_style_pad_all(eyeEditorSelectionOutline, 0, 0);
    lv_obj_clear_flag(eyeEditorSelectionOutline, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(eyeEditorSelectionOutline, LV_OBJ_FLAG_HIDDEN);

    eyeEditorMinusButton = CreateTextButton(eyeEditorPanel, "-", 44, 32, &lv_font_montserrat_vn_22);
    lv_obj_align(eyeEditorMinusButton, LV_ALIGN_BOTTOM_MID, -70, -54);
    eyeEditorValueLabel = lv_label_create(eyeEditorPanel);
    lv_label_set_text(eyeEditorValueLabel, "80");
    lv_obj_set_style_text_color(eyeEditorValueLabel, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_text_font(eyeEditorValueLabel, &lv_font_montserrat_vn_22, 0);
    lv_obj_align(eyeEditorValueLabel, LV_ALIGN_BOTTOM_MID, 0, -48);
    eyeEditorPlusButton = CreateTextButton(eyeEditorPanel, "+", 44, 32, &lv_font_montserrat_vn_22);
    lv_obj_align(eyeEditorPlusButton, LV_ALIGN_BOTTOM_MID, 70, -54);

    eyeEditorSaveButton = CreateTextButton(eyeEditorPanel, "SAVE", 58, 28);
    lv_obj_align(eyeEditorSaveButton, LV_ALIGN_BOTTOM_LEFT, 16, -14);
    eyeEditorResetButton = CreateTextButton(eyeEditorPanel, "RESET", 70, 28);
    lv_obj_align(eyeEditorResetButton, LV_ALIGN_BOTTOM_MID, 0, -14);
    eyeEditorBackButton = CreateTextButton(eyeEditorPanel, "BACK", 58, 28);
    lv_obj_align(eyeEditorBackButton, LV_ALIGN_BOTTOM_RIGHT, -16, -14);

    UpdateEyeEditorModeStyles();
    UpdateEyeEditorValueLabel();
}

void CreateStatsPanel() {
    if (statsPanel != nullptr) {
        return;
    }

    statsPanel = lv_obj_create(lv_screen_active());
    lv_obj_set_size(statsPanel, 240, 240);
    lv_obj_center(statsPanel);
    lv_obj_set_style_radius(statsPanel, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(statsPanel, lv_color_hex(COLOR_BACKGROUND), 0);
    lv_obj_set_style_bg_opa(statsPanel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_opa(statsPanel, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(statsPanel, 0, 0);
    lv_obj_clear_flag(statsPanel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(statsPanel, LV_OBJ_FLAG_HIDDEN);

    statsTitle = lv_label_create(statsPanel);
    lv_obj_set_style_text_color(statsTitle, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_text_font(statsTitle, &lv_font_montserrat_vn_22, 0);
    lv_label_set_text(statsTitle, statNames[0]);
    lv_obj_align(statsTitle, LV_ALIGN_CENTER, 0, 0);

    statsArc = lv_arc_create(statsPanel);
    lv_obj_set_size(statsArc, 240, 240);
    lv_obj_center(statsArc);
    lv_arc_set_rotation(statsArc, 135);
    lv_arc_set_bg_angles(statsArc, 0, 270);
    lv_arc_set_mode(statsArc, LV_ARC_MODE_NORMAL);
    lv_arc_set_range(statsArc, 0, 100);
    lv_obj_clear_flag(statsArc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_style(statsArc, nullptr, LV_PART_KNOB);
    lv_obj_set_style_arc_width(statsArc, 16, LV_PART_MAIN);
    lv_obj_set_style_arc_width(statsArc, 16, LV_PART_INDICATOR);

    statsLeftBtn = CreateNavButton(statsPanel, LV_ALIGN_LEFT_MID, -50, 0, LV_SYMBOL_LEFT,
                                   LV_ALIGN_CENTER, 28, 0,
                                   [](lv_event_t*) { MenuSystem::StatsPrev(); });
    statsRightBtn = CreateNavButton(statsPanel, LV_ALIGN_RIGHT_MID, 50, 0, LV_SYMBOL_RIGHT,
                                    LV_ALIGN_CENTER, -28, 0,
                                    [](lv_event_t*) { MenuSystem::StatsNext(); });

    lv_obj_move_background(statsLeftBtn);
    lv_obj_move_background(statsRightBtn);

    statsActionZone = lv_obj_create(statsPanel);
    lv_obj_set_size(statsActionZone, 120, 120);
    lv_obj_center(statsActionZone);
    lv_obj_set_style_bg_opa(statsActionZone, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(statsActionZone, LV_OPA_TRANSP, 0);
    lv_obj_set_style_outline_opa(statsActionZone, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(statsActionZone, 0, 0);
    lv_obj_clear_flag(statsActionZone, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(statsActionZone, LV_OBJ_FLAG_CLICKABLE);

    UpdateStatsUI();
}

}  // namespace

namespace MenuSystem {

void Begin(Display* display) {
    displayHandle = display;
    MarkMenuActivity();
    EnsureFeedingTimerCreated();
    ESP_LOGI(TAG, "Initializing menu system...");
    CreateCircularPanel();
    CreateMenuRoller();
    CreateCarePanel();
    CreateConnectPanel();
    CreateKeyboardPanel();
    CreateSettingsPanel();
    CreateNotesPanel();
    CreateRemindersPanel();
    CreateVolumePanel();
    CreateEyeEditorPanel();
    CreateStatsPanel();
    ESP_LOGI(TAG, "Menu system ready");
}

void Open() {
    if (currentState == MENU_OPEN) {
        return;
    }

    DisplayLockGuard lock(displayHandle);
    HideAllPanels();
    ShowPanel(menuPanel);
    currentState = MENU_OPEN;
    selectedItem = MENU_CARE;
    connectView = CONNECT_VIEW_METHODS;
    ScrollMenuToIndex(0, LV_ANIM_OFF);
    ApplyConnectView();
    MarkMenuActivity();
}

void Close() {
    if (currentState == MENU_CLOSED) {
        return;
    }

    if (currentState == MENU_VOLUME_OPEN) {
        CommitVolumeIfPending();
    }

    if (currentState == MENU_EYE_EDITOR_OPEN && eyeEditorDirty) {
        RestoreEyeEditorSavedShapes();
    }

    DisplayLockGuard lock(displayHandle);
    StopFeedingAnimationLocked();
    HideAllPanels();
    currentState = MENU_CLOSED;
    statsOpenedFromCare = false;
    connectView = CONNECT_VIEW_METHODS;
}

bool IsOpen() {
    return currentState == MENU_OPEN;
}

bool IsAnyOpen() {
    return currentState != MENU_CLOSED;
}

MenuState GetState() {
    return currentState;
}

MenuItem GetSelected() {
    return selectedItem;
}

void SelectNext() {
    if (currentState != MENU_OPEN) {
        return;
    }
    MarkMenuActivity();
    int current = static_cast<int>(selectedItem);
    if (current < MENU_ITEM_COUNT - 1) {
        DisplayLockGuard lock(displayHandle);
        ScrollMenuToIndex(static_cast<uint8_t>(current + 1), LV_ANIM_ON);
    }
}

void SelectPrev() {
    if (currentState != MENU_OPEN) {
        return;
    }
    MarkMenuActivity();
    int current = static_cast<int>(selectedItem);
    if (current > 0) {
        DisplayLockGuard lock(displayHandle);
        ScrollMenuToIndex(static_cast<uint8_t>(current - 1), LV_ANIM_ON);
    }
}

void ActivateSelected() {
    if (currentState != MENU_OPEN) {
        return;
    }

    MarkMenuActivity();

    switch (selectedItem) {
        case MENU_CARE: {
            DisplayLockGuard lock(displayHandle);
            HidePanel(menuPanel);
            ShowPanel(carePanel);
            currentState = MENU_CARE_OPEN;
            ScrollCareToIndex(static_cast<uint8_t>(selectedCareItem), LV_ANIM_OFF);
            break;
        }
        case MENU_CONNECT: {
            DisplayLockGuard lock(displayHandle);
            HidePanel(menuPanel);
            ShowPanel(connectPanel);
            currentState = MENU_CONNECT_OPEN;
            connectView = CONNECT_VIEW_METHODS;
            selectedConnectItem = CONNECT_PHONE;
            ApplyConnectView();
            ScrollConnectToIndex(static_cast<uint8_t>(selectedConnectItem), LV_ANIM_OFF);
            break;
        }
        case MENU_REMINDERS: {
            DisplayLockGuard lock(displayHandle);
            RebuildRemindersList();
            HidePanel(menuPanel);
            ShowPanel(remindersPanel);
            currentState = MENU_REMINDERS_OPEN;
            ScrollRemindersToIndex(selectedReminderIndex, LV_ANIM_OFF);
            break;
        }
        case MENU_NOTES: {
            DisplayLockGuard lock(displayHandle);
            RebuildNotesList();
            HidePanel(menuPanel);
            ShowPanel(notesPanel);
            currentState = MENU_NOTES_OPEN;
            ScrollNotesToIndex(selectedNoteIndex, LV_ANIM_OFF);
            break;
        }
        case MENU_SETTINGS: {
            DisplayLockGuard lock(displayHandle);
            HidePanel(menuPanel);
            ShowPanel(settingsPanel);
            currentState = MENU_SETTINGS_OPEN;
            selectedSettingsItem = SETTINGS_VOLUME;
            UpdateSettingsItemStyles();
            break;
        }
        case MENU_ITEM_COUNT:
            break;
    }
}

void NavigateNext() {
    if (currentState != MENU_CLOSED) {
        MarkMenuActivity();
    }
    switch (currentState) {
        case MENU_OPEN:
            SelectNext();
            break;
        case MENU_CARE_OPEN:
            SelectCareNext();
            break;
        case MENU_CONNECT_OPEN:
            if (connectView == CONNECT_VIEW_METHODS) {
                if (selectedConnectItem < CONNECT_ITEM_COUNT - 1) {
                    DisplayLockGuard lock(displayHandle);
                    ScrollConnectToIndex(static_cast<uint8_t>(selectedConnectItem + 1), LV_ANIM_ON);
                }
            } else if (!connectWifiItems.empty() && selectedWifiIndex + 1 < connectWifiItems.size()) {
                DisplayLockGuard lock(displayHandle);
                ScrollWifiToIndex(selectedWifiIndex + 1, LV_ANIM_ON);
            }
            break;
        case MENU_STATS_OPEN:
            StatsNext();
            break;
        case MENU_VOLUME_OPEN:
            VolumeStep(true);
            break;
        case MENU_EYE_EDITOR_OPEN:
            EyeEditorCycleMode(true);
            break;
        case MENU_SETTINGS_OPEN:
            if (selectedSettingsItem < SETTINGS_ITEM_COUNT - 1) {
                selectedSettingsItem = static_cast<SettingsItem>(selectedSettingsItem + 1);
                DisplayLockGuard lock(displayHandle);
                UpdateSettingsItemStyles();
            }
            break;
        case MENU_REMINDERS_OPEN:
            SelectRemindersNext();
            break;
        case MENU_REMINDER_DETAIL_OPEN:
            RemindersDetailNext();
            break;
        case MENU_NOTES_OPEN:
            SelectNotesNext();
            break;
        case MENU_NOTE_DETAIL_OPEN:
            NotesDetailNext();
            break;
        default:
            break;
    }
}

void NavigatePrev() {
    if (currentState != MENU_CLOSED) {
        MarkMenuActivity();
    }
    switch (currentState) {
        case MENU_OPEN:
            SelectPrev();
            break;
        case MENU_CARE_OPEN:
            SelectCarePrev();
            break;
        case MENU_CONNECT_OPEN:
            if (connectView == CONNECT_VIEW_METHODS) {
                if (selectedConnectItem > 0) {
                    DisplayLockGuard lock(displayHandle);
                    ScrollConnectToIndex(static_cast<uint8_t>(selectedConnectItem - 1), LV_ANIM_ON);
                }
            } else if (!connectWifiItems.empty() && selectedWifiIndex > 0) {
                DisplayLockGuard lock(displayHandle);
                ScrollWifiToIndex(selectedWifiIndex - 1, LV_ANIM_ON);
            }
            break;
        case MENU_STATS_OPEN:
            StatsPrev();
            break;
        case MENU_VOLUME_OPEN:
            VolumeStep(false);
            break;
        case MENU_EYE_EDITOR_OPEN:
            EyeEditorCycleMode(false);
            break;
        case MENU_SETTINGS_OPEN:
            if (selectedSettingsItem > 0) {
                selectedSettingsItem = static_cast<SettingsItem>(selectedSettingsItem - 1);
                DisplayLockGuard lock(displayHandle);
                UpdateSettingsItemStyles();
            }
            break;
        case MENU_REMINDERS_OPEN:
            SelectRemindersPrev();
            break;
        case MENU_REMINDER_DETAIL_OPEN:
            RemindersDetailPrev();
            break;
        case MENU_NOTES_OPEN:
            SelectNotesPrev();
            break;
        case MENU_NOTE_DETAIL_OPEN:
            NotesDetailPrev();
            break;
        default:
            break;
    }
}

void ActivateCurrent() {
    if (currentState != MENU_CLOSED) {
        MarkMenuActivity();
    }
    switch (currentState) {
        case MENU_OPEN:
            ActivateSelected();
            break;
        case MENU_CARE_OPEN:
            ActivateCareSelected();
            break;
        case MENU_CONNECT_OPEN:
            HandleConnectTap(0, 0);
            break;
        case MENU_KEYBOARD_OPEN:
            break;
        case MENU_STATS_OPEN:
            ActivateCurrentOption();
            break;
        case MENU_SETTINGS_OPEN:
            switch (selectedSettingsItem) {
                case SETTINGS_VOLUME:
                    OpenVolumePanel();
                    break;
                case SETTINGS_ITEM_COUNT:
                    break;
            }
            break;
        case MENU_VOLUME_OPEN:
            VolumeStep(true);
            break;
        case MENU_EYE_EDITOR_OPEN:
            EyeEditorApplyIncrement();
            break;
        case MENU_REMINDERS_OPEN:
            ActivateRemindersSelected();
            break;
        case MENU_REMINDER_DETAIL_OPEN:
            CloseReminderDetailToReminders();
            break;
        case MENU_NOTES_OPEN:
            ActivateNotesSelected();
            break;
        case MENU_NOTE_DETAIL_OPEN:
            CloseNoteDetailToNotes();
            break;
        default:
            break;
    }
}

void SelectCareNext() {
    if (currentState != MENU_CARE_OPEN) {
        return;
    }
    MarkMenuActivity();
    int current = static_cast<int>(selectedCareItem);
    if (current < CARE_ITEM_COUNT - 1) {
        DisplayLockGuard lock(displayHandle);
        ScrollCareToIndex(static_cast<uint8_t>(current + 1), LV_ANIM_ON);
    }
}

void SelectCarePrev() {
    if (currentState != MENU_CARE_OPEN) {
        return;
    }
    MarkMenuActivity();
    int current = static_cast<int>(selectedCareItem);
    if (current > 0) {
        DisplayLockGuard lock(displayHandle);
        ScrollCareToIndex(static_cast<uint8_t>(current - 1), LV_ANIM_ON);
    }
}

void ActivateCareSelected() {
    if (currentState != MENU_CARE_OPEN) {
        return;
    }

    MarkMenuActivity();

    switch (selectedCareItem) {
        case CARE_FEED:
            CareSystem::AddHunger(CareSystem::kSandwichBoost);
            TriggerFeedingAnimationOrNotify();
            break;
        case CARE_PLAY:
            CareSystem::AddMood(CareSystem::kGamesBoost);
            break;
        case CARE_CLEAN:
            CareSystem::AddCleanliness(CareSystem::kCleanAnimBoost);
            break;
        case CARE_SLEEP:
            CareSystem::AddEnergy(CareSystem::kSleepBoost);
            break;
        case CARE_STATS:
            ShowStats();
            return;
        case CARE_LEVEL:
            if (displayHandle != nullptr) {
                displayHandle->ShowNotification("Level screen coming soon");
            }
            break;
        case CARE_ITEM_COUNT:
            break;
    }
}

void CloseCareToMenu() {
    if (currentState != MENU_CARE_OPEN) {
        return;
    }

    DisplayLockGuard lock(displayHandle);
    HidePanel(carePanel);
    ShowPanel(menuPanel);
    currentState = MENU_OPEN;
    selectedItem = MENU_CARE;
    ScrollMenuToIndex(static_cast<uint8_t>(selectedItem), LV_ANIM_OFF);
}

bool HandleConnectTap(uint16_t x, uint16_t y) {
    if (currentState != MENU_CONNECT_OPEN) {
        return false;
    }

    MarkMenuActivity();

    if (connectView == CONNECT_VIEW_METHODS) {
        if (x != 0 || y != 0) {
            if (IsPointInside(connectItems[CONNECT_PHONE], x, y)) {
                selectedConnectItem = CONNECT_PHONE;
                DisplayLockGuard lock(displayHandle);
                ScrollConnectToIndex(static_cast<uint8_t>(selectedConnectItem), LV_ANIM_ON);
            } else if (IsPointInside(connectItems[CONNECT_DEVICE], x, y)) {
                selectedConnectItem = CONNECT_DEVICE;
                DisplayLockGuard lock(displayHandle);
                ScrollConnectToIndex(static_cast<uint8_t>(selectedConnectItem), LV_ANIM_ON);
            } else {
                return false;
            }
        }

        switch (selectedConnectItem) {
            case CONNECT_PHONE: {
                auto* wifi_board = dynamic_cast<WifiBoard*>(&Board::GetInstance());
                if (wifi_board != nullptr) {
                    wifi_board->EnterWifiConfigMode();
                    DisplayLockGuard lock(displayHandle);
                    connectView = CONNECT_VIEW_PHONE_HELP;
                    SetConnectHint("Kết nối điện thoại vào WiFi Bubu,\nsau đó mở 192.168.4.1\nvà chọn mạng WiFi của bạn.");
                    ApplyConnectView();
                } else if (displayHandle != nullptr) {
                    displayHandle->ShowNotification("WiFi config unsupported on this board");
                }
                return true;
            }
            case CONNECT_DEVICE: {
                DisplayLockGuard lock(displayHandle);
                connectView = CONNECT_VIEW_WIFI_LIST;
                ApplyConnectView();
                selectedWifiIndex = 0;
                lastWifiScanVersion = 0;
                lastWifiScanActive = true;
                WifiConnectService::GetInstance().StartScan();
                RebuildWifiList();
                return true;
            }
            case CONNECT_ITEM_COUNT:
                return false;
        }
    }

    if (connectWifiItems.empty()) {
        return false;
    }

    size_t tapped_index = connectWifiItems.size();
    if (x == 0 && y == 0) {
        tapped_index = selectedWifiIndex;
    } else {
        for (size_t i = 0; i < connectWifiItems.size(); ++i) {
            if (IsPointInside(connectWifiItems[i], x, y)) {
                tapped_index = i;
                break;
            }
        }
        if (tapped_index == connectWifiItems.size()) {
            return false;
        }
    }

    if (WifiConnectService::GetInstance().IsScanning() || WifiConnectService::GetInstance().GetScanResults().empty()) {
        return false;
    }

    {
        DisplayLockGuard lock(displayHandle);
        ScrollWifiToIndex(tapped_index, LV_ANIM_ON);
        selectedWifiSsid = WifiConnectService::GetInstance().GetScanResults()[tapped_index].ssid;
        lv_obj_add_flag(connectPanel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(keyboardPanel, LV_OBJ_FLAG_HIDDEN);
        currentState = MENU_KEYBOARD_OPEN;
        lv_label_set_text(keyboardSsid, selectedWifiSsid.c_str());
    }

    std::string existing_password;
    for (const auto& item : SsidManager::GetInstance().GetSsidList()) {
        if (item.ssid == selectedWifiSsid) {
            existing_password = item.password;
            break;
        }
    }
    ResetKeyboardState(existing_password.c_str());
    UpdateKeyboardValue();
    return true;
}

bool HandleKeyboardTap(uint16_t x, uint16_t y) {
    if (currentState != MENU_KEYBOARD_OPEN) {
        return false;
    }

    MarkMenuActivity();

    for (size_t i = 0; i < keyboardButtons.size(); ++i) {
        if (!IsPointInside(keyboardButtons[i], x, y)) {
            continue;
        }

        if (i == 12) {
            keyboardCaps = !keyboardCaps;
            return true;
        }
        if (i == 13) {
            if (!selectedWifiSsid.empty()) {
                std::string password(keyboardText, keyboardLen);
                WifiConnectService::GetInstance().ConnectTo(selectedWifiSsid, password);
                Close();
                if (displayHandle != nullptr) {
                    displayHandle->ShowNotification("Đang kết nối WiFi", 3000);
                }
            }
            return true;
        }
        if (i == 14) {
            HandleKeyboardBackspace();
            return true;
        }

        const char* label = keyboardButtonTexts[i];
        if (label != nullptr && label[0] != '\0') {
            HandleT9Key(label[0]);
            return true;
        }
    }

    return false;
}

void CloseConnectToMenu() {
    if (currentState != MENU_CONNECT_OPEN) {
        return;
    }

    DisplayLockGuard lock(displayHandle);
    HidePanel(connectPanel);
    ShowPanel(menuPanel);
    currentState = MENU_OPEN;
    connectView = CONNECT_VIEW_METHODS;
    selectedItem = MENU_CONNECT;
    ApplyConnectView();
    ScrollMenuToIndex(static_cast<uint8_t>(selectedItem), LV_ANIM_OFF);
}

void CloseKeyboardToConnect() {
    if (currentState != MENU_KEYBOARD_OPEN) {
        return;
    }

    DisplayLockGuard lock(displayHandle);
    HidePanel(keyboardPanel);
    ShowPanel(connectPanel);
    currentState = MENU_CONNECT_OPEN;
    connectView = CONNECT_VIEW_WIFI_LIST;
    ApplyConnectView();
}

void SelectRemindersNext() {
    if (currentState != MENU_REMINDERS_OPEN || remindersItems.empty()) {
        return;
    }
    MarkMenuActivity();
    size_t next = selectedReminderIndex + 1;
    if (next >= remindersItems.size()) {
        next = remindersItems.size() - 1;
    }
    DisplayLockGuard lock(displayHandle);
    ScrollRemindersToIndex(next, LV_ANIM_ON);
}

void SelectRemindersPrev() {
    if (currentState != MENU_REMINDERS_OPEN || remindersItems.empty()) {
        return;
    }
    MarkMenuActivity();
    size_t next = selectedReminderIndex == 0 ? 0 : selectedReminderIndex - 1;
    DisplayLockGuard lock(displayHandle);
    ScrollRemindersToIndex(next, LV_ANIM_ON);
}

void ActivateRemindersSelected() {
    if (currentState != MENU_REMINDERS_OPEN) {
        return;
    }
    MarkMenuActivity();
    DisplayLockGuard lock(displayHandle);
    RebuildRemindersList();
    HidePanel(remindersPanel);
    ShowPanel(reminderDetailPanel);
    currentState = MENU_REMINDER_DETAIL_OPEN;
    ShowReminderDetailForCurrentSelection();
}

void CloseRemindersToMenu() {
    if (currentState != MENU_REMINDERS_OPEN) {
        return;
    }

    DisplayLockGuard lock(displayHandle);
    HidePanel(remindersPanel);
    ShowPanel(menuPanel);
    currentState = MENU_OPEN;
    selectedItem = MENU_REMINDERS;
    ScrollMenuToIndex(static_cast<uint8_t>(selectedItem), LV_ANIM_OFF);
}

void CloseReminderDetailToReminders() {
    if (currentState != MENU_REMINDER_DETAIL_OPEN) {
        return;
    }

    DisplayLockGuard lock(displayHandle);
    RebuildRemindersList();
    HidePanel(reminderDetailPanel);
    ShowPanel(remindersPanel);
    currentState = MENU_REMINDERS_OPEN;
    ScrollRemindersToIndex(selectedReminderIndex, LV_ANIM_OFF);
}

void RemindersDetailNext() {
    if (currentState != MENU_REMINDER_DETAIL_OPEN || remindersEntries.empty()) {
        return;
    }
    MarkMenuActivity();
    selectedReminderIndex = (selectedReminderIndex + 1) % remindersEntries.size();
    DisplayLockGuard lock(displayHandle);
    ShowReminderDetailForCurrentSelection();
}

void RemindersDetailPrev() {
    if (currentState != MENU_REMINDER_DETAIL_OPEN || remindersEntries.empty()) {
        return;
    }
    MarkMenuActivity();
    selectedReminderIndex = (selectedReminderIndex + remindersEntries.size() - 1) % remindersEntries.size();
    DisplayLockGuard lock(displayHandle);
    ShowReminderDetailForCurrentSelection();
}

void ShowStats() {
    if (currentState != MENU_CARE_OPEN && currentState != MENU_OPEN) {
        return;
    }

    MarkMenuActivity();
    statsOpenedFromCare = (currentState == MENU_CARE_OPEN);

    DisplayLockGuard lock(displayHandle);
    if (statsOpenedFromCare) {
        HidePanel(carePanel);
    } else {
        HidePanel(menuPanel);
    }
    ShowPanel(statsPanel);
    currentState = MENU_STATS_OPEN;
    UpdateStatsUI();
}

void CloseStatsToMenu() {
    if (currentState != MENU_STATS_OPEN) {
        return;
    }

    DisplayLockGuard lock(displayHandle);
    HidePanel(statsPanel);
    if (statsOpenedFromCare) {
        ShowPanel(carePanel);
        currentState = MENU_CARE_OPEN;
        selectedCareItem = CARE_STATS;
        ScrollCareToIndex(static_cast<uint8_t>(selectedCareItem), LV_ANIM_OFF);
    } else {
        ShowPanel(menuPanel);
        currentState = MENU_OPEN;
        selectedItem = MENU_CARE;
        ScrollMenuToIndex(static_cast<uint8_t>(selectedItem), LV_ANIM_OFF);
    }
    statsOpenedFromCare = false;
}

void StatsNext() {
    if (currentState != MENU_STATS_OPEN) {
        return;
    }
    MarkMenuActivity();
    statIndex = (statIndex + 1) % STAT_COUNT;
    DisplayLockGuard lock(displayHandle);
    UpdateStatsUI();
}

void StatsPrev() {
    if (currentState != MENU_STATS_OPEN) {
        return;
    }
    MarkMenuActivity();
    statIndex = (statIndex + STAT_COUNT - 1) % STAT_COUNT;
    DisplayLockGuard lock(displayHandle);
    UpdateStatsUI();
}

void OpenOptionsForCurrentStat() {}
void CloseOptionsToStats() {}

void ActivateCurrentOption() {
    if (currentState != MENU_STATS_OPEN) {
        return;
    }
    MarkMenuActivity();
    ApplyCurrentStatAction();
    DisplayLockGuard lock(displayHandle);
    UpdateStatsUI();
}

void OpenGamesMenu() {}
void CloseGamesToStats() {}
void StartTapTheGreens() {}
void HandleGameFinished() {}
void CloseLevelToMenu() {}

void CloseSettingsToMenu() {
    if (currentState != MENU_SETTINGS_OPEN) {
        return;
    }

    DisplayLockGuard lock(displayHandle);
    HidePanel(settingsPanel);
    ShowPanel(menuPanel);
    currentState = MENU_OPEN;
    selectedItem = MENU_SETTINGS;
    ScrollMenuToIndex(static_cast<uint8_t>(selectedItem), LV_ANIM_OFF);
}

bool HandleVolumeTap(uint16_t x, uint16_t y) {
    if (currentState != MENU_VOLUME_OPEN) {
        return false;
    }

    MarkMenuActivity();

    bool minus_pressed = false;
    bool plus_pressed = false;
    bool back_pressed = false;
    if (displayHandle != nullptr) {
        DisplayLockGuard lock(displayHandle);
        minus_pressed = IsPointInside(volumeMinusButton, x, y);
        plus_pressed = IsPointInside(volumePlusButton, x, y);
        back_pressed = IsPointInside(volumeBackButton, x, y);
    }

    if (minus_pressed) {
        VolumeStep(false);
        return true;
    }
    if (plus_pressed) {
        VolumeStep(true);
        return true;
    }
    if (back_pressed) {
        VolumeBack();
        return true;
    }

    return false;
}

void VolumeStep(bool increase) {
    if (currentState != MENU_VOLUME_OPEN) {
        return;
    }

    MarkMenuActivity();
    const int delta = increase ? 10 : -10;
    PreviewOutputVolumeClamped(volumeWorkingValue + delta);
}

void VolumeBack() {
    if (currentState != MENU_VOLUME_OPEN) {
        return;
    }

    CommitVolumeIfPending();
    DisplayLockGuard lock(displayHandle);
    HidePanel(volumePanel);
    ShowPanel(settingsPanel);
    currentState = MENU_SETTINGS_OPEN;
    selectedSettingsItem = SETTINGS_VOLUME;
    UpdateSettingsItemStyles();
}

bool HandleSettingsTap(uint16_t x, uint16_t y) {
    if (currentState != MENU_SETTINGS_OPEN) {
        return false;
    }

    MarkMenuActivity();

    for (int i = 0; i < SETTINGS_ITEM_COUNT; ++i) {
        if (settingsItems[i] == nullptr || !IsPointInside(settingsItems[i], x, y)) {
            continue;
        }

        selectedSettingsItem = static_cast<SettingsItem>(i);
        {
            DisplayLockGuard lock(displayHandle);
            UpdateSettingsItemStyles();
        }
        ActivateCurrent();
        return true;
    }

    return false;
}

bool HandleEyeEditorTap(uint16_t x, uint16_t y) {
    if (currentState != MENU_EYE_EDITOR_OPEN) {
        return false;
    }

    MarkMenuActivity();

    for (int i = 0; i < EYE_EDITOR_MODE_COUNT; ++i) {
        if (IsPointInside(eyeEditorModeButtons[i], x, y)) {
            eyeEditorMode = static_cast<EyeEditorMode>(i);
            UpdateEyeEditorModeStyles();
            UpdateEyeEditorValueLabel();
            return true;
        }
    }

    if (IsPointInside(eyeEditorMinusButton, x, y) || IsPointInside(eyeEditorPlusButton, x, y)) {
        auto* shape = GetMutableWorkingShape(eyeEditorSelectedEye);
        int* value = GetSelectedEyeValue(shape, eyeEditorMode);
        if (value == nullptr) {
            return false;
        }
        const int delta = IsPointInside(eyeEditorMinusButton, x, y) ? -2 : 2;
        *value += delta;
        shape->radius = std::min(shape->radius, shape->w / 2);
        shape->radius = std::min(shape->radius, shape->h / 2);
        shape->w = std::max(48, std::min(110, shape->w));
        shape->h = std::max(24, std::min(110, shape->h));
        shape->radius = std::max(0, std::min(48, shape->radius));
        shape->radius = std::min(shape->radius, shape->w / 2);
        shape->radius = std::min(shape->radius, shape->h / 2);
        eyeEditorDirty = true;
        ApplyEyeEditorPreview();
        UpdateEyeEditorValueLabel();
        UpdateEyeEditorSelectionOutline();
        return true;
    }

    if (IsPointInside(eyeEditorSaveButton, x, y)) {
        SaveEyeEditorShapes();
        return true;
    }

    if (IsPointInside(eyeEditorResetButton, x, y)) {
        ResetEyeEditorWorkingShapes();
        return true;
    }

    if (IsPointInside(eyeEditorBackButton, x, y)) {
        EyeEditorBack();
        return true;
    }

    auto* eye_display = GetEyeDisplay();
    if (eye_display == nullptr) {
        return false;
    }

    const auto left_bounds = eye_display->GetLeftEyeBounds();
    const auto right_bounds = eye_display->GetRightEyeBounds();
    const auto point_in_bounds = [x, y](const EyeDisplay::EyeBounds& b) {
        return b.w > 0 && b.h > 0 && x >= b.x && x <= b.x + b.w && y >= b.y && y <= b.y + b.h;
    };

    if (point_in_bounds(left_bounds)) {
        eyeEditorSelectedEye = EYE_EDITOR_LEFT;
        UpdateEyeEditorValueLabel();
        UpdateEyeEditorSelectionOutline();
        return true;
    }
    if (point_in_bounds(right_bounds)) {
        eyeEditorSelectedEye = EYE_EDITOR_RIGHT;
        UpdateEyeEditorValueLabel();
        UpdateEyeEditorSelectionOutline();
        return true;
    }

    return false;
}

void EyeEditorCycleMode(bool forward) {
    if (currentState != MENU_EYE_EDITOR_OPEN) {
        return;
    }

    MarkMenuActivity();
    int next = static_cast<int>(eyeEditorMode) + (forward ? 1 : -1);
    if (next < 0) {
        next = EYE_EDITOR_MODE_COUNT - 1;
    } else if (next >= EYE_EDITOR_MODE_COUNT) {
        next = 0;
    }
    eyeEditorMode = static_cast<EyeEditorMode>(next);
    UpdateEyeEditorModeStyles();
    UpdateEyeEditorValueLabel();
}

void EyeEditorApplyIncrement() {
    if (currentState != MENU_EYE_EDITOR_OPEN) {
        return;
    }

    MarkMenuActivity();
    auto* shape = GetMutableWorkingShape(eyeEditorSelectedEye);
    int* value = GetSelectedEyeValue(shape, eyeEditorMode);
    if (value == nullptr) {
        return;
    }
    *value += 2;
    shape->w = std::max(48, std::min(110, shape->w));
    shape->h = std::max(24, std::min(110, shape->h));
    shape->radius = std::max(0, std::min(48, shape->radius));
    shape->radius = std::min(shape->radius, shape->w / 2);
    shape->radius = std::min(shape->radius, shape->h / 2);
    eyeEditorDirty = true;
    ApplyEyeEditorPreview();
    UpdateEyeEditorValueLabel();
    UpdateEyeEditorSelectionOutline();
}

void EyeEditorBack() {
    if (currentState != MENU_EYE_EDITOR_OPEN) {
        return;
    }

    if (eyeEditorDirty) {
        RestoreEyeEditorSavedShapes();
    }
    DisplayLockGuard lock(displayHandle);
    HidePanel(eyeEditorPanel);
    ShowPanel(settingsPanel);
    currentState = MENU_SETTINGS_OPEN;
}
void SelectSleepNext() {}
void SelectSleepPrev() {}
void ActivateSleepSelected() {}
void CloseSleepToCare() {}
void SelectNotesNext() {
    if (currentState != MENU_NOTES_OPEN || notesItems.empty()) {
        return;
    }
    MarkMenuActivity();
    size_t next = selectedNoteIndex + 1;
    if (next >= notesItems.size()) {
        next = notesItems.size() - 1;
    }
    DisplayLockGuard lock(displayHandle);
    ScrollNotesToIndex(next, LV_ANIM_ON);
}

void SelectNotesPrev() {
    if (currentState != MENU_NOTES_OPEN || notesItems.empty()) {
        return;
    }
    MarkMenuActivity();
    size_t next = selectedNoteIndex == 0 ? 0 : selectedNoteIndex - 1;
    DisplayLockGuard lock(displayHandle);
    ScrollNotesToIndex(next, LV_ANIM_ON);
}

void ActivateNotesSelected() {
    if (currentState != MENU_NOTES_OPEN) {
        return;
    }
    MarkMenuActivity();
    DisplayLockGuard lock(displayHandle);
    RebuildNotesList();
    HidePanel(notesPanel);
    ShowPanel(noteDetailPanel);
    currentState = MENU_NOTE_DETAIL_OPEN;
    ShowNoteDetailForCurrentSelection();
}

void CloseNotesToMenu() {
    if (currentState != MENU_NOTES_OPEN) {
        return;
    }

    DisplayLockGuard lock(displayHandle);
    HidePanel(notesPanel);
    ShowPanel(menuPanel);
    currentState = MENU_OPEN;
    selectedItem = MENU_NOTES;
    ScrollMenuToIndex(static_cast<uint8_t>(selectedItem), LV_ANIM_OFF);
}

void CloseNoteDetailToNotes() {
    if (currentState != MENU_NOTE_DETAIL_OPEN) {
        return;
    }

    DisplayLockGuard lock(displayHandle);
    RebuildNotesList();
    HidePanel(noteDetailPanel);
    ShowPanel(notesPanel);
    currentState = MENU_NOTES_OPEN;
    ScrollNotesToIndex(selectedNoteIndex, LV_ANIM_OFF);
}

void NotesDetailNext() {
    if (currentState != MENU_NOTE_DETAIL_OPEN || notesEntries.empty()) {
        return;
    }
    MarkMenuActivity();
    selectedNoteIndex = (selectedNoteIndex + 1) % notesEntries.size();
    DisplayLockGuard lock(displayHandle);
    ShowNoteDetailForCurrentSelection();
}

void NotesDetailPrev() {
    if (currentState != MENU_NOTE_DETAIL_OPEN || notesEntries.empty()) {
        return;
    }
    MarkMenuActivity();
    selectedNoteIndex = (selectedNoteIndex + notesEntries.size() - 1) % notesEntries.size();
    DisplayLockGuard lock(displayHandle);
    ShowNoteDetailForCurrentSelection();
}
void StartCleanAnimation() {}

void Render() {
    if (displayHandle == nullptr) {
        return;
    }

    if (feedingAnimationActive && feedingOverlay != nullptr) {
        DisplayLockGuard lock(displayHandle);
        lv_obj_move_foreground(feedingOverlay);
    }

    if (currentState != MENU_CLOSED &&
        lv_tick_elaps(lastMenuActivityMs) >= MENU_INACTIVITY_TIMEOUT_MS) {
        ESP_LOGI(TAG, "Menu inactivity timeout -> close");
        Close();
        return;
    }

    if (currentState == MENU_EYE_EDITOR_OPEN) {
        DisplayLockGuard lock(displayHandle);
        UpdateEyeEditorSelectionOutline();
        UpdateEyeEditorValueLabel();
        return;
    }

    if (currentState == MENU_VOLUME_OPEN) {
        DisplayLockGuard lock(displayHandle);
        UpdateVolumeValueLabel();
        return;
    }

    if (currentState == MENU_CONNECT_OPEN && connectView == CONNECT_VIEW_WIFI_LIST) {
        auto& wifi_service = WifiConnectService::GetInstance();
        const uint32_t scan_version = wifi_service.GetScanVersion();
        const bool scanning = wifi_service.IsScanning();
        if (scan_version != lastWifiScanVersion || scanning != lastWifiScanActive) {
            DisplayLockGuard lock(displayHandle);
            lastWifiScanVersion = scan_version;
            lastWifiScanActive = scanning;
            RebuildWifiList();
        }
        return;
    }

    if (currentState != MENU_STATS_OPEN) {
        return;
    }

    DisplayLockGuard lock(displayHandle);
    UpdateStatsUI();
}

bool IsFeedingAnimationActive() {
    return feedingAnimationActive;
}

bool HandleFeedingAnimationTap() {
    if (!feedingAnimationActive || displayHandle == nullptr) {
        return false;
    }
    DisplayLockGuard lock(displayHandle);
    StopFeedingAnimationLocked();
    return true;
}

bool IsTapOnSelected(uint16_t x, uint16_t y) {
    if (currentState == MENU_OPEN) {
        return IsPointInside(menuItems[selectedItem], x, y);
    }
    return false;
}

bool IsTapOnUpButton(uint16_t x, uint16_t y) {
    if (!IsOpen()) {
        return false;
    }
    return IsPointInside(upButton, x, y);
}

bool IsTapOnDownButton(uint16_t x, uint16_t y) {
    if (!IsOpen()) {
        return false;
    }
    return IsPointInside(downButton, x, y);
}

bool IsTapOnPrevButton(uint16_t x, uint16_t y) {
    switch (currentState) {
        case MENU_OPEN:
            return IsPointInside(upButton, x, y);
        case MENU_CARE_OPEN:
            return IsPointInside(careUpButton, x, y);
        case MENU_CONNECT_OPEN:
            return IsPointInside(connectUpButton, x, y);
        case MENU_REMINDERS_OPEN:
            return IsPointInside(remindersUpButton, x, y);
        case MENU_NOTES_OPEN:
            return IsPointInside(notesUpButton, x, y);
        case MENU_STATS_OPEN:
            return IsPointInside(statsLeftBtn, x, y);
        default:
            return false;
    }
}

bool IsTapOnNextButton(uint16_t x, uint16_t y) {
    switch (currentState) {
        case MENU_OPEN:
            return IsPointInside(downButton, x, y);
        case MENU_CARE_OPEN:
            return IsPointInside(careDownButton, x, y);
        case MENU_CONNECT_OPEN:
            return IsPointInside(connectDownButton, x, y);
        case MENU_REMINDERS_OPEN:
            return IsPointInside(remindersDownButton, x, y);
        case MENU_NOTES_OPEN:
            return IsPointInside(notesDownButton, x, y);
        case MENU_STATS_OPEN:
            return IsPointInside(statsRightBtn, x, y);
        default:
            return false;
    }
}

bool IsTapOnCareSelected(uint16_t x, uint16_t y) {
    if (currentState != MENU_CARE_OPEN) {
        return false;
    }
    return IsPointInside(careItems[selectedCareItem], x, y);
}

bool IsTapOnStatsTitle(uint16_t x, uint16_t y) {
    if (currentState != MENU_STATS_OPEN) {
        return false;
    }
    return IsPointInside(statsActionZone, x, y);
}

bool IsTapOnStatsNav(uint16_t x, uint16_t y) {
    if (currentState != MENU_STATS_OPEN) {
        return false;
    }
    return IsPointInside(statsLeftBtn, x, y) || IsPointInside(statsRightBtn, x, y);
}

bool IsTapOnSleepSelected(uint16_t, uint16_t) { return false; }
bool IsTapOnNotesSelected(uint16_t x, uint16_t y) {
    if (currentState != MENU_NOTES_OPEN || notesItems.empty() || selectedNoteIndex >= notesItems.size()) {
        return false;
    }
    return IsPointInside(notesItems[selectedNoteIndex], x, y);
}

bool IsTapOnRemindersSelected(uint16_t x, uint16_t y) {
    if (currentState != MENU_REMINDERS_OPEN || remindersItems.empty() ||
        selectedReminderIndex >= remindersItems.size()) {
        return false;
    }
    return IsPointInside(remindersItems[selectedReminderIndex], x, y);
}

}  // namespace MenuSystem
