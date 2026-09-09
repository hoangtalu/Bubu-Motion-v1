#include <esp_log.h>
#include <esp_err.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <driver/gpio.h>
#include <esp_event.h>
#include "application.h"
#include "heap_debug.h"  // TEMPORARY instrumentation

#define TAG "main"

extern "C" void app_main(void)
{
    // Initialize NVS flash for WiFi configuration
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "Erasing NVS flash to fix corruption");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // TEMPORARY: catch the internal-SRAM dips that starve the AES DMA
    // allocation. Started before the app allocates so the sampler sees the
    // whole startup profile. Remove along with heap_debug.{h,cc}.
    //
    // DISABLED (2026-09-08): comparing against 1.7.1 (which never had this
    // instrumentation) showed 1.7.2 running with ~13KB less free internal
    // RAM from very early boot, before wake-word setup even starts, and
    // staying fragmented at 5-9KB instead of 1.7.1's stable ~20KB. This is
    // the leading suspect for that gap -- disabling to measure whether
    // removing it alone restores the old headroom. Re-enable (or remove
    // this comment) once that's confirmed either way.
    // HeapDebug::Start();

    // Initialize and run the application
    auto& app = Application::GetInstance();
    app.Initialize();
    app.Run();
}
