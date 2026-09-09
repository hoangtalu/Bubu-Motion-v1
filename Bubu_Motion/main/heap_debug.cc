#include "heap_debug.h"

#include <cstring>

#include <esp_attr.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/idf_additions.h>

#define TAG "HeapDebug"

namespace HeapDebug { void Report(const char* context); void ReportStacks(); }

namespace {

constexpr TickType_t kSamplePeriodMs = 10;
constexpr uint32_t kSamplerStack = 4096;  // Report() runs here on failure
// Above the app main task (5) so a dip caused by a busy lower-priority task is
// still sampled, but well below the WiFi/driver tasks (23) so we never delay
// them. The task body is a handful of heap accounting reads, a few microseconds.
constexpr UBaseType_t kSamplerPriority = 10;
// usStackHighWaterMark only ever decreases, so periodic dumps accumulate the
// true worst case across everything the device has actually done. A single
// reading is NOT a peak -- trimming opus_codec on one cost us a stack overflow
// the moment the encode path first ran.
constexpr uint32_t kStackReportEveryMs = 30000;

// ---------------------------------------------------------------------------
// Failed-allocation record.
//
// The hook is called from heap_caps_alloc_failed(), which is HEAP_IRAM_ATTR --
// it can run with the flash cache disabled. So this callback must touch NOTHING
// but DRAM statics: no ESP_LOG (format strings live in flash), no heap calls,
// no locks. Everything here is a plain store; the reporting happens later from
// normal task context.
// ---------------------------------------------------------------------------
DRAM_ATTR volatile uint32_t s_fail_count = 0;
DRAM_ATTR volatile uint32_t s_fail_reported = 0;
DRAM_ATTR volatile size_t s_fail_size = 0;
DRAM_ATTR volatile uint32_t s_fail_caps = 0;
DRAM_ATTR const char* volatile s_fail_fn = nullptr;
DRAM_ATTR volatile int64_t s_fail_us = 0;

IRAM_ATTR void OnAllocFailed(size_t size, uint32_t caps, const char* function_name) {
    s_fail_count = s_fail_count + 1;
    s_fail_size = size;
    s_fail_caps = caps;
    // The string is a compile-time literal in .rodata; storing the pointer is
    // safe even with the cache off, we just must not dereference it here.
    s_fail_fn = function_name;
    s_fail_us = esp_timer_get_time();
}

// ---------------------------------------------------------------------------
// Sampled minima
// ---------------------------------------------------------------------------
struct Trough {
    size_t free_bytes;
    size_t largest_block;
    int64_t at_us;
};

Trough s_int_free_min;     // deepest MALLOC_CAP_INTERNAL free
Trough s_int_block_min;    // smallest MALLOC_CAP_INTERNAL largest-free-block
Trough s_dma_free_min;     // deepest MALLOC_CAP_DMA free
Trough s_dma_block_min;    // smallest MALLOC_CAP_DMA largest-free-block
portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

void ResetTroughsLocked() {
    s_int_free_min = {SIZE_MAX, SIZE_MAX, 0};
    s_int_block_min = {SIZE_MAX, SIZE_MAX, 0};
    s_dma_free_min = {SIZE_MAX, SIZE_MAX, 0};
    s_dma_block_min = {SIZE_MAX, SIZE_MAX, 0};
}

void SamplerTask(void*) {
    while (true) {
        size_t int_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
        size_t int_block = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
        size_t dma_free = heap_caps_get_free_size(MALLOC_CAP_DMA);
        size_t dma_block = heap_caps_get_largest_free_block(MALLOC_CAP_DMA);
        int64_t now = esp_timer_get_time();

        taskENTER_CRITICAL(&s_mux);
        if (int_free < s_int_free_min.free_bytes) {
            s_int_free_min = {int_free, int_block, now};
        }
        if (int_block < s_int_block_min.largest_block) {
            s_int_block_min = {int_free, int_block, now};
        }
        if (dma_free < s_dma_free_min.free_bytes) {
            s_dma_free_min = {dma_free, dma_block, now};
        }
        if (dma_block < s_dma_block_min.largest_block) {
            s_dma_block_min = {dma_free, dma_block, now};
        }
        taskEXIT_CRITICAL(&s_mux);

        // An allocation can fail anywhere -- it hit the boot-time OTA config
        // fetch, not just websocket sends. Auto-report the first one from here
        // so no failure goes unseen regardless of which callsite triggered it.
        if (s_fail_count != s_fail_reported) {
            HeapDebug::Report("alloc-failure");
        }

        static uint32_t elapsed_ms = 0;
        elapsed_ms += kSamplePeriodMs;
        if (elapsed_ms >= kStackReportEveryMs) {
            elapsed_ms = 0;
            HeapDebug::ReportStacks();
        }

        vTaskDelay(pdMS_TO_TICKS(kSamplePeriodMs));
    }
}

void LogTaskStacks() {
    UBaseType_t count = uxTaskGetNumberOfTasks();
    // The array is transient and sizeable; take it from PSRAM so this debug
    // path does not itself consume the internal memory we are measuring.
    auto* tasks = static_cast<TaskStatus_t*>(
        heap_caps_malloc(count * sizeof(TaskStatus_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (tasks == nullptr) {
        ESP_LOGW(TAG, "  (could not allocate task status array)");
        return;
    }
    count = uxTaskGetSystemState(tasks, count, nullptr);
    ESP_LOGW(TAG, "  task stack high-water (bytes still unused, smallest first):");
    // Simple selection sort -- count is ~25, and this is debug-only code.
    for (UBaseType_t i = 0; i < count; i++) {
        UBaseType_t min_idx = i;
        for (UBaseType_t j = i + 1; j < count; j++) {
            if (tasks[j].usStackHighWaterMark < tasks[min_idx].usStackHighWaterMark) {
                min_idx = j;
            }
        }
        if (min_idx != i) {
            TaskStatus_t tmp = tasks[i];
            tasks[i] = tasks[min_idx];
            tasks[min_idx] = tmp;
        }
        ESP_LOGW(TAG, "    %-18s prio=%2u unused=%u",
                 tasks[i].pcTaskName,
                 static_cast<unsigned>(tasks[i].uxCurrentPriority),
                 static_cast<unsigned>(tasks[i].usStackHighWaterMark));
    }
    heap_caps_free(tasks);
}

}  // namespace

namespace HeapDebug {

void Start() {
    taskENTER_CRITICAL(&s_mux);
    ResetTroughsLocked();
    taskEXIT_CRITICAL(&s_mux);

    esp_err_t err = heap_caps_register_failed_alloc_callback(OnAllocFailed);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register alloc-failure hook: %s", esp_err_to_name(err));
    }

    // Put the sampler's stack in PSRAM. A normal task stack is internal RAM,
    // and 4KB of it would materially shift the very number we are measuring.
    // Safe here because this task never runs with the flash cache disabled.
    if (xTaskCreateWithCaps(SamplerTask, "heapdbg", kSamplerStack, nullptr, kSamplerPriority,
                            nullptr, MALLOC_CAP_SPIRAM) != pdPASS) {
        ESP_LOGE(TAG, "Failed to start sampler task");
        return;
    }
    ESP_LOGW(TAG, "Instrumentation active: alloc-failure hook + %ums sampler. "
                  "This is temporary debug code -- remove before release.",
             static_cast<unsigned>(kSamplePeriodMs));
}

void ReportStacks() {
    ESP_LOGW(TAG, "---- stack high-water @%ums (worst case so far; trim ONLY on a "
                  "fully exercised run) ----", (unsigned)(esp_timer_get_time() / 1000));
    LogTaskStacks();
}

uint32_t FailureCount() {
    return s_fail_count;
}

void ResetMinima() {
    taskENTER_CRITICAL(&s_mux);
    ResetTroughsLocked();
    taskEXIT_CRITICAL(&s_mux);
}

void Report(const char* context) {
    Trough int_free_min, int_block_min, dma_free_min, dma_block_min;
    taskENTER_CRITICAL(&s_mux);
    int_free_min = s_int_free_min;
    int_block_min = s_int_block_min;
    dma_free_min = s_dma_free_min;
    dma_block_min = s_dma_block_min;
    taskEXIT_CRITICAL(&s_mux);

    ESP_LOGW(TAG, "==== heap report [%s] t=%ums ====", context, (unsigned)(esp_timer_get_time() / 1000));
    ESP_LOGW(TAG, "  now      INTERNAL free=%u largest=%u | DMA free=%u largest=%u",
             heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
             heap_caps_get_free_size(MALLOC_CAP_DMA),
             heap_caps_get_largest_free_block(MALLOC_CAP_DMA));
    ESP_LOGW(TAG, "  trough   INTERNAL free=%u (largest was %u) @%ums",
             int_free_min.free_bytes, int_free_min.largest_block, (unsigned)(int_free_min.at_us / 1000));
    ESP_LOGW(TAG, "  trough   INTERNAL largest=%u (free was %u) @%ums",
             int_block_min.largest_block, int_block_min.free_bytes, (unsigned)(int_block_min.at_us / 1000));
    ESP_LOGW(TAG, "  trough   DMA      free=%u (largest was %u) @%ums",
             dma_free_min.free_bytes, dma_free_min.largest_block, (unsigned)(dma_free_min.at_us / 1000));
    ESP_LOGW(TAG, "  trough   DMA      largest=%u (free was %u) @%ums",
             dma_block_min.largest_block, dma_block_min.free_bytes, (unsigned)(dma_block_min.at_us / 1000));

    uint32_t fails = s_fail_count;
    if (fails > 0) {
        ESP_LOGE(TAG, "  ALLOC FAILURES: %u total; last: size=%u caps=0x%08x fn=%s @%ums",
                 static_cast<unsigned>(fails), static_cast<unsigned>(s_fail_size),
                 static_cast<unsigned>(s_fail_caps),
                 s_fail_fn ? s_fail_fn : "?", (unsigned)(s_fail_us / 1000));
        s_fail_reported = fails;
    }

    LogTaskStacks();
    ESP_LOGW(TAG, "  ---- MALLOC_CAP_DMA heap detail ----");
    heap_caps_print_heap_info(MALLOC_CAP_DMA);
    ESP_LOGW(TAG, "==== end heap report [%s] ====", context);
}

}  // namespace HeapDebug
