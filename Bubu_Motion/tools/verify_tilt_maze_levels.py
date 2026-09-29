"""Validate every Tilt Maze level directly from the firmware C++ tables.

This is intentionally a source-level test.  A level is rejected before the
firmware build if its geometry, feature references, or closed-wall route is
invalid.  Run from anywhere with Python 3; no ESP-IDF environment is needed.
"""

from __future__ import annotations

from collections import deque
from dataclasses import dataclass
from pathlib import Path
import re
import sys


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "main/display/tilt_maze_game.cc"
HEADER = ROOT / "main/display/tilt_maze_game.h"


@dataclass(frozen=True)
class Level:
    number: int
    width: int
    height: int
    rows: tuple[str, ...]
    start: tuple[int, int]
    goal: tuple[int, int]
    gold: tuple[tuple[int, int], ...]
    wormholes: tuple[tuple[int, ...], ...]
    boosts: tuple[tuple[int, int, int, int], ...]
    target_seconds: int


def quoted_rows(body: str) -> tuple[str, ...]:
    return tuple(re.findall(r'"([#.X]+)"', body))


def array_bodies(source: str, declaration: str) -> dict[int, str]:
    pattern = rf"constexpr {declaration} kLevel(\d+)\w*\[.*?\]\s*=\s*\{{(.*?)\}};"
    return {int(number): body for number, body in re.findall(pattern, source, re.S)}


def parse_maps(source: str) -> dict[int, tuple[str, ...]]:
    maps: dict[int, tuple[str, ...]] = {}
    row_bodies = array_bodies(source, r"char")
    for number, body in row_bodies.items():
        rows = quoted_rows(body)
        if rows:
            maps[number] = rows

    pattern = r"constexpr const char\* kLevel(\d+)Map\[\]\s*=\s*\{(.*?)\};"
    for number_text, body in re.findall(pattern, source, re.S):
        number = int(number_text)
        rows = quoted_rows(body)
        if rows:
            maps[number] = rows
        elif number not in maps:
            raise AssertionError(f"L{number}: map contains no rows")
    return maps


def parse_cells(source: str, suffix: str) -> dict[int, tuple[tuple[int, int], ...]]:
    pattern = rf"constexpr Cell kLevel(\d+){suffix}\[\]\s*=\s*\{{(.*?)\}};"
    result = {}
    for number, body in re.findall(pattern, source, re.S):
        result[int(number)] = tuple(
            (int(col), int(row))
            for col, row in re.findall(r"\{\s*(\d+)\s*,\s*(\d+)\s*\}", body)
        )
    return result


def parse_wormholes(source: str) -> dict[int, tuple[tuple[int, ...], ...]]:
    pattern = r"constexpr WormholePair kLevel(\d+)Wormholes\[\]\s*=\s*\{(.*?)\};"
    item = re.compile(
        r"\{\s*\{\s*(\d+)\s*,\s*(\d+)\s*\}\s*,"
        r"\s*\{\s*(\d+)\s*,\s*(\d+)\s*\}\s*,"
        r"\s*(-?\d+)\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*,"
        r"\s*(-?\d+)\s*,\s*(\d+)\s*\}"
    )
    return {
        int(number): tuple(tuple(map(int, match)) for match in item.findall(body))
        for number, body in re.findall(pattern, source, re.S)
    }


def parse_boosts(source: str) -> dict[int, tuple[tuple[int, int, int, int], ...]]:
    pattern = r"constexpr BoostPad kLevel(\d+)Boosts\[\]\s*=\s*\{(.*?)\};"
    item = re.compile(
        r"\{\s*\{\s*(\d+)\s*,\s*(\d+)\s*\}\s*,"
        r"\s*(-?\d+)\s*,\s*(-?\d+)\s*\}"
    )
    return {
        int(number): tuple(tuple(map(int, match)) for match in item.findall(body))
        for number, body in re.findall(pattern, source, re.S)
    }


def level_records(source: str) -> list[str]:
    marker = "constexpr LevelDefinition kLevels[] = {"
    start = source.index(marker) + len(marker) - 1
    depth = 0
    record_start = None
    records = []
    for index in range(start, len(source)):
        char = source[index]
        if char == "{":
            depth += 1
            if depth == 2:
                record_start = index
        elif char == "}":
            if depth == 2 and record_start is not None:
                records.append(source[record_start:index + 1])
                record_start = None
            depth -= 1
            if depth == 0:
                break
    return records


