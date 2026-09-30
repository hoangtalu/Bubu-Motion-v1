# Care system plan — Bubu's day, HUY HIỆU, TÌNH BẠN

Status: **`PLAN`, 2026-09-30.** Nothing built, flashed or deployed. The only code is the
simulator `tools/care_sim.py`, which every number in §3–§5 comes from.

## 0. Decisions (user, 2026-09-30)

| # | Question | Decision |
|---|---|---|
| 1 | Direction | Bubu follows the **child's daily routine** |
| 2 | Play budget | **Soft**: a tired Bubu pays less and asks for a nap; the child can play on |
| 3 | Bedtime | **21:00–06:30.** Bubu only gets sleepy and keeps talking. Stopping chat at bedtime is a parent switch in the portal, later |
| 4 | Rewards | **HUY HIỆU** (badges), not phiếu bé ngoan: fits kids and teens, pride like watch badges |
| 5 | Friendship | **TÌNH BẠN** stages approved as an experiment: ship it, watch it, fix it |
| 6 | Chat reward | **+10 CẢM XÚC per conversation stays** |
| — | Dropped: Bubu's dream | A generated story is AI *imagination*: nobody knows in advance what it will tell a child. Today's AI is safe because it only answers |
| — | Dropped: "Bạn dạy Bubu" | Not care. If it returns, it belongs to the learning/tutor roadmap |
| — | Consequence | **Nothing in this plan needs Bubu to speak first** |

## 1. Why today's care system doesn't matter

Read from source on 2026-09-30.

- **Invisible.** The stats sit under CHĂM SÓC → TRẠNG THÁI, one bare arc at a time. The
  care-emotion carousel is off (`CareEmotionConfig::enabled = false`,
  `main/display/eye_display.h:133`), so the only sign on Bubu's face is smudges once
  SẠCH SẼ drops below 60 (`kDirtyLight`, `main/display/eye_animation.h:801`).
- **Ignoring it costs nothing.** Apart from smudges and the feeding pace, Bubu talks, plays
  and looks the same at 0 as at 100.
- **Doing it earns nothing.** Levels unlock nothing: games open at level 1
  (`kGamesUnlockLevel`, `main/display/menu_system.cc:922`), and `LevelSystem::IsUnlocked()` is
  only read by the `self.get_level_info` MCP tool.
- **Two stats refill themselves.** NĂNG LƯỢNG gains +10 a minute in the auto-sleep that starts
  after 5 idle minutes (`main/display/eye_display.cc:1419`, constants at
  `eye_display.h:234–236`), so NGỦ does nothing useful. CẢM XÚC gains +10 on every
  conversation start (`main/application.cc:1279`) plus every game.
- **Uptime, not the clock.** Decay runs on `esp_timer`. Plugged in overnight, CÁI BỤNG empties
  (−10/h) and CẢM XÚC / SẠCH SẼ lose ~70 / ~57 by morning; switched off, nothing moves. A new
  Bubu starts every stat at 30 (`main/care_system.cc:25`).

## 2. Principles

1. **Bubu lives on the child's day.** Needs appear when the child has the same need: waking
   up, coming home, evening, bedtime.
2. **Visible without a menu.** Bubu's face, a thought bubble, a small sound.
3. **Asks little, gives back something visible.** A full day of routine is three small actions.
4. **Kind by construction.** No dying, no running away, floors on every need, nothing earned
   is ever taken back.
5. **The AI only answers.** It is told Bubu's state and may mention it inside a reply to the
   child. It never invents stories or events and never starts a conversation, and Bubu never
   opens the microphone by itself.
6. **Only days spent together count.** A Bubu nobody plays with stays clean on its own; that is
   not care.

## 3. The care loop

### 3.1 Time

- **Wall clock** (SNTP since 1.7.6, `main/time_sync.h`). A `t` (epoch seconds) is saved with
  the stats.
- **Bubu's three modes:**
  - **awake** — the child touched or talked to Bubu less than 5 minutes ago;
  - **dozing** — 5 idle minutes, today's timeout; this is "the child is away";
  - **night** — put to bed in the bed window, or idle after 21:30; ends at 06:30.
- **Device off counts as dozing or night.** On boot with a valid clock, the elapsed time is
  applied at those rates, capped at 24 h. Plugged in or off, the morning is the same.
- **No valid clock** (never online): today's uptime behaviour at the awake/doze rates, with no
  windows, anchors or badge progress.
