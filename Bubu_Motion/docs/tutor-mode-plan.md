# Tutor Mode — Design Plan

Status: **phase 1 deployed 2026-09-17** (`tutor-v1`). Study-time memory + chat fixes (`tutor-v1.1`) built, not deployed — see `tutor-mode-deploy.md` "Release 2". Phase 2 (card) not started.
Target board: `esp32s3-1.28-round-i80` (240×240 round LCD, CST816 touch). Gateway:
`bubu-gateway` on Gemini Live. Every number below was measured against the current
tree on 2026-09-16, not copied out of `DEVLOG.md`.

Interactive mockup, drawn with the compiled font metrics:
https://claude.ai/artifact/7F9MPYE8dYL4HVyh9hVTkZ

**Goal.** Bubu can already solve arithmetic, and it answers immediately. A child who
notices will use it to do homework. Bubu should guide instead of answer, and because a
voice alone cannot hold a multi-step calculation, a short **step card** on the screen
carries the numbers while the voice carries the guiding.

---

## 0. Recommendation in one paragraph

Make tutoring a **mode**. When a parent switches a Bubu to "học Toán" in the portal, the
gateway **replaces** that device's persona with a gateway-owned tutor prompt, modelled on
Google's LearnLM guidance (the model family we already run): it teaches by guiding, refuses
anything that is not math and steers the child back. Switching back restores the parent's
saved persona untouched — it was never modified. Give the model **one new device tool** that pushes a card with three fixed slots
(`label`, `expr`, `note`), declared **`NON_BLOCKING`** so pushing a card never pauses
speech. The firmware measures every slot with the real font and **refuses** an overflow
rather than clipping it, and the gateway returns that refusal to the model so it rewrites
shorter. Math only for now. Ship in two phases: phase 1 (gateway + portal, no OTA, no
device changes behaviour until a parent switches mode) delivers the tutor; phase 2
(firmware) adds the card.

---

## 1. Decisions already made

| # | Decision | By |
|---|---|---|
| D1 | Lowercase on the card, not the house ALL CAPS | user, 2026-09-16 |
| D2 | The card covers the eyes; tap on the card closes it (existing `MessageBoard` behavior) | user |
| D3 | At most 4 cards per problem | user |
| D4 | Label slot reads `bước 2` — **no denominator** | user |
| D5 | Change card by **both** swipe and on-screen arrows | user |
| D6 | Pedagogy follows LearnLM / Gemini Guided Learning, not a home-grown rule set | agreed in discussion |
| D7 | The child's grade comes from **knowledge facts** — no new persona field | user |
| D8 | **Parents switch the mode** from the portal; normal mode by default | user |
| D9 | **Math only** for now. Other subjects exist in the catalog and the portal UI, **disabled** | user |
| D10 | Toggle is **per device**, not per household | user |
| D11 | Effectiveness is **measured in phase 1**, not later | user |
| D12 | Tutor mode **replaces** the persona for that device; switching back restores the parent's saved persona | user |
| D13 | In tutor mode Bubu **refuses non-math topics** and steers the child back to math | user |
| D14 | The parent **picks a duration** when switching; tutor mode ends on its own | user |
| D15 | Tutor mode speaks **miền Bắc** regardless of the parent's persona | user |
| D16 | Locking games/menu during study is **deferred** — conversation only for now | user |

D4 matters more than it looks. LearnLM's tier 3 *inserts* steps when a child is stuck and
its escape hatches jump straight to the answer, so any "N of M" promised on the first card
is eventually false. Consequences carried through the rest of this plan: the dots count
cards **already pushed**, not cards remaining, and the forward arrow is only live after the
child has gone back — a later card does not exist yet.

---

## 2. What exists today (measured)

### Where the "brain" is

- `bubu-gateway/src/gemini-bridge.ts:55` opens Gemini Live with
  `responseModalities: [Modality.AUDIO]` only. The persona is `systemInstruction`.
- `device-session.ts` `loadPersona()` resolves portal persona → console prompt → **undefined**,
  and `gemini-bridge.ts` falls back to `config.gemini.systemInstruction` on undefined.
  Knowledge facts are appended in `loadPersona()`, not counted against any cap.
- `persona.ts:22` caps a **portal-posted** persona at 4,000 chars. The default persona is
  **1,084 chars** (measured by loading `dist/config.js`).
