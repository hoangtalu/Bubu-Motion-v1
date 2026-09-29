# Dictation (chính tả) — Design Plan

Status: **live since 2026-09-18** (`dictation-v1.1`); phrase-aware grouping `dictation-v1.2` built 2026-09-21, see §3. Code: `bubu-gateway/src/dictation.ts`; deploy: `tutor-mode-deploy.md` "Release 4". Phase 0 results in §6. Extends tutor mode (`tutor-mode-plan.md`);
the `vietnamese` catalog entry is the switch. Code references were read against the current
tree on 2026-09-17/18.

**Goal.** Bubu reads a passage the parent chose, a few words at a time, slowly and with every
punctuation mark spoken, while the child writes it down. The child asks Bubu to repeat, or
to go on. Bubu never shows the text and never loses its place.

---

## 1. Decisions

| # | Decision | By |
|---|---|---|
| C1 | The passage is **typed by the parent in the portal**. Bubu never invents one | user, 2026-09-18 |
| C2 | Read in groups of at most **6 chữ** (syllables). The **parent marks the breaks** with a line break and the gateway reads exactly those; where they do not, it cuts where a phrase survives. Was "5 chữ, even groups" until a customer reported nonsense groups | user, 2026-09-18 / 2026-09-21 |
| C6 | **One line break = a reading break, silent. A blank line = a new line in the notebook, read as "xuống dòng."** Short guidance sits next to the passage box | user, 2026-09-21 |
| C3 | **Miền Bắc** voice, same as tutor mode (D15) | user |
| C4 | Tune the wait, and **do not close the session automatically while the child writes** (wake-word mode) | user |
| C5 | Asked "chữ X viết thế nào?": read the word again, slowly — **no spelling**. Default, not yet confirmed by the user | proposed |

C1 also settles copyright: the text is whatever the parent types, usually the school's own
passage, and it never leaves that household.

---

## 2. Position lives in the gateway, never in the model

Every close of the audio channel ends the Gemini Live session, and the tutor-v1.1 recall (30
transcript lines) cannot say with certainty which chunk comes next. One chunk off ruins the
exercise. So:

- Portal saves the passage; gateway stores it per device with a **cursor** (sentence, chunk).
- The gateway splits the text and writes the **read-aloud script** in code (§3), not the model.
- The model gets **gateway-side tools**, the same mechanism as `weather-tool.ts`
  (`device-session.ts:338`): `doc_tiep`, `doc_lai`, `doc_cum_truoc`, `doc_ca_cau`,
  `doc_lai_ca_bai`, `doc_lai_tu_dau`. Each returns the exact script to speak; the model's only
  job is to say it and to understand the child.
- **Guard against false advances:** a tool that moves the cursor is refused unless the child
  said something since the last move (the gateway sees `inputTranscription`). Noise cannot
  skip a chunk.
- A new session in dictation mode opens with "we are at sentence N, chunk K" from the DB, so a
  reconnect (GoAway, timeout, reboot) resumes in place.

## 3. Chunking and the read-aloud script (C2)

- **The parent's own breaks come first (C6).** A single line break ends the group there and is
  never spoken; the sentence carries on, so "đọc cả câu" still reads the whole sentence. A blank
  line is a real new line and is read as "xuống dòng". This is the answer to compounds: no word
  list can know "xào xạc" or "Lao động", and a parent pressing Enter always can.
- A clause that already fits the ceiling is **never** split, so a marked piece is read exactly
  as marked.
- Split into sentences, then into clauses at `, ; :`. Inside a clause, `groupClause()` picks
  the boundaries by cost: **at most 6 syllables** (hard), 4 preferred, a large penalty for
  ending a group on a word that still needs the next one (classifier, number, "đã/đang/sẽ",
  "hãy/lại/tự", any preposition or conjunction) or starting one on a word that leans back
  ("ấy", "nhé"), and a small bonus for opening on a preposition. Even groups were the first
  design and produced "em thấy những bông" — a customer reported it on 2026-09-21.
- Punctuation is spoken **at the end of the chunk it follows**, as a word:
  `,` phẩy · `.` chấm · `?` chấm hỏi · `!` chấm than · `:` hai chấm · `;` chấm phẩy ·
  `“ ”` mở/đóng ngoặc kép · `( )` mở/đóng ngoặc đơn · `-`/`–` at line start gạch đầu dòng ·
  paragraph break xuống dòng.
