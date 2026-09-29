# Current state

**Read this before `DEVLOG.md`.** This file holds what is true *now*. `DEVLOG.md`
is append-only by design, so it is full of statements that later entries
overturned — every number below has a stale twin somewhere in it.

**This file is overwritable.** When you change something here, replace the line
rather than adding to it. Take the same lock `DEVLOG.md` uses (`.devlog.lock`)
before writing, so two sessions cannot clobber each other.

Last verified: **2026-09-25** — portal redeployed (BUILD_ID `E3qGNIlrIlunTLumFLrN5`): animated setup
tutorial `/guide/basic` (ends with in-tutorial sign-up; a signed-in parent's code joins their existing
account) + `/guide/advanced`, buttons on `/login` and `/enter`.
32/32 tests, `/guide/basic` 200, `/guide/advanced` 200, `/enter` 200, `/tutor` 307, `/admin/login` 401.
Rollback: `/opt/bubu-portal.bak-linkacct-1790306727` (`7aErHbZKR3FTdajkdvAcn`, no account-linking);
before the tutorial: `/opt/bubu-portal.bak-tutorial-1790303801` (`UOXXEM5sUcKqM5e2Dxpxb`, an undocumented
2026-09-23 12:11 rebuild).

Previously: **2026-09-21** (10:55) — portal redeployed (BUILD_ID `paqLKoIjOyFOeZig879BG`)
with durable per-household state in MariaDB, plus gateway `persona.js` only. Both services
restarted clean; `/enter` 200, `/admin/login` 401, `/tutor` 307, OTA 200; gateway stores all
report `mysql`; no errors in either log. **Durability confirmed at 11:05** by a real
round trip: a parent changed a setting, the portal was restarted, and the row survived across
three PIDs (901982 → 904457 → 904931); `bubu_household_state` holds 1 row, 1,257 bytes. Earlier lines (dictation
v1.2 at 10:10, step cards off at 2026-09-18 14:37, firmware 1.7.6/SNTP at 2026-09-17 16:00,
wake word/"Bubu" catalog entry at 2026-09-11) last verified then, not re-checked this session.
**Firmware 1.7.8 pushed to OTA 2026-09-22 11:40** — on-demand game UI plus 1.7.7's study-time SFX mute. The
bench device's code and the OTA image are the same again (the 1.7.7-stamped bench build is superseded). Hardware
evidence so far: `Ota: Study time: off (until=0)` in the boot log, i.e. the study block is parsed on a real
device, negative case only.
**Its gateway half went live 2026-09-22 08:28**: `dist/ota.js` on the VPS is md5-identical to the local build,
and a claimed device's check-in now answers `study: {active:false, until:0}`. The `active:true` case, and the
mute itself on hardware, are still unverified. **Added 15:10 the same day** (measurement only, nothing deployed): the Gemini model inventory and Live tool-blocking results in "Gemini models" below.

---

## Hardware budget

| | allocated | in use | free |
|---|---|---|---|
| Flash `ota_0` / `ota_1` | 5,767,168 each | 3,449,280 (**59.8%**) | 2,317,888 each |
| Flash `assets` | 5,111,808 | 1,375,385 (**26.9%**) | 3,736,423 |
| Flash hole @ `0x10000` | 65,536 | 0 | 65,536 |
| PSRAM heap | 4,832 KiB pool (8 MB chip **minus 3,233 KiB** of app `.text`+`.rodata` mirrored in at boot) | **idle ~750 KiB · in use while played with ~2,850 KiB** | **idle ~4,084 KiB** (largest ~4,063 KiB) · **~1,985 KiB while the menu/eyes/games are up** (largest 1,280 KiB). Both measured 2026-09-22 on hardware. The screens themselves cost ~2 MB of PSRAM — that is the gap, not a leak |
| Internal SRAM | 341,760 B window | 164,723 B static | Measured on OTA 1.7.8, **reproduced on two different units** (2026-09-22): idle **46.5–52.4 KB**, low-water **42,327–42,331 B**, largest block **43,008 B** and flat through every game load/unload. **Before/after on the same day:** 1.7.6, idle, free **22,155 B** and largest **15,360 B** — so on-demand game UI is worth roughly **+26 KB free and 2.8× the largest contiguous block** |

- **~7.9 MiB of the 16 MB flash is allocated but unused.** Rebalancing needs a
  partition-table change, which **cannot** be delivered by OTA — decide it before
  mass production or not at all.
- Every byte of app `.text`/`.rodata` also costs a byte of PSRAM:
  `CONFIG_SPIRAM_FETCH_INSTRUCTIONS`/`RODATA` mirror the image into PSRAM at boot.
  Data in the assets bundle does **not** — it is mmapped from flash.
