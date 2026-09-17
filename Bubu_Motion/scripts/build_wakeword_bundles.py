#!/usr/bin/env python3
"""Build one assets bundle per selectable wake word.

Why this exists
---------------
The device runs whichever WakeNet model happens to be inside its assets
partition: ``AfeWakeWord::Initialize()`` scans the packed model list and takes
the ``wn*`` entry it finds. So "let the parent choose a wake word" reduces to
"ship that parent's device a different assets bundle" -- no model-selection code
on the device at all.

Every bundle is byte-identical except for ``srmodels.bin``. This script builds
them by re-running ``build_default_assets.py`` once per model against a throwaway
sdkconfig in which exactly one ``CONFIG_SR_WN_*`` is enabled, then writes a
manifest the console serves to the portal, so the catalog lives on the server and
the portal hardcodes nothing.

Usage
-----
    python3 scripts/build_wakeword_bundles.py --out-dir build/wakeword_bundles
    python3 scripts/build_wakeword_bundles.py --list
"""

import argparse
import hashlib
import io
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD_ASSETS = os.path.join(REPO, "scripts", "build_default_assets.py")
ESP_SR = os.path.join(REPO, "managed_components", "espressif__esp-sr")
FONTS = os.path.join(REPO, "managed_components", "78__xiaozhi-fonts")
SDKCONFIG = os.path.join(REPO, "sdkconfig")
REFERENCE_BUNDLE = os.path.join(REPO, "main", "assets.bin")

# The bundle deliberately carries NEITHER a text font NOR an emoji collection.
#
# Both are upstream xiaozhi concepts that this firmware does not use. Assets::Apply()
# feeds index.json's "text_font" into LvglTheme::set_text_font and "emoji_collection"
# into set_emoji_collection, and the only readers of those are LcdDisplay's chat UI
# and OledDisplay. On this board EyeDisplay overrides SetStatus/SetChatMessage/
# SetEmotion, and LcdDisplay's screen is created behind EyeAnimation's opaque
# full-screen canvas and never foregrounded (see the comment at eye_display.cc:468).
# So font_puhui_common_20_4.bin (1,240,332 B) and the 21 twemoji PNGs (~59 KB) were
# downloaded, checksummed and mapped on every device, and never once drawn.
#
# Dropping them is safe: both are read behind `if` guards in Assets::Apply(), and
# LcdDisplay::InitializeLcdThemes() has already set the theme's font to the
# compiled-in BUILTIN_TEXT_FONT, so nothing is left holding a null font.
#
# It halves the bundle, which is the whole cost of changing a wake word.
EXTRA_FILES = os.path.join(REPO, "main", "assets", "menu_icons")

# Wake words offered to parents. Deliberately a subset of the ~54 models esp-sr
# ships: every name here is free of third-party trademark. esp-sr's own README
# is explicit that the caller must hold the rights to a wake word before
# commercial use, which rules out Alexa (Amazon), Jarvis (Marvel), Wall-E
# (Disney) and Mycroft (Mycroft AI) however well they perform.
#
# Add "Hey Bubu" here the day Espressif delivers it (esp-sr issue #88) -- that is
# a one-line change plus a bundle rebuild, with no firmware release.
#
# `model=None` is the one deliberate exception: "Bubu", the no-wake-word default
# state. Its bundle carries zero CONFIG_SR_WN_* models. AudioService::SetModelsList
# already handles that gracefully -- esp_srmodel_filter finds no wn* model and
# leaves wake_word_ null, no crash, no retry loop -- so the device simply never
# runs wake-word detection and answers only to the eye tap. That fallback existed
# before this catalog entry; this just makes it a selectable, named choice instead
# of only what happens if every bundle failed to apply.
CATALOG = [
    ("wn9_hijoy_tts",     "Hi Joy",    "hai-JOY"),
    ("wn9_heyivy_tts2",   "Hey Ivy",   "hay-AI-vi"),
    ("wn9_hilili_tts",    "Hi Lily",   "hai-LI-li"),
    ("wn9_heykira_tts3",  "Hey Kira",  "hay-KI-ra"),
    ("wn9_hifairy_tts2",  "Hi Fairy",  "hai-FE-ri"),
    ("wn9_sophia_tts",    "Sophia",    "sô-FI-a"),
    (None,                "Bubu",      None),
]

# Filename/URL stem for the no-wake-word entry, since its `model` is None.
NO_WAKE_WORD_STUB = "none"


def kconfig_symbol(model_dir_name):
    """wn9_hijoy_tts -> CONFIG_SR_WN_WN9_HIJOY_TTS, verified against esp-sr."""
    symbol = "CONFIG_SR_WN_" + model_dir_name.upper()
    kconfig = io.open(os.path.join(ESP_SR, "Kconfig.projbuild"), encoding="utf8").read()
    if re.search(r"^\s*config %s\s*$" % symbol[len("CONFIG_"):], kconfig, re.M) is None:
        raise SystemExit(
            "esp-sr has no Kconfig symbol %s for model %s.\n"
            "Check the exact directory name under %s/model/wakenet_model/."
            % (symbol, model_dir_name, ESP_SR))
    return symbol