def parse_levels(source: str) -> list[Level]:
    maps = parse_maps(source)
    gold = parse_cells(source, "Gold")
    wormholes = parse_wormholes(source)
    boosts = parse_boosts(source)
    record_pattern = re.compile(
        r"^\{(\d+),(\d+),kLevel(\d+)Map,"
        r"\{(\d+),(\d+)\},\{(\d+),(\d+)\},"
        r"kLevel(\d+)Gold,(\d+),(?:kLevel\d+Wormholes|nullptr),(\d+),"
        r"(?:kLevel\d+Boosts|nullptr),(\d+),(\d+)\}$"
    )
    levels = []
    for expected, record in enumerate(level_records(source), start=1):
        compact = re.sub(r"\s+", "", record)
        match = record_pattern.match(compact)
        if not match:
            raise AssertionError(f"Cannot parse level table record {expected}: {compact}")
        (width, height, map_number, sx, sy, gx, gy, gold_number,
         gold_count, wormhole_count, boost_count, target) = map(int, match.groups())
        if map_number != expected or gold_number != expected:
            raise AssertionError(f"L{expected}: table references another level's data")
        level_gold = gold.get(expected, ())
        level_wormholes = wormholes.get(expected, ())
        level_boosts = boosts.get(expected, ())
        if len(level_gold) != gold_count:
            raise AssertionError(f"L{expected}: gold count table={gold_count}, data={len(level_gold)}")
        if len(level_wormholes) != wormhole_count:
            raise AssertionError(
                f"L{expected}: wormhole count table={wormhole_count}, data={len(level_wormholes)}"
            )
        if len(level_boosts) != boost_count:
            raise AssertionError(f"L{expected}: boost count table={boost_count}, data={len(level_boosts)}")
        levels.append(Level(
            expected, width, height, maps[expected], (sx, sy), (gx, gy),
            level_gold, level_wormholes, level_boosts, target,
        ))
    return levels


def distances(level: Level, opened: frozenset[tuple[int, int]] = frozenset()):
    distance = {level.start: 0}
    queue = deque([level.start])
    while queue:
        col, row = queue.popleft()
        for dc, dr in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            nxt = (col + dc, row + dr)
            if nxt in distance:
                continue
            x, y = nxt
            if not (0 <= x < level.width and 0 <= y < level.height):
                continue
            tile = level.rows[y][x]
            if tile == "#" or (tile == "X" and nxt not in opened):
                continue
            distance[nxt] = distance[(col, row)] + 1
            queue.append(nxt)
    return distance


