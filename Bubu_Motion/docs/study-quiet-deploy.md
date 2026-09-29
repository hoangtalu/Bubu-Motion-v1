# Deploy — SFX off during study time ("giờ học")

One gateway file, one firmware release, one optional portal copy change. The three are
independent and can go in any order, but **nothing is silent until both the gateway file and
firmware 1.7.7 are live**: the firmware reads a `study` block the current gateway does not
send, and the current fleet firmware ignores the block a new gateway does send. Neither half
breaks the other — an old device sees an unknown JSON key, a new device sees no key and
leaves its sound alone.

## What changes

- **Gateway** (`src/ota.ts` → `dist/ota.js`): every `/xiaozhi/ota/` check-in reply now carries
  `study: { active, until }`, computed from the device's own `tutorSubject`/`tutorUntil` with
  the same `activeTutorSubject()` the tutor prompt uses. `until: 0` when no window is running —
  sent on every check-in, because that zero is how a device learns a parent pressed
  "Kết thúc giờ học" early.
- **Firmware 1.7.7**: mutes the SFX overlay lane for the whole window — games, eye-tap and
  mischief voices, emotion lines, Pomodoro chimes. Bubu's own voice, the notification/popup
  sounds and the low-battery warning are untouched. The end time is held in NVS
  (`study/until_s`), so a reboot mid-window stays quiet, and the device compares it against
  its own clock every second, so the sound returns at the right minute even if the next
  check-in never arrives.
- **Check-in interval 15 min → 5 min** (`kAssetsRefreshIntervalSeconds`). This interval is now
  the worst-case delay between a parent starting study time and the toy going quiet.
- **Portal** (`src/app/tutor/page.tsx`): copy only. The old line promised games work "như
  thường" during study time, which is no longer true.

## Step 1 — Gateway: check what is about to be replaced

```bash
ssh root@110.172.29.207 'grep -c studyState /opt/bubu-gateway/dist/ota.js; ls -l /opt/bubu-gateway/dist/ota.js'
```

Expected: `0` (the live file has no study code yet) and the current size/mtime.

## Step 2 — Gateway: back up the one file

```bash
ssh root@110.172.29.207 'TS=$(date +%s); cp -a /opt/bubu-gateway/dist/ota.js /opt/bubu-gateway/dist/ota.js.bak-study-$TS && echo "backup suffix: study-$TS"'
```

Write down the suffix. **One file only** — `dist/` must not be rsynced wholesale, it still
carries unshipped tutor-card code (see `STATE.md`).

## Step 3 — Gateway: ship it

```bash
scp /Users/judes/Downloads/Bubu-Motion-v1-main/bubu-gateway/dist/ota.js root@110.172.29.207:/opt/bubu-gateway/dist/ota.js
```

## Step 4 — Gateway: restart and verify

```bash
ssh root@110.172.29.207 'systemctl restart bubu-gateway && sleep 4 && systemctl is-active bubu-gateway && curl -s localhost:8080/healthz && echo && journalctl -u bubu-gateway --since "-1min" --no-pager | grep -E "\[services\]|\[ota\]|\[tutor\]|listening"'
```

Expected: `active`, `{"ok":true,"geminiConfigured":true}`, the usual `[services]` lines, and
**no `[tutor]` line** (one means the subject catalog failed to parse).

The reply itself, against a device id that is already claimed (substitute a real one — an
unknown id makes the endpoint issue an activation code instead):

```bash
ssh root@110.172.29.207 'curl -s -X POST localhost:8080/xiaozhi/ota/ -H "Device-Id: AA:BB:CC:DD:EE:FF" -H "Client-Id: deploy-check" -H "Content-Type: application/json" -d "{}" | head -c 400'
```

Expected: a `study` key next to `websocket` and `server_time`, reading `{"active":false,"until":0}`
outside study time, and `{"active":true,"until":<epoch ms>}` for a Bubu whose study window the
portal shows as running.

## Step 5 — Firmware 1.7.7

Built from `PROJECT_VER 1.7.7` (backup of the previous file: `CMakeLists.txt.bak-ver176`).
Push `build/xiaozhi.bin` + `latest.json` (`version: 1.7.7`) to `hoangtalu/Bubu-OTA`, the
channel `main/ota.cc` polls. `assets.bin` is unchanged and does not need pushing.

## Step 6 — End-to-end check on a real device

1. Portal `/tutor` → start a study window for the bench Bubu.
2. Within 5 minutes the device log shows `Study window set (until=…)` then
   `Study time started, SFX muted`.
3. Open a game or tap the eye: silent. Say the wake word: Bubu still talks normally.
4. Press "Kết thúc giờ học": within 5 minutes, `Study time ended, SFX unmuted` and the game
   sounds come back. Letting the window run out to its own end time does the same thing at the
   minute it expires, with no check-in needed.

## Rollback

Gateway (`$TS` = the suffix from step 2):

```bash
ssh root@110.172.29.207 'cp -a /opt/bubu-gateway/dist/ota.js.bak-study-$TS /opt/bubu-gateway/dist/ota.js && systemctl restart bubu-gateway && sleep 3 && systemctl is-active bubu-gateway'
```

Firmware: push `latest.json` back to `version: 1.7.6` with the 1.7.6 binary. Devices already on
1.7.7 with the gateway rolled back simply stop being told about study time and keep their
sound on; the NVS window they hold expires on its own.