def write_single_model_sdkconfig(path, symbol):
    """Copy the real sdkconfig with exactly one CONFIG_SR_WN_* left enabled.

    build_default_assets.py decides what to pack by scanning sdkconfig for
    CONFIG_SR_WN_* lines (read_wakenet_from_sdkconfig), so this is the supported
    way to steer it without touching the checked-in config.
    """
    out, seen = [], False
    for line in io.open(SDKCONFIG, encoding="utf8"):
        s = line.rstrip("\n")
        if s.startswith("CONFIG_SR_WN_") and s.endswith("=y"):
            out.append("# %s is not set\n" % s.split("=")[0])
            continue
        if s == "# %s is not set" % symbol:
            out.append("%s=y\n" % symbol)
            seen = True
            continue
        if s == "%s=y" % symbol:
            out.append(line)
            seen = True
            continue
        out.append(line)
    if not seen:
        out.append("%s=y\n" % symbol)
    io.open(path, "w", encoding="utf8").write("".join(out))


def write_no_wakenet_sdkconfig(path):
    """Copy the real sdkconfig with every CONFIG_SR_WN_* disabled.

    Same mechanism as write_single_model_sdkconfig, but for the "Bubu" default
    state: read_wakenet_from_sdkconfig then finds nothing, and
    build_default_assets.py packs no srmodels.bin at all.
    """
    out = []
    for line in io.open(SDKCONFIG, encoding="utf8"):
        s = line.rstrip("\n")
        if s.startswith("CONFIG_SR_WN_") and s.endswith("=y"):
            out.append("# %s is not set\n" % s.split("=")[0])
            continue
        out.append(line)
    io.open(path, "w", encoding="utf8").write("".join(out))


def bundle_contents(path):
    """Unpack an assets bundle into {name: bytes}.

    Header is (count, checksum, length) then a table of 44-byte entries
    (32-byte name, size, offset, width, height), mirroring mmap_assets_table in
    main/assets.cc; payloads follow the table and offsets are relative to its end.

    Each payload is preceded by a 2-byte 0x5A5A marker that pack_assets_simple()
    in build_default_assets.py writes, and the stored offset points at that marker,
    not at the data -- hence the +2. Getting this wrong shifts every file by two
    bytes, which still compares equal between two bundles and so hides itself.
    """
    d = io.open(path, "rb").read()
    n, _chksum, _len = struct.unpack("<III", d[:12])
    base = 12 + n * 44
    files = {}
    for i in range(n):
        e = d[12 + i * 44: 12 + (i + 1) * 44]
        name = e[:32].split(b"\0")[0].decode("utf8")
        size, off, _w, _h = struct.unpack("<IIHH", e[32:44])
        start = base + off + 2
        payload = d[start: start + size]
        if len(payload) != size:
            raise SystemExit("%s: entry %s is truncated (%d of %d bytes)"
                             % (path, name, len(payload), size))
        files[name] = payload
    return files


