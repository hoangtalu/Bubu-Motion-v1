"""Deterministically author Tilt Maze level data from the DEVLOG formula.

The script is an offline authoring aid, not runtime firmware.  It searches
seeded braided-DFS candidates, then prints C++ ready for tilt_maze_game.cc.
Run `python3 tools/generate_tilt_maze_levels.py 21 34`.
"""

from __future__ import annotations

from collections import deque
from dataclasses import dataclass
import random
import sys


Cell = tuple[int, int]


@dataclass
class AuthoredLevel:
    number: int
    rows: list[str]
    start: Cell
    goal: Cell
    gold: list[Cell]
    wormholes: list[tuple[Cell, Cell, Cell, Cell, int]]
    boosts: list[tuple[Cell, Cell]]
    path_length: int
    target_seconds: int
    attempt: int


def fnv1a32(text: str) -> int:
    value = 2166136261
    for byte in text.encode("utf-8"):
        value ^= byte
        value = (value * 16777619) & 0xFFFFFFFF
    return value


def chapter_beat(level: int) -> tuple[int, int]:
    return (level - 1) // 10, (level - 1) % 10


def dimensions(beat: int) -> tuple[int, int]:
    widths = (15, 15, 15, 17, 17, 17, 19, 19, 19, 19)
    heights = (9, 9, 9, 11, 11, 11, 13, 13, 13, 13)
    return widths[beat], heights[beat]