- `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=2048` — allocations above 2 KB land in
  PSRAM on their own and never touch the internal-SRAM pool.

## Wake word

- **WakeNet behind the AFE.** MultiNet is gone — not merely disabled, its sources
  are excluded from the build.
- **One model per assets bundle**, so "choose a wake word" = "install a different
  bundle". `AfeWakeWord::Initialize()` runs whichever `wn*` model it finds; there
  is no model-selection code in the firmware.
- Default `wn9_hijoy_tts` (Hi Joy). Bundles: `https://api.bubumotion.vn/assets/`.
- Six published: Hi Joy, Hey Ivy, Hi Lily, Hey Kira, Hi Fairy, Sophia.
- **A 7th catalog entry, "Bubu", is now live (2026-09-11).** `model=None` in
  `scripts/build_wakeword_bundles.py`'s `CATALOG` — a bundle with zero WakeNet
  models, same full icon set as every other bundle. A device on it never runs
  wake-word detection (`AudioService::SetModelsList` already goes to
  `wake_word_ = nullptr` gracefully when no `wn*` model is present — no
  firmware change was needed) and answers only to the eye tap. Gateway/portal
  needed no code change either; both are catalog-driven off
  `WAKE_WORD_CATALOG`. `assets-none.bin` (1,084,271 B) is published at
  `https://api.bubumotion.vn/assets/v2/assets-none.bin` (sha256
  `139a0ec2...a979d9c`, confirmed matching on the server), and
  `/opt/bubu-gateway/.env`'s `WAKE_WORD_CATALOG` now carries all 7 entries
  (backup: `.env.bak-bubu-default-1789115422`); gateway restarted clean, no
  `[wake-word]` parse error in the startup log. **Not yet checked: a real
  portal click-through** — a parent actually selecting "Bubu" for a device and
  confirming it applies and stops answering to any spoken wake word. Verifying
  `/internal/wake-word`'s full response was skipped this session because it
  needs `PORTAL_SHARED_SECRET`, and probing that value further than "is it
  set" was correctly refused as credential access.
- **Chosen per device**, not per household (portal `/wake-word` has one form per Bubu).
- **A device's name is its wake word's name** ("Hi Joy" → Joy), derived in
  `bubu-gateway/src/wake-word.ts` `deviceNameFor()`, never stored. The same name
  replaces "Bubu" in the persona at session time, so the AI introduces itself
  by it. Measured on production after deploy: all 10 devices read `Joy`.

## Gemini models

Measured 2026-09-21 15:10 against the project key (`GET /v1beta/models`, 58 models) plus a
host-side Live probe. Nothing here has been changed in production.

- **Production Live model is `gemini-3.1-flash-live-preview`** (`GEMINI_LIVE_MODEL` unset →
  `config.ts` default). Google now lists that id as **Legacy** — "recommend updating to Gemini
  3.8 Live". `gemini-3.8-live` and `gemini-3.8-live-extended-thinking` went stable **2026-09-15**.
  Not switched, not tried on a device. Every tutor-era measurement in DEVLOG was taken on the
  legacy model. `gemini-3.8-live-extended-thinking` would not connect with our config (20 s
  timeout), unexplained.
- **Model lifecycle, from Google's deprecation table (checked 2026-09-21).** Listed dates are
  "the *earliest possible* dates on which a model might be retired"; Google promises advance
  notice but publishes **no guaranteed support window for a GA model**.

  | model | shutdown date | replacement named |
  |---|---|---|
  | `gemini-3.8-live` | **none announced** | — |
  | `gemini-3.1-flash-live-preview` (**what we run**) | **none announced** | `gemini-3.8-live` |
  | `gemini-3.5-transcribe` / `-live` | none announced | — |
  | `gemini-3.5-flash-lite` | none announced | — |
  | `gemini-3.1-flash-lite` | **2027-05-07** | `gemini-3.5-flash-lite` |
  | `gemini-2.0-flash-live-001` | 2025-12-09 (gone) | `gemini-3.8-live` |
  | `gemini-live-2.5-flash-preview` | 2025-12-09 (gone) | `gemini-3.8-live` |

  So there is **no deadline forcing the Live migration today** — but our model is a *preview*
  build already carrying a named replacement, which is the stage just before a date appears, and
  the table shows preview models in this lineup being retired 3–9 months after that point. Treat
  the move to `gemini-3.8-live` as scheduled work, not as something to start when a date lands.
  **For text, prefer `gemini-3.5-flash-lite` over `gemini-3.1-flash-lite`**: nearly the same
  measured throughput and no end date, versus a dated one.
- **No `learnlm-*` model exists on the key.** `learnlm-2.0-flash-experimental` stopped working for
  existing projects on 2025-12-03. "LearnLM" is a prompt technique, not something callable — the
  tutor prompts already use it that way.