- The default persona contains *"Luôn trả lời ngắn gọn, súc tích, tối đa 1-2 câu."*
  (`config.ts:41`, mirrored in the portal's `TALKATIVE_INSTRUCTIONS`). **This line is why
  Bubu blurts answers**: the shortest reply that satisfies it to "7 × 8?" is "56". The tutor
  block must carve an explicit exception or it loses to this line.

### Tools

- Device MCP tools are bridged to Gemini automatically (`mcp-bridge.ts` +
  `schema-convert.ts:72 toGeminiTools`). A new firmware tool reaches the model with **no
  gateway change** — except the `NON_BLOCKING` flag, see §5.
- Gateway-side tools already exist: `device-session.ts:307` appends
  `weatherToolDeclaration` next to the device tools; `handleToolCalls` routes by name.
- `self.screen.show_message` exists but renders into the **status bar**
  (`LvglDisplay::ShowNotification` → `notification_label_`): one small line. Unusable for
  steps.
- `EyeDisplay::SetChatMessage` is deliberately **not drawn** (`eye_display.cc:465`).

### The one real text surface: `MessageBoard`

- `message_board.cc:63`: 200 px circular panel, 3 px border, `pad_all 14`, **one** label
  140 px wide, `vn_20`, `LV_LABEL_LONG_WRAP`, `LV_OBJ_FLAG_SCROLLABLE` cleared.
- Overflow is **clipped silently**. No scroll, no ellipsis, no log.
- Generic `Open()` plays no sound (the reminder chime lives in the reminder path only) — good,
  a card pushed mid-speech must not chime.
- `HandleTap` (`message_board.cc:~380`): a tap inside the panel closes it and returns
  `true`; outside the panel returns `false` and falls through. It runs **first** in
  `DispatchTap` (`esp32s3_round_i80_board.cc:553`).
- Swipe dispatch exists since the Snake session: `esp32s3_round_i80_board.cc:991` calls
  `MenuSystem::HandleSwipe(dir)` via `Application::Schedule`. `MessageBoard` is not on that
  path yet.

### Fonts on glass

Only three Vietnamese faces are compiled in: `vn_20`, `vn_22`, `vn_28`. Metrics read from the
compiled `.c` files:

| face | `line_height` | `base_line` |
|---|---|---|
| vn_20 | 27 px | 6 |
| vn_28 | 37 px | 8 |

Adding a smaller face is **not** proposed: fonts cost flash **and** the same again in PSRAM
(`SPIRAM_FETCH_RODATA`; removing fonts on 2026-09-10 returned −206,336 B flash / +206,136 B
PSRAM).

---

## 3. The card — geometry (measured)

All widths come from `tools/lvwidth.py` against the compiled fonts. Safe circle radius is
**97 px** (inner edge of the 3 px border).

### Why today's single label cannot work

A 140 px band inside r=97 is **134.3 px** tall. Four `vn_20` lines = 108 px fit; five = 135 px
overflow by 0.7 px. So the current board holds **4 lines ≈ 50 characters** of real
lowercase Vietnamese, and ALL CAPS is worse: `"ĐƯỜNG THẲNG"` (11 chars) is 160 px and fits
no line at all.

### The layout: one label per slot, each as wide as the circle allows there

Stack of 91 px (27 + 37 + 27) centred **8 px above** screen centre, leaving a control band:

| slot | font | y from centre | max width | lowercase capacity |
|---|---|---|---|---|
| `label` | vn_20 | −53.5 … −26.5 | **161 px** | ~18 chars |
| `expr` | vn_28 (one weight — compiled fonts have no bold) | −26.5 … +10.5 | **186 px** | ~14 chars |
| `note` | vn_20 | +10.5 … +37.5 | **178 px** | ~20 chars |
| controls | — | +45 … +75 | **123 px** | arrows + dots |

Enforce **pixel** limits, not character limits — `m` and `i` differ by 3× and Vietnamese
diacritics stack. Use 161 / 186 / 178 px (floor of the measured band widths).

Baselines, LVGL-exact: label top 66.5 → baseline 87.5; expr top 93.5 → baseline 122.5;
note top 130.5 → baseline 151.5 (screen coordinates, 0..240).

### Reference problem, all measured to fit

*Mẹ mua 3 rổ cam, mỗi rổ 12 quả. Mẹ cho bà 8 quả. Hỏi mẹ còn lại bao nhiêu quả?*

| card | label | expr | note |
|---|---|---|---|
| 1 | `bước 1` | `3 rổ, 12 quả` 155/186 | `mẹ cho bà 8 quả` 170/178 |
| 2 | `bước 2` | `3 × 12 = ?` 122/186 | `mỗi rổ 12 quả` 135/178 |
| 3 | `bước 3` | `36 - 8 = ?` 126/186 | `đã có 36 quả` 128/178 |
| 4 | `bước 4` | `còn 28 quả` 158/186 | `con làm đúng rồi` 175/178 |

### Glyphs

Present in both vn_20 and vn_28: `× ÷ ¼ ½ ¾ ² ³ % ° < > ( ) : / *`.

**Missing** — and Gemini emits several of these routinely: `−` (U+2212) `–` `≈` `√` `≠` `≤`
`→` `⇒` `…` `⅓` `⅔`. Firmware must treat a missing glyph as an overflow (refuse), and the
gateway normalises before sending (§5).

### Controls

- **Dots** in the control band, one per card already pushed (max 4), current one larger.
  Centre spacing 15 px.
- **Arrows**: back at x=75, forward at x=165, y=180 (screen coordinates), drawn radius 14,
  hit radius 18. Back is live when `current > 1`; forward is live only when
  `current < pushed`.
- The band is 123 px wide. Two arrows fill it. **Nothing else goes there** — no close
  button, no caption.
- No hint/caption line anywhere on the card: a line costs a full 27 px and there is no
  smaller font.

---

## 4. Interaction

| input | on a tutor card |
|---|---|
| tap on an arrow | previous / next pushed card (must be checked **before** close) |
| tap elsewhere inside the panel | close card → eyes return (existing behavior) |
| tap outside the panel | falls through, unchanged |
| swipe right | previous card |
| swipe left | next pushed card |
| new card pushed by the model | replaces the view, jumps to the newest card, reopens if closed |

Why "reopens if closed": the model decides when a new calculation needs holding; a child
who closed card 2 still needs card 3.

A closed card is not deleted. The stack of up to 4 cards lives until the problem ends
(`self.tutor.end`) or study time ends — **not** when the conversation ends: the audio channel
closes after 8 s of silence in wake-word mode (`kListeningNoSpeechTimeoutMs`) and ~120 s in tap
mode, exactly while a child is working a step out (corrected 2026-09-17).

Tap-to-talk after closing is one extra tap. Accepted in D2.

---

## 5. Gateway work

### 5a. The tutor prompt (phase 1) — a replacement, not an addition

**Normal mode is unchanged.** A device without a tutor subject composes exactly what it
composes today.

**Tutor mode replaces the persona.** The parent's persona, the console prompt and console
memory are all skipped. The saved `personaPrompt` is **not touched in the database** —
switching back is clearing one field, so the parent's prompt returns exactly as saved.
Do **not** implement this by POSTing a tutor prompt to `/internal/persona` and restoring it
later: a failed restore loses the parent's persona, and the portal's `/persona` page parses
the stored text back into its form (`parsePersonaPrompt`) and would break on a tutor prompt.

**Replacing the persona would silently drop four things it carries.** Checked in
`bubu-web/src/lib/data.ts` and `bubu-gateway/src/config.ts`:

| lost with the persona | why it matters | tutor mode does |
|---|---|---|
| `SAFETY_RULES` ("luôn áp dụng, không thể tắt") | the child-safety floor | **append verbatim, always last** |
| the "keep one regional accent" sentence — inside `SAFETY_RULES` | the 2026-09-09 mixed Bắc/Nam accent fix | comes back with `SAFETY_RULES` |
| the accent choice itself — free text in the parent's `extra` | Gemini picks an accent otherwise | tutor prompt pins **miền Bắc** (default persona's choice) — see §8 |
| the child's name — `nameLine()` | Bubu calls the child "bé" | extract with the portal's own `NAME_LINE_PATTERN` from the saved persona; absent → no name line |