- Flow per sentence (primary-school practice): read the **whole sentence once** at normal pace
  for meaning, then each chunk **slowly**, then wait. Repeat only on request. At the end, read
  the whole passage once more for the child to check (soát lỗi).
- Example — the sentence the customer reported on:
  *"Dưới ánh nắng vàng ươm của buổi chiều thu, những chiếc lá khô khẽ chao nghiêng rồi đáp
  xuống mặt đất."* → `Dưới ánh nắng vàng ươm` · `của buổi chiều thu – phẩy` ·
  `những chiếc lá khô` · `khẽ chao nghiêng` · `rồi đáp xuống mặt đất – chấm` (5, 4, 4, 3, 5).
  Even groups gave `khẽ chao nghiêng rồi` before. The splitter is plain code with unit tests;
  the example set lives in its tests.
- Known limitation, now the parent's to avoid: where a long clause carries no break of its own,
  a split can still fall inside a compound (`xào | xạc`, `Lao | động`, `bông hoa | phượng`).
  Measured on a customer passage of 51 groups: 9 fell inside a compound. With the parent's line
  breaks the same passage reads exactly as typed. The function words that produced the reported fragments are
  covered; compounds need either a dictionary or a parser, neither of which is here.

## 4. Not leaking the text

`device-session.ts:314` forwards every `outputTranscription` fragment to the device as
`tts sentence_start`, and the chat subtitles in the firmware tree (built 2026-09-16, **not on
OTA yet**) draw it on the eyes screen — the child would see the spelling. **In dictation mode
the gateway does not send that text.** The chat history still stores it.

## 5. Waiting while the child writes (C4)

### What closes the session today (measured in code)

| path | where | when |
|---|---|---|
| After Bubu speaks, AutoStop (wake word) goes back to **listening** | `application.cc:~624` | always |
| Listening with no speech detected on-device | `kListeningNoSpeechTimeoutMs = 8000`, `application.cc:1708` | **8 s** → channel closed |
| Channel with no incoming message | `Protocol::IsTimeout()`, `protocol.cc:82` | **120 s** → treated as closed |
| Gemini Live GoAway | gateway sends `session_end` | ~10 min per connection |

So today the session closes 8 s after every chunk. The child needs 30 s to several minutes to
write 5 syllables.

### Design: wait in *idle with the channel open*

The firmware already has this state: after a reply in tap mode (ManualStop) the device goes
**idle with the channel open**. In that state:

- the mic is **not** streamed — pencil scratching, TV and siblings reach nobody, and Gemini is
  not billed for minutes of silence;
- the wake word **is** running, and "Hi Joy" reuses the open channel with no reconnect
  (`HandleWakeWordDetectedEvent`, `application.cc:1432`); a tap on the eyes also works;
- sleep mode is blocked while the channel is open (`CanEnterSleepMode`, `application.cc:1875`);
- the gateway can still make Bubu speak: `tts start` from idle with the channel open goes to
  speaking (`application.cc:~596`).

Child's side: *"Hi Joy, đọc tiếp"* / *"Hi Joy, đọc lại"*.

Rejected alternative: stretch the 8 s listening timeout to minutes. The mic would stream the
whole time; any sound would trigger Gemini's voice detection and could advance the text, and
the timer disarms for good on the first on-device VAD hit (`listening_voice_detected_`).

### Pieces