- **Windows** (defaults; parents set them later): breakfast 06:00–09:30, lunch 11:00–13:30,
  dinner 17:00–20:00, bath 15:00–21:30, bed 19:30–21:30, sleepy from 21:00.

### 3.2 Needs

| Stat | Falls with | Filled by | Shows below | The child sees |
|---|---|---|---|---|
| **CÁI BỤNG** | awake −3.5/h; dozing and night −1/h. Waking from the night or a nap of 2 h+ caps it at 35: "Bubu ngủ dậy là đói" | feed, 3 bites × 10 (unchanged); refuses at ≥ 85 (unchanged) | 40 | bowl bubble, hungry look, tummy clip at meal times |
| **NĂNG LƯỢNG** | games −1.2 per minute; awake −2/h | night +15/h; dozing or a nap +2 per minute (today +10) | 30 tired, 10 exhausted | yawns, the dozing look (§3.4) |
| **SẠCH SẼ** | games −0.7 per minute; each feed −3 (crumbs); awake −1.5/h; dozing −0.5/h | bath +90 (unchanged) | 50 | smudges from 60 (existing), bubbles bubble |
| **CẢM XÚC** | awake −2.5/h; dozing −1/h; its ceiling (§3.3) | chat +10 (kept); games as today; +5 for meeting a need while it showed | 40 | quieter idle moods |

Floors: CÁI BỤNG 10, SẠCH SẼ 10, CẢM XÚC 20, NĂNG LƯỢNG 0.

The "wake up hungry" rule is what makes the routine: every school day, Bubu wakes up hungry at
breakfast and again when the child comes home, which gives the child a job the moment they
walk in.

### 3.3 CẢM XÚC ceiling: "Bubu đói thì không vui được"

Each of hungry / dirty / tired that is showing lowers how high CẢM XÚC can go by 20
(100 → 80 → 60 → 40).

- Rewards cannot lift CẢM XÚC past the ceiling.
- Above the ceiling, CẢM XÚC sinks 20/h while Bubu is awake.
- Chat still pays +10 (decision 6), but only up to the ceiling.

This rule exists because the first simulator run had games alone keeping a neglected Bubu at
CẢM XÚC 97. With the rule, bedtime CẢM XÚC averages: engaged 96, casual 87, gamer 82,
forgetful 57.

### 3.4 Soft play budget (decision 2)

- **Tired** (< 30): games pay half.
- **Exhausted** (< 10): games pay nothing. Before the next game Bubu yawns and the bubble offers
  a nap, but the child can play on.
- **What that means in time:** from full, continuous games reach *tired* after ~57 minutes and
  *exhausted* after ~74. A 20-minute break (5 minutes to doze, then 15 at +2/min) gives back
  ~25 minutes of play.
- In the simulator, children who play 15–40 minutes per sitting never hit it. The gamer
  (~126 min/day) spends 16% of game minutes tired and 13% exhausted, and hears ~3 nap offers a
  day.

### 3.5 Bedtime (decision 3)

- From 21:00 Bubu shows the sleepy dozing look (locked in the Eye Lab, already ported), yawns,
  and the bubble shows a moon.
- NGỦ between 19:30 and 21:30 is the bed anchor. Waking Bubu again the same evening cancels it.
- If the child talks to Bubu, Bubu still answers. The parent switch that stops chat at bedtime
  comes with the portal (Phase 3); the `bed_talk_blocked` clip already exists.
- If nobody puts Bubu to bed, it falls asleep at the first 5 idle minutes after 21:30, with no
  bed credit.

### 3.6 How Bubu shows it

- **Thought bubble.** Shows the most urgent need: bowl, bubbles, moon, or a medal for a badge
  waiting to be received.
  - Drawn with LVGL primitives, like the feeding dish, so it needs no image.
  - Tapping it goes straight to that action. The tap is checked before "tap outside the eyes
    opens the menu".
  - Hidden during study time, games, menus and conversations.
- **Idle moods.** Needs bias the existing mischief mood weights (`lab_mood_weights_`,
  `main/display/eye_animation.h:484`, in the order Mumble, Hum, Think, Surprise, Happy, Laugh,
  Annoyed, Sleepy):
  - tired → Sleepy ×4, Happy/Laugh ×½;
  - dirty → Annoyed ×2;
  - hungry → Think ×2;
  - nothing showing and CẢM XÚC ≥ 80 → Hum ×2, Laugh ×2, Happy ×1.5;
  - CẢM XÚC < 40 → Happy/Laugh/Hum ×⅓.

  The vox clip already follows the mood (`BubuInteractionVoice::GetVoiceForMood`).
