# OTA release checklist (general — every firmware push)

Written 2026-09-23 after `CMakeLists.txt`'s `project(xiaozhi)` was renamed to `project(bubu)`
(see DEVLOG 2026-09-23). That rename changed the build artifact's name from `xiaozhi.bin` to
`bubu.bin` — every DEVLOG entry before that date that says "push `xiaozhi.bin`" is describing
the old artifact name, not a mistake and not something to copy literally into a new release.
**This file is the one to follow for the *next* push; do not pattern-match off an old DEVLOG
entry's exact filename.**

## What never changes, and why it's safe

`main/ota.cc` never hardcodes a firmware filename. It POSTs to the gateway's
`/xiaozhi/ota/` endpoint (that path is a fixed protocol convention, unrelated to the
project-name rename — do not "fix" it) and separately GETs
`https://raw.githubusercontent.com/hoangtalu/Bubu-OTA/main/latest.json`, then downloads
whatever full URL string that JSON's `"url"` field contains. The filename on GitHub is
therefore **entirely a human convention** decided at push time, not something baked into any
fielded device. The device also does not check the incoming image's `esp_app_desc_t.project_name`
against anything (verified by reading `ota.cc`'s image-header parsing directly) — a renamed
project can update a device that already has an older, differently-named image installed.

## Step 0 — Decide what's actually shipping

Check `STATE.md`'s Firmware row and the bullets under it for anything marked "local tree,
not flashed, not OTA-pushed." Everything listed there ships in the same build as whatever
prompted this release, whether you meant to include it or not.

## Step 1 — Bump `PROJECT_VER`

`CMakeLists.txt` near the top: `set(PROJECT_VER "x.y.z")`. It must be **strictly newer** than
the version currently live (check `STATE.md`'s Firmware row) — `ota.cc`'s
`IsNewVersionAvailable()` does a real version comparison, so pushing the same string as what's
already live updates nobody.

## Step 2 — Build clean and bench-flash before touching GitHub

```bash
source ~/esp/esp-idf/export.sh
idf.py -B build build
```

Flash the result to a real bench device — **not just a `build-verify` compile check**. A
build-only pass proves the code compiles; it proves nothing about button input, WiFi,
menu navigation, or anything else on real hardware. Confirm at minimum: normal boot, buttons
respond, WiFi connects, the menu opens, one game loads and unloads cleanly.

Confirm the binary identifies itself correctly:

```bash
python3 scripts/app_desc.py build/bubu.bin
```

Expect `project bubu` and the version string from Step 1.

## Step 3 — Name the file correctly (the step this doc exists for)

The build output is `build/bubu.bin` — **not** `xiaozhi.bin`. Push it to `hoangtalu/Bubu-OTA`
under that same name. Do not rename it back to `xiaozhi.bin` "to match old entries" — old
DEVLOG entries describe a build system that no longer exists.

## Step 4 — Write `latest.json`

`hoangtalu/Bubu-OTA/latest.json` uses the **flat** schema (confirmed by reading `ota.cc`'s
fallback parser, which exists specifically for this repo's format — it is not the nested
`{"firmware": {...}}` shape the gateway itself sends):

```json
{
  "version": "x.y.z",
  "url": "https://raw.githubusercontent.com/hoangtalu/Bubu-OTA/main/bubu.bin"
}
```

The `url` value must point at the exact filename pushed in Step 3. A mismatch here is a 404
for the entire fleet on their next check-in, not a partial failure.

## Step 5 — Do not touch `assets.bin` in this repo

`assets.bin` was deliberately removed from `hoangtalu/Bubu-OTA` on 2026-09-09 (see DEVLOG that
date): a global `assets_url` there was overwriting each device's per-device wake-word choice.
Assets ship exclusively from the gateway (`api.bubumotion.vn/assets/v2/...`). This repo should
hold only `latest.json` and the firmware binary — if `assets.bin` reappears here, that is a
regression, not a feature.

## Step 6 — Push

```bash
git add bubu.bin latest.json
git commit -m "Release x.y.z: <one-line summary>"
git push origin main
```

## Step 7 — Verify the push bypassing CDN cache

`raw.githubusercontent.com` caches; `gh api` does not.

```bash
gh api repos/hoangtalu/Bubu-OTA/contents/latest.json
gh api repos/hoangtalu/Bubu-OTA/contents/bubu.bin
```

Confirm the returned `version`/`url` match what you intended, and the blob `sha` matches the
local file (`git hash-object bubu.bin`).

## Step 8 — Watch the fleet pick it up

Check-in interval is 5 minutes (since 1.7.7 — check `STATE.md` in case this changed again).
Tail the gateway journal for 10-15 minutes and confirm at least one already-known device
reports the new version on connect (`device MCP ready … v<x.y.z>`), the same pattern used to
confirm 1.7.6→1.7.7 rollout in DEVLOG 2026-09-22.

## Step 9 — Update the two logs

- `STATE.md`: move the Firmware row's version forward, remove any "local tree, not flashed"
  bullets this release now supersedes, update the OTA commit/blob-sha references.
- `DEVLOG.md`: one `DEPLOYED` entry — version, commit, blob sha, what shipped, what's still
  unverified on real hardware vs. only bench-tested.

## Rollback

Push `latest.json` back to the previous `version`/`url` pair (kept in the previous `STATE.md`
Firmware row before you overwrote it — this is why Step 9 matters even under time pressure).
Devices already on the new version simply stop being told about a newer one; nothing forces
them backward, matching the existing rollback pattern for every prior release in DEVLOG.
