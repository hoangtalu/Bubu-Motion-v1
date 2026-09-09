#pragma once

#include <cstdint>

namespace CheckerGame {

struct Config {
    uint8_t take_best_move_chance_pct = 45;
    uint8_t block_player_chance_pct = 72;
    uint8_t take_center_chance_pct = 65;
    uint8_t take_corner_chance_pct = 75;
};

enum class Cell : uint8_t {
    kEmpty = 0,
    kPlayer,
    kBubu,
};

enum class Result : uint8_t {
    kNone = 0,
    kPlayerWin,
    kBubuWin,
    kDraw,
    kStopped,
};

enum class TapOutcome : uint8_t {
    kIgnored = 0,
    kPlaced,
};

void Configure(const Config& config);
void Start();
void Stop();
// Places the player's mark only. If the game is still running afterwards,
// it becomes Bubu's turn — call PlayBubuTurn() to resolve it (the caller
// may delay that call to show a "thinking" pause).
TapOutcome HandleTap(uint8_t cell_index);
void PlayBubuTurn();

bool IsRunning();
bool IsPlayerTurn();
Cell GetCell(uint8_t cell_index);
Result GetResult();
bool IsWinningCell(uint8_t cell_index);

}  // namespace CheckerGame
