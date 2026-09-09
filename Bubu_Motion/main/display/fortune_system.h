#pragma once

#include <cstdint>

class Display;

namespace FortuneSystem {

using FinishedCallback = void (*)();

void Begin(Display* display);
void Open(FinishedCallback on_finished = nullptr);
void Close(bool invoke_callback = false);
bool IsOpen();
bool HandleTap(uint16_t x, uint16_t y);
bool HandleLongPress(uint16_t x, uint16_t y);

}  // namespace FortuneSystem
