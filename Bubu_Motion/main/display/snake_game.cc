#include "snake_game.h"

#include <algorithm>
#include <array>

#include <esp_random.h>
#include <esp_timer.h>

namespace SnakeGame {
namespace {

constexpr uint16_t kStartLength = 3;

// Base step interval, and how much each pellet shortens it.
//
// The floor matters more than the base: the panel repaints at 33ms, so an
// interval under ~80ms puts two steps inside one frame and the snake reads as
// teleporting rather than moving. Every floor below is above that.
struct Profile {
    uint16_t start_interval_ms;
    uint16_t min_interval_ms;
    uint8_t  ramp_ms_per_pellet;
};

constexpr std::array<Profile, static_cast<size_t>(Speed::kSpeedCount)> kProfiles = {{
    {320, 170, 5},   // CHẬM
    {230, 120, 4},   // VỪA
    {165,  85, 3},   // NHANH
}};

struct State {
    bool running = false;
    Speed speed = Speed::kNormal;
    Result result = Result::kNone;

    // Body as a ring buffer: the head sits at `head` and the snake runs
    // backwards from there, so a step is two writes instead of shifting 225
    // cells. Segment i is body[(head + kCellCount - i) % kCellCount].
    std::array<Cell, kCellCount> body = {};
    uint16_t head = 0;
    uint16_t length = 0;

    // Mirror of the body as a flat grid. Self-collision is then O(1) instead
    // of a walk down the whole snake on every step, and picking a free cell
    // for the pellet needs one pass rather than rejection sampling that gets
    // pathological once the board is nearly full.
    std::array<bool, kCellCount> occupied = {};

    Direction direction = Direction::kRight;
    // Turns land between steps, so they are queued rather than applied live.
    // Two is enough for the one case that matters -- a quick right-then-up
    // round a corner -- without letting a mash of taps buffer up a whole path.
    std::array<Direction, 2> queue = {};
    uint8_t queued = 0;

