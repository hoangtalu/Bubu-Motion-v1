#include "settings.h"

#include <esp_log.h>
#include <nvs_flash.h>

#define TAG "Settings"

Settings::Settings(const std::string& ns, bool read_write) : ns_(ns), read_write_(read_write) {
    auto ret = nvs_open(ns.c_str(), read_write_ ? NVS_READWRITE : NVS_READONLY, &nvs_handle_);
    if (ret != ESP_OK) {
        // A missing namespace is normal on first boot in read-only mode; anything
        // else means reads fall back to defaults and writes are dropped.
        nvs_handle_ = 0;
        if (read_write_ || ret != ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGW(TAG, "Failed to open namespace %s: %s", ns_.c_str(), esp_err_to_name(ret));
        }
    }
}

Settings::~Settings() {
    if (nvs_handle_ != 0) {
        if (read_write_ && dirty_) {
            auto ret = nvs_commit(nvs_handle_);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to commit namespace %s: %s", ns_.c_str(), esp_err_to_name(ret));
            }
        }
        nvs_close(nvs_handle_);
    }
}

std::string Settings::GetString(const std::string& key, const std::string& default_value) {
    if (nvs_handle_ == 0) {
        return default_value;
    }

    size_t length = 0;
    if (nvs_get_str(nvs_handle_, key.c_str(), nullptr, &length) != ESP_OK) {
        return default_value;
    }

    std::string value;
    value.resize(length);
    auto ret = nvs_get_str(nvs_handle_, key.c_str(), value.data(), &length);
    if (ret != ESP_OK) {
        // The value can disappear between the size query and the read.
        ESP_LOGW(TAG, "Failed to read %s/%s: %s", ns_.c_str(), key.c_str(), esp_err_to_name(ret));
        return default_value;
    }
    while (!value.empty() && value.back() == '\0') {
        value.pop_back();
    }
    return value;
}

esp_err_t Settings::SetString(const std::string& key, const std::string& value) {
    if (!writable()) {
        ESP_LOGW(TAG, "Namespace %s is not open for writing", ns_.c_str());
        return ESP_ERR_INVALID_STATE;
    }
    auto ret = nvs_set_str(nvs_handle_, key.c_str(), value.c_str());
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write %s/%s: %s", ns_.c_str(), key.c_str(), esp_err_to_name(ret));
        return ret;
    }
    dirty_ = true;
    return ESP_OK;
}

int32_t Settings::GetInt(const std::string& key, int32_t default_value) {
    if (nvs_handle_ == 0) {
        return default_value;
    }

    int32_t value;
    if (nvs_get_i32(nvs_handle_, key.c_str(), &value) != ESP_OK) {
        return default_value;
    }
    return value;
}

esp_err_t Settings::SetInt(const std::string& key, int32_t value) {
    if (!writable()) {
        ESP_LOGW(TAG, "Namespace %s is not open for writing", ns_.c_str());
        return ESP_ERR_INVALID_STATE;
    }
    auto ret = nvs_set_i32(nvs_handle_, key.c_str(), value);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write %s/%s: %s", ns_.c_str(), key.c_str(), esp_err_to_name(ret));
        return ret;
    }
    dirty_ = true;
    return ESP_OK;
}

bool Settings::GetBool(const std::string& key, bool default_value) {
    if (nvs_handle_ == 0) {
        return default_value;
    }

    uint8_t value;
    if (nvs_get_u8(nvs_handle_, key.c_str(), &value) != ESP_OK) {
        return default_value;
    }
    return value != 0;
}

esp_err_t Settings::SetBool(const std::string& key, bool value) {
    if (!writable()) {
        ESP_LOGW(TAG, "Namespace %s is not open for writing", ns_.c_str());
        return ESP_ERR_INVALID_STATE;
    }
    auto ret = nvs_set_u8(nvs_handle_, key.c_str(), value ? 1 : 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write %s/%s: %s", ns_.c_str(), key.c_str(), esp_err_to_name(ret));
        return ret;
    }
    dirty_ = true;
    return ESP_OK;
}

std::vector<uint8_t> Settings::GetBlob(const std::string& key,
                                       const std::vector<uint8_t>& default_value) {
    if (nvs_handle_ == 0) {
        return default_value;
    }

    size_t length = 0;
    if (nvs_get_blob(nvs_handle_, key.c_str(), nullptr, &length) != ESP_OK || length == 0) {
        return default_value;
    }

    std::vector<uint8_t> value(length);
    auto ret = nvs_get_blob(nvs_handle_, key.c_str(), value.data(), &length);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to read blob %s/%s: %s", ns_.c_str(), key.c_str(),
                 esp_err_to_name(ret));
        return default_value;
    }
    value.resize(length);
    return value;
}

esp_err_t Settings::SetBlob(const std::string& key, const void* data, size_t length) {
    if (!writable()) {
        ESP_LOGW(TAG, "Namespace %s is not open for writing", ns_.c_str());
        return ESP_ERR_INVALID_STATE;
    }
    if (data == nullptr || length == 0) {
        ESP_LOGW(TAG, "Refusing empty blob for %s/%s", ns_.c_str(), key.c_str());
        return ESP_ERR_INVALID_ARG;
    }
    auto ret = nvs_set_blob(nvs_handle_, key.c_str(), data, length);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write blob %s/%s: %s", ns_.c_str(), key.c_str(),
                 esp_err_to_name(ret));
        return ret;
    }
    dirty_ = true;
    return ESP_OK;
}

esp_err_t Settings::EraseKey(const std::string& key) {
    if (!writable()) {
        ESP_LOGW(TAG, "Namespace %s is not open for writing", ns_.c_str());
        return ESP_ERR_INVALID_STATE;
    }
    auto ret = nvs_erase_key(nvs_handle_, key.c_str());
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        // Erasing an absent key is not a failure.
        return ESP_OK;
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to erase %s/%s: %s", ns_.c_str(), key.c_str(), esp_err_to_name(ret));
        return ret;
    }
    dirty_ = true;
    return ESP_OK;
}

esp_err_t Settings::EraseAll() {
    if (!writable()) {
        ESP_LOGW(TAG, "Namespace %s is not open for writing", ns_.c_str());
        return ESP_ERR_INVALID_STATE;
    }
    auto ret = nvs_erase_all(nvs_handle_);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to erase namespace %s: %s", ns_.c_str(), esp_err_to_name(ret));
        return ret;
    }
    dirty_ = true;
    return ESP_OK;
}
