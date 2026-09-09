# Two-Bubu Interaction — Design Plan (Duel Mode)

Status: proposal, not implemented.
Target board: `esp32s3-1.28-round-i80` (240×240 round LCD, CST816 touch, QMI8658 IMU,
3 keys via TCA6408, mic + I2S speaker, 8 MB PSRAM, 6 MB app partition).

Goal: two Bubus in the same room notice each other, greet, and play a short,
cute head-to-head match ("Bonk Duel"). No phone, no router, no cloud in the loop.

---

## 1. Transport decision

**Recommendation: BLE (NimBLE), with ESP-NOW as a later fast path.**

| Option | Verdict |
|---|---|
| **BLE GATT (NimBLE)** | **Chosen.** Works anywhere with zero infrastructure. Advertising doubles as *ambient* presence detection ("a friend is nearby") without connecting. ~30–100 ms round-trip at a 30 ms connection interval — far more than enough for a turn-based match. Costs ~250–400 KB flash and ~50–70 KB internal RAM. |
| Wi-Fi STA + mDNS/UDP | Requires both Bubus on the same router. Dead at a park, at school, in a car. Fine as an *extra* discovery path later, not as the primary. |
| Wi-Fi SoftAP + UDP | One Bubu becomes an AP, which drops it off the home network and kills the AI session for the duration. Ugly. |
| ESP-NOW | Lowest latency, no router, cheap. **But** peers must sit on the same Wi-Fi channel; if a Bubu is associated to a router, ESP-NOW is pinned to that router's channel. Two Bubus on different networks can't talk without channel-hopping, which breaks the audio websocket. Good v2 optimization for a *real-time* mode (tug-of-war, sync dancing); wrong for v1 discovery. |

### Current state of the tree

- `CONFIG_BT_ENABLED` is **not set** — the controller is fully compiled out today.
  `boards/common/blufi.cpp` only builds under `CONFIG_USE_ESP_BLUFI_WIFI_PROVISIONING`,
  and provisioning currently uses `CONFIG_USE_HOTSPOT_WIFI_PROVISIONING=y`.
  Turning BLE on is a fresh addition, not a re-enable.
- Flash headroom is fine: `build/xiaozhi.bin` is ~3.6 MB in a 6 MB OTA slot.
- Internal RAM is the real budget. NimBLE host + controller wants ~50–70 KB of
  internal SRAM, competing with the AFE wake-word and Opus codec. Measure
  `heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL)` before/after.

### sdkconfig deltas

```
CONFIG_BT_ENABLED=y
CONFIG_BT_NIMBLE_ENABLED=y
CONFIG_BT_NIMBLE_ROLE_CENTRAL=y
CONFIG_BT_NIMBLE_ROLE_PERIPHERAL=y
CONFIG_BT_NIMBLE_ROLE_OBSERVER=y
CONFIG_BT_NIMBLE_ROLE_BROADCASTER=y
CONFIG_BT_NIMBLE_MAX_CONNECTIONS=1
CONFIG_BT_NIMBLE_NVS_PERSIST=n        # no bonding; nothing to persist
CONFIG_ESP_COEX_SW_COEXIST_ENABLE=y   # single antenna, Wi-Fi + BLE
```

Gate all of it behind a new `CONFIG_BUBU_PEER_DUEL` in `main/Kconfig.projbuild`
so boards without the RAM budget keep the current binary.

### Fully offline by construction

**A duel needs no internet, no router, no phone and no cloud.** BLE is direct
device-to-device radio. Two Bubus in a field with no Wi-Fi in range play a
complete match: game logic runs on-device, the referee is one of the two Bubus,
all audio is local Ogg from flash, and XP / care stats / the rival record are
local NVS. This is the whole reason BLE beat Wi-Fi STA in the table above.

Three consequences worth handling explicitly:

- **The Arena must be reachable from an unprovisioned device.** Today a Bubu
  with no Wi-Fi credentials sits in `kDeviceStateWifiConfiguring`, and there is
  a separate activation / bind-code flow. A brand-new Bubu at a playground must
  still be able to duel — otherwise the offline story is theoretical. Requires a
  path into the Arena from the offline state, and a `MenuSystem` entry that
  isn't gated on activation.