- **Gemini's quizzes / flashcards / practice tests / storybooks are Gemini *app* features, not API
  endpoints.** Anything like them has to be built from `generateContent` + `responseSchema` here.
- **`gemini-3.5-transcribe` / `gemini-3.5-transcribe-live` are GA since 2026-08-26**: word-level
  timestamps, diarization, utterance-based language detection, custom vocabulary biasing. Unused.
  Candidate for the transcript-language bug and dictation's verbatim check — but a transcriber
  normalises accent, so it is **not** an answer to "did the learner pronounce it right".
- TTS (`gemini-3.1-flash-tts-preview`, `gemini-2.5-flash-preview-tts`) and image
  (`gemini-3.1-flash-image`, `gemini-3-pro-image`) models are on the key and unused.

## Deployment

| | state |
|---|---|
| Bundles + Caddy `/assets/*` | **live and current**, all 7 answer 200. Re-checked 2026-09-22: the published `assets-wn9_hijoy_tts.bin` is **byte-identical** to the local reference `main/assets.bin` (1,375,385 B, sha256 `3189bd6b…`), so there is nothing to rebuild. Sizes: hijoy 1,375,385 · heyivy2 1,375,413 · hilili 1,375,399 · heykira3 1,375,414 · hifairy2 1,375,414 · sophia 1,375,518 · none 1,084,271 |
| `ASSETS_BASE_URL` | **`https://api.bubumotion.vn/assets/v2`** (bumped from `/assets` 2026-09-10 — see gap note below). Old `/opt/bubu-assets/*.bin` (non-`v2`) still 200 but nothing offers them anymore. |
| Gateway (wake word + sha256 + applied-state) | **live**, `WAKE_WORD_CATALOG` sha256 matches the current bundles; per-device wake word + name-from-wake-word deployed 2026-09-10 |
| Portal | **live** on my.bubumotion.vn, BUILD_ID **`E3qGNIlrIlunTLumFLrN5`** (2026-09-25, adds the setup tutorial with in-tutorial sign-up and add-to-existing-account `/guide/basic` + `/guide/advanced`; its assets must stay under `public/guide/` because `proxy.ts` redirects every other signed-out path). Includes `/wake-word`, `/tutor`, `/chat`, `/guide`, `/admin` (behind Caddy basic auth). Per-household settings are durable in MySQL (`bubu_household_state`); the portal refuses to start in production without `MYSQL_URL`. Rollback: `/opt/bubu-portal.bak-linkacct-1790306727` (was BUILD_ID `7aErHbZKR3FTdajkdvAcn`) |
| Tutor mode | **Voice-only, prompt `tutor-v1.1`** (reads back the current study window, 30 lines). Step cards (phase 2) were live 12:12–14:37 on 2026-09-18 and are now **off**: `TUTOR_CARDS` is not set in `.env`, so card rules are never added and the card tools are not declared to Gemini. Firmware card mode (`BoardMode::kSteps`, `self.tutor.*`) exists only on the bench device …92:d0, not on OTA. Gateway: `TUTOR_SUBJECT_CATALOG` (math enabled; Tiếng Việt, Tiếng Anh disabled), `bubu_device.tutor_subject`/`tutor_until`, table `bubu_tutor_session`; sends `session_end` on Live GoAway/close. Portal `/tutor`. Recall across a closed session confirmed by the user 2026-09-17. **Not done:** baseline report (see Known gaps). Plan: `docs/tutor-mode-plan.md`, runbook: `docs/tutor-mode-deploy.md` |
| Study-time quiet (SFX) | **Both halves live; unverified by ear.** 1.7.7 mutes the SFX overlay lane (games, eye-tap and mischief voices, emotion lines, Pomodoro chimes — not the notification/popup/low-battery sounds on the main lane) for a whole `/tutor` window. The device learns the window from a new `study: {active, until}` block in the `/xiaozhi/ota/` reply, holds the end time in NVS (`study/until_s`) and re-decides against its own clock every second, so only the *start* of the silence depends on a check-in. Gateway `dist/ota.js` deployed 2026-09-22 08:28 (backup `/opt/bubu-gateway/dist/ota.js.bak-study-1790040462`); verified `{active:false, until:0}` for devices …92:d0 and …92:2c. **Never verified:** a real window making a real device go quiet — no hardware has run 1.7.7 either. Runbook: `docs/study-quiet-deploy.md` |
| Dictation (Tiếng Việt study time) | **live.** Prompt **`dictation-v1.2`** since 2026-09-21 10:10 (gateway only: `dictation.js` + map, backup suffix `groups-1789960222`). Groups follow the phrase with a hard ceiling of **6 chữ**, and a parent's single line break is an exact, silent reading break (a blank line is read as "xuống dòng"). Portal copy caught up 2026-09-21 10:55: the passage box now carries the three-line guidance (Enter = silent reading break, blank line = "xuống dòng") and the "6 chữ" wording. Earlier: v1.1 pacing 2026-09-18 15:56, first live use 15:20 |
| Firmware | **1.7.9 live on OTA** (`hoangtalu/Bubu-OTA`, pushed 2026-09-23, commit `80a0c13`, blob sha `f0c2d53b`, 3,455,808 B) — voice waves beside the eyes (listening yellow/inward, speaking green/outward), breathing status arc (amber loading, cyan OTA), status text hidden for idle/listen/speak/loading; also ships the 2026-09-22 dead-code cleanup (8 unused `boards/common/` files, dead camera MCP tool). **Artifact is now `bubu.bin`** (ESP-IDF project renamed `xiaozhi`→`bubu`; `project_name` reads `bubu`); `latest.json` url points at it. `xiaozhi.bin` (1.7.8, blob `8a388d94`) left in the repo as the **rollback target**: restore `latest.json` to `1.7.8` + `.../main/xiaozhi.bin`. **End-to-end OTA verified on the bench 2026-09-23**: a 1.7.8 image built from this tree (`ota.cc` unchanged since 2026-09-21, i.e. same as fielded 1.7.8) fetched `bubu.bin`, wrote `ota_1`, rebooted into 1.7.9, no loop. **A fielded unit updated to 1.7.9 (reported by user 2026-09-23).** Firmware only installs updates at boot (`application.cc:1044`); the 5-min check-in (`AssetsRefreshTask`) never upgrades, so always-on units update only after a power cycle. Build dir is `build-waves/` — the old `build/` has a stale CMake cache. Source sync to `hoangtalu/Bubu-Motion-v1` still at commit `817956d7` (2026-09-17), i.e. stale. **Both repos are public.** Chat-subtitle feature is in source but disabled (`SetupSubtitle()` call commented out in `main/display/eye_display.cc`) — never flashed, held for a future release. |
| Eye behaviour (source, unreleased) | **Source tree differs from fielded 1.7.9** since 2026-09-29: `main/display/eye_animation.*` is the Eye Lab version (sibling project `../Bubu_Eye_Lab/`, bench-only test firmware sharing this partition table; its README lists the locked motions) and the care-emotion "carousel" is **off** (`CareEmotionConfig::enabled = false`). Device states drive the eyes via `EyeDisplay::SyncDeviceLook()`. Built in `build-waves/` (3,312,272 B, still versioned 1.7.9 — bump before any OTA); not flashed or released. Rollback: `main/display/eye_*.bak-eyelab-1790673138`. |
| MẮT XANH (source, unreleased) | **Rewritten 2026-09-29**: panel game `main/display/green_eye_game.*` (tap the leaf-green eye among 2–4; +1 CẢM XÚC per green, cap 25; record NVS `greeneye/best`). Old `eye_game.*` and EyeAnimation's game mode are gone (lab copy synced). **Bench-verified 2026-09-29** (2 games): reward paid on normal end and on mid-game exit, open costs ~2.7 KB internal, largest block 43,008 B flat, clean unloads. Not released to OTA; colours/tap feel not yet judged by the user. Checks: `tools/green_eye_sim.cc`, `tools/verify_green_eye_layout.py`. Rollback: `*.bak-greeneye-1790676583` + `_to_delete/eye-game-v1-1790676583/`. |