    Cell pellet = {};
    uint16_t score = 0;
    uint32_t next_step_ms = 0;
};

State s;

uint32_t NowMs() {
    return static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
}

int Index(const Cell& c) {
    return static_cast<int>(c.y) * kBoardCells + static_cast<int>(c.x);
}

bool SameCell(const Cell& a, const Cell& b) {
    return a.x == b.x && a.y == b.y;
}

Cell SegmentAt(uint16_t index) {
    const uint16_t slot = static_cast<uint16_t>((s.head + kCellCount - index) % kCellCount);
    return s.body[slot];
}

bool Opposite(Direction a, Direction b) {
    return (a == Direction::kUp && b == Direction::kDown) ||
           (a == Direction::kDown && b == Direction::kUp) ||
           (a == Direction::kLeft && b == Direction::kRight) ||
           (a == Direction::kRight && b == Direction::kLeft);
}

Cell Advance(const Cell& from, Direction direction) {
    Cell next = from;
    switch (direction) {
        case Direction::kUp:    next.y = static_cast<int8_t>(next.y - 1); break;
        case Direction::kDown:  next.y = static_cast<int8_t>(next.y + 1); break;
        case Direction::kLeft:  next.x = static_cast<int8_t>(next.x - 1); break;
        case Direction::kRight: next.x = static_cast<int8_t>(next.x + 1); break;
    }
    return next;
}

bool InBounds(const Cell& c) {
    return c.x >= 0 && c.x < kBoardCells && c.y >= 0 && c.y < kBoardCells;
}

// Uniform over the free cells: pick k in [0, free) and walk to the k-th
// unoccupied square. One pass, and it stays correct when only a handful of
// cells are left -- which is exactly where rejection sampling falls apart.
void SpawnPellet() {
    const int free_cells = kCellCount - static_cast<int>(s.length);
    if (free_cells <= 0) {
        return;
    }
    int target = static_cast<int>(esp_random() % static_cast<uint32_t>(free_cells));
    for (int i = 0; i < kCellCount; ++i) {
        if (s.occupied[i]) {
            continue;
        }
        if (target == 0) {
            s.pellet.x = static_cast<int8_t>(i % kBoardCells);
            s.pellet.y = static_cast<int8_t>(i / kBoardCells);
            return;
        }
        target--;
    }
}

void Finish(Result result) {
    s.running = false;
    s.result = result;
}

const Profile& GetProfile(Speed speed) {
    const size_t index = static_cast<size_t>(speed);
    if (index >= kProfiles.size()) {
        return kProfiles[static_cast<size_t>(Speed::kNormal)];
    }
    return kProfiles[index];
}

// The direction a newly queued turn has to be legal against: the last one
// already queued, not the one currently being travelled. Validating against
// the current direction is the bug -- right then up, both inside one step,
// would each pass on their own and the snake would still fold in half.
Direction PendingDirection() {
    return s.queued > 0 ? s.queue[s.queued - 1] : s.direction;
}

}  // namespace

void Start(Speed speed) {
    s = State{};
    s.running = true;
    s.speed = speed;
    s.result = Result::kNone;
    s.direction = Direction::kRight;

    // Three segments across the middle, heading right, with the head one cell
    // past centre so there is room to read the board before the first step.
    const int8_t mid = static_cast<int8_t>(kBoardCells / 2);
    for (uint16_t i = 0; i < kStartLength; ++i) {
        const Cell c{static_cast<int8_t>(mid + 1 - static_cast<int8_t>(i)), mid};
        const uint16_t slot = static_cast<uint16_t>((kCellCount - i) % kCellCount);
        s.body[slot] = c;
        s.occupied[Index(c)] = true;
    }
    s.head = 0;
    s.length = kStartLength;

    SpawnPellet();
    s.next_step_ms = NowMs() + GetProfile(speed).start_interval_ms;
}

void Stop() {
    if (!s.running) {
        return;
    }
    Finish(Result::kStopped);
}

bool Update() {
    if (!s.running) {
        return false;
    }
    const uint32_t now = NowMs();
    if (static_cast<int32_t>(now - s.next_step_ms) < 0) {
        return false;
    }
    s.next_step_ms = now + StepIntervalMs();

    if (s.queued > 0) {
        s.direction = s.queue[0];
        s.queue[0] = s.queue[1];
        s.queued--;
    }

    const Cell next = Advance(SegmentAt(0), s.direction);
    if (!InBounds(next)) {
        Finish(Result::kHitWall);
        return true;
    }

    const bool eating = SameCell(next, s.pellet);
    // Stepping onto the cell the tail is about to vacate is legal. Without
    // this the snake dies for no visible reason whenever it follows its own
    // tail round a tight loop -- the square looks empty by the time the head
    // is drawn there, because it is.
    const bool onto_tail =
        !eating && s.length >= 2 && SameCell(next, SegmentAt(static_cast<uint16_t>(s.length - 1)));
    if (s.occupied[Index(next)] && !onto_tail) {
        Finish(Result::kHitSelf);
        return true;
    }

    // Pop before push so the grid is consistent for the onto_tail case above.
    if (!eating) {
        const Cell tail = SegmentAt(static_cast<uint16_t>(s.length - 1));
        s.occupied[Index(tail)] = false;
        s.length--;
    }
    s.head = static_cast<uint16_t>((s.head + 1) % kCellCount);
    s.body[s.head] = next;
    s.occupied[Index(next)] = true;
    s.length++;

    if (eating) {
        s.score++;
        if (s.length >= kCellCount) {
            Finish(Result::kFilledBoard);
        } else {
            SpawnPellet();
        }
    }
    return true;
}

bool Steer(Direction direction) {
    if (!s.running) {
        return false;
    }
    if (s.queued >= s.queue.size()) {
        return false;
    }
    const Direction against = PendingDirection();
    if (direction == against || Opposite(direction, against)) {
        return false;
    }
    s.queue[s.queued++] = direction;
    return true;
}

bool IsRunning() {
    return s.running;
}

Speed GetSpeed() {
    return s.speed;
}

Result GetResult() {
    return s.result;
}

Direction GetDirection() {
    return s.direction;
}

uint16_t GetScore() {
    return s.score;
}

uint16_t GetLength() {
    return s.length;
}

uint32_t StepIntervalMs() {
    const Profile& profile = GetProfile(s.speed);
    const int ramped = static_cast<int>(profile.start_interval_ms) -
                       static_cast<int>(s.score) * static_cast<int>(profile.ramp_ms_per_pellet);
    return static_cast<uint32_t>(std::max<int>(ramped, profile.min_interval_ms));
}

Cell GetPellet() {
    return s.pellet;
}

Cell GetSegment(uint16_t index) {
    if (s.length == 0) {
        return Cell{};
    }
    if (index >= s.length) {
        index = 0;
    }
    return SegmentAt(index);
}

}  // namespace SnakeGame