- **There is no valid wall clock offline.** `settimeofday()` is only ever called
  from the OTA server response ([ota.cc:273](main/ota.cc:273)), so a
  never-connected Bubu has no real date. The rival record must therefore key on
  a match counter and monotonic uptime, and only write a real timestamp when the
  clock is known good.
- **Radio contention is not a connectivity dependency.** If Wi-Fi *is*
  connected, BLE and Wi-Fi share one antenna — which is why the Arena suspends
  the AI session below. That is a performance choice, not a requirement.

### Coexistence rule

The ESP32-S3 has one radio. Rather than fight Wi-Fi/BLE coexistence while Opus
audio is streaming, **the Arena suspends the AI session**: on entering, call
`Application::EndConversation()` / `AbortSpeaking()`; on leaving, return to idle
normally. All duel audio is local Ogg from flash (`AudioService::PlaySound`), so
a duel needs no network at all. This also keeps the fiction clean — Bubu is busy
playing with a friend, not listening to you.

---

## 2. How the two devices actually connect

The whole method in three rules:

1. **While the Arena is open, every Bubu both advertises and scans.** Fully
   symmetric — nobody is designated "the host" in advance.
2. **Link role is decided by `device_id`, never by who asked.** Lower id is
   always the BLE peripheral, GATT server, and match referee. Higher id is
   always the central and initiates the connection.
3. **Connecting is not consent.** Consent is a protocol handshake
   (`HELLO` → `HELLO_ACK`) shown on the screen of whoever didn't initiate.

Separating *intent* (either side may challenge) from *link role* (fixed by id)
is what removes every race. There is no negotiation round-trip and no
double-connect to untangle.

### Phase A — Discovery (connectionless)

Both devices run a 50% duty cycle of advertise + scan. One radio can't do both
in the same instant, so the controller time-slices them:

| Parameter | Value | Why |
|---|---|---|
| Advertising type | `ADV_IND` (connectable, undirected) | anyone may connect; consent is handled above the link |
| Advertising interval | 100 ms | Arena is a brief foreground state; fast discovery beats power here |
| Scan interval / window | 60 ms / 30 ms | 50% duty; expected discovery well under 1 s |
| Scan type | **passive** | every field we need is in the AD payload — no `SCAN_REQ`, less radio time, we don't announce ourselves by scanning |
| Channels | 37 / 38 / 39 | standard primary advertising set |
| RSSI gate | > −70 dBm to list | ≈ same room. You should see the Bubu on the table, not one two flats away |

**Filtering is on manufacturer data, not on service UUID** — and that's forced,
not arbitrary. Legacy advertising gives 31 bytes; a 128-bit service UUID would
eat 18 of them and the payload below wouldn't fit. So the `'B''B'` magic in the
manufacturer-specific AD field (type `0xFF`) is what filters the world down to
Bubus. (BLE 5 extended advertising would give 255 bytes and remove the
constraint — unnecessary here, worth knowing it exists.)

The advertising payload doubles as the peer card, so the Arena can render a
name, level and eye colour for a Bubu it has **never connected to**.

| Field | Bytes | Notes |
|---|---|---|
| company id | 2 | 0xFFFF (test/dev) until a real one exists |
| magic | 2 | `'B''B'` |
| proto_ver | 1 | reject mismatched majors before connecting |
| device_id | 4 | derived from base MAC — stable, this is the rival key |
| level | 1 | from `LevelSystem::GetLevel()` |
| eye color | 3 | RGB, so the card looks like *that* Bubu |
| flags | 1 | open-to-challenge / in-a-duel / sleeping / **wants-to-play-with** |
| wants_id | 2 | low 16 bits of the target's device_id (see Phase B) |
| short name | ≤8 | UTF-8, truncated |

That totals 24 bytes of manufacturer data + 2 AD header + 3 for the Flags AD
structure = 29 of 31. Tight. Any new field costs name characters.

**Do not enable BLE privacy / resolvable private addresses.** Rival recognition
depends on a stable identity, and a rotating address defeats the ambient
"a friend is nearby" feature.

### Phase B — Intent, and the role flip

When a user taps a peer card, the device compares ids:

- **`my_id > peer_id` → I am the central.** Stop advertising, stop scanning,
  call `ble_gap_connect()` straight at that address. ~1–2 connection intervals.