- **At least one fielded device (not the bench unit) had already applied an
  assets bundle before 2026-09-10** — disproves the "none of them had ever
  received one" assumption written earlier the same day (below this bullet was
  first drafted). Surfaced immediately: it took the 1.7.4 OTA update but kept
  showing the HỌC TẬP fallback text, because its `applied_url` already matched
  what the gateway kept offering (see gap note). Unknown how many other fielded
  devices are in the same state — assume some are.
- **URL-identity gap (real, hit once already, now worked around operationally
  but not fixed in code).** `CheckAssetsVersion()`/`StoreAssetsDownloadUrl`
  compare only the **URL string** (`applied_url == url`), never content, so a
  device that already applied a bundle at some URL silently skips
  re-downloading even if the file behind that same URL changes later. Worked
  around today by bumping `ASSETS_BASE_URL` to `/assets/v2` (see table above) —
  `assetsUrlFor()` in `bubu-gateway/dist/wake-word.js:62` builds every model's
  URL from that one env var, so this forces **every** already-applied device,
  not just the one reported, to see a new URL and redownload on its next boot.
  **This is a one-time workaround, not a fix** — the comparison is still
  URL-string-only. The next time a bundle's content needs to change post-ship,
  either bump `ASSETS_BASE_URL` again (`/v3`, ...) or add real
  content-versioning to the gateway/firmware — there isn't one today.
