"""Check every MẮT XANH eye layout against the round panel it is drawn on.

Reads the layout table straight out of main/display/green_eye_game.cc and the
HUD positions out of main/display/menu_system.cc, so it checks what the device
will draw rather than a copy of it. For every eye of every layout:

  1. rim     -- the rounded square stays inside the rim arc's inner edge, with margin
  2. score   -- it clears the ink of a 3-digit score at the top
  3. hearts  -- it clears the ink of the hearts row at the bottom
  4. gap     -- neighbouring eyes keep a visible gap between their shapes
  5. touch   -- every pixel of an eye resolves to that eye under the game's own
                nearest-centre rule with finger slack, so a tap on an eye can
                never be scored as its neighbour

Usage:  python3 tools/verify_green_eye_layout.py
"""
import math
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import lvwidth  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GAME = os.path.join(ROOT, 'main/display/green_eye_game.cc')
MENU = os.path.join(ROOT, 'main/display/menu_system.cc')
FONT28 = os.path.join(ROOT, 'main/display/fonts/lv_font_montserrat_vn_28.c')
ICONS = os.path.join(ROOT, 'managed_components/78__xiaozhi-fonts/src/font_awesome_20_4.c')

CENTER = 120
RIM_MARGIN = 4      # px between an eye and the rim's inner edge
HUD_MARGIN = 5      # px between an eye and HUD ink
MIN_GAP = 14        # px between two eyes' shapes


def const(src, name):
    m = re.search(r'constexpr\s+\w+\s+' + name + r'\s*=\s*(-?\d+)', src)
    if not m:
        sys.exit(f'constant {name} not found')
    return int(m.group(1))


def layouts(src):
    body = re.search(r'kLayouts\[\]\s*=\s*\{(.*?)\n\};', src, re.S).group(1)
    out = []
    for line in body.splitlines():
        m = re.match(r'\s*\{(\d+),\s*(\d+),\s*(\d+),\s*\{(.*)\}\},', line)
        if not m:
            continue
        count, size, radius = (int(m.group(i)) for i in (1, 2, 3))
        slots = [(int(a), int(b)) for a, b in re.findall(r'\{(-?\d+),\s*(-?\d+)\}', m.group(4))]
        if len(slots) != count:
            sys.exit(f'layout {line.strip()}: {len(slots)} slots for count {count}')
        out.append((count, size, radius, slots))
    return out


def glyph_boxes(path):
    """(box_h, ofs_y) per glyph id, plus line height and base line."""
    src = open(path, encoding='utf-8', errors='replace').read()
    body = re.search(r'glyph_dsc\[\]\s*=\s*\{(.*?)\n\};', src, re.S).group(1)
    boxes = [(int(h), int(oy)) for h, oy in
             re.findall(r'\.box_h\s*=\s*(\d+).*?\.ofs_y\s*=\s*(-?\d+)', body, re.S)]
    lh = int(re.search(r'\.line_height\s*=\s*(\d+)', src).group(1))
    bl = int(re.search(r'\.base_line\s*=\s*(\d+)', src).group(1))
    return boxes, lh, bl


def cmaps_of(path):
    src = open(path, encoding='utf-8', errors='replace').read()
    m = re.search(r'cmaps\[\]\s*=\s*\{(.*?)\n\};', src, re.S)
    cmaps = []
    for blk in re.findall(r'\{(.*?)\}', m.group(1), re.S):
        g = lambda k: re.search(k + r'\s*=\s*(\d+)', blk)
        if not g(r'\.range_start'):
            continue
        lst = re.search(r'\.unicode_list\s*=\s*(\w+)', blk)
        c = {'start': int(g(r'\.range_start').group(1)), 'len': int(g(r'\.range_length').group(1)),
             'gid': int(g(r'\.glyph_id_start').group(1)), 'sparse': 'SPARSE_TINY' in blk,
             'list': lst.group(1) if lst else 'NULL'}
        if c['list'] != 'NULL':
            mm = re.search(re.escape(c['list']) + r'\[\]\s*=\s*\{(.*?)\n\};', src, re.S)
            b = re.sub(r'/\*.*?\*/', '', mm.group(1), flags=re.S)
            c['ulist'] = [int(x, 0) for x in re.findall(r'-?0[xX][0-9a-fA-F]+|-?\d+', b)]
        else:
            c['ulist'] = None
        cmaps.append(c)
    return cmaps


def ink_y(path, text, top):
    """Vertical ink extent of text whose label box starts at top."""
    boxes, lh, bl = glyph_boxes(path)
    cmaps = cmaps_of(path)
    baseline = top + lh - bl
    y0, y1 = 10**6, -10**6
    for ch in text:
        g = lvwidth.gid(ord(ch), cmaps)
        if g == 0:
            sys.exit(f'glyph {ch!r} missing from {os.path.basename(path)}')
        h, oy = boxes[g]
        y0 = min(y0, baseline - oy - h)
        y1 = max(y1, baseline - oy)
    return y0, y1


def inner_rect(cx, cy, size, radius):
    half = size / 2 - radius
    return cx - half, cy - half, cx + half, cy + half


def rect_gap(a, b):
    dx = max(0, max(a[0], b[0]) - min(a[2], b[2]))
    dy = max(0, max(a[1], b[1]) - min(a[3], b[3]))
    return math.hypot(dx, dy)