- **`my_id < peer_id` → I am the peripheral.** I cannot initiate. Instead I
  raise the `wants-to-play-with` flag in my advertising payload with
  `wants_id = peer_id & 0xFFFF`, and show "asking Mimi…". The peer sees the
  flag on its next scan window, shows its accept prompt, and connects.

Cost of the second path is roughly one advertising interval plus one scan window
— ~150 ms before the other side even sees the request. Invisible next to a human
tapping "yes".

**Simultaneous tap resolves itself.** Both sides run the same comparison and
reach the same conclusion, so there is no double-connect and no jitter-retry —
the rule that decides normal operation is the same rule that decides the race.

### Phase C — Link setup

```
central                                     peripheral (referee)
   │  ble_gap_connect(peer_addr, 5 s timeout)      │
   ├──────────────── CONNECT_IND ─────────────────>│  (adv stops automatically)
   │        interval 30–45 ms, latency 0, sup 4 s  │
   ├────────── exchange MTU (request 128) ────────>│
   ├── discover service by 128-bit UUID ──────────>│
   ├── discover RX / TX characteristics ──────────>│
   ├── write 0x0001 to TX's CCCD ─────────────────>│  notifications on
   │                                               │
   ├──────────────── HELLO ───────────────────────>│  ← accept prompt shows here
   │<────────────── HELLO_ACK(accept | decline) ───┤     if this side didn't initiate
   ├─────────── PING ×5 ──────────────────────────>│
   │<────────────────────── PONG ×5 ───────────────┤     one-way latency estimate
   │                                               │
   └──────────────── match begins ─────────────────┘
```

Connection parameters: **30–45 ms interval, slave latency 0, supervision
timeout 4 s.** The timeout is deliberately just above the 3 s application
heartbeat, so the link layer and the app agree on roughly when a Bubu has
wandered off.

Service discovery costs 3–4 round trips (~150 ms). For a known rival, cache the
attribute handles in NVS against `device_id` + the peer's firmware version and
skip straight to the CCCD write; invalidate on any version change.

Budget from "accept" to first countdown: **~500 ms.** Connect ~60 ms, MTU and
discovery ~150 ms, handshake and ping probe ~250 ms.

### No pairing, no bonding, no encryption

Deliberate. Nothing secret crosses the link, there is no credential to protect,
and bonding would add NVS wear plus a "forget this device" flow that a toy
should never need. Note the genuine conflict: LE Secure Connections would
normally come with address privacy, and address privacy breaks rival
recognition. Security therefore lives entirely at the application layer —
explicit accept on screen, every frame length- and CRC-checked, peer-reported
values clamped, rewards capped regardless of what the peer claims (§3, §5).

### Why writes and notifications need no app-level retransmit

Central → peripheral is Write Without Response; peripheral → central is Notify.
Both are unacknowledged at the ATT layer, but the **link layer** retransmits
until acknowledged. A frame is therefore never quietly lost — either it arrives,
or the connection itself drops and the supervision timeout fires. That is why
§3 needs only round-index idempotency and a heartbeat, and no sequence-number
retransmit machinery.

### Dropping and reconnecting mid-match

Supervision timeout fires → both sides show "reconnecting…" for 10 s. The
central retries `ble_gap_connect()` at the same address; on success the referee
replays current state (`round_idx`, `hp_a`, `hp_b`) and play resumes from the
round boundary. Round idempotency makes the replay safe. After 10 s it's a
friendly draw.

### Three or more Bubus in the room

`CONFIG_BT_NIMBLE_MAX_CONNECTIONS=1` — strictly 1:1. A Bubu already duelling
raises the in-a-duel flag in its advertising, so everyone else's Arena shows it
greyed out as "busy" without wasting a connection attempt on it.

### NimBLE surface used

`ble_gap_adv_set_data` / `ble_gap_adv_start`, `ble_gap_disc` with a
discovery callback, `ble_gap_connect`, `ble_gattc_exchange_mtu`,
`ble_gattc_disc_svc_by_uuid`, `ble_gattc_disc_all_chrs`,
`ble_gattc_write_no_rsp_flat`, `ble_gatts_notify_custom`. Every one of these
callbacks lands on the NimBLE host task — see the threading rule in §6.