def index_shape(blob):
    """The comparable part of index.json.

    Compared structurally rather than byte-for-byte because two things legitimately
    differ from the shipped bundle: the generator's JSON formatting, and the
    multinet_model block, which is gone now that the engine is WakeNet.
    """
    i = blob.index(b"{")
    depth, in_str, esc, end = 0, False, False, None
    for k in range(i, len(blob)):
        c = blob[k:k + 1]
        if esc:
            esc = False
            continue
        if c == b"\\":
            esc = True
            continue
        if c == b'"':
            in_str = not in_str
            continue
        if in_str:
            continue
        if c == b"{":
            depth += 1
        elif c == b"}":
            depth -= 1
            if depth == 0:
                end = k + 1
                break
    j = json.loads(blob[i:end].decode("utf8"))
    return {
        "srmodels": j.get("srmodels"),
        "text_font": j.get("text_font"),
        "emoji": sorted(e["name"] for e in j.get("emoji_collection", [])),
        "extra_files": sorted(j.get("extra_files", [])),
    }


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out-dir", default=os.path.join(REPO, "build", "wakeword_bundles"))
    ap.add_argument("--list", action="store_true", help="print the catalog and exit")
    ap.add_argument("--only", help="build just this model (e.g. wn9_hijoy_tts)")
    ap.add_argument("--base-url", default="",
                    help="prefix put in front of each filename in the manifest")
    args = ap.parse_args()

    if args.list:
        for model, label, pron in CATALOG:
            print("%-22s %-10s %s" % (model or NO_WAKE_WORD_STUB, label, pron or ""))
        return 0

    def matches_only(model):
        return (model or NO_WAKE_WORD_STUB) == args.only

    catalog = [c for c in CATALOG if not args.only or matches_only(c[0])]
    if not catalog:
        raise SystemExit("--only %s does not match any catalog entry" % args.only)

    os.makedirs(args.out_dir, exist_ok=True)
    reference = bundle_contents(REFERENCE_BUNDLE) if os.path.exists(REFERENCE_BUNDLE) else None

    entries = []
    tmp = tempfile.mkdtemp(prefix="wwbundle-")
    try:
        for model, label, pronunciation in catalog:
            stub = model or NO_WAKE_WORD_STUB
            cfg = os.path.join(tmp, "sdkconfig-%s" % stub)
            if model is None:
                write_no_wakenet_sdkconfig(cfg)
            else:
                symbol = kconfig_symbol(model)
                write_single_model_sdkconfig(cfg, symbol)

            out = os.path.join(args.out_dir, "assets-%s.bin" % stub)
            cmd = [sys.executable, BUILD_ASSETS,
                   "--sdkconfig", cfg,
                   "--output", out,
                   "--extra_files", EXTRA_FILES,
                   "--esp_sr_model_path", os.path.join(ESP_SR, "model"),
                   "--xiaozhi_fonts_path", FONTS]
            print("\n=== %s (%s) ===" % (stub, label))
            r = subprocess.run(cmd, capture_output=True, text=True)
            if r.returncode != 0 or not os.path.exists(out):
                sys.stdout.write(r.stdout)
                sys.stderr.write(r.stderr)
                raise SystemExit("build_default_assets.py failed for %s" % stub)

            files = bundle_contents(out)
            # The trap this guards against is real and already cost a session: a
            # bundle that silently drops the font still builds fine and only fails
            # on the device. Everything except srmodels.bin and index.json must be
            # byte-identical to the bundle we ship today.
            if reference:
                # Anything absent must be something we deliberately dropped: the
                # unused text font and the emoji PNGs it indexes. A missing menu
                # icon or srmodels.bin is a real failure and must still stop the
                # build -- that is the font trap from 2026-09-03.
                ref_index = index_shape(reference["index.json"])
                intentionally_dropped = {ref_index["text_font"]} | {
                    "%s.png" % name for name in ref_index["emoji"]}
                # The "Bubu" default-state bundle deliberately carries no wakenet
                # model at all -- that is the whole point of it -- so srmodels.bin
                # is a third, entry-specific intentional drop, not a bug.
                if model is None:
                    intentionally_dropped |= {"srmodels.bin"}
                missing = (set(reference) - set(files)) - intentionally_dropped
                added = set(files) - set(reference)
                if missing or added:
                    raise SystemExit(
                        "%s file list differs from %s\n  unexpectedly missing: %s\n  added: %s"
                        % (stub, REFERENCE_BUNDLE, sorted(missing), sorted(added)))
                # Every file we DO ship must be byte-identical to the reference
                # bundle; the font and emoji are expected to be absent entirely.
                shared = (set(files) & set(reference)) - {"srmodels.bin", "index.json"}
                differing = sorted(n for n in shared if files[n] != reference[n])
                if differing:
                    raise SystemExit(
                        "%s: these files are not byte-identical to the shipped "
                        "bundle, but only srmodels.bin should change: %s"
                        % (stub, differing))
                # The menu icons are ours and are drawn, so losing one is a real
                # failure -- this is the guard that catches a silently dropped asset.
                # The "Bubu" bundle still ships every one of them, same as any other.
                built, ref = index_shape(files["index.json"]), index_shape(reference["index.json"])
                if built["extra_files"] != ref["extra_files"]:
                    raise SystemExit(
                        "%s: index.json lost or changed a menu icon\n"
                        "  shipped: %s\n  built:   %s"
                        % (stub, ref["extra_files"], built["extra_files"]))
                if built["text_font"] is not None or built["emoji"]:
                    raise SystemExit(
                        "%s: bundle still carries a text font or emoji; they are "
                        "unused on this board and must not be shipped" % stub)
                if model is None:
                    if "srmodels.bin" in files:
                        raise SystemExit(
                            "%s: the no-wake-word bundle must not carry srmodels.bin"
                            % stub)
                elif b"wn9" not in files["srmodels.bin"][:4096] and \
                        model.encode() not in files["srmodels.bin"][:4096]:
                    raise SystemExit("%s: srmodels.bin does not name that model" % stub)

            blob = io.open(out, "rb").read()
            entries.append({
                "model": stub,
                "label": label,
                "pronunciation": pronunciation,
                "file": os.path.basename(out),
                "url": args.base_url + os.path.basename(out) if args.base_url else "",
                "size": len(blob),
                "sha256": hashlib.sha256(blob).hexdigest(),
            })
            srmodels_note = (
                "no srmodels.bin (default state)" if "srmodels.bin" not in files
                else "srmodels.bin %s B" % format(len(files["srmodels.bin"]), ","))
            print("  -> %s  %s B  (%s)"
                  % (os.path.basename(out), format(len(blob), ","), srmodels_note))
    finally:
        shutil.rmtree(tmp, ignore_errors=True)

    manifest = os.path.join(args.out_dir, "wake-words.json")
    io.open(manifest, "w", encoding="utf8").write(
        json.dumps({"version": 1, "wake_words": entries}, indent=2, ensure_ascii=False) + "\n")
    print("\nManifest: %s (%d wake words)" % (manifest, len(entries)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
