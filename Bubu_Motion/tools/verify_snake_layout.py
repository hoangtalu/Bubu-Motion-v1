"""Read the RẮN SĂN MỒI layout constants out of menu_system.cc and re-test them.

Same contract as verify_games_layout.py: this deliberately does not restate the
numbers. Edit a constant without re-measuring and this fails.

Covers three things the round screen can silently break:
  * every string's worst ink corner against the ring's inner edge,
  * every pill's worst corner against the same edge,
  * the board's corners, and that the chevrons sit in the gaps the board leaves
    rather than under it or under the score line.

It also refuses any string containing a glyph the compiled VN faces lack. That
check is worth keeping even though nothing currently trips it: while writing it
lvwidth.py reported Ư/ư (U+01AF/U+01B0) as absent from all three faces, which
turned out to be a hex-parsing bug in lvwidth.arr() rather than a real gap --
see the comment there. The fonts do carry them.
"""
import contextlib, io, math, re, sys
# fit.py prints a report at import time; this file has its own.
with contextlib.redirect_stdout(io.StringIO()):
    from fit import FONTS, BOXES, ink, R_RING_IN
    from lvwidth import width, gid

SRC = '/Users/judes/Downloads/Bubu-Motion-v1-main/Bubu_Motion/main/display/menu_system.cc'
src = open(SRC, encoding='utf-8').read()


def const(name):
    m = re.search(r'constexpr int ' + name + r'\s*=\s*(-?\d+)', src)
    assert m, 'missing ' + name
    return int(m.group(1))


RING_IN = R_RING_IN          # 109.0, the ring's inner edge
CLIP_R = 112.0               # gamesPanel content circle (240px, 8px border)

GAME_SRC = ('/Users/judes/Downloads/Bubu-Motion-v1-main/Bubu_Motion/'
            'main/display/snake_game.h')
game_src = open(GAME_SRC, encoding='utf-8').read()
m = re.search(r'constexpr int kBoardCells\s*=\s*(\d+)', game_src)
assert m, 'missing kBoardCells in snake_game.h'
CELLS = int(m.group(1))

CELL = const('kSnakeCell')
BOARD = CELLS * CELL        # kSnakeBoardPx, recomputed rather than restated
BOARD_DY = const('kSnakeBoardOffsetY')

# LVGL: a label aligned CENTER at dy has its box centred on 120+dy, and puts
# the baseline at box_top + line_height - base_line.
MET = {20: (27, 6), 22: (29, 6), 28: (37, 8)}


def baseline(dy, size):
    lh, bl = MET[size]
    return 120 + dy - lh / 2.0 + (lh - bl)


# (label, text, size, dy-constant name)
TEXTS = [
    ('score',      '999',          20, 'kSnakeScoreDy'),
    ('title',      'RẮN',          28, 'kSnakeTitleDy'),
    ('best',       'KỶ LỤC 999',   20, 'kSnakeBestDy'),
    ('speed cap',  'TỐC ĐỘ',       20, 'kSnakeSpeedCapDy'),
    ('speed',      'NHANH',        20, 'kSnakeSpeedDy'),
    ('play',       'CHƠI',         22, 'kSnakePlayDy'),
    ('hint',       'Vuốt để lái',  20, 'kSnakeHintDy'),
    ('over head',  'ĐỤNG VÁCH',    22, 'kSnakeOverHeadDy'),
    ('over head',  'CẮN ĐUÔI',     22, 'kSnakeOverHeadDy'),
    ('over head',  'ĂN HẾT BÀN!',  22, 'kSnakeOverHeadDy'),
    ('over head',  'ĐÃ DỪNG',      22, 'kSnakeOverHeadDy'),
    ('over score', 'ĐIỂM 999',     20, 'kSnakeOverScoreDy'),
    ('over score', 'KỶ LỤC MỚI!',  20, 'kSnakeOverScoreDy'),
    ('over best',  'KỶ LỤC 999',   20, 'kSnakeOverBestDy'),
    ('again',      'CHƠI LẠI',     20, 'kSnakeAgainDy'),
    ('menu',       'MENU',         20, 'kSnakeMenuDy'),
]

# (label, width-constant, height-constant, dy-constant)
PILLS = [
    ('speed pill', 'kSnakeSpeedPillW', 'kSnakeSpeedPillH', 'kSnakeSpeedDy'),
    ('play pill',  'kSnakePlayPillW',  'kSnakePlayPillH',  'kSnakePlayDy'),
    ('again pill', 'kSnakeAgainPillW', 'kSnakeAgainPillH', 'kSnakeAgainDy'),
    ('menu pill',  'kSnakeMenuPillW',  'kSnakeMenuPillH',  'kSnakeMenuDy'),
]

ok = True
worst = 0.0