### Session lifecycle

```
IDLE ──open Arena──> ADVERTISE + SCAN ──> PEER_LISTED
                                              │ user taps a peer
                                              ▼
                                    id compare → CENTRAL or PERIPHERAL
                                              │
                                              ▼
                              CONNECTED ──> HANDSHAKE ──> consent
                                              │
                        ┌──── ROUND_START ──> COMMIT ──> RESOLVE ────┐
                        └──────────── (×N, until KO) ────────────────┘
                                              │
                                              ▼
                                 RESULT ──> REMATCH? ──> BYE
```

---

## 3. Wire protocol

One GATT service, two characteristics (classic serial-port shape):

- `RX` — write-without-response, central → peripheral
- `TX` — notify, peripheral → central

Request MTU 128; **assume 20-byte payloads** and never rely on the negotiated
MTU. Every frame fits in 20 bytes by construction.

```
byte 0   : ver (high nibble) | type (low nibble)
byte 1   : seq (wraps)
byte 2   : len of payload
byte 3..n: payload
byte n+1 : crc8
```

| Type | Dir | Payload |
|---|---|---|
| `HELLO` | both | proto_ver, device_id, level, eye RGB, name |
| `HELLO_ACK` | ref | accepted / rejected(reason), match config (hp, rounds, window_ms) |
| `PING` / `PONG` | both | 2-byte token — used ×5 at handshake to measure one-way latency |
| `ROUND_START` | ref→ | round_idx, window_ms |
| `MOVE` | →ref | round_idx, move(0=bonk,1=block,2=zap), reaction_ms |
| `ROUND_RESULT` | ref→ | round_idx, my_move, their_move, damage, hp_a, hp_b, flags(crit/clash/flinch) |
| `MATCH_END` | ref→ | winner_id, rounds, xp_award |
| `EMOTE` | both | 1-byte taunt id (out-of-band, allowed any time) |
| `BYE` | both | reason (user quit / timeout / error) |

Rules:

- **Idempotent by round index.** Re-sending `MOVE` for a round the referee has
  already resolved is ignored, not double-counted.
- **Heartbeat:** if no frame for 3 s, send `PING`; two misses → `BYE(timeout)`,
  show "Mimi wandered off…" and award a friendly draw.
- **Version skew:** `HELLO` with a different major → `HELLO_ACK(rejected)` and a
  screen saying one of them needs an update. Never guess at an unknown frame.
- **Treat every byte from the peer as hostile input.** Bounds-check `len`,
  validate crc8, clamp move ids to the enum, clamp `reaction_ms` to the window,
  truncate and sanitize the name before it ever reaches LVGL, cap XP awarded per
  match regardless of what the peer claims. A modified peer must not be able to
  grant levels or crash the other Bubu.

### What actually crosses the link during a round

Only *decisions*, never media. Each Bubu renders its own eyes, plays its own
Ogg, and runs its own animations locally — the radio carries a few bytes saying
what happened, and both sides then perform the same beat independently.

One round, in full:

| # | Direction | Frame | Bytes on air |
|---|---|---|---|
| 1 | referee → other | `ROUND_START` (round_idx, window_ms) | 7 |
| 2 | other → referee | `MOVE` (round_idx, move, reaction_ms) | 8 |
| 3 | referee → other | `ROUND_RESULT` (both moves, damage, both HP, flags) | 11 |

**The referee's own move never leaves the device** — it's already local. Only
one side's move is transmitted; the referee resolves and publishes the outcome.
`ROUND_RESULT` carries *both* moves so the other Bubu can animate the opponent
correctly.

That is **26 bytes and 3 packets per round**. A six-round match, including the
handshake and heartbeats, is well under 1 KB total — less than a single BLE
connection event can carry (251 bytes/packet). Bandwidth is a non-issue; the
design is shaped entirely by latency and fairness, not throughput.

Link overhead per round is ~100 ms (three transmissions, each landing within one
30–45 ms connection interval) against a 2.5 s round window. `EMOTE` taunts flow
either direction at any time and are equally tiny.

### Fair timing without a shared clock

