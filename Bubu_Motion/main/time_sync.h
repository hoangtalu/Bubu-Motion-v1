#pragma once

#include <cstdint>

// Wall clock for the device.
//
// The system clock holds true UTC, and TZ is set once at boot, so every
// localtime_r() in the tree reads Vietnam time. There are two sources:
//
//   1. SNTP (primary). Started on the first network connect, re-syncs on its
//      own every CONFIG_LWIP_SNTP_UPDATE_DELAY.
//   2. The OTA reply's server_time (fallback). Used only when SNTP has not
//      synced recently, e.g. a home network that blocks UDP 123.
//
// Before 2026-09-17 the OTA reply was the only source, and ota.cc added the
// timezone offset into the epoch itself. A drifting VPS clock then showed up
// on every device. Do not add the offset into the epoch again: it breaks as
// soon as SNTP, which sets true UTC, overwrites it.
namespace TimeSync {

// Set TZ. Call once, before anything formats a time.
void Begin();

// Start SNTP. Safe to call on every network connect; only the first call does
// anything.
void StartSntp();

// Apply the OTA reply's server_time (Unix epoch, milliseconds, UTC). Ignored
// while SNTP is fresh.
void ApplyServerTime(double epoch_ms);

bool IsSntpSynced();

}  // namespace TimeSync
