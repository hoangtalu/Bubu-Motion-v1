# Downloadable Games — Design Plan

Status: proposal, not implemented. No code changed by the session that wrote this.
Target board: `esp32s3-1.28-round-i80` (240×240 round LCD, CST816 touch, 16 MB flash,
8 MB PSRAM). Numbers below were measured against the current tree on 2026-09-16, not
copied out of `DEVLOG.md`.

**Goal.** Games stop being compiled into the firmware. The server holds a catalog, the
child (or the parent) installs a game onto the Bubu, it stays there until somebody
deletes it, and the device knows how much room is left and says so before a download
that cannot fit.

---

## 0. Recommendation in one paragraph

Build a **`GameStore`**: a slot allocator over a mmapped flash region, holding
self-contained **game packs** (`.bgp`) that the device downloads, verifies by SHA-256,
installs into free slots and deletes on demand. Put the region **in the unused tail of
the existing `assets` partition** so this reaches devices already in the field, and
*also* reserve a real `games` partition in the production partition table — the same
`GameStore` binds to whichever it finds at boot. For what a game *is*, ship **engines in
firmware and content in packs** first (v1: re-parameterised versions of the games that
already exist), then a **declarative scene+rules engine** (v2) that lets genuinely new
games ship without a firmware release. Do **not** ship a script VM or downloadable native
code in v1 — see §4.

**One concern, stated once.** Games are not a flash problem today. Measured from
`build/xiaozhi.map`: the three games' logic is 3,237 B and their UI is 17,903 B — about
**21 KB of a 3,397,024 B image**. This system will *add* roughly 35–55 KB of firmware,
not remove any. Its value is content velocity (new games without an OTA and without a
firmware risk window) and the storage UX you asked for, not memory. That is a good reason
to build it; it is not a memory optimisation and should not be sold internally as one.

**On timing, which §0b argues at length:** reserve the space now, ship the next one or two
games through firmware, and build the download half when a named trigger fires. Sections
3–9 describe the end state; §0b decides when to start paying for it.

---

## 0b. Should we do this at all? — firmware OTA vs. server packs

The question that has to be answered before any of the rest matters. There are **four**
options, not two, and the two middle ones cost no new code.

| | Where code lives | Where art lives | New subsystems | On-device capacity | Cost of changing one game |
|---|---|---|---|---|---|
| **A** All in firmware | firmware | firmware (`.rodata`) | none | **~1 MB** (double-charged, see below) | 3,397,024 B firmware OTA to every device |
| **B** Code in firmware, art in the assets bundle | firmware | assets bundle | **none — this is the menu-icon path** | ~2.6 MB | ~1.4 MB bundle to every device, ×7 bundle rebuilds, + a URL bump |
| **C** Download packs | firmware (engines) | packs | GameStore + catalog + portal + tooling | 2.9 MB (assets tail) / 4.1 MB (v3) | pack only (~50–500 KB), only to devices that want it |
| **D** Hybrid — engines in firmware, content in packs | firmware | packs | as C | as C | as C |

### The numbers that decide it

**1. Firmware double-charges every byte.** `CONFIG_SPIRAM_FETCH_INSTRUCTIONS`/`RODATA`
mirror the whole app image into PSRAM at boot. Confirmed on hardware, not inferred:
removing 206,336 B of fonts returned 206,136 B of PSRAM. So **art compiled into firmware
costs flash *and* PSRAM 1:1**, while anything in the assets partition is mmapped and costs
**zero** PSRAM. Free PSRAM is ~4,169,728 B and it took the whole MultiNet→WakeNet switch to
get there (1.87 MB → 3.80 MB). Spending 1 MB of it on game art would undo a third of that.
Option A is therefore the worst possible home for art, and it is the only option whose
capacity is limited by RAM rather than flash.

**2. Art is the entire story, and today there is none.** All three games draw with LVGL
primitives — there is not one image asset among them. For scale: the 13 menu/care icons are
240×240 PNGs averaging **83,322 B each**, i.e. **one screen-sized image is ~3.9× the total
code of all three games combined** (21,140 B). The moment games get real artwork, the
"games are tiny" argument in §0 stops holding and the storage question becomes the whole
question. A product for 4–10 year olds will have artwork.

**3. Bandwidth is not an argument either way.** 11 devices in the field; a device only
downloads when something changes. The same reasoning already settled bundle hosting. Ignore
it.