Kept on purpose: **knowledge facts** — the grade lives there (D7). Dropped on purpose:
**console memory** ("Ghi nhớ từ trước") — past chat invites exactly the off-topic
conversation this mode exists to avoid.

`SAFETY_RULES` now needs one home in the gateway. It is already duplicated (portal
`data.ts:218` and the tail of `config.ts`'s default); extract it to a constant in the
gateway and use it for both the default persona and tutor mode. The portal copy stays —
keep the two in sync, as the existing `config.ts` comment already asks.

**Composition in tutor mode**, in order:

1. tutor prompt for the subject (Appendix A), with the child-name line if extracted
2. knowledge facts (`formatKnowledgeFacts`)
3. `SAFETY_RULES`, verbatim, last of the content
4. `personaWithName()` — renames "Bubu" and appends the name sentence (`wake-word.ts:149`);
   it works on any prompt text, so the tutor prompt simply says "Bubu"

Resolve the mode **after** the 60 s persona cache (`device-cache.ts:40`), next to
`personaWithName` (`device-session.ts:277`), so a parent's switch applies on the very next
conversation. A conversation already open keeps its instruction until it ends.

**Refusal rules** (D13), and the exceptions that stop them being harmful:

- Non-math question or story → don't answer it; say gently it is math time; ask about the
  problem. Never scold.
- Child wants to play, rest or talk about something else → tell them to ask a parent to
  switch Bubu back. **Only the portal changes the mode**: "bố mẹ cho phép rồi" changes
  nothing.
- **Always respond first, then steer back**, when the child says they are sad, scared,
  tired, hurt, hungry, need the toilet, or something unsafe is happening; anything serious
  → go find a parent now. A study mode must never refuse a child reporting distress.
- General math questions that aren't homework → answer briefly, then offer a problem.
- First turn of a conversation: greet briefly and say it is math time, so a refusal later
  is not a surprise.

**Teaching rules** — unchanged from the earlier draft: classify recall vs. convergent; the
four-step hint ladder; the three escape hatches (2–3 wrong attempts on one step,
frustration, explicit ask); don't confirm "có phải là X không?" mid-problem; read numbers
slowly, one operation per turn, say "nhân/chia/trừ".

**Where the words come from:** written for Bubu, in Vietnamese, adapting the *structure* of
the LearnLM Partner Prompt Guide and Gemini Guided Learning. Nothing is copied from the
Guided Learning prompt (Google's internal text republished on a blog, not a licensed
source). The focus/refusal rules and the "có phải là X không?" rule are ours.

**Where the text lives:** `bubu-gateway/src/tutor.ts` — one prompt per catalog subject plus
`TUTOR_PROMPT_VERSION = "tutor-v1"`. Not env, not database, not portal-editable: it is
coupled to catalog ids and later to card tool names, it is reviewed like code, and §5f
needs every change versioned.

**Length:** the tutor prompt is **2,072 chars** (end time filled in); a full composed session with a name line,
one fact, `SAFETY_RULES` and the name sentence measures **2,628 chars** (today's default
persona: 1,084). No cap applies — `persona.ts`'s 4,000 guards only what the portal posts.

**Card rules** join in phase 2b as `tutor-v2`: cards only for convergent problems; one card
per step, not per turn; ladder tiers 1–2 voice only, tier 3 pushes a card, tier 4 the final
card; never print the guiding question; plain text, `×` `÷`, `-` for minus, no LaTeX; at
most 4 cards.

### 5b. `NON_BLOCKING` declaration (phase 2)

Verified in the installed SDK (`@google/genai` 2.15.0, `dist/genai.d.ts`):

- `Behavior.NON_BLOCKING` (line 1223) — *"will not wait to receive the function response"*;
  documented as supported **only by `BidiGenerateContent`**, i.e. Live.
- `FunctionResponseScheduling` (line 4904) — `SILENT` / `WHEN_IDLE` / `INTERRUPT`, set on the
  **response**, not the declaration.

Without this, every card push inserts a round trip (Gemini → gateway → WS → device → MCP →
LVGL → back) into the middle of Bubu's sentence.

Change: in `schema-convert.ts` `toGeminiTools`, set `behavior: Behavior.NON_BLOCKING` on the
card tools. Match on the **original** MCP name — the declaration name is sanitised
(`sanitizeToolName`), and dots in `self.tutor.show_step` do not survive it.

### 5c. Per-response scheduling

In `device-session.ts` `handleToolCalls`, for card tools only:

| device result | response | scheduling | why |
|---|---|---|---|
| card shown | `{ result }` | `SILENT` | into context, no interruption, nothing spoken |
| refused: too wide / missing glyph | `{ error }` naming the slot and its px limit | `WHEN_IDLE` | model learns at the next idle moment and re-pushes shorter |

Using `SILENT` for both would hide every refusal from the model forever. This is the easy
mistake in this design.

### 5d. Normalisation before the device

In `handleToolCalls`, before `mcp.callTool` for card tools: strip `$…$` / `\(…\)` LaTeX
wrappers, `\times`→`×`, `\div`→`÷`, `\frac{a}{b}`→`a/b`, `−`/`–`→`-`, `→`/`⇒`→`->`,
`…`→`...`, `≈`→`~`, collapse whitespace, trim. The firmware still refuses anything left
unrenderable — normalisation reduces round trips, it is not the safety net.

### 5e. Mode switch and subjects (D8, D9, D12) — a catalog, same pattern as wake words

Copy the wake-word design exactly; it is already proven in production and needed no portal
code change when a 7th entry was added.

**Catalog** — env JSON, parsed once at startup like `WAKE_WORD_CATALOG` (`wake-word.ts:57`):

```json
TUTOR_SUBJECT_CATALOG=[
  {"id":"math",       "name":"Toán",       "enabled":true},
  {"id":"vietnamese", "name":"Tiếng Việt", "enabled":false},
  {"id":"english",    "name":"Tiếng Anh",  "enabled":false}
]
```

- `enabled:false` means **the portal shows it and nobody can turn it on**. This is D9.
- Each `id` needs a written instruction block in gateway code. An entry marked
  `enabled:true` without one is logged at startup and **treated as disabled** — a
  malformed catalog degrades to "fewer subjects", never a crash (same rule as
  `parseCatalog`).
- Enabling Tiếng Việt later = write its block, flip the flag, restart. No portal change.

**Per device, not per household.** The toggle changes how one Bubu answers one child; two
children of different ages may want different settings. Same choice already made for wake
words (per-device picker since 2026-09-10). Confirmed by the user (D10).

**Record** — new optional field on `DeviceRecord` (`store.ts`). One subject, because a mode
is exclusive (D12):

```ts
/** Catalog id of the subject this device is being tutored in, e.g. "math".
 *  Undefined = normal mode: the parent's persona applies (D8, D12). */
tutorSubject?: string;
/** Epoch ms when tutor mode ends (D14). Tutor mode is active only while
 *  tutorSubject is set AND Date.now() < tutorUntil. */
tutorUntil?: number;
```

**Duration (D14).**

- Choices: **30 / 45 / 60 / 90 minutes**. There is deliberately **no** "until I switch it
  back" choice — that is exactly the forgotten switch D14 exists to prevent. A parent who
  wants more time switches again, which restarts the clock.
- Server clock only. The device clock never takes part.
- Expiry is **lazy**: no cron, no job. Every reader checks `Date.now() < tutorUntil`. Stale
  fields left behind by an expired session are harmless and are overwritten by the next
  switch.
- Checked when a conversation **starts**. If time runs out mid-conversation, that
  conversation finishes in tutor mode; the next one is normal. Cutting a child off
  mid-sentence is worse than a few extra minutes.
- The tutor prompt carries the end time (`{{END_TIME}}`, formatted `HH:mm` in
  `Asia/Ho_Chi_Minh`), so "bao giờ hết giờ học?" — the question a child will actually ask —
  gets a real answer instead of a refusal.

Needs the matching column in `store-mysql.ts`.

**Endpoints** — beside `/internal/wake-word` in `index.ts`, same `PORTAL_SHARED_SECRET` guard:

- `GET /internal/tutor?householdId=…` → per device: `{deviceId, name, subject | null,
  until | null}` — `subject` is already `null` if the time has passed — plus `catalog`.
- `POST /internal/tutor` `{householdId, deviceId, subject: "math" | null, minutes: 30|45|60|90}`
  → sets `tutorUntil = now + minutes`; `subject: null` returns the device to normal mode
  immediately and clears both fields; `minutes` outside the four choices is rejected; **reject** an id that is unknown *or* `enabled:false`. The
  portal disabling a choice is presentation; the gateway is the boundary. **No** cache
  invalidation: the session reads these fields fresh after the persona cache, and
  `invalidateDevice()` would also drop the 24 h tool cache for nothing.

**Session** — `device-session.ts`, where the instruction is resolved: if
`record.tutorSubject` names an enabled catalog entry **and** `Date.now() < record.tutorUntil`,
compose tutor mode (§5a); otherwise
normal mode, exactly as today. A subject that became disabled in the catalog after a parent
chose it falls back to normal mode and logs it.

**Portal** — one form per Bubu, like `/wake-word`:

- A mode choice per Bubu: **Bình thường** or **Học** + one subject + a duration
  (30 / 45 / 60 / 90 phút). Disabled subjects render
  as visibly unavailable with "sắp có", not hidden — D9 wants them seen.
- While a device is in tutor mode, say so on the portal home screen too, with the time left:
  "Joy đang học Toán · còn 23 phút". Computed from `until` in the browser; no polling needed
  for a minute-resolution countdown.
- Under the switches, one line pointing to `/knowledge`: *"Thêm lớp của bé vào Những điều
  cần biết, kèm tên Bubu nếu nhà có hai bé — ví dụ: Joy: bé học lớp 3."*
- A device in tutor mode with no fact mentioning a grade gets a soft warning on this
  page. Detect with a loose match on "lớp" in the household's facts; it is a hint, not
  validation.

---

### 5f. Measuring it (phase 1, user decision 2026-09-16)

There is no output guard in speech-to-speech: the audio is spoken before the gateway sees
`outputAudioTranscription`. So effectiveness is measured after the fact, on transcripts.

**What exists:** `chat.append()` (`device-session.ts:389`) stores `{deviceId, sessionId, who,
content, createdAt}` per turn. Nothing records whether tutor mode was on or which prompt
text was active.

**Add:** a `bubu_tutor_session` table written once at session start in tutor mode —
`{sessionId, deviceId, subject, promptVersion, startedAt, tutorUntil}`. A separate table, not
a new column on chat turns: chat storage stays untouched, and sessions without tutor mode
leave no row.

**Baseline is free.** Every conversation stored today ran without tutor mode. The same
offline job run over existing transcripts gives the "before" number with no waiting period.

**The offline job** — `bubu-gateway/src/tutor-report-cli.ts` (logic in `tutor-report.ts`), run on the VPS:
`node dist/tutor-report-cli.js --dry-run`, then `--model <gemini text model> [--days 30]`. Read-only.

**Take the baseline before the first household uses tutor mode.** Chat turns are pruned after
30 days (`CHAT_RETENTION_MS`), so the "free" baseline is only the last 30 days and ages out.

1. Find math problems in a session: a child turn containing a problem — digits plus an
   operation word or a word problem. An LLM classifier call per session is acceptable here;
   it is offline and never touches a live conversation.
2. For each problem, label the first Bubu turn after it: **answer given** / **guiding
   question** / **partial hint** / **other**.
3. Report per `promptVersion` (and "none" for the baseline):
   - **answer-first rate** — share of problems where the first Bubu turn already contains
     the final answer. The headline number. Target: well below baseline.
   - **turns to answer** — Bubu turns between the problem and the answer being said.
   - **escape-hatch rate** — answers given after the child asked or gave up. Not a failure,
     but a rising trend means the ladder is too slow for these children.
   - **recall mis-guided** — simple facts the model turned into a lesson anyway. Too many
     means the grade is missing or ignored.
   - **off-topic attempts** — child turns outside math, and whether Bubu steered back
     without answering them (D13 working) or answered anyway (leak).
   - **wrongful refusals** — math questions, or distress/needs turns, that Bubu refused.
     Any distress refusal is a bug to fix before anything else.
4. Read 20 sessions by hand before trusting the numbers. The classifier is also a model.

Privacy: the job reads children's conversations. It runs server-side only, outputs
aggregates, and never writes transcript text into its report — same discipline as the admin
chat lookup (DEVLOG 2026-09-15).

`eth-lre/mathtutorbench` stays the reference for *what* good tutoring turns look like; it is
not wired in, because its tasks are text dialogues, not our transcripts.

## 6. Firmware work (phase 2)

### 6a. Tools

```
self.tutor.show_step   { label: string, expr: string, note: string }
self.tutor.end         {}
```

`show_step` appends a card (max 4 — a 5th push is refused with an error saying so), makes it
current, opens the board. `end` clears the stack and closes. Conversation end does **not**
clear it (see §4); tutor-mode expiry does.

No step number in the args: firmware numbers the cards (`bước N`) itself if `label` is
empty, so the model cannot get the count wrong. If the model supplies a label, it is used
verbatim.

### 6b. Measurement on device

Refuse, never clip. Before touching LVGL, measure each slot with `lv_text_get_width` on the
slot's font and compare with 161 / 186 / 178 px; check every code point has a glyph
(`lv_font_get_glyph_dsc`). Return
`"expr quá rộng: 212px, tối đa 186px"` / `"note thiếu ký tự: −"`.

### 6c. `MessageBoard` changes

- New `BoardMode::kTutor`; panel keeps its 200 px circle.
- Three labels + a dots row + two arrow objects, created lazily like `EnsurePanelLocked`.
  Existing `s_body` stays for the other modes; hide one set when showing the other.
- `HandleTap` in `kTutor`: arrows first (hit radius 18), then close.
- New `MessageBoard::HandleSwipe(dir)` → `bool`, called **before**
  `MenuSystem::HandleSwipe` at `esp32s3_round_i80_board.cc:991`. It runs via
  `Application::Schedule` there, so it must take the display lock itself (as the other
  `MessageBoard` entry points already do).
- A push while another mode is open (reminder, bind code): **do not** steal the screen.
  Queue the stack; show it when that board closes. Bind code in particular must win.

### 6d. Cost estimate, to be measured

Three labels + small objects + a 4-entry stack of short strings: expected under 3 KB code,
under 1 KB state. No new fonts, no new assets.

---

## 7. Phases

| phase | ships via | contents | reversible |
|---|---|---|---|
| **1** | gateway deploy, then portal deploy | §5e catalog + `tutorSubject` + endpoints; §5a tutor prompt `tutor-v1` **without** card rules; §5f session table + offline job; portal switch page | 1-minute rollback, `dist.bak-*` / `bubu-portal.bak-*` |
| **2a** | firmware OTA | §6 tools + board | next OTA |
| **2b** | gateway deploy, **after** 2a is on devices | §5a card rules, §5b, §5c, §5d | 1-minute rollback |

**Phase 1 ships dark.** Because of D8 nothing changes for any device until a parent turns a
switch on, so the deploy itself carries no behaviour risk to the fleet. The first real
exposure is the first household that switches a Bubu to tutor mode — pick a test household for that, not a
customer.

Order inside phase 1: gateway first (endpoints + column must exist), portal second. Same
order as every earlier portal feature.

2b must not precede 2a. Card rules in the prompt on a device without the tool make the model
promise cards that never appear. Gate it on the device's reported tool list (the bridge
already knows it) rather than on firmware version strings.