Each device measures `reaction_ms` from the moment **it** received
`ROUND_START` — no clock sync needed. The residual unfairness is the difference
in notify latency, which the handshake's 5× `PING`/`PONG` measures; the referee
subtracts half the RTT delta from the central's reported time. Good to a few
milliseconds, which is invisible against human reaction times of 300–800 ms.

Optional hardening (v2): commit-reveal — each side sends `crc(move ‖ salt)`
first, then the salt — so a modified referee can't peek before choosing. Not
needed for two friends; cheap enough to add if duels ever get competitive.

---

## 4. The game: "Bonk Duel"

Cute, not violent. Nobody gets hurt; they get *bonked*, puff into a cloud, and
sulk adorably.

**Format:** 3 hearts each, best-of, ~5–8 rounds, 60–90 seconds total.

**Moves — one gesture each, in rock-paper-scissors:**

| Move | Gesture | Beats | Fiction |
|---|---|---|---|
| 🥊 **Bonk** | shake the Bubu | Zap | you smack them before the spark charges |
| 🛡️ **Block** | cover the screen with your palm (large-area touch) or lay it face-down | Bonk | eyes squint, tiny shield |
| ⚡ **Zap** | double-tap an eye | Block | a sneaky spark curls around the shield |

Buttons (`XIO_KEY_UP` / `POWER` / `DOWN`) mirror the three moves as a fallback —
gestures are the fun path, buttons are the reliable and accessible one.

**Round loop (2.5 s window):**

1. Both screens do a 3-2-1 countdown, eyes lean toward the opponent.
2. `ROUND_START` → window opens, eye color pulses.
3. Player performs a gesture → `MOVE` sent, eyes freeze in the chosen pose.
4. Referee resolves once both moves are in, or the window expires + 400 ms grace.
5. `ROUND_RESULT` → both play the same 1.2 s animation, in sync.

**Outcomes:**

- **Hit:** loser takes 1 heart, `AnimConfused()` shake, `bubu_sad2.ogg`; winner
  `AnimLaugh()`, `bubu_happy3.ogg`.
- **Clash** (same move): both bounce, no damage, `bubu_curious1.ogg`. The faster
  player gets a ✨ *style point* — decorative in v1, tiebreak in v2.
- **Flinch** (no move in the window): counts as a miss; eyes go wide,
  `exclamation.ogg`. No damage taken — it's not a punishment, it's a giggle.
- **Crit:** won the round in under 500 ms → the bonk animation gets fireworks
  (`LegacyEmotionMode::Fireworks`). Still 1 heart. Spectacle, not power creep.

**Rewards — the loser must never feel bad:**

| | Winner | Loser |
|---|---|---|
| XP (`LevelSystem::AddXP`) | +40 | +25 |
| Mood (`CareSystem::AddMood`) | +10 | **+15** consolation hug |
| Energy | −10 | −10 (they both played hard) |

