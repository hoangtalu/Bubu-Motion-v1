#include "heartbeat.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_heap_caps.h>
#include <Arduino.h>

namespace Heartbeat {

static void heartbeatTask(void*) {
    while (true) {
        Serial.printf("[HB] alive | heap: %u | psram: %u\n",
                      (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                      (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

void begin() {
    xTaskCreatePinnedToCore(heartbeatTask, "heartbeat", 2048, nullptr, 1, nullptr, 0);
}

}  // namespace Heartbeat
