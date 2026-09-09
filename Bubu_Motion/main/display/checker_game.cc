#include "checker_game.h"

#include <array>
#include <cstddef>
#include <cstdint>

#include <esp_random.h>

namespace CheckerGame {
namespace {

struct Line {
    uint8_t a;
    uint8_t b;
    uint8_t c;
};

struct State {
    bool running = false;
    bool player_turn = true;
    Result result = Result::kNone;
    Config config = {};
    std::array<Cell, 9> cells = {
        Cell::kEmpty, Cell::kEmpty, Cell::kEmpty,
        Cell::kEmpty, Cell::kEmpty, Cell::kEmpty,
        Cell::kEmpty, Cell::kEmpty, Cell::kEmpty,
    };
    std::array<uint8_t, 3> winning_line = {0, 0, 0};
    bool has_winning_line = false;
};

constexpr std::array<Line, 8> kWinningLines = {{
    {0, 1, 2},
    {3, 4, 5},
    {6, 7, 8},
    {0, 3, 6},
    {1, 4, 7},
    {2, 5, 8},
    {0, 4, 8},
    {2, 4, 6},
}};

State state;

uint32_t RandomRange(uint32_t max_exclusive) {
    if (max_exclusive == 0) {
        return 0;
    }
    return esp_random() % max_exclusive;
}

bool Chance(uint8_t pct) {
    if (pct >= 100) {
        return true;
    }
    if (pct == 0) {
        return false;
    }
    return RandomRange(100) < pct;
}

void ClearBoard() {
    state.cells.fill(Cell::kEmpty);
    state.winning_line = {0, 0, 0};
    state.has_winning_line = false;
}

bool IsBoardFull(const std::array<Cell, 9>& cells) {
    for (Cell cell : cells) {
        if (cell == Cell::kEmpty) {
            return false;
        }
    }
    return true;
}

Cell GetWinner(const std::array<Cell, 9>& cells, std::array<uint8_t, 3>* winning_line = nullptr) {
    for (const auto& line : kWinningLines) {
        const Cell first = cells[line.a];
        if (first == Cell::kEmpty) {
            continue;
        }
        if (first == cells[line.b] && first == cells[line.c]) {
            if (winning_line != nullptr) {
                *winning_line = {line.a, line.b, line.c};
            }
            return first;
        }
    }
    return Cell::kEmpty;
}

void Finish(Result result) {
    state.running = false;
    state.player_turn = true;
    state.result = result;
}

int EvaluateBoard(std::array<Cell, 9>& cells, bool bubu_turn, int depth) {
    const Cell winner = GetWinner(cells);
    if (winner == Cell::kBubu) {
        return 10 - depth;
    }
    if (winner == Cell::kPlayer) {
        return depth - 10;
    }
    if (IsBoardFull(cells)) {
        return 0;
    }

    if (bubu_turn) {
        int best_score = -1000;
        for (size_t index = 0; index < cells.size(); ++index) {
            if (cells[index] != Cell::kEmpty) {
                continue;
            }
            cells[index] = Cell::kBubu;
            const int score = EvaluateBoard(cells, false, depth + 1);
            cells[index] = Cell::kEmpty;
            if (score > best_score) {
                best_score = score;
            }
        }
        return best_score;
    }

    int best_score = 1000;
    for (size_t index = 0; index < cells.size(); ++index) {
        if (cells[index] != Cell::kEmpty) {
            continue;
        }
        cells[index] = Cell::kPlayer;
        const int score = EvaluateBoard(cells, true, depth + 1);
        cells[index] = Cell::kEmpty;
        if (score < best_score) {
            best_score = score;
        }
    }
    return best_score;
}

uint8_t FindImmediateWinningMove(Cell player) {
    for (uint8_t index = 0; index < state.cells.size(); ++index) {
        if (state.cells[index] != Cell::kEmpty) {
            continue;
        }
        state.cells[index] = player;
        const Cell winner = GetWinner(state.cells);
        state.cells[index] = Cell::kEmpty;
        if (winner == player) {
            return index;
        }
    }
    return 0xFF;
}

uint8_t ChooseImperfectMove() {
    static constexpr std::array<uint8_t, 4> kCorners = {0, 2, 6, 8};
    static constexpr std::array<uint8_t, 4> kEdges = {1, 3, 5, 7};

    if (state.cells[4] == Cell::kEmpty && Chance(state.config.take_center_chance_pct)) {
        return 4;
    }

    std::array<uint8_t, 4> open_corners = {};
    uint8_t open_corner_count = 0;
    for (uint8_t cell : kCorners) {
        if (state.cells[cell] == Cell::kEmpty) {
            open_corners[open_corner_count++] = cell;
        }
    }
    if (open_corner_count > 0 && Chance(state.config.take_corner_chance_pct)) {
        return open_corners[RandomRange(open_corner_count)];
    }

    std::array<uint8_t, 9> open_cells = {};
    uint8_t open_count = 0;
    for (uint8_t index = 0; index < state.cells.size(); ++index) {
        if (state.cells[index] == Cell::kEmpty) {
            open_cells[open_count++] = index;
        }
    }
    if (open_count == 0) {
        return 0xFF;
    }

    std::array<uint8_t, 4> open_edges = {};
    uint8_t open_edge_count = 0;
    for (uint8_t cell : kEdges) {
        if (state.cells[cell] == Cell::kEmpty) {
            open_edges[open_edge_count++] = cell;
        }
    }
    if (open_edge_count > 0 && !Chance(state.config.take_corner_chance_pct)) {
        return open_edges[RandomRange(open_edge_count)];
    }

    return open_cells[RandomRange(open_count)];
}

uint8_t ChooseBestBubuMove() {
    const uint8_t winning_move = FindImmediateWinningMove(Cell::kBubu);
    if (winning_move < state.cells.size()) {
        return winning_move;
    }

    const uint8_t blocking_move = FindImmediateWinningMove(Cell::kPlayer);
    if (blocking_move < state.cells.size() && Chance(state.config.block_player_chance_pct)) {
        return blocking_move;
    }

    if (!Chance(state.config.take_best_move_chance_pct)) {
        return ChooseImperfectMove();
    }

    std::array<Cell, 9> scratch = state.cells;
    int best_score = -1000;
    uint8_t best_move = 0xFF;

    for (uint8_t index = 0; index < scratch.size(); ++index) {
        if (scratch[index] != Cell::kEmpty) {
            continue;
        }
        scratch[index] = Cell::kBubu;
        const int score = EvaluateBoard(scratch, false, 0);
        scratch[index] = Cell::kEmpty;
        if (score > best_score) {
            best_score = score;
            best_move = index;
        }
    }

    return best_move;
}

bool FinishIfResolvedFor(Cell player) {
    std::array<uint8_t, 3> winning_line = {0, 0, 0};
    const Cell winner = GetWinner(state.cells, &winning_line);
    if (winner == player) {
        state.winning_line = winning_line;
        state.has_winning_line = true;
        Finish(player == Cell::kPlayer ? Result::kPlayerWin : Result::kBubuWin);
        return true;
    }
    if (IsBoardFull(state.cells)) {
        Finish(Result::kDraw);
        return true;
    }
    return false;
}

}  // namespace

void Configure(const Config& config) {
    state.config = config;
}

void Start() {
    ClearBoard();
    state.running = true;
    state.player_turn = true;
    state.result = Result::kNone;
}

void Stop() {
    if (!state.running) {
        return;
    }
    Finish(Result::kStopped);
}

TapOutcome HandleTap(uint8_t cell_index) {
    if (!state.running || !state.player_turn || cell_index >= state.cells.size()) {
        return TapOutcome::kIgnored;
    }
    if (state.cells[cell_index] != Cell::kEmpty) {
        return TapOutcome::kIgnored;
    }

    state.cells[cell_index] = Cell::kPlayer;
    if (!FinishIfResolvedFor(Cell::kPlayer)) {
        state.player_turn = false;
    }

    return TapOutcome::kPlaced;
}

void PlayBubuTurn() {
    if (!state.running || state.player_turn) {
        return;
    }

    const uint8_t bubu_move = ChooseBestBubuMove();
    if (bubu_move < state.cells.size()) {
        state.cells[bubu_move] = Cell::kBubu;
    }
    if (!FinishIfResolvedFor(Cell::kBubu)) {
        state.player_turn = true;
    }
}

bool IsRunning() {
    return state.running;
}

bool IsPlayerTurn() {
    return state.player_turn;
}

Cell GetCell(uint8_t cell_index) {
    if (cell_index >= state.cells.size()) {
        return Cell::kEmpty;
    }
    return state.cells[cell_index];
}

Result GetResult() {
    return state.result;
}

bool IsWinningCell(uint8_t cell_index) {
    if (!state.has_winning_line) {
        return false;
    }
    return state.winning_line[0] == cell_index ||
           state.winning_line[1] == cell_index ||
           state.winning_line[2] == cell_index;
}

}  // namespace CheckerGame