- **Voice asks.** Local non-verbal clips on the overlay lane: the same lane and rules as the
  mischief voices, so idle-only and muted in study time (`Application::CanPlayIdleOnlySfx`,
  `UpdateStudyMute`).
  - At most 4 a day, 45 minutes apart.
  - Only when someone touched or talked to Bubu in the last 15 minutes.
  - Only in the need's window: hungry at meal times, dirty in the bath window, sleepy from
    21:00, tired when a game is about to start.
  - Simulator: about 1 a day for engaged and casual children, 2.7 for the gamer.
  - Clips: `vox_yawn_1` (tired), `vox_annoyed_*` (dirty), `bed_reminder` (bedtime). Hungry
    needs **one new non-verbal clip** in the Bubu voice, same Opus spec as `vox_*`.

### 3.7 First boot and upgrade

- **New Bubu:** after hatching, CÁI BỤNG 35 and the rest ~85. The first bowl bubble appears
  within minutes and teaches the loop, and the first feed earns the first badge.
- **Upgrade:** NVS `care_stats` has no `t` key, so this is a fresh start. All four stats go to
  70 and `t` is set to now. Level and XP in `bubu-level` are kept.

## 4. HUY HIỆU (badges)

### 4.1 Rules

- **Earned only through care and routine.** Never for staying up late, playing past the budget,
  or anything paid.
- **Never taken away.** Repeatable badges show a count (×4).
- **Weekly badges start fresh every Monday.** A bad week costs nothing already earned.
- **Only days spent together count.**
- **The award waits for an idle moment:** never during study time, a game or a conversation.

### 4.2 First set: 17 badges

**Routine points** are the currency of the weekly badges: one point per anchor per day, so at
most 21 a week. The three anchors are:
- breakfast fed in its window;
- no smudges at bedtime, on a day spent together;
- in bed on time.

"Ngày bên nhau" means a day with at least one care action.

| Group | Badge | Earned by | Repeats | Simulator |
|---|---|---|---|---|
| Tuần | TUẦN ĐỒNG | ≥ 10 routine points in a week | ×N | casual 56% of weeks |
| Tuần | TUẦN BẠC | ≥ 15 | ×N | engaged 76%, casual 2% |
| Tuần | TUẦN VÀNG | ≥ 20 (one slip allowed) | ×N | engaged 23% |
| Tuần | TUẦN HOÀN HẢO | 21 / 21 | ×N | engaged 6%: the rare one |
| Chuỗi 7 ngày | ĂN SÁNG ĐỀU | breakfast 7 days running | ×N | engaged ~2.5 per 4 weeks |
| Chuỗi 7 ngày | NGỦ ĐÚNG GIỜ | in bed on time 7 nights running | ×N | engaged ~1.2 per 4 weeks |
| Chuỗi 7 ngày | SẠCH SẼ | no smudges at bedtime 7 days running | ×N | engaged ~2.4, casual ~0.8 per 4 weeks |
| Chăm sóc | BỮA ĐẦU TIÊN | first feed ever | once | everyone, day 1 |
| Chăm sóc | ĐẦU BẾP NHÍ | fed at breakfast, lunch and dinner in one day | ×N | engaged ~3 per 4 weeks (weekends) |
| Bên nhau | 7 / 30 / 100 / 365 NGÀY BÊN NHAU | days together, counted and never reset | once each | |
| Tình bạn | MỚI QUEN · HIỂU BẠN · CÓ CÁ TÍNH · BẠN THÂN | reaching each stage (§5) | once each | |

Candidates for a later set:
- **Special days:** TẾT, TRUNG THU, 1/6, KHAI GIẢNG, and SINH NHẬT BUBU on the anniversary of
  hatching. Earned by caring for Bubu that day. The lunar dates need a table on the device or a
  field in the OTA reply.
- **HỌC TẬP and game achievements:** only if they pass the "is it care?" test (§9).

### 4.3 On the device

- **HUY HIỆU screen.**
  - One badge per screen, like the CHĂM SÓC carousel.
  - Earned badges show in full colour with their count.
  - Unearned badges use the same art greyed out by LVGL recolour, with progress such as
    "12/15 điểm tuần này" or "4/7 ngày".
  - Built on demand like the game screens (the 1.7.8 pattern), so it costs no internal SRAM
    while closed.