print(f"constants read from menu_system.cc:")
print(f"  board {BOARD}px ({CELLS}x{CELLS} @ {CELL}px), dy={BOARD_DY:+d}")
print(f"  ring inner edge {RING_IN}, panel clip circle {CLIP_R}\n")

print("text:")
for label, text, size, dyname in TEXTS:
    dy = const(dyname)
    w, missing = width(text, FONTS[size])
    if missing:
        print(f"  FAIL  {label:<11} {text:<14} missing glyphs {missing}")
        ok = False
        continue
    # width() only reports glyphs it could not advance; check coverage directly
    # too, because a gid of 0 still advances and just draws nothing.
    absent = [c for c in text if gid(ord(c), FONTS[size][1]) == 0]
    if absent:
        print(f"  FAIL  {label:<11} {text:<14} glyphs absent from vn_{size}: "
              + ' '.join('U+%04X' % ord(c) for c in absent))
        ok = False
        continue
    base = baseline(dy, size)
    a, d = ink(text, FONTS[size], BOXES[size])
    itop, ibot = base - a, base + d
    r = math.hypot(w / 2.0, max(abs(itop - 120), abs(ibot - 120)))
    worst = max(worst, r)
    good = r <= RING_IN
    ok &= good
    print(f"  {'PASS' if good else 'FAIL'}  {label:<11} {text:<14} vn_{size} "
          f"dy={dy:+4d} w={w:3d} r={r:6.2f} clear {RING_IN - r:+6.2f}")

print("\npills:")
for label, wn, hn, dyn in PILLS:
    pw, ph, dy = const(wn), const(hn), const(dyn)
    r = math.hypot(pw / 2.0, abs(dy) + ph / 2.0)
    worst = max(worst, r)
    good = r <= RING_IN
    ok &= good
    print(f"  {'PASS' if good else 'FAIL'}  {label:<11} {pw}x{ph} dy={dy:+4d} "
          f"r={r:6.2f} clear {RING_IN - r:+6.2f}")

print("\nboard:")
half = BOARD / 2.0
corner = math.hypot(half, abs(BOARD_DY) + half)
good = corner <= CLIP_R
ok &= good
print(f"  {'PASS' if good else 'FAIL'}  corners   r={corner:6.2f} "
      f"clear vs clip {CLIP_R} = {CLIP_R - corner:+6.2f}")

print("\nchevrons (must sit in the gaps the board leaves, not under it):")
up_dy = const('kSnakeChevronUpDy')
down_dy = const('kSnakeChevronDownDy')
side_dx = const('kSnakeChevronSideDx')
CH_LONG, CH_SHORT = 23, 10          # CreateSnakeChevron's box, long x short
board_top = 120 + BOARD_DY - half
board_bot = 120 + BOARD_DY + half
board_left = 120 - half
board_right = 120 + half

checks = [
    ('up',    math.hypot(CH_LONG / 2.0, abs(up_dy) + CH_SHORT / 2.0),
     (120 + up_dy + CH_SHORT / 2.0) <= board_top, 'above the board'),
    ('down',  math.hypot(CH_LONG / 2.0, abs(down_dy) + CH_SHORT / 2.0),
     (120 + down_dy - CH_SHORT / 2.0) >= board_bot, 'below the board'),
    ('left',  math.hypot(side_dx + CH_SHORT / 2.0, abs(BOARD_DY) + CH_LONG / 2.0),
     (120 - side_dx + CH_SHORT / 2.0) <= board_left, 'left of the board'),
    ('right', math.hypot(side_dx + CH_SHORT / 2.0, abs(BOARD_DY) + CH_LONG / 2.0),
     (120 + side_dx - CH_SHORT / 2.0) >= board_right, 'right of the board'),
]
for name, r, clear_of_board, what in checks:
    worst = max(worst, r)
    good = r <= CLIP_R and clear_of_board
    ok &= good
    print(f"  {'PASS' if good else 'FAIL'}  {name:<6} r={r:6.2f} "
          f"clear vs clip {CLIP_R - r:+6.2f}, {what}: {clear_of_board}")

# The score line and the up chevron share the top gap; they must not overlap.
score_dy = const('kSnakeScoreDy')
score_lh = MET[20][0]
score_bottom = 120 + score_dy + score_lh / 2.0
chev_top = 120 + up_dy - CH_SHORT / 2.0
gap = chev_top - score_bottom
ok &= gap >= 0
print(f"  {'PASS' if gap >= 0 else 'FAIL'}  score line vs up chevron: {gap:+.1f}px gap")

print(f"\nworst corner {worst:.2f} / ring {RING_IN}   min clearance {RING_IN - worst:.2f}px")
print("RESULT:", "all clear" if ok else "FAILED")
sys.exit(0 if ok else 1)