Duels also spend hunger and cleanliness — see [§5](#5-stats-and-the-care-system)
for how the care system feeds into a match without deciding it.

Both Bubus end on a "friends" beat: hearts eyes (`LegacyEmotionMode::Love`),
`bubu_laugh.ogg`, a shared victory/hug frame. Cap XP per rival per hour so two
Bubus left face-to-face on a desk can't farm levels overnight.

**Rival memory** (NVS via `Settings("bubu_rival", true)`): device_id, name,
wins, losses, last-played timestamp. Next meeting: "Mimi is back! 3–2 to you."
This is the feature that makes the whole thing sticky, and it's ~30 lines.

**Later modes on the same transport:** tug-of-war (mash taps, real-time — the
ESP-NOW use case), staring contest (whoever blinks or moves first loses; the IMU
and the existing blink animation already do all the work), sync dance, snack
sharing (transfer 10 hunger to a hungry friend).

---

## 5. Stats and the care system

**Yes — but stats should be the *fuel and the flavor*, not the win condition.**
Skill decides who wins the round; care decides whether you can play, what you
bring, and how good you look doing it.

### Why not straight stat-power

The obvious version — better stats, higher damage — breaks in four ways:

1. **It double-punishes.** The kid who forgot to feed their Bubu already has a
   sad, grubby Bubu. Now they also lose every match. Neglect should make you
   feel *sorry* for Bubu, not bad at the game.
2. **It kills the reason to play together.** Once you know your stats are lower,
   the rational move is to decline the duel. A social feature that punishes the
   weaker party gets used once.
3. **The stats decay.** `CareSystem` ticks every 60 s, so a duel decided by
   stats is really decided by *who last opened the care menu* — a timer, not a
   choice. That's the least interesting input in the design.
4. **Stats are spoofable.** They arrive over BLE as bytes the peer chose. Any
   number the referee trusts is a number a modified firmware sets to 100.

### The loop that actually gives care meaning

The strongest link isn't care → power, it's **duels *spend* stats**:

| Stat | Duel effect |
|---|---|
| `STAT_ENERGY` | −10 per match. Playing is tiring — this is the main sink. |
| `STAT_CLEANLINESS` | −8 per match. They rolled around in the dust; now they need a bath. |
| `STAT_MOOD` | **+10 / +15** (winner/loser). Playing with a friend is the best mood source in the game. |
| `STAT_HUNGER` | −5. Bonking works up an appetite. |

That alone makes the care system matter more than any damage multiplier would:
duelling creates care needs, care enables duelling, and the loop closes without
ever making a match unfair. Ship this part first — it's ~10 lines against the
existing `CareSystem::Add*` API, and it does 80% of the work.

### Where stats *are* allowed to bite

**Gate, don't handicap.** A stat can stop a duel from starting; it shouldn't
rig one that's already started.

- `GetEnergy() < 15` → "Bubu is too sleepy to play 😴". The Arena won't open.
  Honest, legible, and it teaches care without a rigged match.
- `IsCritical()` (any stat at 0) → same, with the relevant care prompt.
- Recommended floor: **a fully neglected Bubu is never more than ~15% worse off
  than a perfectly kept one**, once inside a match. Anything more and rule 1
  above kicks in.

### Where stats belong: the special meter

`STAT_HUNGER` and `STAT_MOOD` set the **charge rate of the Super Bonk** — a
fourth move that isn't in the RPS triangle:

- The meter fills over rounds. Well-kept Bubu: ~1 special per match. Neglected
  Bubu: none.
- The special beats Bonk, Block and Zap — **but it telegraphs.** The wind-up is
  visible on the opponent's screen for ~600 ms, and a Counter (hold Block
  through the wind-up) turns it around for 2 hearts.
- So care buys you an *option with counterplay*, not a stat line. The
  well-cared Bubu has a tool; the neglected one can still read it and win.

Referee-enforced caps make the spoofing problem moot: **max 1 special per
match**, minimum 3 rounds before the first one, and it can always be countered.
A peer that lies about its stats gains exactly one telegraphed move.

### Level: handicap, not power

Use `LevelSystem::GetLevel()` for **matchmaking balance, Go-style** — the higher
level Bubu starts with fewer hearts:

| Level gap | Hearts (higher / lower) |
|---|---|
| 0–2 | 3 / 3 |
| 3–6 | 3 / 4 |
| 7+ | 2 / 4 |

Progression stays visible and meaningful, but a level-2 Bubu meeting a level-20
Bubu gets a real match instead of a beating. Announce it on the VS screen
("Mimi is giving you a heart!") so the handicap reads as generosity, not pity.

### The real reward for good care is *social*

Cosmetics carry the status, and they're what actually drives care:

- `STAT_CLEANLINESS` → visible shine. High: eyes sparkle, a soft glow.
  Low: the existing `SetDirtyLevel()` grime is on screen **in front of a
  friend**. That embarrassment motivates baths far better than −5% damage.
- `STAT_MOOD` → which taunt emotes are available (`EMOTE` frames). A grumpy
  Bubu just sulks; a happy one has the whole set.
- Level → aura and unlocked eye effects, via the existing
  `LevelSystem::IsUnlocked()` feature gates.
- Rival record and win streak → a badge on the VS screen.

A Bubu that looks fantastic in front of its friend is the reward. That is the
feature that gets kids to open the care menu, and it costs nothing in fairness.

### Scrappy bonus

Whichever Bubu has the lower average condition gets a slightly wider input
window (+200 ms) for the match. Framed as heart, not pity — "Bubu is fighting
scrappy!" It stops the rich-get-richer spiral where the well-kept Bubu wins,
gains XP, and pulls further ahead.

