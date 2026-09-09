"""Does a centred text run fit inside the 240px circle at a given y?

Uses real ink extents (ofs_y, box_h) from the compiled font, not the label box,
then tests the worst corner of the ink rectangle against the radius.
"""
import math, re, sys
sys.path.insert(0, '.')
from lvwidth import parse, gid, kval, width

FONTDIR = '/Users/judes/Downloads/Bubu-Motion-v1-main/Bubu_Motion/main/display/fonts'

def boxes(path):
    src = open(path, encoding='utf-8', errors='replace').read()
    m = re.search(r'glyph_dsc\[\]\s*=\s*\{(.*?)\n\};', src, re.S)
    out = []
    for e in re.findall(r'\{([^}]*)\}', m.group(1)):
        f = dict(re.findall(r'\.(\w+)\s*=\s*(-?\d+)', e))
        out.append((int(f['box_h']), int(f['ofs_y'])))
    return out

def ink(text, font, bx):
    """Ascent above baseline, descent below baseline, in px."""
    adv, cmaps, kern, scale = font
    asc = desc = 0
    for ch in text:
        g = gid(ord(ch), cmaps)
        if g == 0 or g >= len(bx):
            continue
        h, oy = bx[g]
        if h == 0:
            continue
        asc = max(asc, oy + h)
        desc = max(desc, -oy)
    return asc, desc

def fits(w, y_top, y_bot, R, cx=120, cy=120):
    """Worst corner of the ink rect vs the radius."""
    dx = w / 2.0
    dy = max(abs(y_top - cy), abs(y_bot - cy))
    return math.hypot(dx, dy)

FONTS = {s: parse(f'{FONTDIR}/lv_font_montserrat_vn_{s}.c') for s in (20, 22, 28)}
BOXES = {s: boxes(f'{FONTDIR}/lv_font_montserrat_vn_{s}.c') for s in (20, 22, 28)}

R_RING_IN = 109.0   # ring is r=112, 6px stroke -> inner edge 109
R_GLASS   = 120.0

def report(label, text, size, baseline_y):
    f, bx = FONTS[size], BOXES[size]
    w, _ = width(text, f)
    a, d = ink(text, f, bx)
    top, bot = baseline_y - a, baseline_y + d
    r = fits(w, top, bot, R_RING_IN)
    print(f"  {text:<15} {size}px  w={w:3d}  ink y={top:5.1f}..{bot:5.1f}  "
          f"worst corner r={r:6.2f}  "
          f"ring clear {R_RING_IN - r:+6.2f}  glass clear {R_GLASS - r:+6.2f}"
          f"   {'OK' if r <= R_RING_IN else 'COLLIDES'}")

print("\n--- game name, baseline at y=186 (as mocked up) ---")
for t in ("CHẠM NHANH", "MẮT XANH", "CỜ CA-RÔ"):
    report("name", t, 22, 186)

print("\n--- game name at 22px, sweeping the baseline ---")
for by in (176, 178, 180, 182, 184, 186):
    f, bx = FONTS[22], BOXES[22]
    w, _ = width("CHẠM NHANH", f)
    a, d = ink("CHẠM NHANH", f, bx)
    r = fits(w, by - a, by + d, R_RING_IN)
    print(f"  baseline y={by}  worst r={r:6.2f}  ring clear {R_RING_IN - r:+6.2f}")

print("\n--- game name at 20px instead ---")
for t in ("CHẠM NHANH", "MẮT XANH", "CỜ CA-RÔ"):
    report("name", t, 20, 186)

print("\n--- eyebrow TRÒ CHƠI at 20px, baseline y=42 ---")
report("eyebrow", "TRÒ CHƠI", 20, 42)

print("\n--- chips at 20px, baseline y=210 (pill adds ~5px padding each side) ---")
for t in ("KỶ LỤC 48", "MỞ Ở CẤP 1", "ĐẤU VỚI BUBU", "CHẠM MẮT XANH"):
    report("chip", t, 20, 210)