- **Award.** The bubble shows a medal. Tapping it plays a full-screen award: the badge scales
  in with its name and `success.ogg`.
- **Menu.** CHĂM SÓC gains HUY HIỆU, and CẤP ĐỘ becomes TÌNH BẠN. That needs two new 240×240
  icons, same convention as `main/assets/menu_icons/sub_care_*.png`.

### 4.4 Art and the assets budget

The art is most of what makes a badge feel worth earning, and it is the largest single cost in
this plan.

- **It ships in the assets bundle, not the firmware.** App bytes are mirrored into PSRAM 1:1;
  the assets partition is mmapped from flash (STATE, hardware budget).
- **Size.** The `assets` partition has 3,736,423 B free (STATE). Full-screen art like the menu
  icons (67–108 KB each) would take ~1.5 MB for 17 badges; 160×160 art at ~30 KB takes
  ~0.5 MB. **Use 160×160.**
- **Release.** All 7 wake-word bundles must be rebuilt and `ASSETS_BASE_URL` bumped to `/v3`,
  because bundle identity is still the URL string only (STATE, "URL-identity gap").

## 5. TÌNH BẠN (experiment, decision 5)

### 5.1 XP

- **Once a day, at bedtime:** +10 per anchor, plus +5 if CẢM XÚC is ≥ 60 at bedtime on a day
  spent together. At most 35 a day.
- **Removed:** the per-change XP in `care_system.cc` (`(new − old) / 10`), which pays for
  button presses rather than looking after Bubu.
- **Kept:** HỌC TẬP's +10 XP per focus block (`BankPomodoroFocusBlock`,
  `main/display/menu_system.cc:6130`) as time spent together. It is not in the simulator.
- The level curve is unchanged: 50 + 25 × level (`main/level_system.cc:89`).

### 5.2 Stages

Timings are the median day each stage is reached, from 200 simulated runs of 84 days.

| Level | Stage | What changes | engaged | casual | forgetful |
|---|---|---|---|---|---|
| 1–2 | **MỚI QUEN** | Curious. Inside its replies, Bubu asks about the child's likes (food, colour, game, animal), at most once a day | hatching | hatching | hatching |
| 3–5 | **HIỂU BẠN** | Uses what it knows and greets the child personally. Milestone: nods and shakes its head (Eye Lab motions) | day 6 | day 10 | day 37 |
| 6–9 | **CÓ CÁ TÍNH** | A personality forms from the last 14 days: mostly games → playful, HỌC TẬP → curious, chat → chatty. Shown in the idle mood weights and one persona line | day 21 | day 33 | rarely within 12 weeks |
| 10+ | **BẠN THÂN** | Refers to real shared moments only: badges, streaks, favourites. No invented ones | day 51 | day 81 (56% by week 12) | — |

**Stages change personality, never usefulness.** Homework help, tutor mode and dictation work
the same at every stage, and their prompts ignore it.

### 5.3 Privacy

- Bubu asks about **likes, never identities**: no friends' names, school or address.
- Answers are stored on the device in `NotesSystem` (20 entries, `main/notes_system.h:8`), and
  parents can clear them.
- This falls under the children's-data legal review that is still open
  (`docs/language-tutor-plan.md`).

### 5.4 How we'll know it needs fixing

- **Transcripts.** Read real conversations in portal chat (30-day retention):
  - Does Bubu mention care more than once a session?
  - Does it sound forced?
  - Do children answer the "get to know you" questions?
- **Device logs** for stage-ups and badge awards. The OTA check-in could carry counts later.
- **Rollback.** Stage persona lines live only in the gateway: removing the line removes the
  behaviour. On the device, a stage only changes motions and mood weights.

## 6. What the AI is told (Phase 2)

- **Device.** Adds a `care` object of about 150 B to the websocket hello
  (`main/protocols/websocket_protocol.cc:269`). It carries:
  - the needs showing and CẢM XÚC;
  - the stage and the personality trait;
  - badges earned and care done since the last session.
- **Gateway.** Adds one line to the system instruction, with these rules:
  1. Answer the child first.
  2. Mention care at most once a session, in one short sentence.
  3. Thank the child for care done since last time, and congratulate a new badge.
  4. Never guilt the child, and never say Bubu is sad because of them.
  5. Say nothing about care during study time (tutor and dictation).
  6. Never invent stories or events.
- **No tool call.** Live tool calls block speech on every model we would ship (STATE, disproven
  table).