**4. Reliability of the transfer is an argument.** Measured on real devices: a 3.4 MB
firmware download hit two consecutive `HTTP content receive timeout` failures at RSSI
−74/−75 dBm, while 1.3 MB bundles complete in 20–30 s at 22–39 KB/s. A 200 KB pack is a
qualitatively easier thing to deliver into a Vietnamese home's weak-Wi-Fi corner than a
3.4 MB firmware image.

**5. Risk coupling is the strongest argument for C — but it is weaker today than it will
be.** Shipping a game through firmware means re-qualifying the whole firmware. This repo's
own record: 1.7.2 shipped untested and had to be rolled back; 1.7.4 shipped and broke every
menu icon on 3 real devices, hotfixed as 1.7.5 within a day. On top of that,
`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y` with `MarkCurrentVersionValid()` gated behind a
full network round-trip is a known, unfixed structural risk.
*The honest counter:* the team currently ships firmware every few days (four releases in
five days at peak), so right now a game would ride along for free.
*Why the counter expires:* that cadence is a symptom of an unstable period, not a steady
state. After production the fleet grows and each firmware release gets more expensive to
justify — exactly when content cadence needs to stay high. **The coupling argument gets
stronger after launch, not weaker.**

**6. Two things only C can do at all.** Per-child game selection (a 4-year-old and a
9-year-old should not get the same catalog) and the "hết bộ nhớ" experience you asked for.
With A or B every device holds every game and there is no choice to run out of room for.

### Verdict

**Split by *what* ships, not by *whether*: engines through firmware OTA, content through
packs — option D.** Engines are code, secure boot signs them, and they stabilise. Content
is the thing that must ship often and differ per child.

**But C/D does not pay for itself today** — 3 games, no art, firmware shipping weekly
anyway. Its fixed cost is ~35–55 KB of firmware in the riskiest subsystem in the tree
(flash erase/write against live LVGL mmap pointers, where this repo has already shipped two
crashes) plus gateway, portal and tooling work. Building it now, before there is content to
justify it, is infrastructure ahead of demand.

The asymmetry that resolves the timing: **the space reservation is irreversible and cheap;
the subsystem is reversible and expensive.** So:

1. **Now (days, mandatory):** reserve the space — the 2 MiB guard in `Assets::Download()`
   and the v3 partition decision (§3). This is the only part with a deadline.
2. **Next 1–2 games: ship them in firmware, art in the assets bundle (option B).** Writing
   games teaches you what the pack format needs; writing the pack format first guesses.
3. **Write those games as if they were already data-driven** — every tunable in a `struct`,
   no magic constants in the draw code, presentation described rather than coded. This costs
   nothing now and turns C into a serialisation exercise instead of a rewrite.
4. **Build C when a trigger fires**, and expect one to:
   - ≥ 8 games, or
   - a game-only change forces a firmware release more than twice a quarter, or
   - total game art passes ~1.5 MB, or
   - you want per-child or age-filtered catalogs, or
   - the fleet passes ~500 devices (build the flash-write path while bugs cost 11 devices,
     not 5,000 — this one argues for *sooner*, and it is the best argument against waiting).

### The one question this does not answer

Is Bubu a toy with **~5 fixed games** that ship once and rarely change, or a **platform** with
a growing library? If the former, C is pure overhead forever and option B is the final
answer. If the latter, C is inevitable and the only question is timing. That is a product
decision, not an engineering one.

---

## 1. What exists today (measured)

### The three games

| File | Flash |
|---|---|
| `main/display/eye_game.cc` | 973 B |
| `main/display/checker_game.cc` | 1,224 B |
| `main/display/quick_tap_game.cc` | 1,040 B |
| game-related symbols inside `menu_system.cc` (`CreateGamesPanel`, `UpdateGamesUI`, `HandleGameTap`, all the Quick-Tap/Checker screen builders) | 17,903 B |

The split is the important part: **the logic is trivial, the presentation is everything.**
Each game contributes its own LVGL widget set, its own colour constants, its own measured
layout numbers, its own `lv_timer`, its own hit-testing and its own screen enum, all
inline in `menu_system.cc` (6,298 lines, 55,200 B of flash). A downloadable game that
still needs its UI compiled in buys nothing. **The pack format must carry presentation,
not just parameters.**

Structurally, a game today is:

- a logic namespace with `Start/Stop/Update/HandleTap/Get*` (see `quick_tap_game.h` — it is
  a clean contract already, no LVGL in it),
- an `ActiveGameType` enum arm and a `GameSelection` enum arm in `menu_system.cc`,
- a block in `UpdateGamesUI()`, `HandleGameTap()`, `HandleGameFinished()`,
- a 33 ms `lv_timer` it must stop in `Close()` (the funnel for every close path),
- NVS records in its own namespace (`quicktap`).