---

## 6. Firmware architecture

New files, following the existing module shape (namespace + `Configure/Start/
Stop/Update/Handle*/IsRunning/Get*`, exactly like
[eye_game.h](main/display/eye_game.h) and [checker_game.h](main/display/checker_game.h)):

```
main/peer/peer_link.h        abstract transport: StartDiscovery/GetPeers/Connect/Send/OnFrame
main/peer/ble_peer_link.cc   NimBLE impl (advertise + scan + GATT both roles)
main/peer/duel_protocol.h    frame structs, encode/decode, crc8, version constants
main/peer/duel_session.cc    connection + handshake + referee state machine
main/display/duel_game.cc    rules, HP, round resolution, animation cues
main/gesture_detector.cc     IMU + touch → Bonk / Block / Zap
```

Mirroring `main/protocols/protocol.h`'s abstraction is deliberate: when ESP-NOW
arrives, it's a second `PeerLink` implementation and nothing above it changes.

### Threading — the one rule that will bite

NimBLE callbacks run on the **host task**, not the app task. LVGL and the game
state are app-task-owned. Every callback must do nothing but copy bytes into a
queue and call `Application::Schedule(...)` — the pattern already used in
[message_board.cc:83](main/message_board.cc:83) and
[wifi_board.cc:154](main/boards/common/wifi_board.cc:154). No LVGL, no
`CareSystem`, no logging of peer strings from the BLE task.

### Integration points that already exist

| Hook | Where |
|---|---|
| New menu states `MENU_ARENA_*` | [menu_system.h](main/display/menu_system.h) `enum MenuState` |
| Entry point next to the other games | `OpenGamesMenu()` / `StartTapTheGreens()` — [menu_system.cc:3556](main/display/menu_system.cc:3556) |
| Per-frame game tick | the `EyeGame::Update()` block at [menu_system.cc:4176](main/display/menu_system.cc:4176) |
| Tap routing | `HandleGameTap` at [menu_system.cc:3719](main/display/menu_system.cc:3719); board-level `DispatchTap` at [esp32s3_round_i80_board.cc:534](main/boards/esp32s3-1.28-round-i80/esp32s3_round_i80_board.cc:534) |
| Expressions | `EyeAnimation::AnimConfused/AnimLaugh/SetEyeColor/SetLegacyEmotionMode/SetGameMode` — [eye_animation.h](main/display/eye_animation.h) |
| Sound | `AudioService::PlaySound` + existing `bubu_*.ogg` in [main/assets/common](main/assets/common) — no new assets needed for v1 |
| Rewards | `LevelSystem::AddXP`, `CareSystem::AddMood/AddEnergy` |
| Persistence | `Settings` — [settings.h](main/settings.h) |
| Build | source list + `CONFIG_BUBU_PEER_DUEL` in [main/CMakeLists.txt:148](main/CMakeLists.txt:148) and [Kconfig.projbuild](main/Kconfig.projbuild) |

### The IMU is present but unwired

`Qmi8658` is built ([CMakeLists.txt:62](main/CMakeLists.txt:62)) and
`EyeAnimation::SetImuAccel(ax, ay)` exists, but **nothing calls either** — no
board instantiates the driver today. Gesture input therefore needs real work:

1. Instantiate `Qmi8658` on the shared I2C bus (0x6B, IO8/IO9) in the board file.
2. Poll at 50 Hz from the existing touch timer task, or use `XIO_IMU_INT1` on the
   TCA6408 expander.
3. Feed `SetImuAccel()` — free win, the eyes get physics everywhere, not just in duels.
4. Feed `GestureDetector`: shake = |a| > ~2.2 g with ≥2 sign reversals inside
   400 ms; face-down = az < −0.8 g held 200 ms; thresholds in a `Config` struct
   so they're tunable without a rebuild loop.

Note the driver is **accel-only** by design ("ported accel-only" in
[qmi8658.h](main/boards/common/qmi8658.h)) — no gyro. Shake and tilt are fine;
anything needing rotation rate would need the driver extended.

---

## 7. Screens (240×240 round)

1. **Arena** — radar-ish ring of nearby Bubu cards (name, level, eye color, RSSI
   as distance). Empty state: "Looking for friends…" with searching eyes.
