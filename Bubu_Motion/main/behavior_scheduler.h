#pragma once

namespace BehaviorScheduler {

// Initializes scheduler state and loads persisted configuration.
void Begin();

// Records explicit user interaction (touch, wake word, menu actions).
void SetLastInteractionTime();

// Periodic evaluator. Safe to call frequently; internal throttling is 60s.
void Tick();

// Idle timeout control (minutes), persisted in NVS.
int GetIdleTimeoutMinutes();
bool SetIdleTimeoutMinutes(int minutes);

}  // namespace BehaviorScheduler