def shape_gap_to_box(cx, cy, size, radius, box):
    """Distance from a rounded square to an axis-aligned box (0 if they overlap)."""
    return max(0.0, rect_gap(inner_rect(cx, cy, size, radius), box) - radius)


def main():
    game = open(GAME, encoding='utf-8').read()
    menu = open(MENU, encoding='utf-8').read()

    slack = const(game, 'kTapSlackPx')
    rim_size = const(menu, 'kGreenEyeRimSize')
    rim_width = const(menu, 'kGreenEyeRimWidth')
    score_y = const(menu, 'kGreenEyeScoreY')
    hearts_y = const(menu, 'kGreenEyeHeartsY')
    heart_pitch = const(menu, 'kGreenEyeHeartPitch')
    rim_inner = rim_size / 2 - rim_width

    # The HUD labels are aligned LV_ALIGN_CENTER at those offsets; LVGL puts a
    # content-sized label's top at centre - h/2 (integer) + offset, and a
    # single-line label is exactly one line_height tall.
    def label_top(path, offset):
        _, lh, _ = glyph_boxes(path)
        return CENTER - lh // 2 + offset

    score_top = label_top(FONT28, score_y)
    hearts_top = label_top(ICONS, hearts_y)

    # Score: the widest a 3-digit score gets, centred.
    font28 = lvwidth.parse(FONT28)
    score_w = max(lvwidth.width(str(n), font28)[0] for n in (888, 999, 100, 444))
    sy0, sy1 = ink_y(FONT28, '0123456789', score_top)
    score_box = (CENTER - score_w / 2, sy0, CENTER + score_w / 2, sy1)

    # Hearts: three glyphs, pitch apart, centred on the panel.
    cm = cmaps_of(ICONS)
    boxes, _, _ = glyph_boxes(ICONS)
    src = open(ICONS, encoding='utf-8', errors='replace').read()
    body = re.search(r'glyph_dsc\[\]\s*=\s*\{(.*?)\n\};', src, re.S).group(1)
    box_w = [int(w) for w in re.findall(r'\.box_w\s*=\s*(\d+)', body)]
    heart = lvwidth.gid(0xF004, cm)
    hy0, hy1 = ink_y(ICONS, '', hearts_top)
    hearts_half = heart_pitch + box_w[heart] / 2
    hearts_box = (CENTER - hearts_half, hy0, CENTER + hearts_half, hy1)

    # Both HUD rows must themselves sit inside the rim.
    failures = 0
    for name, box in (('score', score_box), ('hearts', hearts_box)):
        far = max(math.hypot(x - CENTER, y - CENTER) for x in (box[0], box[2]) for y in (box[1], box[3]))
        ok = far <= rim_inner - 1
        failures += not ok
        print(f'{name:6s} box x {box[0]:.0f}..{box[2]:.0f} y {box[1]}..{box[3]}  '
              f'farthest {far:.1f} / rim {rim_inner:.0f}  {"ok" if ok else "FAIL"}')

    for li, (count, size, radius, slots) in enumerate(layouts(game)):
        eyes = [(CENTER + x, CENTER + y) for x, y in slots]
        # The farthest point of a rounded square from the centre is its
        # outermost corner-arc centre pushed out by the corner radius.
        worst_rim = min(rim_inner - (math.hypot(abs(cx - CENTER) + size / 2 - radius,
                                                abs(cy - CENTER) + size / 2 - radius) + radius)
                        for cx, cy in eyes)
        worst_score = min(shape_gap_to_box(cx, cy, size, radius, score_box) for cx, cy in eyes)
        worst_hearts = min(shape_gap_to_box(cx, cy, size, radius, hearts_box) for cx, cy in eyes)
        gaps = [max(0.0, rect_gap(inner_rect(*a, size, radius), inner_rect(*b, size, radius)) - 2 * radius)
                for i, a in enumerate(eyes) for b in eyes[i + 1:]]
        worst_gap = min(gaps)

        # Touch: every pixel inside each eye must resolve to it under EyeAt()'s
        # rule -- nearest edge within slack, ties to the nearest centre.
        touch_ok = True
        half = size // 2
        for i, (cx, cy) in enumerate(eyes):
            for px in range(cx - half, cx + half + 1):
                for py in range(cy - half, cy + half + 1):
                    best, best_key = -1, None
                    for j, (ex, ey) in enumerate(eyes):
                        dx, dy = px - ex, py - ey
                        ox, oy = max(0, abs(dx) - half), max(0, abs(dy) - half)
                        edge2 = ox * ox + oy * oy
                        if edge2 > slack * slack:
                            continue
                        key = (edge2, dx * dx + dy * dy)
                        if best_key is None or key < best_key:
                            best, best_key = j, key
                    if best != i:
                        touch_ok = False

        ok = (worst_rim >= RIM_MARGIN and worst_score >= HUD_MARGIN and
              worst_hearts >= HUD_MARGIN and worst_gap >= MIN_GAP and touch_ok)
        failures += not ok
        print(f'layout {li}: {count} eyes {size}px r{radius}  rim {worst_rim:5.1f}  '
              f'score {worst_score:5.1f}  hearts {worst_hearts:5.1f}  gap {worst_gap:5.1f}  '
              f'touch {"ok" if touch_ok else "FAIL"}  {"ok" if ok else "FAIL"}')

    print('RESULT:', 'all layouts valid' if failures == 0 else f'{failures} FAILED')
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
