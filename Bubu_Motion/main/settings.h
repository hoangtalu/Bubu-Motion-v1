#ifndef SETTINGS_H
#define SETTINGS_H

#include <string>
#include <vector>
#include <nvs_flash.h>

class Settings {
public:
    Settings(const std::string& ns, bool read_write = false);
    ~Settings();

    // Setters return the underlying NVS error so a full or worn partition
    // surfaces as a failed write rather than aborting the device. Callers that
    // ignore the result still get a logged warning.
    std::string GetString(const std::string& key, const std::string& default_value = "");
    esp_err_t SetString(const std::string& key, const std::string& value);
    int32_t GetInt(const std::string& key, int32_t default_value = 0);
    esp_err_t SetInt(const std::string& key, int32_t value);
    bool GetBool(const std::string& key, bool default_value = false);
    esp_err_t SetBool(const std::string& key, bool value);
    std::vector<uint8_t> GetBlob(const std::string& key,
                                 const std::vector<uint8_t>& default_value = {});
    esp_err_t SetBlob(const std::string& key, const void* data, size_t length);
    esp_err_t EraseKey(const std::string& key);
    esp_err_t EraseAll();

    // True when the namespace was opened successfully and is writable.
    bool valid() const { return nvs_handle_ != 0; }
    bool writable() const { return nvs_handle_ != 0 && read_write_; }

private:
    std::string ns_;
    nvs_handle_t nvs_handle_ = 0;
    bool read_write_ = false;
    bool dirty_ = false;
};

#endif