### Flash

Partition table `partitions/v2/16m.csv`, all 16,777,216 B allocated:

| | offset | size | used | free |
|---|---|---|---|---|
| `nvs` | 0x9000 | 16,384 | — | — |
| `otadata` | 0xd000 | 8,192 | | |
| `phy_init` | 0xf000 | 4,096 | | |
| *(hole)* | 0x10000 | 65,536 | 0 | 65,536 |
| `ota_0` | 0x20000 | 5,767,168 | 3,397,024 (58.9%) | 2,370,144 |
| `ota_1` | 0x5A0000 | 5,767,168 | same image | 2,370,144 |
| `assets` | 0xB20000 | 5,111,808 | 1,375,385 (26.9%) | **3,736,423** |

### The `assets` partition is not a filesystem, and that is the opening

`Assets::LvglStrategy::InitializePartition()` (assets.cc:148) `esp_partition_mmap`s the
**whole** partition, `0 .. partition->size`, and then reads a 12-byte header plus a
44-byte-per-file table. `Assets::Download()` erases and writes only
`ceil(content_length / 4096)` sectors (assets.cc, the `sectors_to_erase` loop), and
`LvglStrategy::CalculateChecksum` only covers `stored_len`.

So everything past `12 + stored_len`:

- is already memory-mapped and readable at `mmap_root_ + offset`,
- is **never touched** by an assets/wake-word bundle update,
- is not covered by the bundle's checksum, so we can own its integrity ourselves.

`STATE.md` says of that 3.7 MB: *"unformatted trailing bytes — you cannot put a file there
without new code or a new partition."* That is still true. This plan is the new code; it
does not need the new partition.

### Everything else the plan leans on already exists

| Need | Already in the tree |
|---|---|
| HTTP GET streamed to flash, sector at a time | `Assets::Download()` |
| SHA-256 verification while streaming | `mbedtls_sha256_*` in the same function, `MBEDTLS_HARDWARE_SHA=y` |
| Server catalog + per-device choice + sha256 manifest | `WAKE_WORD_CATALOG` → `/internal/wake-word` → `ota.ts` |
| Static bundle hosting | Caddy `handle /assets/*` → `/opt/bubu-assets` on the VPS |
| Idle-gated background poll that defers mid-conversation | `Application::MaybeRefreshAssetsBundle()` / `AssetsRefreshTask()` |
| Honest "device has actually applied it" reporting | `assets_url` in `GetSystemInfoJson()` → `appliedAssetsUrl` on the device row |
| Round-display layout validation against the real fonts | `tools/lvwidth.py`, `tools/fit.py`, `tools/verify_games_layout.py` |
| Carousel UI for a list of games | `MENU_GAMES_OPEN` (ring + emblem + name + stat line) |
| Full-screen overlay with tap-to-dismiss | `MessageBoard::Open()` |

This is a re-skin of five mechanisms that already work, not five new ones.

---

## 2. The three problems, separated

They are independent and should be decided independently.

1. **Where the bytes live** — a flash region with an allocator and free-space accounting. §3, §5, §6.
2. **What a "game" is** — the boundary between what ships in firmware and what ships in a pack. §4.
3. **How a pack gets there and leaves** — catalog, download, install, uninstall, failure. §7, §8.

Problem 1 is the only one with a **production deadline** attached (see §3).

---

## 3. Decision 1 — where the bytes live

### 3a. Now: the tail of `assets` (no partition-table change)

Reserve the first **2 MiB** of the `assets` partition for the wake-word bundle and give the
rest to games:

```
assets partition @ 0xB20000, 5,111,808 B
  [0x000000 .. 0x200000)   2,097,152 B   wake-word bundle   (largest bundle today 1,375,385 = 65.6%)
  [0x200000 .. 0x4E0000)   3,014,656 B   games region       (2.875 MiB)
```

- Region start `0x200000` is 64 KiB-aligned, so it can take its own `esp_partition_mmap`
  if we ever want one independent of the assets mapping.
- 3,014,656 B = exactly **46 slots of 64 KiB**. Slot 0 is metadata (§5), so **45 installable
  slots = 2,949,120 B**.
- **Hard guard required:** `Assets::Download()` must refuse any bundle whose
  `content_length` exceeds the 2 MiB reserve, instead of erasing into the games region.
  One `if` at the existing `content_length > partition_->size` check. Without it, one
  oversized bundle silently eats every installed game.
