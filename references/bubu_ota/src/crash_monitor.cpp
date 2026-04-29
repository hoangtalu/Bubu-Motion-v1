#include "crash_monitor.h"
#include <Arduino.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <stdio.h>
#include <string.h>

// RTC slow memory — survives soft resets (WDT, panic, sw restart).
// Lost only on power-on or deep sleep without retention.
RTC_DATA_ATTR static char sContext[96] = {};
RTC_DATA_ATTR static bool sHadContext  = false;

namespace CrashMonitor {

void begin() {
    esp_reset_reason_t reason = esp_reset_reason();

    const char* reasonStr;
    switch (reason) {
        case ESP_RST_PANIC:    reasonStr = "PANIC (abort/assert/stack overflow)"; break;
        case ESP_RST_TASK_WDT: reasonStr = "TASK WATCHDOG"; break;
        case ESP_RST_INT_WDT:  reasonStr = "INTERRUPT WATCHDOG"; break;
        case ESP_RST_WDT:      reasonStr = "OTHER WATCHDOG"; break;
        case ESP_RST_SW:       reasonStr = "SOFTWARE RESTART"; break;
        case ESP_RST_POWERON:  reasonStr = nullptr; break;
        default:               reasonStr = nullptr; break;
    }

    if (reasonStr) {
        Serial.printf("[CRASH] Previous reset: %s\n", reasonStr);
        if (sHadContext && sContext[0]) {
            Serial.printf("[CRASH] Was in: %s\n", sContext);
        } else {
            Serial.println("[CRASH] No context saved");
        }
    }

    sContext[0]  = '\0';
    sHadContext  = false;
}

void setContext(const char* ctx) {
    strncpy(sContext, ctx, sizeof(sContext) - 1);
    sContext[sizeof(sContext) - 1] = '\0';
    sHadContext = true;
}

void clearContext() {
    sContext[0] = '\0';
    sHadContext = false;
}

} // namespace CrashMonitor

// FreeRTOS stack overflow hook — called before the crash, writes task name to RTC memory.
extern "C" void vApplicationStackOverflowHook(TaskHandle_t /*xTask*/, char* pcTaskName) {
    snprintf(sContext, sizeof(sContext), "STACK OVERFLOW in task: %s", pcTaskName);
    sHadContext = true;
}