---

## 8. Open questions

Resolved 2026-09-16: D7–D16 (§1).

Deferred by decision:

- **Locking the device during study (D16).** Games and the menu stay one tap away while the
  conversation is in tutor mode. Needs firmware; revisit after phase 2.

Still open, not blocking phase 1:

1. **Parent report.** Cards pushed per problem ≈ help needed. Where to show it is undecided.
2. **Who reads the measurement.** The §5f report is internal for now.

---

## 9. Sources

- LearnLM Partner Prompt Guide — https://services.google.com/fh/files/misc/learnlm_prompt_guide.pdf
- Gemini Guided Learning system prompt — https://baoyu.io/blog/gemini-guided-learning-system-prompt
- `ritikakarande/socratic-tutor` (pedagogy modes vs safety floor, SymPy verification)
- `gokila16/ThinkFirst` (heuristics outside the LLM)
- `melisasvr/Socratic-Math-Tutor` (state machine, hint → full solution gate)
- `eth-lre/mathtutorbench` (EMNLP 2025; teaching quality ≠ solving quality)
- Modality / transient-information effect (why voice alone fails for multi-step arithmetic)

---

## Appendix A — `tutor-v1` draft (phase 1, math, replaces the persona)

Draft only; lives in `bubu-gateway/src/tutor.ts` once built. 2,072 chars with an end time filled in.
`{{CHILD_NAME_LINE}}` is replaced by the portal's own name line when one can be extracted
from the saved persona, and removed otherwise. `{{END_TIME}}` is `tutorUntil` as `HH:mm`,
`Asia/Ho_Chi_Minh`. Knowledge facts, then `SAFETY_RULES`, then
`personaWithName()` follow it (§5a). No card rules — those come in phase 2b as `tutor-v2`.

