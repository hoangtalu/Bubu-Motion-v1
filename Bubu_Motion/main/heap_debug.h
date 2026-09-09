#ifndef _HEAP_DEBUG_H_
#define _HEAP_DEBUG_H_

/*
 * TEMPORARY INSTRUMENTATION -- remove once the internal-SRAM shortage is fixed.
 *
 * Why this exists: the device idles at ~20KB free internal SRAM but dips to
 * ~2.9KB, and it is those transient dips that starve the ESP32-S3 AES
 * accelerator's DMA allocation (it can only DMA from internal SRAM), which
 * shows up as `esp-aes: Failed to allocate memory` -> TLS write fails -> the
 * websocket session dies. The 10s SystemInfo log samples far too slowly to
 * see the trough, so this adds:
 *
 *   1. A failed-allocation hook, so we learn the EXACT size/caps/function of
 *      the allocation that actually fails, instead of inferring it.
 *   2. A fast sampler task recording the minima (free + largest free block,
 *      for both MALLOC_CAP_INTERNAL and MALLOC_CAP_DMA) with timestamps, so
 *      we can see how deep the dips go and when.
 *   3. An on-demand report including per-task stack high-water marks, to find
 *      who is holding the memory.
 */

#include <cstddef>
#include <cstdint>

namespace HeapDebug {

// Registers the failed-allocation hook and starts the sampler task.
// Call once, early in startup.
void Start();

// Logs current heap state, the recorded minima, and any recorded allocation
// failure. `context` is a short label identifying the callsite.
// Safe to call from normal task context only (it logs and walks the task list).
void Report(const char* context);

// Logs per-task stack high-water marks. Called automatically every 30s; the
// values only decrease, so the last report of a fully exercised session is the
// real peak. Never size a stack from a single early reading.
void ReportStacks();

// Number of failed heap allocations recorded since boot (0 == none).
uint32_t FailureCount();

// Resets the recorded minima so a later window can be measured in isolation.
void ResetMinima();

}  // namespace HeapDebug

#endif  // _HEAP_DEBUG_H_
