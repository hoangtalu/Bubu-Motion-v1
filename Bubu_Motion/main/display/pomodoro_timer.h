#pragma once

#include <cstdint>

// HỌC TẬP -- a Pomodoro focus timer for the round screen.
//
// Only kClassic (25/5/15) is an actual pomodoro: in Cirillo's technique the
// pomodoro is a fixed, indivisible 25-minute unit, which is what the name
// refers to. kSprint and kDeep are plain focus timers wearing the same ring,
// and IsPomodoro() says which is which so the UI can be honest about it.
//
// Two rules from the technique are load-bearing here and are not simplifiable:
//
//   1. There is no pause. An interruption VOIDS the block -- VoidBlock()
//      discards it and nothing is banked. Partial credit does not exist.
//   2. A focus block is only ever completed by running to zero. That is the
//      single credit path; SkipBreak() shortens a break and pays nothing.
//
// Pure logic plus its own NVS bookkeeping -- no LVGL here. The caller polls
// Update() each tick and draws whatever this reports.
namespace PomodoroTimer {

enum class Preset : uint8_t {
    kSprint = 0,   // 15' -- a plain timer
    kClassic,      // 25' -- the pomodoro
    kDeep,         // 45' -- a plain timer
    kPresetCount,
};

enum class Phase : uint8_t {
    kIdle = 0,
    kFocus,        // TẬP TRUNG
    kShortBreak,   // NGHỈ NGẮN
    kLongBreak,    // NGHỈ DÀI
};

// What Update() crossed on this call. kFocusCompleted is the only value that
// means anything was banked, and it is the caller's cue to hand
// CommitCompletedFocus() to the main task.
enum class Event : uint8_t {
    kNone = 0,
    kFocusCompleted,
    kBreakCompleted,
};

struct Profile {
    uint8_t focus_min;
    uint8_t short_break_min;
    uint8_t long_break_min;
    uint8_t focus_before_long;   // long break after this many focus blocks
};

// today/streak_days need a valid clock; see the note on CommitCompletedFocus.
struct Stats {
    uint16_t today = 0;         // focus blocks completed on the current day
    uint16_t streak_days = 0;   // consecutive days with at least one
    uint16_t best_day = 0;
    uint32_t total = 0;         // lifetime
};

// Load stats from NVS. Call once, before the panel is built.
void Begin();

const Profile& GetProfile(Preset preset);
// True only for kClassic. The other two are timers, not pomodoros.
bool IsPomodoro(Preset preset);
const char* PresetLabel(Preset preset);   // "15'" / "25'" / "45'"
const char* PhaseName(Phase phase);       // Vietnamese, for the panel

// Begin a run: enters kFocus immediately and clears the cycle counter.
void Start(Preset preset);

// Discard a running focus block. No credit, no resume -- this is the
// technique's interruption rule, and it is also what closing the menu does,
// since nothing ticks this in the background.
void VoidBlock();

// Leave any phase for kIdle without crediting anything.
void Stop();

// End a running break early. Returns false (and does nothing) during focus:
// a focus block cannot be skipped, only voided.
bool SkipBreak();

// Advance the clock. Safe to call at any rate; the phase deadline is absolute,
// so a late call cannot lose time. Crossing a boundary starts the next phase
// on its own and updates the in-memory stats, so a caller that never calls
// CommitCompletedFocus() still shows the right numbers -- it just does not
// persist them.
Event Update();

bool     IsActive();          // phase != kIdle
Phase    GetPhase();
Preset   GetPreset();
uint32_t PhaseTotalMs();
uint32_t RemainingMs();
uint8_t  FocusDoneInRun();    // completed blocks since Start()
uint8_t  FocusBeforeLong();   // of the current preset

Stats GetStats();

// Persist what Update() already counted. Writes NVS synchronously and so must
// run on the main task, never on the LVGL task -- Application::Schedule it,
// the way FinishQuickTapRound() does.
//
// Day and streak bookkeeping needs a real clock. Before SNTP lands (tm_year <
// 2025) only `total` moves; today/streak are left alone rather than being
// credited to a wrong day.
void CommitCompletedFocus();

}  // namespace PomodoroTimer