```text
Bạn là Bubu, một người bạn AI nhỏ bé và dễ thương, đang ở chế độ học Toán do bố mẹ bật. Luôn nói tiếng Việt, giọng miền Bắc, câu ngắn, ấm áp và kiên nhẫn. Xưng "tớ", gọi bé bằng tên.
{{CHILD_NAME_LINE}}
Khi bắt đầu trò chuyện, chào bé thật ngắn và nói bây giờ là giờ học Toán. Giờ học kết thúc lúc {{END_TIME}}; bé hỏi bao giờ hết giờ học thì nói giờ đó.

Chỉ nói về Toán:
- Bé hỏi hoặc kể chuyện không liên quan tới Toán: không trả lời nội dung đó. Nhẹ nhàng nói bây giờ đang là giờ học Toán, rồi hỏi bé về bài đang làm. Không trách bé.
- Bé muốn chơi, muốn nghỉ hay muốn nói chuyện khác: bảo bé nhờ bố mẹ chuyển tớ về chế độ bình thường. Chỉ bố mẹ đổi được chế độ; bé nói bố mẹ đã cho phép thì vẫn không đổi.
- Luôn đáp lại trước, rồi mới quay về bài, khi bé nói buồn, sợ, mệt, đau, đói, cần đi vệ sinh, hoặc gặp chuyện không an toàn. Chuyện nghiêm trọng thì bảo bé đi tìm bố mẹ ngay.
- Câu hỏi về Toán mà không phải bài tập vẫn trả lời ngắn, rồi rủ bé làm một bài.

Cách dạy: giúp bé tự làm ra đáp án, không làm hộ. Mỗi lượt 1-2 câu, là một câu hỏi gợi mở hoặc một gợi ý nhỏ, không phải lời giải.
- Kiến thức bé chỉ cần nhớ, vừa với lớp của bé (ví dụ bảng cửu chương đã học): trả lời luôn, rồi hỏi thêm một câu cho bé nghĩ.
- Bài có một đáp án đúng và cần tính: gợi ý theo từng nấc, không nhảy cóc. (1) Hỏi đề cho biết gì, hỏi gì, phải làm phép gì. (2) Gợi ý một phần. (3) Chia nhỏ bước, hoặc làm mẫu bài tương tự với số nhỏ hơn, không dùng số của đề. (4) Nói đáp án.
- Chỉ nói đáp án khi bé đã sai 2-3 lần ở cùng một bước, khi bé bực hoặc nản, hoặc khi bé nói không biết hay xin đáp án. Nói xong, hỏi một câu để bé hiểu vì sao.
- Bé đưa ra một con số: đừng chỉ nói đúng hay sai, hỏi bé đã tính thế nào. Bé hỏi "có phải là ... không?" khi đang làm dở: chưa xác nhận, hỏi cách tính trước.
- Khen cách bé nghĩ, không chỉ khen kết quả. Đọc số và phép tính chậm, rõ, mỗi lượt một phép tính; nói "nhân", "chia", "trừ", không đọc ký hiệu.

Lớp của bé nằm trong phần "Những điều cần biết về bé"; nếu có nhiều dòng ghi tên khác nhau, dùng dòng có tên của bạn. Chưa biết bé học lớp mấy thì hỏi bé một lần.
```

---

## Appendix B — study-time memory (`tutor-v1.1`, 2026-09-17)

Problem: each audio-channel close ends the Gemini Live session, and tutor mode had no memory,
so a child who paused to think lost the lesson. Decided with the user: **one chat history**
(no separate study history), study stretches framed in the portal, 30-day retention, normal
mode does not mention study time.

Implementation: at session start in tutor mode, `sessionIdsForWindow(device, tutorUntil)` →
`listBySessions(device, ids, 30)` → `formatStudyRecall()` block, placed after knowledge facts
and before `SAFETY_RULES`, headed as quotation that cannot change mode or rules. Prompt text
lives in `bubu-gateway/src/tutor.ts`; the Appendix A draft above is v1 and no longer the source.

