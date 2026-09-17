"""Measure a string exactly as LVGL renders it, straight from the compiled font.

Mirrors lv_font_fmt_txt.c:245-253 -- kern value scaled by kern_scale>>4, added to
the raw 1/16 px advance, then rounded to a whole pixel PER GLYPH.
"""
import re, sys

def parse(path):
    src = open(path, encoding='utf-8', errors='replace').read()

    # --- adv_w for every glyph id, in 1/16 px ---
    m = re.search(r'glyph_dsc\[\]\s*=\s*\{(.*?)\n\};', src, re.S)
    adv = [int(x) for x in re.findall(r'\.adv_w\s*=\s*(\d+)', m.group(1))]

    # --- cmaps ---
    m = re.search(r'cmaps\[\]\s*=\s*\{(.*?)\n\};', src, re.S)
    cmaps = []
    for blk in re.findall(r'\{(.*?)\}', m.group(1), re.S):
        g = lambda k: re.search(k + r'\s*=\s*(\d+)', blk)
        if not g(r'\.range_start'):
            continue
        cmaps.append({
            'start':  int(g(r'\.range_start').group(1)),
            'len':    int(g(r'\.range_length').group(1)),
            'gid':    int(g(r'\.glyph_id_start').group(1)),
            'sparse': 'SPARSE_TINY' in blk,
            'list':   (re.search(r'\.unicode_list\s*=\s*(\w+)', blk).group(1)
                       if re.search(r'\.unicode_list\s*=\s*(\w+)', blk) else 'NULL'),
        })

    def arr(name):
        mm = re.search(re.escape(name) + r'\[\]\s*=\s*\{(.*?)\n\};', src, re.S)
        if not mm:
            return []
        body = re.sub(r'/\*.*?\*/', '', mm.group(1), flags=re.S)
        # Hex first, and as one token. The old r'-?\d+' split every 0xNN into
        # a spurious 0 plus the digits read as DECIMAL, so unicode_list_2 came
        # back 71 entries long instead of 33 and every sparse lookup past the
        # first entry was wrong -- U+01AF (Ư) reported as absent from fonts
        # that do carry it. Only the SPARSE_TINY cmaps are written in hex, so
        # this never showed up in the FORMAT0 ranges that cover plain ASCII.
        return [int(x, 0) for x in re.findall(r'-?0[xX][0-9a-fA-F]+|-?\d+', body)]

    for c in cmaps:
        c['ulist'] = arr(c['list']) if c['list'] != 'NULL' else None

    kern = {
        'left':  arr('kern_left_class_mapping'),
        'right': arr('kern_right_class_mapping'),
        'vals':  arr('kern_class_values'),
    }
    kb = re.search(r'kern_classes\s*=\s*\{(.*?)\};', src, re.S).group(1)
    kern['lcnt'] = int(re.search(r'left_class_cnt\s*=\s*(\d+)', kb).group(1))
    kern['rcnt'] = int(re.search(r'right_class_cnt\s*=\s*(\d+)', kb).group(1))
    scale = int(re.search(r'\.kern_scale\s*=\s*(\d+)', src).group(1))
    return adv, cmaps, kern, scale

def gid(cp, cmaps):
    for c in cmaps:
        if c['start'] <= cp < c['start'] + c['len']:
            if c['sparse']:
                rel = cp - c['start']
                if c['ulist'] is None or rel not in c['ulist']:
                    continue
                return c['gid'] + c['ulist'].index(rel)
            return c['gid'] + (cp - c['start'])
    return 0

def kval(a, b, kern):
    if a == 0 or b == 0:
        return 0
    lc = kern['left'][a]  if a < len(kern['left'])  else 0
    rc = kern['right'][b] if b < len(kern['right']) else 0
    if lc == 0 or rc == 0:
        return 0
    i = (lc - 1) * kern['rcnt'] + (rc - 1)
    return kern['vals'][i] if i < len(kern['vals']) else 0

def width(text, font):
    adv, cmaps, kern, scale = font
    total, missing = 0, []
    for i, ch in enumerate(text):
        g = gid(ord(ch), cmaps)
        if g == 0 or g >= len(adv):
            missing.append(ch)
            continue
        nxt = gid(ord(text[i + 1]), cmaps) if i + 1 < len(text) else 0
        kv = (kval(g, nxt, kern) * scale) >> 4
        total += (adv[g] + kv + 8) >> 4      # LVGL rounds per glyph
    return total, missing

if __name__ == '__main__':
    import os
    base = os.path.dirname(os.path.abspath(__file__))
    fonts = {}
    for size in (20, 22, 28):
        p = f'/Users/judes/Downloads/Bubu-Motion-v1-main/Bubu_Motion/main/display/fonts/lv_font_montserrat_vn_{size}.c'
        fonts[size] = parse(p)

    strings = [
        ("CHẠM NHANH",    "game name  - Quick Tap"),
        ("MẮT XANH",      "game name  - Eye"),
        ("CỜ CA-RÔ",      "game name  - Checker"),
        ("TRÒ CHƠI",      "eyebrow"),
        ("KỶ LỤC 48",     "chip - record"),
        ("CHẠM MẮT XANH", "chip - prompt"),
        ("ĐẤU VỚI BUBU",  "chip - prompt"),
        ("MỞ Ở CẤP 1",    "chip - locked"),
    ]
    for size in (20, 22, 28):
        print(f"\n=== lv_font_montserrat_vn_{size} ===")
        for s, note in strings:
            w, miss = width(s, fonts[size])
            flag = f"   MISSING GLYPHS: {miss}" if miss else ""
            print(f"  {w:4d} px   {s:<16} {note}{flag}")
