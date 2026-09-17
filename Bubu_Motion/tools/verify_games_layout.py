"""Read the layout constants out of menu_system.cc and re-test them.

This deliberately does not restate the numbers -- if someone edits a constant
without re-measuring, this fails.
"""
import math, re, sys
from fit import FONTS, BOXES, ink, R_RING_IN
from lvwidth import width

SRC = '/Users/judes/Downloads/Bubu-Motion-v1-main/Bubu_Motion/main/display/menu_system.cc'
src = open(SRC, encoding='utf-8').read()

def const(name):
    m = re.search(r'constexpr int ' + name + r'\s*=\s*(-?\d+)', src)
    assert m, 'missing ' + name
    return int(m.group(1))

EMB_SIZE = const('kGamesEmblemSize')
EMB_TOP  = const('kGamesEmblemTop')
NAME_TOP = const('kGamesNameTop')
CHIP_TOP = const('kGamesChipTop')
RING_R   = const('kGamesRingRadius')
RING_W   = const('kGamesRingWidth')
ring_in  = RING_R - RING_W // 2

# LVGL: baseline = label_top + line_height - base_line
MET = {22: (29, 6), 20: (27, 6)}
def baseline(top, size):
    lh, bl = MET[size]
    return top + lh - bl

NAMES = ["MẮT XANH", "CỜ CA-RÔ", "CHẠM NHANH", "RẮN SĂN MỒI"]
CHIPS = ["KỶ LỤC 48", "KỶ LỤC 999"]

print(f"constants read from menu_system.cc:")
print(f"  ring r={RING_R} w={RING_W} -> inner edge {ring_in}")
print(f"  emblem {EMB_SIZE}px at y={EMB_TOP}..{EMB_TOP+EMB_SIZE}")
print(f"  name  top={NAME_TOP} -> baseline {baseline(NAME_TOP,22)}")
print(f"  chip  top={CHIP_TOP} -> baseline {baseline(CHIP_TOP,20)}\n")

ok = True
worst = 0.0
min_gap = 999
for text, size, top in ([(t, 22, NAME_TOP) for t in NAMES] +
                        [(t, 20, CHIP_TOP) for t in CHIPS]):
    base = baseline(top, size)
    w, missing = width(text, FONTS[size])
    assert not missing, f'{text}: missing glyphs {missing}'
    a, d = ink(text, FONTS[size], BOXES[size])
    itop, ibot = base - a, base + d
    r = math.hypot(w / 2, max(abs(itop - 120), abs(ibot - 120)))
    worst = max(worst, r)
    good = r <= ring_in
    ok &= good
    gap = ''
    if size == 22:
        g = itop - (EMB_TOP + EMB_SIZE)
        min_gap = min(min_gap, g)
        gap = f'  emblem gap {g:+d}px'
        ok &= (g >= 0)
    print(f"  {'PASS' if good else 'FAIL'}  {text:<12} {size}px  w={w:3d}  "
          f"ink {itop}..{ibot}  r={r:6.2f}  clear {ring_in - r:+5.2f}{gap}")

# the emblem disc itself
emb_r = abs(120 - (EMB_TOP + EMB_SIZE / 2)) + EMB_SIZE / 2
print(f"\n  {'PASS' if emb_r <= ring_in else 'FAIL'}  emblem disc          "
      f"r={emb_r:6.2f}  clear {ring_in - emb_r:+5.2f}")
ok &= emb_r <= ring_in

print(f"\nworst text corner {worst:.2f} / ring {ring_in}   "
      f"min clearance {ring_in - worst:.2f}px   min emblem gap {min_gap}px")
print("RESULT:", "all clear" if ok else "FAILED")
sys.exit(0 if ok else 1)