- **Unknown field.** The gateway is not in this repo. Whether it ignores an unknown hello field
  must be checked before the firmware half ships.

## 7. Simulator results

`python3 tools/care_sim.py --runs 200` (28 days, seed 1). Stage timings come from the 84-day run
in §5.2.

| | engaged | casual | forgetful | gamer |
|---|---|---|---|---|
| Minutes with Bubu a day | 90 | 44 | 10 | 145 |
| Need showing: hungry / dirty / tired, % of time together | 11 / 8 / 0 | 25 / 27 / 0 | 60 / 37 / 0 | 33 / 56 / 29 |
| Voice asks a day (p90) | 0.96 (2) | 1.02 (2) | 0.35 (1) | 2.65 (4) |
| Care actions a day | 5.9 | 2.9 | 0.7 | 3.5 |
| Game minutes a day (share tired / exhausted) | 37 (0 / 0) | 31 (0 / 0) | 7 (0 / 0) | 126 (16% / 13%) |
| Bedtime CẢM XÚC mean (p10) | 96 (89) | 87 (72) | 57 (20) | 82 (58) |
| Routine points a week, of 21 | 18.3 | 9.9 | 2.1 | 9.9 |
| Weekly badge | Bạc 76%, Vàng 23% (hoàn hảo 6%) | Đồng 56%, Bạc 2%, none 41% | none | Đồng 53%, Bạc 3%, none 44% |

**The runs changed three rules:**
1. Games kept a neglected Bubu at CẢM XÚC 97 → added the ceiling (§3.3).
2. Weekly badges that counted only complete days gave a casual child nothing → switched to
   routine points.
3. "Clean at bedtime" counted days nobody played → only days spent together count.

**Limits.** The children are invented schedules, not measurements. They check the rules for
contradictions; they do not forecast behaviour. Re-tune from bench and field logs once
Phase 1 runs.

## 8. Phases

**Phase 0 — simulator.** Done: `tools/care_sim.py`.

**Phase 1 — firmware only, one OTA, no gateway change.**

- `main/care_system.{h,cc}`:
  - wall-clock modes, rates, windows, floors, ceiling, anchors, routine points;
  - NVS `t` plus the migration.
  - Pure logic with no LVGL, so it can be host-tested (as `green_eye_game` is) and checked
    against the simulator's rules.
- `main/level_system.cc`: XP from the daily summary; stage from level.
- `main/display/eye_display.cc`:
  - replace the +10/min sleep energy with the nap rate;
  - drive the mood weights from needs;
  - sleepy look from 21:00;
  - bubble state and badge award.
- `main/display/eye_animation.{h,cc}`: draw the bubble; a real setter for the mood weights (today
  `LabSetMoodWeight`).
- `main/boards/esp32s3-1.28-round-i80/esp32s3_round_i80_board.cc`: route bubble taps before
  the menu opens.
- `main/display/menu_system.cc`:
  - energy cost per game minute, the reward factor and the nap offer;
  - the HUY HIỆU screen, on demand;
  - CẤP ĐỘ becomes TÌNH BẠN.
- `main/application.cc`: `RewardMoodForAiChatUse` keeps +10 (decision 6), through the ceiling.
- New `main/badge_system.{h,cc}`: NVS `badges` with earned bits, counts, streaks and progress.
- **Assets:** badge art, 2 care icons and 1 hungry clip; rebuild the 7 bundles and bump
  `ASSETS_BASE_URL`.
- **Verification:** bench flash, then a soak. Check:
  - one real night plugged in and one switched off end the same;
  - never more than 4 asks a day;
  - internal SRAM unchanged with HUY HIỆU closed.

**Phase 2 — AI.** The hello `care` block plus the gateway line (§6), checked on bench
transcripts before the fleet gets it.

**Phase 3 — portal.**
- Routine times per child.
- The bedtime chat switch (decision 3).
- A badge wall for parents.

**Later.** Special-day badges, and game/study badges if wanted.

## 9. Open questions

1. **Badge art:** who draws it? This plan assumes 160×160 art in the assets bundle.
2. **HỌC TẬP and game badges** tie the whole device together, but are they care? They are out
   of the first set until decided.
3. **Parents attaching a real reward** to a weekly badge in the portal: yes or no?
4. **The hungry clip:** one new non-verbal clip in the Bubu voice, recorded from the same voice
   and settings as the 2026-09-23 vox set.