- Headroom check: the biggest published bundle is 1,375,518 B and the smallest
  (`assets-none.bin`) is 1,084,271 B. 2 MiB leaves 52% margin. If a WakeNet10 model ever
  lands (see the esp-sr #88 thread) re-measure before assuming the margin holds.

**Why this first:** it works on every device already in the field via a normal OTA. There
are 11 devices out there today and there will be more before the production run.

### 3b. Production: a real `games` partition (`partitions/v3/16m.csv`)

A partition-table change **cannot be delivered by OTA**. It has to be decided before mass
production or not at all. Proposal, using the dead 64 KiB hole at `0x10000` for a bigger
NVS along the way:

```
# Name,   Type, SubType, Offset,   Size,     Flags
nvs,      data, nvs,     0x9000,   0x14000             # 80 KB  (was 16 KB)
otadata,  data, ota,     0x1D000,  0x2000
phy_init, data, phy,     0x1F000,  0x1000
ota_0,    app,  ota_0,   0x20000,  0x4A0000            # 4.625 MB, 36% headroom over today's image
ota_1,    app,  ota_1,   0x4C0000, 0x4A0000
assets,   data, spiffs,  0x960000, 0x280000            # 2.5 MB
games,    data, spiffs,  0xBE0000, 0x420000            # 4.125 MB = 66 slots, 65 installable
```

Ends at exactly `0x1000000`, nothing wasted. Every app partition is 64 KiB-aligned.

`GameStore::Begin()` resolves its region in this order:

1. `esp_partition_find_first(..., "games")` → use that partition whole;
2. otherwise → the `assets` tail at offset `0x200000`.

Same allocator, same pack format, same UI. One `#if`-free runtime branch, and the field
fleet and the production fleet run the same binary.

### 3c. Two things to settle before freezing v3

- **Secure Boot V2 grows the bootloader.** On ESP32-S3 a signed bootloader can exceed the
  `0x8000` partition-table offset, and IDF then requires `CONFIG_PARTITION_TABLE_OFFSET` to
  move. That shifts `nvs`'s start and invalidates the table above. **Build once with secure
  boot actually enabled and read the reported bootloader size before committing v3.**
  Per the production memo, secure boot + flash encryption are the only irreversible
  decisions; the partition table is the second-most irreversible and they interact.
- **Flash encryption flag on `games`.** Data partitions are not encrypted unless flagged.
  Encrypted writes need 16-byte-aligned offsets and lengths, and `esp_partition_mmap`
  decrypts transparently. Downloaded game content is not secret, so plain is defensible —
  but if it is flagged, the allocator's write path must be alignment-correct from day one.
  Decide with the secure-boot build in hand. **Not tested by this plan.**

---

## 4. Decision 2 — what a "game" actually is

Three tiers. The honest trade is between how much firmware you spend up front and how much
firmware you must ship per new game afterwards.

| | Firmware cost | New game without OTA? | Verdict |
|---|---|---|---|
| **T1 — parameter packs** | ~5 KB on top of the loader | Only re-skins/re-parameterisations of existing engines | **v1.** Delivers the whole download/install/storage system with near-zero gameplay risk. |
| **T2 — declarative scene + rules** | ~40–80 KB | **Yes**, for most casual toy games | **v2.** The real target. |
| **T3 — Lua (or any script VM)** | ~150–250 KB flash **and the same again in PSRAM** | Yes, anything | **Not now.** See below. |

### Why not T3 yet

- `CONFIG_SPIRAM_FETCH_INSTRUCTIONS`/`RODATA` mirror the entire app image into PSRAM at
  boot — confirmed on hardware when removing 206,336 B of fonts returned 206,136 B of
  PSRAM. **Every KB of interpreter costs a KB of flash and a KB of PSRAM.**
- It needs an LVGL binding layer, which is where the real work is, not the VM.
- It puts author-supplied control flow on a device with a live microphone pointed at a
  child. Secure boot does not help here — a script is data, so the sandbox would be
  entirely ours to get right. That is a security review, not a sprint.

Revisit T3 only if T2 demonstrably cannot express a game the product needs.

### Why not downloadable native code

Rejected outright. There is no ELF loader in the tree, and running unsigned machine code is
exactly what secure boot exists to prevent. Do not reopen this.

### T2 sketch — what "declarative" means concretely

Enough to cover tap/avoid/match/sort/sequence/rhythm, which is most of what a 4–10 year old
plays on a 240×240 round screen:

- **Entities**: sprite (mmapped LVGL binary image) or primitive (circle/arc/label), with
  position, size, lifetime, and a small set of motion tweens.
- **Spawners**: rate, count, random placement inside a region (the round safe area is a
  first-class region, since everything here is clipped by a circle).
- **Input**: hit regions → outcomes (`hit`, `miss`, `penalty`, `advance`).
- **State**: a handful of named integer counters, plus timers.
- **Rules**: `when <event> then <action>` over those counters — the score expression
  `HIT + (STREAK × k) − MISS` that Quick Tap already hard-codes becomes one line of pack.
- **End conditions** and an outcome screen template.

`quick_tap_game.cc`, `eye_game.cc` and `checker_game.cc` are all expressible in that shape,
which is a useful correctness test: **v2 is done when the three built-in games can be
rebuilt as packs and behave identically.**

---

## 5. On-device format

### The games region

```
slot size = 64 KiB (aligned to the 64 KiB mmap page, so any slot run is independently mappable)

slot 0                     metadata slot
  +0x0000  4 KiB  directory copy A
  +0x1000  4 KiB  directory copy B
  +0x2000  4 KiB  catalog cache (the server's game list, so the store UI opens offline)
  +0x3000 52 KiB  save area: 52 × 1 KiB fixed records, keyed by game id
slots 1..N                 game extents, one contiguous run of slots per installed game
```

**Save data does not go in NVS.** `nvs` is 16,384 B today and `Settings` has no blob API
(string/int/bool only). One namespace per game would exhaust it and leave orphan records
after an uninstall. A 1 KiB record in the metadata slot is bigger than any high-score
table needs, is deleted for free when the game is, and costs NVS nothing.

### Directory sector

```
  0   4   magic "BGD1"
  4   4   seq                 monotonic; the copy with the higher seq and a valid CRC wins
  8   2   slot_count
 10   2   slot_size_kb  (=64)
 12   4   crc32 of everything after this field
 16  ...  entries[]           72 B each
```

Entry: `char id[24]; u16 version; u8 first_slot; u8 slot_count; u32 byte_len; u8 sha256[32];
u32 installed_at; u8 flags; u8 pad[3]`.

Two copies written alternately with an increasing `seq` means **an install interrupted by a
power cut can never lose the directory** — the older copy is still intact and the
half-written slot is simply unreferenced, reclaimed on the next boot. 45 entries × 72 B =
3,240 B, inside one 4 KiB sector.

### Pack format (`.bgp`) — byte-identical to what the server serves

```
  0   4   magic "BGP1"
  4   2   format_version
  6   2   engine_id
  8   2   min_engine_version
 10   2   file_count
 12   4   manifest_len
 16   4   payload_len
 20  ...  file table: file_count × { char name[24]; u32 size; u32 offset; }
      ... manifest (JSON, cJSON is already linked)
      ... payloads, each padded to a 4-byte boundary
```

Three deliberate choices, each one a bug this repo has already paid for:

- **`offset` points at the payload, not at a marker.** The assets container prefixes every
  payload with `0x5A5A` and its table offset points at the marker — read it without the
  `+2` and every file is shifted two bytes, which still compares equal between two bundles,
  so the mistake hides itself (DEVLOG 2026-09-09). No markers here.
- **Integrity is a SHA-256 in the catalog, not a 16-bit additive sum in the container.**
  The assets bundle's own checksum is `sum & 0xFFFF` — blind to byte reordering, 1-in-65536
  miss rate on a 1.3 MB file. The pack is verified against the catalog's digest while
  streaming, exactly as `Assets::Download` now does.
- **Payloads are 4-byte aligned** so an `lv_image_dsc_t` can point straight into the mmap.

### Image and audio rules for pack authors

- **Images: pre-converted LVGL binary (RGB565 / RGB565A8) for anything drawn inside the
  game loop.** `LV_USE_LODEPNG` decodes into a RAM buffer on every load; a mmapped LVGL
  binary costs zero RAM and zero decode time. **Note this is a flash-for-RAM trade, not a
  free win** — a 240×240 RGB565 blob is 115,200 B against 67–108 KB for the equivalent PNG.
  It is the right trade for sprites redrawn at 30 Hz and the wrong one for a
  shown-once title screen, so the pack builder should allow both and default to binary.
  For scale on the PNG side: the 13 existing menu/care icons average 83,322 B each,
  1,083,195 B in total.
- **Audio: 16 kHz mono Opus in 20 ms packets.** The overlay SFX lane assumed 60 ms packets
  once and produced an audible crackle on every clip until it was fixed to read the
  duration off the TOC byte. Packs must match what the lane actually plays.
- **Text: only glyphs present in `lv_font_montserrat_vn_20/22/28`.** A pack shipping a
  character the compiled faces lack renders nothing — the same class as `U+2032` vs
  `U+0027` in the Pomodoro screen. The pack builder must validate every string against the
  compiled font, reusing `tools/lvwidth.py`.
- **Names must fit the round display.** The carousel name row clears the r=109 ring by as
  little as 4.15 px today, and it was Vietnamese diacritics — not letter widths — that made
  it tight. Server-supplied names must be width-checked at **pack build time** with
  `tools/fit.py`, and clamped/ellipsised at runtime as a second line of defence.

---

## 6. Free space, and the "full" message

`GameStore::Stats`:

```cpp
struct Stats {
    uint32_t total_bytes;          // installable region, excluding slot 0
    uint32_t used_bytes;           // sum of installed extents
    uint32_t free_bytes;
    uint32_t largest_free_bytes;   // largest contiguous run -- the number that decides an install
    uint8_t  slots_total;
    uint8_t  slots_free;
    uint8_t  installed_count;
};
```

**Surface both `free_bytes` and `largest_free_bytes`.** A packed region can hold 600 KB free
and still refuse a 200 KB game if the free slots are not adjacent. A UI that only shows
total free will look like it is lying. Two ways to avoid ever having to explain that:

- allocate from the lowest free run and **compact on uninstall** (move later extents down —
  at ~64 KB of flash write per slot this is seconds, and it can run on the store screen with
  a progress ring), or
- accept fragmentation and show both numbers.

Recommended: compact on uninstall, *and* show both numbers anyway.

**The check happens before a byte is erased.** The catalog carries the pack's exact size, so
`largest_free_bytes < pack.size` is decided in the store list, not discovered halfway
through a download. This is where the existing assets path is worst — it erases the
partition *before* it can fail — and there is no reason to copy that here.

Screens (round-display idioms already in use):

- **BỘ NHỚ** — an `lv_arc` gauge of % used, `"n/m game"`, `"còn X,X MB"`, reachable from the
  store and from Settings.
- **Store list** — each row shows the game's size; rows that cannot fit are dimmed with
  `"cần X KB"` rather than hidden, so the child sees *why*.
- **Full** — `"Hết chỗ rồi! Xoá bớt game để tải game mới"` on a `MessageBoard`, with a
  direct jump to the uninstall list sorted **largest first**, each row showing its size.
- **Progress** — the download screen owns input for its duration (§7).

---

## 7. Lifecycle, and the failure modes this repo has already paid for

### Install

1. Catalog says size + sha256. Check `largest_free_bytes` → refuse early with a specific message.
2. Reserve a free slot run. **Do not touch the directory yet.**
3. Stream the pack into the run, erasing sector by sector, hashing as it goes — the
   `Assets::Download` loop, unchanged in shape.
4. Verify SHA-256 and the `BGP1` header. **On any failure, abandon the run.** Nothing that
   was already installed has been touched.
5. Write the directory (A/B, `seq+1`). This is the commit point and it is a single sector write.
6. Invalidate the icon/pointer cache and refresh the carousel.

An interrupted install leaves an unreferenced slot run. `GameStore::Begin()` reclaims any
slot not claimed by the winning directory copy. There is no state in which the device is
worse off than before the install started — which is a strictly better property than the
assets path has, and it costs nothing because slots are independent.

### Uninstall

1. Refuse while that game is running or is the active screen.
2. Erase its slots, clear its save record, write the directory, compact, refresh caches.

### The five traps, named

1. **Dangling mmap pointers.** Menu icons are raw pointers into the mmap, resolved once at
   boot and cached (`ResolvePersistentAssetImage`). `UnApplyPartition()` used to unmap
   without clearing `partition_valid_`, and LVGL redrawing a cached icon after that was a
   `Cache error / MMU entry fault` (fixed in 1.7.4). Then 1.7.5 had to add
   `MenuSystem::RefreshIcons()` because a bundle applied *later in the same boot* left the
   pointers stale-but-mapped — silently wrong icons, not a crash. **Every GameStore
   mutation must invalidate and re-resolve every pack-owned pointer, and no icon-bearing
   screen may be entered while a write is in flight.** Both halves. One without the other
   has already shipped twice.
2. **Identity by content, never by URL.** `CheckAssetsVersion()` compares only
   `applied_url == url`, so a bundle whose *content* changed behind the same URL is silently
   skipped — it hit a real fielded device and had to be worked around by bumping the base
   URL to `/assets/v2`. That workaround is still the only fix. Packs are keyed by
   `(id, version, sha256)` and the directory stores the digest. Never compare URLs.
3. **Never erase before verifying.** §7 step 4.
4. **The applied-state report lags one check-in.** `GetSystemInfoJson()` runs inside
   `Ota::CheckVersion()`, before anything installs, so whatever the device reports describes
   its state *at check-in*. The portal's wake-word page can say "chưa nhận" for up to 15
   minutes after it has in fact applied. `installed_games` will have the identical lag —
   document it in the portal copy from day one instead of rediscovering it.
5. **Touch during a flash write.** Reproducible on demand before 1.7.4. The download screen
   must own input for its whole duration, and `GameStore` must expose a `busy()` that the
   menu checks the same way it checks `Assets::partition_valid()`.

### Runtime memory

- Download buffer: one 4 KiB sector, `MALLOC_CAP_INTERNAL`, same as `Assets::Download`.
  Internal SRAM idles at 30–33 KB free with a 24,576 B largest block — a 4 KiB internal
  allocation is safe, anything much bigger is not.
- Everything above 2,048 B lands in PSRAM automatically
  (`CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=2048`), and PSRAM has ~4 MB free. Pack parsing and
  engine state belong there.
- Mmapped image data costs **no** RAM. This is the whole argument for the LVGL binary format
  rule in §5.
- A game's `lv_timer` must be stopped in `MenuSystem::Close()`, which is the funnel for every
  close path, or it outlives the menu at 30 Hz. The timer callback must **not** take
  `DisplayLockGuard` — `lv_timer_handler` already holds it.

---

## 8. Server side

Everything mirrors the wake-word machinery, which is deployed, tested and understood.

### Hosting

`https://api.bubumotion.vn/games/v1/` served by the existing Caddy `file_server` from
`/opt/bubu-games`. Same host, no new DNS, no new certificate — the reasoning that put
bundles on the existing host applies unchanged.

```
/opt/bubu-games/v1/catalog.json
/opt/bubu-games/v1/<id>-<version>.bgp
```

**Version the path, not just the file.** The URL-identity gap in §7.2 is not fixed in
firmware; `<id>-<version>.bgp` makes a content change impossible without a URL change.

### Gateway (`bubu-gateway`)

- `GAME_CATALOG` in `/opt/bubu-gateway/.env`, same shape as `WAKE_WORD_CATALOG` (compact
  JSON, single-quoted, per-entry sha256 and size). Same failure mode to avoid: **the `.bin`
  files and the catalog metadata are two independent things to update**, and shipping one
  without the other produced a device that re-downloaded and re-failed on every boot.
- `ota.ts` adds a `games` block to the OTA response:
  `{ catalog_url, catalog_sha256, catalog_version }`. The device only refetches the catalog
  when `catalog_version` changes.
- **Server-side filtering by the device's reported firmware version.** The OTA check-in
  already carries the full `app_desc`. A game whose `min_firmware` the device does not meet
  must not appear in that device's catalog at all — a child should never see a game that
  cannot install.
- Per-device `gamesPolicy: "open" | "approve" | "locked"` on `DeviceRecord`, alongside
  `wakeWord`. `store-mysql.ts` has no migration mechanism beyond `CREATE TABLE IF NOT
  EXISTS`, so the column must be added through the idempotent `ensureColumns()` helper that
  the wake-word work added — adding it to `SCHEMA` alone is a silent no-op on the deployed
  VPS and the next INSERT fails.
- `installed_games: [{id, version}]` reported in `GetSystemInfoJson()` → stored on the
  device row, exactly as `assets_url` → `appliedAssetsUrl`.

### Portal (`bubu-web`)

A `/games` page per Bubu: catalog with sizes, what is installed, a storage bar, remote
install/uninstall requests, and the parental policy switch. Honest status language
("Đã gửi, Bubu chưa nhận") because of the check-in lag in §7.4.

Deploy order, as with every prior change here: **gateway first, then portal**, so the portal
in production keeps working if only half lands. Build the portal from an APFS-cloned tree
(`cp -Rc`) — Turbopack refuses a symlinked `node_modules` outside the tree.

### Pack build tooling

`scripts/build_game_packs.py`, modelled on `scripts/build_wakeword_bundles.py`:

- packs a source directory into `.bgp`,
- validates every string against the compiled VN faces and every name against the round
  safe area (`tools/lvwidth.py`, `tools/fit.py`),
- rejects PNGs where an LVGL binary is required,
- rejects audio that is not 16 kHz mono 20 ms Opus,
- emits `catalog.json` with size + sha256 per pack — **one generator for both the files and
  the catalog**, so they cannot drift apart the way the bundles and `WAKE_WORD_CATALOG` did.

---

## 9. Firmware cost

| Piece | Estimate |
|---|---|
| `GameStore` — directory, A/B commit, slot allocator, mmap, GC, compaction | 8–12 KB |
| Pack parser + manifest (cJSON already linked) | 4–6 KB |
| Downloader (HttpClient + mbedtls SHA-256, both already linked) | ~3 KB |
| Store / storage / uninstall UI on the round panel | 10–15 KB |
| Engine v1 (generic tap-and-avoid + memory match) | 10–20 KB |
| **Total** | **35–55 KB flash, and the same again in PSRAM** |

Against 2,370,144 B free in each app slot and ~4 MB free PSRAM, that is comfortable. Engine
v2 adds another 40–80 KB and is still comfortable.

The offsetting saving is real but small: once the three built-in games are packs, roughly
21 KB of firmware and a large slice of `menu_system.cc`'s complexity leaves the image.

---

## 10. Phasing

**Phase 0 — decide (blocks production).**
Pick §3a + §3b together. Build once with secure boot enabled and confirm the bootloader
size before freezing `partitions/v3/16m.csv`. Nothing else in this plan is
schedule-critical; this is.

**Phase 1 — storage, with no downloads at all.**
`GameStore` over the assets tail. The three existing games become directory entries flagged
`builtin` with `byte_len = 0`. Ship the BỘ NHỚ screen. Nothing downloads yet, but the
allocator, the directory, the A/B commit and the free-space numbers are all live and
testable on hardware. Verifiable on the bench with `esptool read_flash`.

**Phase 2 — one pack, end to end.**
Catalog on the gateway, one `.bgp` on Caddy, the store list, install, uninstall, and the
full/refuse path. The first pack should be a re-parameterised Quick Tap (engine 1), because
its logic is already written and its layout is already measured — the thing under test is
the pipeline, not the game.

**Phase 3 — engine v2 and a launch set.**
Declarative scene+rules. Done when the three built-in games can be rebuilt as packs and
behave identically. 3–5 launch games.

**Phase 4 — parent controls and telemetry.**
Portal `/games`, `gamesPolicy`, install state per device, and which games actually get
played (the device already reports nothing about usage; this is the first feature where
that would be worth adding).

---

## 11. What has not been verified

Written down so nobody takes these as established.

- **A second `esp_partition_mmap` over a sub-range of the already-mapped `assets` partition.**
  Plan A avoids needing one by reading through the existing `mmap_root_`, but if the games
  region ever wants its own handle, this must be tested. MMU page accounting on this build
  is unknown — `spi_flash_mmap_get_free_pages(SPI_FLASH_MMAP_DATA)` is logged at every boot
  (assets.cc:139) and **nobody has recorded the number**. Read it before sizing anything.
- **Whether Secure Boot V2 forces `CONFIG_PARTITION_TABLE_OFFSET` off `0x8000`** on this
  target. §3c.
- **Flash-encryption write alignment** on a `games` partition. §3c.
- **Compaction timing.** Moving a 64 KiB slot is an erase plus a write; the total time for a
  worst-case compaction has not been measured and decides whether it can run inline.
- **Whether the carousel reads well past ~6 entries.** The ring segment is `360/N` degrees;
  at N=20 each segment is 18° and the position ring stops communicating position. The store
  list probably needs a different layout from the installed-games carousel.
- **Engine v2's expressive range.** The claim that it covers "most casual toy games" is an
  argument, not a measurement. The three-built-in-games test in Phase 3 is what settles it.

---

## 12. Open product questions

1. **Who installs?** You asked for on-device install ("tải xuống máy khi muốn chơi"), which
   is what §6 is designed around. The portal path in §8 is additive. The default for
   `gamesPolicy` is the real question: `open` (child installs freely) or `approve` (parent
   confirms in the portal).
2. **Does a download need Wi-Fi to be explained?** A 200 KB pack at the 22–39 KB/s measured
   on real devices is 5–10 seconds; the 2.5 MB assets bundle once took ~6 minutes on weak
   Wi-Fi. Packs should stay small enough that the answer is always "a few seconds", which
   argues for a per-pack size cap in the build tooling — suggest **512 KB**.
3. **Are games free?** Nothing in this plan assumes so, but paid packs would need identity
   and entitlement on the gateway, which does not exist. Decide before the catalog schema is
   frozen, because `entitlement` is a field.
4. **Do the three built-in games stay built in?** Keeping them guarantees a Bubu with a full
   or empty games region is never game-less. Recommended: keep exactly one built in
   (MẮT XANH — it needs no panel of its own) and ship the other two as preinstalled packs in
   the factory image.
