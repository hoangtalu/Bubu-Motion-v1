# Current state

**Read this before `DEVLOG.md`.** This file holds what is true *now*. `DEVLOG.md`
is append-only by design, so it is full of statements that later entries
overturned — every number below has a stale twin somewhere in it.

**This file is overwritable.** When you change something here, replace the line
rather than adding to it. Take the same lock `DEVLOG.md` uses (`.devlog.lock`)
before writing, so two sessions cannot clobber each other.

Last verified: **2026-09-17** (14:55, release 2) — tutor mode phase 1 deployed to gateway and
portal and checked on the production VPS (service state, startup log, endpoint,
BUILD_ID, public probes, one real normal-mode conversation afterwards). Earlier
lines (firmware 1.7.5, wake word, "Bubu" catalog entry) last verified
2026-09-11 on the bench device, fielded devices and the VPS, not from `DEVLOG.md`.

---

## Hardware budget

| | allocated | in use | free |
|---|---|---|---|
| Flash `ota_0` / `ota_1` | 5,767,168 each | 3,417,840 (**59.3%**) | 2,349,328 each |
| Flash `assets` | 5,111,808 | 1,375,385 (**26.9%**) | 3,736,423 |
| Flash hole @ `0x10000` | 65,536 | 0 | 65,536 |
| PSRAM heap | 4,832 KiB pool | ~760 KiB | **~4,072 KiB** |
| Internal SRAM | 334 KiB window | 159 KiB static | idle **24–26 KB**, largest block **18,432 B** (2026-09-17, bench, same with and without SNTP; down from 30–33 KB, cause unknown) |

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

## Deployment

| | state |
|---|---|
| Bundles + Caddy `/assets/*` | **live**, all 6 now include the HỌC TẬP menu icon |
| `ASSETS_BASE_URL` | **`https://api.bubumotion.vn/assets/v2`** (bumped from `/assets` 2026-09-10 — see gap note below). Old `/opt/bubu-assets/*.bin` (non-`v2`) still 200 but nothing offers them anymore. |
| Gateway (wake word + sha256 + applied-state) | **live**, `WAKE_WORD_CATALOG` sha256 matches the current bundles; per-device wake word + name-from-wake-word deployed 2026-09-10 |
| Portal | **live** on my.bubumotion.vn, BUILD_ID **`o8eVFFgAQ2J_SUkqWzf9y`** (2026-09-17 14:54, study-time memory + chat fixes). Includes `/wake-word`, `/tutor`, `/chat`, `/admin` (behind Caddy basic auth) |
| Tutor mode, phase 1 | **deployed 2026-09-17; prompt now `tutor-v1.1`** (reads back the current study window, 30 lines) since 14:53. Only use so far: the user's own test on device …92:d0 (34 rows, `tutor-v1`). Gateway: `TUTOR_SUBJECT_CATALOG` in `.env` (math enabled; Tiếng Việt, Tiếng Anh disabled), columns `bubu_device.tutor_subject`/`tutor_until` added on start, table `bubu_tutor_session`. Portal `/tutor`. Live test of v1 done by the user. **Not done:** live test of v1.1 recall across a closed session (runbook "Release 2"), baseline report (see Known gaps). Gateway also sends `session_end` on Live GoAway/close since 14:53. Plan: `docs/tutor-mode-plan.md`, runbook: `docs/tutor-mode-deploy.md` |
| Firmware | **1.7.5 live on OTA** (`hoangtalu/Bubu-OTA`, pushed 2026-09-10, commit `acab53f`) — supersedes 1.7.4, which is now known-broken, see below |

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
- `PROJECT_VER` is **1.7.5** in the tree as of 2026-09-10, matching what the OTA
  channel now offers (pushed and verified live this session, blob sha
  `841a96f1` local and remote identical). **Still set it to 1.7.0 before
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
- **Firmware clock: SNTP is back (2026-09-17, bench-verified, NOT on OTA yet).** `main/time_sync.cc`:
  system clock holds true UTC and `TZ=ICT-7`; SNTP is the primary source, the OTA reply's `server_time` is the
  fallback. Fielded devices on 1.7.5 still take their time from the VPS only. Ships with the next
  `PROJECT_VER` bump.
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
  `-uFSQoYlrTdpLMlUNP5tc`). Rollback commands: `docs/tutor-mode-deploy.md` step 9.
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
| "PSRAM bus latency causes the SFX crackle" (entry ~266) | **Disproven on hardware** by the very next entry (~271): pinning the stack to internal RAM starved WiFi *and* the crackle remained. Remaining lead: the 24000→16000 Hz resample on every TTS stream. |
| "`assets` is 87.9% full, ~600 KB free" | Now **25.5%**, 3,806,360 B free. |
| "Internal SRAM 5.5–9.1 KB free, largest block 1,920 B" | Now **28–30 KB**, largest **24,576 B**. Fixed by moving `sfx_codec`'s stack to PSRAM. |
| "PSRAM ~1.87 MB free" | Now **~3.8 MB**. |
| "MultiNet is the wake word engine" | Removed. WakeNet, measured 5/5 on hardware where MultiNet was 0/20. |
| "`assets` is a SPIFFS filesystem" | Declared `spiffs` in the CSV but **never mounted**. The firmware `esp_partition_mmap`s it as one packed blob and `Assets::Download` overwrites the whole partition. The 3.8 MB of free space is unformatted trailing bytes — you cannot put a file there without new code or a new partition. |
| "The bundle's font and emoji are used" | The **bundle's** font and emoji are not drawn and were removed. But the **compiled-in** `BUILTIN_TEXT_FONT`/`BUILTIN_ICON_FONT` **are** drawn — the status bar (battery/WiFi/mute icons and the status text) is visible. The eye_display.cc:468 comment covers `chat_message_label_`/`bottom_bar_` only, **not** `status_bar_`. Removing them on 2026-09-10 made the icons vanish. |

## Known gaps

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
- **The production Gemini key is on the free tier for text models**: `generateContent`
  on `gemini-3.8-flash` is capped at 5 requests/minute
  (`generate_content_free_tier_requests`), measured 2026-09-17. That model was also
  returning 503 "high demand". Quotas are per model, so this did not touch the Live
  model — but **whether the Live model itself runs on a free-tier quota was not
  checked**, and would cap the whole fleet if so.
- **Tutor-mode baseline report not taken.** `tutor-report-cli.js` works against the real
  DB (426 sessions / 30 days, 0 in tutor mode, 142 math-like), but classification failed
  on quota/overload. The baseline is still valid later (normal-mode sessions are those
  without a `bubu_tutor_session` row), but pre-launch sessions age out of the 30-day
  chat retention day by day.
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
  a device reports the bundle it had *at check-in*. Up to 15 minutes of "chưa
  nhận" after it has in fact applied.
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
