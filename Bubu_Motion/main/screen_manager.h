#ifndef SCREEN_MANAGER_H
#define SCREEN_MANAGER_H

#include <cstdint>
#include <functional>

/**
 * ScreenManager - one active screen at a time.
 *
 * Exactly one screen owns the display and the input at any moment. Each screen
 * declares what it needs through a ScreenPolicy; everything it does not declare
 * is switched off. This is what stops the eye renderer, the IMU poll and the
 * status chrome from all running simultaneously regardless of what is on screen.
 *
 * The eye renderer is a drawing surface, not a screen: Main, Hatching, Feeding,
 * Bathing and Sleep all paint with it, so eye_fps is a per-screen property
 * rather than a property of the main screen.
 *
 * Cheap 1 Hz bookkeeping (care decay, level/reminder saves, the screensaver and
 * sleep idle countdowns) is deliberately NOT governed here -- it is independent
 * of the display and must keep running on every screen.
 */
namespace ScreenManager {

enum class ScreenId : uint8_t {
    Boot,
    Hatching,
    Main,
    Feeding,
    Bathing,
    Clock,
    Sleep,
    Menu,
    Care,
    Connect,
    Keyboard,
    Settings,
    Notes,
    NoteDetail,
    Reminders,
    ReminderDetail,
    Volume,
    Stats,
    GamesList,
    Level,
    Fortune,
    GreenEyeGame,
    CheckerGame,
    QuickTapGame,
    SnakeGame,
    TiltMazeGame,
    TrafficRunnerGame,
    Pomodoro,
    Celebration,
    Count,
};

/**
 * What a screen needs while it is active.
 *
 * eye_fps == 0 means the eye render timer is paused and its container hidden,
 * so both the CPU render and the LCD flush stop. eye_static freezes the idle
 * float, bounce and flicker lerps while leaving the sleep Z particles drifting.
 */
struct ScreenPolicy {
    uint8_t eye_fps;
    bool    eye_static;
    bool    imu;                 // accelerometer poll (shake -> confused)
    bool    status_chrome;       // state arc + animated status text
    bool    mischief;            // timed mischief engine
    bool    care_emotions;       // care-stat-driven emotion scheduler
    bool    interaction_voices;  // blink / mischief voice lines
};

/** Current screen. Lock-free; safe to call from any task. */
ScreenId Current();

/** Policy of the current screen. Lock-free; safe to call from the IMU timer. */
ScreenPolicy Policy();

/** Policy of a specific screen. */
ScreenPolicy PolicyFor(ScreenId screen);

/**
 * Make screen the active one. No-op if it is already active.
 * Listeners are invoked in the caller's context, as DeviceStateMachine does.
 */
void Set(ScreenId screen);

using ScreenCallback = std::function<void(ScreenId, ScreenId)>;

int  AddListener(ScreenCallback callback);
void RemoveListener(int listener_id);

const char* GetScreenName(ScreenId screen);

}  // namespace ScreenManager

#endif  // SCREEN_MANAGER_H