def parameters(level: int):
    chapter, beat = chapter_beat(level)
    width, height = dimensions(beat)
    path_target = max(40, min(90, 45 + 2 * beat + (3 * chapter + 1) // 2))
    gold = min(16, 6 + chapter // 2 + beat // 3)
    wormholes = 0 if beat == 0 else min(2, 1 + int(beat >= 4))
    boosts = 0 if beat < 3 else min(4, 1 + (beat - 3) // 3 + chapter // 6)
    loops = min(8, 1 + chapter // 2 + int(beat in (2, 5, 8, 9)))
    return width, height, path_target, gold, wormholes, boosts, loops


def make_maze(width: int, height: int, rng: random.Random, loops: int) -> list[list[str]]:
    grid = [["#"] * width for _ in range(height)]
    start = (1, height - 2)
    grid[start[1]][start[0]] = "."
    stack = [start]
    visited = {start}
    while stack:
        col, row = stack[-1]
        neighbors = []
        for dc, dr in ((2, 0), (-2, 0), (0, 2), (0, -2)):
            nxt = (col + dc, row + dr)
            if 1 <= nxt[0] < width - 1 and 1 <= nxt[1] < height - 1 and nxt not in visited:
                neighbors.append((nxt, (col + dc // 2, row + dr // 2)))
        if not neighbors:
            stack.pop()
            continue
        nxt, between = rng.choice(neighbors)
        grid[between[1]][between[0]] = "."
        grid[nxt[1]][nxt[0]] = "."
        visited.add(nxt)
        stack.append(nxt)

    loop_candidates = connecting_walls(grid)
    rng.shuffle(loop_candidates)
    for wall in loop_candidates[:loops]:
        grid[wall[1]][wall[0]] = "."
    return grid


def connecting_walls(grid: list[list[str]]) -> list[Cell]:
    height, width = len(grid), len(grid[0])
    result = []
    for row in range(1, height - 1):
        for col in range(1, width - 1):
            if grid[row][col] != "#":
                continue
            horizontal = grid[row][col - 1] == "." and grid[row][col + 1] == "."
            vertical = grid[row - 1][col] == "." and grid[row + 1][col] == "."
            if horizontal != vertical:
                result.append((col, row))
    return result


def shortest_path(grid: list[list[str]], start: Cell, goal: Cell,
                  opened: frozenset[Cell] = frozenset()) -> list[Cell]:
    queue = deque([start])
    previous: dict[Cell, Cell | None] = {start: None}
    while queue:
        current = queue.popleft()
        if current == goal:
            break
        col, row = current
        for dc, dr in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            nxt = (col + dc, row + dr)
            if nxt in previous:
                continue
            x, y = nxt
            if not (0 <= y < len(grid) and 0 <= x < len(grid[0])):
                continue
            if grid[y][x] == "#" and nxt not in opened:
                continue
            previous[nxt] = current
            queue.append(nxt)
    if goal not in previous:
        return []
    path = []
    node: Cell | None = goal
    while node is not None:
        path.append(node)
        node = previous[node]
    path.reverse()
    return path


def shortcut_candidates(grid: list[list[str]], start: Cell, goal: Cell,
                        normal_length: int) -> list[tuple[int, Cell, Cell, Cell]]:
    candidates = []
    for wall in connecting_walls(grid):
        col, row = wall
        if grid[row][col - 1] == "." and grid[row][col + 1] == ".":
            pads = (((col - 1, row), (1, 0)), ((col + 1, row), (-1, 0)))
        else:
            pads = (((col, row - 1), (0, 1)), ((col, row + 1), (0, -1)))
        opened = shortest_path(grid, start, goal, frozenset((wall,)))
        gain = normal_length - (len(opened) - 1)
        if gain * 10 < normal_length:
            continue
        for pad, direction in pads:
            candidates.append((gain, wall, pad, direction))
    candidates.sort(reverse=True)
    return candidates


def choose_shortcuts(candidates, count: int, reserved: set[Cell]):
    if count == 0:
        return []
    selected = []
    walls = set()
    pads = set()
    for _gain, wall, pad, direction in candidates:
        if wall in walls or pad in pads or pad in reserved:
            continue
        if any(abs(pad[0] - other[0]) + abs(pad[1] - other[1]) <= 1 for other in pads):
            continue
        selected.append((wall, pad, direction))
        walls.add(wall)
        pads.add(pad)
        if len(selected) == count:
            break
    return selected


def pick_path_cell(path: list[Cell], fraction: float, reserved: set[Cell]) -> Cell:
    center = max(2, min(len(path) - 3, int((len(path) - 1) * fraction)))
    for delta in range(len(path)):
        for index in (center + delta, center - delta):
            if 1 <= index < len(path) - 1 and path[index] not in reserved:
                return path[index]
    raise RuntimeError("no free path cell")


def author_level(number: int) -> AuthoredLevel:
    width, height, target, gold_count, worm_count, boost_count, loops = parameters(number)
    start, goal = (1, height - 2), (width - 2, 1)
    base_seed = fnv1a32(f"BUBU_MAZE_V1:{number}")
    best = None
    for attempt in range(12000):
        rng = random.Random((base_seed + attempt * 0x9E3779B9) & 0xFFFFFFFF)
        grid = make_maze(width, height, rng, loops)
        path = shortest_path(grid, start, goal)
        path_length = len(path) - 1
        shortcut_options = shortcut_candidates(grid, start, goal, path_length)
        score = abs(path_length - target) * 10 + max(0, boost_count * 2 - len(shortcut_options)) * 1000
        if best is None or score < best[0]:
            best = (score, attempt, rng, grid, path, shortcut_options)
        if abs(path_length - target) <= 2 and len(shortcut_options) >= boost_count * 2:
            break
    assert best is not None
    _, attempt, rng, grid, path, shortcut_options = best
    path_length = len(path) - 1
    reserved = {start, goal}
    shortcuts = choose_shortcuts(shortcut_options, boost_count, reserved)
    if len(shortcuts) != boost_count:
        raise RuntimeError(f"L{number}: found only {len(shortcuts)}/{boost_count} shortcuts")
    for wall, pad, _direction in shortcuts:
        grid[wall[1]][wall[0]] = "X"
        reserved.add(pad)

    wormholes = []
    fractions = ((0.22, 0.68), (0.38, 0.84))
    offsets = (((-10, 0), (10, 0)), ((0, -10), (0, 10)))
    for style in range(worm_count):
        a = pick_path_cell(path, fractions[style][0], reserved)
        reserved.add(a)
        b = pick_path_cell(path, fractions[style][1], reserved)
        reserved.add(b)
        wormholes.append((a, b, offsets[style][0], offsets[style][1], style))

    floor = [
        (col, row)
        for row in range(1, height - 1)
        for col in range(1, width - 1)
        if grid[row][col] == "." and (col, row) not in reserved
    ]
    main = [cell for cell in path[1:-1] if cell not in reserved and grid[cell[1]][cell[0]] == "."]
    side = [cell for cell in floor if cell not in set(path)]
    rng.shuffle(main)
    rng.shuffle(side)
    main_count = min(len(main), (gold_count * 3 + 4) // 5)
    gold = main[:main_count]
    for pool in (side, main[main_count:], floor):
        for cell in pool:
            if cell not in gold:
                gold.append(cell)
                if len(gold) == gold_count:
                    break
        if len(gold) == gold_count:
            break
    if len(gold) != gold_count:
        raise RuntimeError(f"L{number}: found only {len(gold)}/{gold_count} gold cells")

    boost_data = [(pad, direction) for _wall, pad, direction in shortcuts]
    target_seconds = int(4 + 2.5 * path_length + 0.5)
    return AuthoredLevel(
        number, ["".join(row) for row in grid], start, goal, gold,
        wormholes, boost_data, path_length, target_seconds, attempt,
    )


def cells(items: list[Cell]) -> str:
    return ", ".join(f"{{{col}, {row}}}" for col, row in items)


def print_cpp(level: AuthoredLevel):
    n = level.number
    print(f"// L{n}: generated attempt {level.attempt}, normal route {level.path_length} cells")
    print(f"constexpr const char* kLevel{n}Map[] = {{")
    for row in level.rows:
        print(f'    "{row}",')
    print("};")
    print(f"constexpr Cell kLevel{n}Gold[] = {{{cells(level.gold)}}};")
    if level.wormholes:
        print(f"constexpr WormholePair kLevel{n}Wormholes[] = {{")
        for a, b, ao, bo, style in level.wormholes:
            print(f"    {{{{{a[0]}, {a[1]}}}, {{{b[0]}, {b[1]}}}, "
                  f"{ao[0]}, {ao[1]}, {bo[0]}, {bo[1]}, {style}}},")
        print("};")
    if level.boosts:
        print(f"constexpr BoostPad kLevel{n}Boosts[] = {{")
        for pad, direction in level.boosts:
            print(f"    {{{{{pad[0]}, {pad[1]}}}, {direction[0]}, {direction[1]}}},")
        print("};")
    worm_ptr = f"kLevel{n}Wormholes" if level.wormholes else "nullptr"
    boost_ptr = f"kLevel{n}Boosts" if level.boosts else "nullptr"
    print(f"// TABLE: {{{len(level.rows[0])}, {len(level.rows)}, kLevel{n}Map, "
          f"{{{level.start[0]}, {level.start[1]}}}, {{{level.goal[0]}, {level.goal[1]}}}, "
          f"kLevel{n}Gold, {len(level.gold)}, {worm_ptr}, {len(level.wormholes)}, "
          f"{boost_ptr}, {len(level.boosts)}, {level.target_seconds}}},")
    print()


def main() -> int:
    first = int(sys.argv[1]) if len(sys.argv) > 1 else 21
    last = int(sys.argv[2]) if len(sys.argv) > 2 else first
    if not (1 <= first <= last <= 200):
        raise SystemExit("usage: generate_tilt_maze_levels.py FIRST LAST (1..200)")
    for number in range(first, last + 1):
        print_cpp(author_level(number))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
