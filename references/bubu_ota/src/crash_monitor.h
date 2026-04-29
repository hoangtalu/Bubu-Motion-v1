#pragma once

// Crash monitor: records operation context in RTC memory (survives reboots)
// and reports the crash type + location on next boot.
//
// Usage:
//   CrashMonitor::begin();           // early in setup() — prints last crash info
//   CrashMonitor::setContext("...");  // before a risky operation
//   CrashMonitor::clearContext();     // after it completes safely

namespace CrashMonitor {
    void begin();
    void setContext(const char* ctx);
    void clearContext();
}