- `PROJECT_VER` is **1.7.7** in the tree (checked 2026-09-22), matching what the OTA
  channel now offers by version *string* only. **The binaries are NOT the same**
  (measured 2026-09-22): OTA blob `6fe6ffa0`, 3,442,560 B, versus local
  `build/xiaozhi.bin` `59697221`, 3,449,344 B — the local tree carries the
  on-demand game UI on top, and both stamp themselves `1.7.7`. Never identify
  one of these builds by its version string; check size or hash. **Still set it to 1.7.0 before
  building a factory image** — see the 2026-09-08 DEVLOG entry; that is about
  the factory-flash version stamp, unrelated to what ships over OTA.
- **1.7.4 is known-broken, do not re-push it.** It fixed the touch-during-download
  crash and shipped the HỌC TẬP icon, but exposed a second bug: every menu/care
  icon goes stale (silently wrong, not crashed) if an assets bundle downloads
  and applies later in the *same* boot that already resolved icons at
  `MenuSystem::Begin()` — the normal first-OTA-then-assets sequence, so this
  hit real devices immediately. Confirmed on 3 real devices: icons vanish after
  the first OTA+assets load, fine only after a reboot. Fixed in 1.7.5 via
  `MenuSystem::RefreshIcons()`. At least one real device self-updated to the
  broken 1.7.4 mid-session before 1.7.5 was pushed; it self-corrected to 1.7.5
  on its next OTA check, no manual intervention needed.