def validate(level: Level) -> tuple[list[str], list[str]]:
    errors: list[str] = []
    notes: list[str] = []

    if len(level.rows) != level.height:
        errors.append(f"height={level.height}, rows={len(level.rows)}")
    for row_number, row in enumerate(level.rows):
        if len(row) != level.width:
            errors.append(f"row {row_number} width={len(row)}, expected={level.width}")
        if any(tile not in "#.X" for tile in row):
            errors.append(f"row {row_number} has an unknown tile")
    if level.rows and (
        any(tile != "#" for tile in level.rows[0])
        or any(tile != "#" for tile in level.rows[-1])
        or any(row[0] != "#" or row[-1] != "#" for row in level.rows)
    ):
        errors.append("outer perimeter is not solid")

    def require_floor(name: str, cell: tuple[int, int]):
        col, row = cell
        if not (0 <= col < level.width and 0 <= row < level.height):
            errors.append(f"{name} {cell} is outside the map")
        elif level.rows[row][col] != ".":
            errors.append(f"{name} {cell} is on '{level.rows[row][col]}' instead of floor")

    require_floor("start", level.start)
    require_floor("goal", level.goal)
    if not (1 <= len(level.gold) <= 16):
        errors.append(f"gold count {len(level.gold)} is outside 1..16")
    if len(set(level.gold)) != len(level.gold):
        errors.append("gold contains duplicate cells")
    for index, cell in enumerate(level.gold):
        require_floor(f"gold[{index}]", cell)

    if len(level.wormholes) > 2:
        errors.append(f"wormhole count {len(level.wormholes)} exceeds the two UI styles")
    wormhole_cells = []
    for index, wormhole in enumerate(level.wormholes):
        ax, ay, bx, by, aox, aoy, box, boy, style = wormhole
        require_floor(f"wormhole[{index}].a", (ax, ay))
        require_floor(f"wormhole[{index}].b", (bx, by))
        wormhole_cells.extend(((ax, ay), (bx, by)))
        if (ax, ay) == (bx, by):
            errors.append(f"wormhole[{index}] has identical endpoints")
        if style not in (0, 1):
            errors.append(f"wormhole[{index}] style {style} has no UI color")
        for label, ox, oy in (("a", aox, aoy), ("b", box, boy)):
            if (ox, oy) not in ((-10, 0), (10, 0), (0, -10), (0, 10)):
                errors.append(f"wormhole[{index}].{label} offset {(ox, oy)} is not an approved offset")
    if len(set(wormhole_cells)) != len(wormhole_cells):
        errors.append("wormhole endpoints overlap each other")

    breakables = {
        (col, row)
        for row, line in enumerate(level.rows)
        for col, tile in enumerate(line)
        if tile == "X"
    }
    if len(breakables) > 16:
        errors.append(f"breakable count {len(breakables)} exceeds the 16-bit mask")
    targeted = []
    boost_cells = []
    for index, (col, row, dx, dy) in enumerate(level.boosts):
        require_floor(f"boost[{index}]", (col, row))
        boost_cells.append((col, row))
        if (dx, dy) not in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            errors.append(f"boost[{index}] direction {(dx, dy)} is not cardinal")
            continue
        target = (col + dx, row + dy)
        targeted.append(target)
        if target not in breakables:
            errors.append(f"boost[{index}] points to {target}, which is not X")
    if len(set(boost_cells)) != len(boost_cells):
        errors.append("boost pads overlap each other")
    if set(targeted) != breakables:
        errors.append(f"boost targets {set(targeted)} do not match X cells {breakables}")

    route = distances(level)
    if level.goal not in route:
        errors.append("goal is unreachable with every X closed and wormholes unused")
    for index, cell in enumerate(level.gold):
        if cell not in route:
            errors.append(f"gold[{index}] {cell} is unreachable on the normal route")
    for index, cell in enumerate(wormhole_cells):
        if cell not in route:
            errors.append(f"wormhole endpoint {index} {cell} is unreachable")
    for index, cell in enumerate(boost_cells):
        if cell not in route:
            errors.append(f"boost[{index}] {cell} is unreachable")

    if level.goal in route:
        normal = route[level.goal]
        all_open = distances(level, frozenset(breakables)).get(level.goal, normal)
        notes.append(f"route={normal}, all-X-open={all_open}, target={level.target_seconds}s")
        for wall in sorted(breakables):
            opened = distances(level, frozenset((wall,))).get(level.goal, normal)
            gain = normal - opened
            if gain <= 0:
                errors.append(f"X at {wall} does not shorten the start-goal route")
            elif gain * 10 < normal:
                errors.append(f"X at {wall} shortcut gain {gain}/{normal} is below 10%")

    occupied = set(level.gold) & (set(wormhole_cells) | set(boost_cells))
    if occupied:
        notes.append(f"intentional feature overlap at {sorted(occupied)}")
    return errors, notes


def main() -> int:
    source = SOURCE.read_text(encoding="utf-8")
    header = HEADER.read_text(encoding="utf-8")
    match = re.search(r"kImplementedLevels\s*=\s*(\d+)", header)
    if not match:
        raise AssertionError("kImplementedLevels is missing")
    implemented = int(match.group(1))
    levels = parse_levels(source)
    failed = False
    if len(levels) != implemented:
        print(f"FAIL table has {len(levels)} levels, header says {implemented}")
        failed = True

    print(f"Tilt Maze source validation: {len(levels)} levels")
    for level in levels:
        errors, notes = validate(level)
        status = "FAIL" if errors else "PASS"
        detail = "; ".join(notes)
        print(f"  {status} L{level.number:02d} {level.width}x{level.height} "
              f"gold={len(level.gold)} wh={len(level.wormholes)} boost={len(level.boosts)}; {detail}")
        for error in errors:
            print(f"       - {error}")
        failed |= bool(errors)
    print("RESULT:", "FAILED" if failed else "all levels valid")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