| # | piece | where | needs OTA |
|---|---|---|---|
| W1 | After a dictation reply, go **idle** instead of listening: `tts stop` carries `"next":"idle"`; firmware honours it in AutoStop | firmware + gateway | **yes** |
| W2 | Keepalive: gateway sends a message every ~60 s while waiting, so the 120 s channel timeout never fires. Any incoming text already resets it (`websocket_protocol.cc:224`); firmware should learn a silent `{"type":"ping"}` so it stops logging "Unknown message type" | gateway (+ 1 line firmware) | no (works today with a warning) |
| W3 | One gentle nudge at 2× the expected writing time: *"Bé viết xong chưa? Xong thì gọi tớ nhé."* | gateway | no |
| W4 | Hard stop after **5 min** with no call: save the cursor, `session_end`. Next "Hi Joy" resumes in place | gateway | no |
| W5 | Log every wait (chunk read → child's next call) per device, to calibrate §5 timings from real data | gateway | no |
| W6 | Gemini keepalive: while waiting, send 100 ms of silent PCM to Live every 30 s. Without it Gemini drops an idle session after ~150 s (measured, §6) | gateway | no |

Old firmware ignores the unknown `next` field, so without W1 a device keeps today's behaviour
(close after 8 s) and the gateway cursor still makes resuming correct — only slower (a new
connection per chunk). **Phase 1 therefore works on the fleet as-is.**

### Starting numbers, to be replaced by W5 data

The primary curriculum's dictation targets are roughly 30 / 50 / 60 / 80 / 100 chữ per 15
minutes for lớp 1–5 (whole exercise, reading included). Expected writing time for a 5-syllable
chunk, first guess: **lớp 1 ≈ 90 s, lớp 2 ≈ 60 s, lớp 3 ≈ 45 s, lớp 4–5 ≈ 30 s**. Grade from
knowledge facts (D7); unknown grade → lớp 2. These only set W3's nudge; nothing is cut off
before W4.

## 6. Phase 0 results (measured 2026-09-18, host-side, same model and config as the gateway)

Harness: `gemini-3.1-flash-live-preview`, voice Kore, `languageCode: "vi"`, a draft
`dictation-v0` prompt and the three tools above, the §3 splitter on an original 40-syllable
passage (11 chunks). Child turns sent as text so runs are identical. Two full runs.

1. **Verbatim: yes.** Every chunk was read word for word, with every punctuation word
   ("phẩy", "chấm hỏi", "mở ngoặc kép"…), no additions, and the model stayed silent after
   each chunk as told. `doc_lai` repeated the same chunk; the whole-sentence read was natural
   and without punctuation words. Asked "chữ phượng viết thế nào?", it said only "phượng"
   (C5 holds). At the end it praised once and said the passage was over.
2. **`outputTranscription` drops words, the audio does not.** Two turns transcribed as "mẹ
   đưa" / "Mẹ cười" (2 of 6 and 2 of 5 words), reproducibly on the same chunk. The audio
   envelope of the "mẹ đưa" turn is the same as the full repeat of that chunk (3.0 s, same
   burst pattern), so the model spoke everything. Consequences: an automatic "did it read
   verbatim" check from the transcript gives false alarms, so treat it as a signal, not a gate;
   and the chat history can store a truncated Bubu line.
3. **Pace:** 1.3–2.5 syllables/s for chunks (punctuation words and pauses included), 3.4–3.6/s
   for the natural whole-sentence read. Prompt-only (`@google/genai` has no speaking rate for
   Live). First audio ~1 s after the child's text. Sample for listening:
   scratchpad `dictation-test/bubu-doc-chinh-ta-mau.wav`.
4. **Gemini closes an idle Live session after ~150 s**, code 1008 "The operation was aborted",
   **with no GoAway**. Reproduced twice (closed 150 s and 149 s after the last message).
   **100 ms of silent PCM every 30 s keeps it open**: 8 minutes with no GoAway, answered
   normally afterwards, and never spoke on its own. → **W6** below.
5. Still open: Northern pronunciation of tr/ch, s/x, r/d/gi (C3). Northern speech merges
   them, and those are the errors children make; needs a listen with a passage that contains
   them. Also not yet tried: real audio input (STT of "đọc tiếp" from a child) and the
   on-device round trip.

Not needed any more: the separate-TTS fallback. The Live model reads verbatim.

## 7. Phases

| phase | ships via | contents |
|---|---|---|
| **0** | local test, no deploy | Unknowns 1–3 on the bench device with a fixed passage |
| **1** | gateway + portal | splitter + script, cursor table, tools + guard, subtitle suppression, W2–W6, `dictation-v1` prompt, portal passage box under Tiếng Việt, `vietnamese` catalog entry enabled. **Built 2026-09-18**; one addition not in the plan: the first group of a multi-group sentence also carries the whole sentence (`ca_cau`), read once at natural pace |
| **2** | firmware OTA | W1 (`next:"idle"`), silent `ping` |
| later | — | show the finished passage sentence by sentence on the round screen for self-checking; parent-selectable accent |