- **Testing gotcha, costs real time if forgotten:** esptool flashing to `ota_0`
  (offset `0x20000`) is silently a no-op for what actually *boots* if the
  device's `otadata` already points at `ota_1` — which happens the moment the
  device does a real self-OTA in the background (e.g. while WiFi is up and a
  session is mid-conversation with the user, unrelated to what's being tested).
  `idf_monitor`'s own "Checksum mismatch between flashed and built
  applications" line is the tell; `Ota: Running partition:` in the boot log
  says which slot is actually live. When testing a fresh esptool flash on a
  device with WiFi, flash **both** `0x20000` (`ota_0`) and `0x5A0000` (`ota_1`)
  to be sure, or check the running-partition log line before trusting a result.
- **Firmware clock: SNTP is back and live on OTA as of 1.7.6 (2026-09-17).** `main/time_sync.cc`:
  system clock holds true UTC and `TZ=ICT-7`; SNTP is the primary source, the OTA reply's `server_time` is the
  fallback. Bench-verified only — no fielded device has taken this OTA update yet; devices still on 1.7.5
  take their time from the VPS only until they self-update.
- Rollbacks: `/opt/bubu-portal.bak-wakeword`,
  `/opt/bubu-gateway/dist.bak-wakeword` + `.env.bak-wakeword`,
  `/etc/caddy/Caddyfile.bak-wakeword`,
  `/opt/bubu-assets.bak-hoctap-icon` (pre-icon `.bin` files),
  `/opt/bubu-gateway/.env.bak-hoctap-icon-1789023197` (pre-icon catalog hashes),
  `/opt/bubu-gateway/.env.bak-hoctap-icon-v2-<timestamp>` (pre-`/v2` `ASSETS_BASE_URL`),
  `/opt/bubu-gateway/dist.bak-devicename-1789036388` + `/opt/bubu-portal.bak-devicename-1789036452`
  (before per-device wake word / names),
  **`/opt/bubu-gateway/dist.bak-tutor-1789610170` + `.env.bak-tutor-1789610170` +
  `/opt/bubu-portal.bak-tutor-1789610170`** (before tutor mode; portal was
  `S9ggiZ32jvqr2Y2Rg7-3Y`), **`/opt/bubu-gateway/dist.bak-study-recall-1789631581` +
  `/opt/bubu-portal.bak-study-recall-1789631581`** (before release 2; portal was
  `-uFSQoYlrTdpLMlUNP5tc`), **`/opt/bubu-gateway/dist.bak-cards-1789708312`** (before step
  cards, i.e. `tutor-v1.1` without card code), **`dist.bak-cardsoff-1789715828`** (cards ON —
  do not restore unless cards are wanted back). Rollback commands: `docs/tutor-mode-deploy.md` step 9.
- SSH/rsync/scp to the production VPS (`root@110.172.29.207`) was blocked by the
  Claude Code auto-mode classifier earlier in the 2026-09-10 session —
  in-chat user permission alone did not lift it, even explicit repeated
  approval, only working after the user changed something on their end. Once
  that happened it stayed allowed for the rest of the session (multiple SSH
  calls succeeded afterward without re-prompting). A session hitting the block
  fresh should hand the user a ready-to-run script/commands rather than
  retrying blindly.

## Claims that are DISPROVEN or SUPERSEDED

Do not rebuild an argument on any of these. Each was true, or believed, at some
point in `DEVLOG.md`.

| Claim you may find in DEVLOG | Reality |
|---|---|
| "Step cards are declared `NON_BLOCKING`, so pushing a card never pauses speech" (tutor-mode-plan §0/§5b, DEVLOG 2026-09-18) | **False for the model in use.** Google: "Asynchronous function calling is not yet supported in Gemini 3.1 Flash Live." Every card push was a synchronous call: the device sat in "listening" while the model composed the card and waited for the device. Bench 2026-09-18: 11 pushes, 8 refused as too wide, one ~11 s silence. Cards switched off; `behavior` removed from declarations. **And a version bump does not fix it (measured 2026-09-21): `gemini-3.8-live` blocks too**, 2/2 runs — worse, it calls the tool having said nothing first (~6 s of total silence vs 3.1's ~4.4 s after a filler). Async works only on `gemini-2.5-flash-native-audio-*` (spoke over a pending call in 2/3 runs). |
| "Turning off `SPIRAM_FETCH_INSTRUCTIONS`/`SPIRAM_RODATA` is the big free win — it returns 3.16 MB of PSRAM" (proposed 2026-09-22) | **The 3.19 MB is real and REJECTED ON HARDWARE anyway.** A/B on …92:d0, both images from one tree: idle PSRAM free 4,083,940 → 7,431,560 B, boot 0.31 s faster, internal SRAM unchanged — but the audio DMA then fails continuously: **2,529 × `gdma-link: gdma_link_mount_buffers(173): no more space for buffer mounting`** in 6 min (baseline: 0), starting 70 ms after `AudioCodec: Audio codec started`. Internal/DMA free is unchanged, so it is the DMA descriptor link list being too small for buffers that now sit in PSRAM, not a memory shortage. Reverted. Fixable in principle (pin the audio buffers to internal DMA memory, or size the link list) — not free. Detail: `docs/psram-xip-experiment.md` |
| "PSRAM bus latency causes the SFX crackle" (entry ~266) | **Disproven on hardware** by the very next entry (~271): pinning the stack to internal RAM starved WiFi *and* the crackle remained. Remaining lead: the 24000→16000 Hz resample on every TTS stream. |
| "`assets` is 87.9% full, ~600 KB free" | Now **25.5%**, 3,806,360 B free. |
| "Internal SRAM 5.5–9.1 KB free, largest block 1,920 B" | Now **28–30 KB**, largest **24,576 B**. Fixed by moving `sfx_codec`'s stack to PSRAM. |
| "PSRAM ~1.87 MB free" | Now **~3.8 MB**. |
| "MultiNet is the wake word engine" | Removed. WakeNet, measured 5/5 on hardware where MultiNet was 0/20. |
| "`assets` is a SPIFFS filesystem" | Declared `spiffs` in the CSV but **never mounted**. The firmware `esp_partition_mmap`s it as one packed blob and `Assets::Download` overwrites the whole partition. The 3.8 MB of free space is unformatted trailing bytes — you cannot put a file there without new code or a new partition. |
| "The bundle's font and emoji are used" | The **bundle's** font and emoji are not drawn and were removed. But the **compiled-in** `BUILTIN_TEXT_FONT`/`BUILTIN_ICON_FONT` **are** drawn — the status bar (battery/WiFi/mute icons and the status text) is visible. The eye_display.cc:468 comment covers `chat_message_label_`/`bottom_bar_` only, **not** `status_bar_`. Removing them on 2026-09-10 made the icons vanish. |

## Known gaps

- **Orphaned claims (found 2026-09-25, open).** A code redeemed at step 2 of `/guide/basic` claims the
  device into a new household; sign-up is step 6. After the 15-min pairing cookie or a page reload the
  household has no account, and a claimed device never shows a code again — the parent is locked out.
  Only recovery: `/admin/devices` → Unclaim (wipes name/voice/persona). `/enter` has the same gap, narrower.

- **Live experiment since 2026-09-21: the gateway no longer sends `speechConfig.languageCode`.**
  Deployed as a single file (`dist/gemini-bridge.js`; backup `.bak-nolangcode-1789957966`), not
  a full `dist/` sync. Unverified until the user retests: does language mixing drop, and does the
  09-09 Bắc/Nam accent drift come back on a long Vietnamese-only chat? Rollback = restore the backup
  and restart `bubu-gateway`. Local `dist/` still holds unshipped Phase 2b tutor-cards code, so never
  rsync the whole directory without the dry-run check in `docs/tutor-mode-deploy.md`.

- **Single DNS provider is a single point of failure (outage 2026-09-18, resolved ~14:40).**
  `bubumotion.vn` is delegated only to Mắt Bão (`ns1.matbao.vn`, `ns2.matbao.vn`). On
  2026-09-18 their nameservers stopped answering: Google, Cloudflare, Quad9, AdGuard and FPT
  (210.245.1.254) all failed for `api.`/`my.`/apex, and `matbao.vn` itself failed too; `.vn`
  TLD and other `.vn` domains were fine. The bench device logged `couldn't get hostname for
  :api.bubumotion.vn` while the VPS answered normally when reached by IP (OTA 200, portal 307).
  No device session started 13:57–14:40. Onset unknown (journal and cache effects hide it).
  Recovered by ~14:40 with no action on our side. **Open:** add a secondary DNS provider so one
  provider's outage cannot take the whole product offline; change it while DNS is healthy
  (the `.vn` delegation TTL is 43,200 s).
- **Persona edits (like today's forced-Vietnamese removal) do not retroactively reach
  already-configured households.** `device-session.ts`'s `loadPersona()` reads each
  household's `personaPrompt` from MySQL — composed once at `/persona` save time, never
  recomputed from the current `INTRO_LINE`/scaffold on connect. Every already-configured
  household must open `/persona` and press Save again (no edits needed) to pick up a
  scaffold change. Only a never-configured device gets a new default automatically. Second
  time this exact gap has bitten a persona rollout (see DEVLOG ~line 500 and 2026-09-18).

- **`MySqlHouseholdStateRepo` works in production but has no test coverage.** Proven
  2026-09-21 11:05 by a real save → restart → reload round trip (row survived three PIDs). All
  three `household-state.test.ts` tests still use `MemoryHouseholdStateRepo`, so the SQL, the
  `FOR UPDATE` locking and the insert race are exercised only by production traffic.
- **The database is MariaDB 11.8.8, not MySQL**, and portal + gateway share one schema
  (`bubu_account`, `bubu_chat_turn`, `bubu_device`, `bubu_dictation`, `bubu_household_state`,
  `bubu_tutor_session`, all InnoDB). MariaDB makes `JSON` a plain alias for `LONGTEXT`, so
  `state_json` is stored as text with no server-side validation — `decode()` already handles
  that, but do not assume MySQL JSON functions or generated columns are available.
- **No automated database backup exists.** Since 2026-09-21 the same schema holds parent
  logins, device ownership, chat history and now each household's portal settings, so losing
  the VPS loses all of it. Nothing is dumped, nothing is stored off the box, and no restore has
  ever been tested.
- **Gateway `dist/` still cannot be rsynced wholesale**: it carries unshipped Phase 2b
  tutor-cards code. Deploy gateway changes file by file.

- **VPS clock: sync runs over HTTPS, not NTP (fixed 2026-09-17).** The clock was 47m56s slow
  because outbound NTP (UDP 123) never gets a reply (ufw vs provider not yet determined).
  Cron `/etc/cron.d/bubu-timesync` → `/usr/local/sbin/bubu-timesync` sets it from Google's HTTPS
  `Date` header every 10 min, with guards against an empty or implausible value. Checked from
  outside 05:26 UTC: matches google.com. This matters for the fleet: devices have no SNTP, so
  their only clock is the OTA reply's `server_time`.

- ~~Portal chat hid older days (500-row cap) and showed Bubu's reply above the child's
  line~~ **FIXED and deployed 2026-09-17 14:53.** Cap is 5,000 rows, API returns written
  order. Verified on production for device …92:d0's household: 80 rows, 39/39
  same-timestamp pairs child-first, 1 study window of 34 rows labelled Toán.
- Portal chat does not say which Bubu a line came from: a two-Bubu household sees both
  children's conversations interleaved per day. The API now returns `deviceId`; the page does
  not use it yet. (All 11 active households are single-device today.)

- **`/opt/bubu-gateway/src` on the VPS is stale** — older than the 2026-09-09 wake-word
  work (no `wake-word.ts`; `ota.ts`, `gemini-bridge.ts`, `devices.ts` differ). Deploys
  rsync `dist/` built locally. **Never run `npm run build` on the box**: it would
  silently replace the live `dist/` with pre-wake-word, pre-tutor code. Measured
  2026-09-17 by rsync dry-run.
- **The Gemini key is on the free tier for text models**: `generateContent`
  on `gemini-3.8-flash` is capped at 5 requests/minute
  (`generate_content_free_tier_requests`), measured 2026-09-17 and **re-confirmed
  2026-09-21** (15 parallel requests → 6×200, 8×429, 1×503). Quotas are per model, so this
  did not touch the Live model — but **whether the Live model itself runs on a free-tier
  quota was not checked**, and would cap the whole fleet if so.
- **The per-minute cap is a model choice, not a fixed property of the key** (measured
  2026-09-21 with parallel bursts on the dev key in `bubu-gateway/.env`; RPM only, the
  **daily** cap was deliberately not probed and remains unknown):

  | model | burst | 200s | ceiling |
  |---|---|---|---|
  | `gemini-3.1-flash-lite` | 20 | 16 (4×429) | **~16 RPM** |
  | `gemini-3.5-flash-lite` | 15 | 13 (2×429) | **~13–15 RPM** |
  | `gemini-3.6-flash` | sequential | 8 then 429 | ~8 RPM |
  | `gemini-3.8-flash` | 15 | 6 (8×429, 1×503) | **~5 RPM** |
  | `gemma-4-31b-it` | 20 | 11 (9× **500**, no 429) | no RPM cap hit; endpoint unstable under concurrency |
  | `gemini-3.1-pro-preview` | 1 | 0 (429, `...FreeTier` quota) | **Pro is unusable on free tier** |
  | `gemini-2.5-flash` / `-lite` / `-pro` | 1 | 0 (404) | "no longer available to new users" — **gone** |

  So a flash-lite model gives roughly **3× the throughput of `gemini-3.8-flash` on the same
  free key**, for offline/batch text work. `gemini-3.5-flash` and `gemini-3.7-flash` both
  answered 503 "high demand" on a single call the same day — availability, not quota.
- **Tutor-mode baseline report not taken.** `tutor-report-cli.js` works against the real
  DB (426 sessions / 30 days, 0 in tutor mode, 142 math-like), but classification failed
  on quota/overload. The baseline is still valid later (normal-mode sessions are those
  without a `bubu_tutor_session` row), but pre-launch sessions age out of the 30-day
  chat retention day by day.
- **Gemini Live drops an idle session after ~150 s**, code 1008 "The operation was aborted", with
  **no GoAway** (measured 2026-09-18, twice, `gemini-3.1-flash-live-preview`). 100 ms of silent PCM
  every 30 s kept one open for 8 min. The gateway sends no such keepalive today, so a tap-mode
  conversation left idle with the channel open loses its Gemini session after ~2.5 min.

- Gateway activation codes are **in memory** in production (`[services] activation
  codes: IN MEMORY`, `REDIS_URL` unset): every gateway restart drops codes a parent is
  mid-way through typing. Seen in the 2026-09-17 startup log.

- **Noise suppression is off, and turning it on is NOT a one-line change.**
  `AfeAudioProcessor::Initialize` enables NS only when it finds an NSNet model, and
  no bundle has ever contained one, so NS has never run. AGC is explicitly off too:
  audio reaching STT is raw. **Tried on 2026-09-10 and it crashed the device on
  every wake word** — `LoadProhibited` in `afe_init_aec_ns`, because this lane
  passes `NULL` as the models list to `afe_config_init()` and AFE resolves
  `ns_model_name` against that list. Any attempt must first deal with that NULL,
  which is itself deliberate (AFE memory footprint). Not a regression from the
  WakeNet switch; NS predates it.

- `applied` in the portal **lags one device check-in**: `GetSystemInfoJson()` runs
  inside `Ota::CheckVersion()`, before `CheckAssetsVersion()` installs anything, so
  a device reports the bundle it had *at check-in*. Up to 5 minutes of "chưa
  nhận" after it has in fact applied (was 15, until 1.7.7 shortened the poll).
- MultiNet never fired, but **WakeNet's false-accept rate has not been measured**.
  Only 5 positive samples exist.
- ~161 KB of libstdc++ locale/iostream is compiled in and never used, pulled in by
  **two** `std::stringstream` uses (`ota.cc`, `afe_wake_word.cc`), both of which
  only split a string. Costs the same again in PSRAM. The unused **fonts** that sat
  beside it were removed on 2026-09-10 (−206,336 B, measured).

- ~~Touching the screen while an assets download is in progress crashes the
  device~~ **FIXED 2026-09-10.** `LvglStrategy::UnApplyPartition()` (assets.cc)
  now correctly clears `partition_valid_` the instant it unmaps the partition
  (it never did before — a `partition_valid()` accessor other code already
  trusted was lying), and `MenuSystem::Open()` — the sole entry point from the
  idle eyes screen into any icon-bearing panel — checks it first and no-ops
  instead of showing a screen that would dereference a now-dangling icon
  pointer. Verified on hardware: forced a genuine re-download (NVS wipe),
  tapped mid-download, no crash, tap simply not consumed. Not touched:
  `Assets::EmoteStrategy` (different board family, unverified, out of scope).