2. **Invite** — big yes/no, 15 s auto-decline, name of the challenger.
3. **VS** — the two eye-pairs slam together, hearts on both sides, 3-2-1.
4. **Round** — hearts top-left/right, big countdown ring, your Bubu's eyes react.
5. **Result** — winner/loser animation, XP bar filling, rival record line.
6. **Rematch?** — yes/no, 10 s timeout → `BYE`.

Round-display constraint: keep HUD elements inside the inscribed square
(~170×170 centered), corners are cut off by the bezel.

---

## 8. Failure modes

| Case | Behavior |
|---|---|
| Peer walks out of range mid-match | 3 s heartbeat miss → "Mimi wandered off…", friendly draw, half XP for both |
| One side crashes/reboots | Other side times out identically; no orphan state — nothing is written to NVS until `MATCH_END` |
| Both tap Challenge simultaneously | No special case — both run the same id comparison and reach the same role assignment (§2) |
| Three or more Bubus in range | Arena lists all; connection is strictly 1:1 (`MAX_CONNECTIONS=1`); others see the in-duel flag in adv data and show "busy" |
| Wake word fires mid-duel | Suppressed while the Arena owns input — same as `MenuSystem::IsGameActive()` does today |
| Battery critical mid-duel | `BYE(low_battery)`, no match record, existing low-battery flow takes over |
| Malformed / hostile frames | crc8 + length check + enum clamp; drop silently, count it, `BYE` after 10 bad frames |

---

## 9. Phasing

| Phase | Deliverable | Done when |
|---|---|---|
| **0** | NimBLE enabled behind Kconfig; measure flash + internal RAM cost with Wi-Fi and audio running | numbers are in hand and acceptable |
| **1** | Advertising + scanning + presence only. No connection. "A friend is nearby!" perk-up | two Bubus on a desk react to each other |
| **2** | GATT link, `HELLO`/`PING`, Arena + Invite screens, connect/disconnect/timeout | link survives 10 min idle and a walk out of range |
| **3** | IMU wiring + `GestureDetector`, standalone (shake/block/zap recognized offline, printed to log) | <5% false positives over 100 gestures each |
| **4** | Full Bonk Duel: rounds, referee, animations, rewards, rival record in NVS | 20 matches end cleanly, no hangs, no NVS corruption |
| **5** | Polish: crit fireworks, taunt emotes, rematch flow, XP farming caps | — |
| **6** (later) | ESP-NOW `PeerLink` for real-time modes; Wi-Fi/mDNS discovery for same-network play | tug-of-war at <20 ms |

**Test rig:** two devices is the minimum, three is better (busy-state, wrong-peer
selection). Add a host-side Python fake-peer over BLE (`bleak`) that speaks the
protocol and can be told to misbehave — drop frames, send garbage, claim level
999, vanish mid-round. Most of phases 2–4 can be tested against it with a single
Bubu on the bench, which is the difference between a two-day debug loop and a
two-hour one.

---

## 10. Decisions still needed

1. **Naming.** Bubus need per-device names for the Arena to read well. Owner-set
   in Settings, or generated from device_id (`Bubu-4F2A`)?
2. **Do duels work while the AI session is live?** Recommendation above says no —
   the Arena suspends conversation. Confirm that's acceptable product-wise.
3. **Move set.** Three RPS moves is the safe v1. A fourth ("charge/counter")
   adds depth but doubles the animation work.
4. **Match length.** 3 hearts ≈ 60–90 s. Right for a toy, or should there be a
   30-second "quick bonk"?
5. **Level handicap on or off?** §5 recommends the Go-style heart handicap. It
   makes mismatched levels playable, but some kids want their level to *mean*
   raw strength. Worth watching two real players before committing.
6. **Offline entry point.** Confirm the Arena is reachable while the device is
   unprovisioned / in `kDeviceStateWifiConfiguring`. This is what makes "two
   kids at a park" real rather than aspirational.
7. **Internal RAM.** If phase 0 shows NimBLE + AFE + Opus is too tight, the
   fallback is ESP-NOW-only (a few KB) with same-channel and thus
   same-Wi-Fi-network restrictions, plus a channel-hop discovery beacon.
