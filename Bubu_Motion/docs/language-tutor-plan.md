# Language tutor (English first) — Design Plan

Status: **plan only, nothing built (2026-09-21; §11 added later the same day).** Extends tutor mode (`tutor-mode-plan.md`) and
reuses the dictation machinery (`dictation-plan.md`, `bubu-gateway/src/dictation.ts`). Code
references were read against the tree on 2026-09-21.

**Goal.** Find out, with evidence, whether Bubu can teach a foreign language to a Vietnamese
speaker — children and adults — and keep a trustworthy record of what each learner has and has
not learned. English first. The first release is an **experiment that must be able to say no**,
not a product launch.

**Why it is risky.** Nothing below the syllabus is proven: whether the Live voice speaks clean
English inside a Vietnamese session, whether the model can *hear* a Vietnamese speaker's
English errors, and whether its own judgement of "correct" is worth recording. Section 2 makes
those questions measurable **before** anything is built; section 5 says what to trust in the
meantime.

---

## 1. Decisions

| # | Decision | By |
|---|---|---|
| L1 | Learners include **adults**, not only children | user, 2026-09-21 |
| L2 | **English first**, to find out whether teaching on Bubu works at all; the design stays language-agnostic (Mandarin / HSK 1 is the analysed second candidate) | user, 2026-09-21 |
| L3 | Teaching must be done carefully, with **progress assessment and a kept learning history** | user, 2026-09-21 |
| L4 | Content skeleton comes from a public framework; **no book text in the repo** (public repo, copyrighted books) | proposed |
| L5 | One device = one learner in v1 (see §7) | proposed |
| **L8** | **The parent may not speak English and authors nothing.** They pick a course from a catalogue we supply; we support the whole content chain. Kills the "parent types the school word list" option and makes the authored curriculum mandatory | user, 2026-09-22 |
| **L6** | **Children first**, not adults. Adults deferred to v2 — supersedes §9's "why adults first" | user, 2026-09-21 |
| **L7** | **Pronunciation is never graded.** U2 is dropped; no spoken score is ever shown, and Bubu does not correct a child's pronunciation — it models the target instead (see §4, §5) | user, 2026-09-21 |

---

## 2. What "it works" means — decided before building

A plan that cannot fail is not an experiment. Fix these in writing, with numbers, **before the
pilot starts**, not after seeing data.

**Primary outcome: delayed recall in production.** Of the items Bubu taught, how many can the
learner still *say* (not just recognise) after 7 and after 30 days.

**Within-learner control.** For every learner, split the unit's items at random into *taught*
and *held-out* (same difficulty band). Test all of them, from the same weekly quiz. Gain on
taught minus gain on held-out is what Bubu did; everything else (school, other apps, the child's
own growth) hits both halves equally. Small pilots cannot prove effect sizes, but this design
does not need a separate control group to be interpretable.

**Independent measurement.** Bubu never grades its own homework for the headline number. The
outcome test is a **portal quiz** (deterministic scoring, §5). The human-rated speaking sample is
gone with U2 (L7), and under L8 the parent cannot supply that judgement either — so the quiz is the
only outcome measure this feature has, and it must stay deterministic and fully Vietnamese.

**Secondary:** sessions per week per learner, completion rate, week-4 retention of the learner
themselves, self-reported confidence.

**Safety metrics, checked by a human reading a sample of transcripts:** Bubu said something
wrong about English; Bubu gave the answer away; Bubu corrected something that was not an error;
Bubu blended languages mid-sentence.

**Go / no-go.** Thresholds are yours to set. Placeholders, guesses rather than findings:
taught items beat held-out items by ≥ 20 percentage points at day 7; ≥ 60 % of learners still
using it weekly in week 4; zero confirmed instances of Bubu teaching wrong English in the
reviewed sample.

**Honest limit.** A pilot of ~10 adults (a guess) shows a signal, not proof. It can kill the
idea or justify a larger test; it cannot prove the product.

---

## 3. Curriculum spine (English)

Shape copied from what the HSK 1 analysis showed works structurally: a **unit** = situation +
"I can…" goal + 8–12 items + 1–2 patterns + one pronunciation focus + spiral review of earlier
units. 15–20 units for CEFR A1 → early A2.

- **Settled 2026-09-21 — see [`english-curriculum-v1.md`](english-curriculum-v1.md)** for the
  16-unit spine, the unit JSON schema and two worked units. Word source is **NGSL** (CC BY-SA 4.0,
  commercial use allowed, attribution + share-alike); Oxford 3000 and the Cambridge young-learner
  lists are commercial publications and were rejected.
- **Correction to an earlier claim here: the CEFR descriptors are not "public".** The Companion
  Volume is all-rights-reserved and reproduction needs written permission from the Council of
  Europe. Level *names* (A1, A2) are fine; every "Tớ làm được…" goal is written by us.
- **Authoring:** content is data (`curriculum/en/unit-NN.json`), written and reviewed by a
  person. A text model may draft items; nothing reaches a learner unreviewed, because the whole
  point is that the model does not invent the syllabus (§4).
- **One skin in v1 (L6): children.** Pet, school, food, family. The adult skin (work
  introductions, travel, ordering, directions) reuses the same items and is v2 work.
- **Vietnamese-first phonology, to test rather than assume.** Common patterns for Vietnamese
  speakers learning English, from the literature: dropped final consonants, simplified clusters,
  /θ ð/ → /t d s/, plural and past endings omitted, flat word stress, long/short vowel pairs
  merged. **Under L7 these are not error targets.** Each unit names one of them as the sound Bubu
  says especially clearly and invites the child to echo — modelling, not correction (§4).

---

## 4. Pedagogy — what Bubu does, and what it does not

**LearnLM, honestly.** It is folded into Gemini and steered by system instructions (Google's
prompt guide), which `tutor-v1` already borrows. So "LearnLM" here means *its principles, written
into the prompt and enforced in code* — not a model we can call. **Confirmed 2026-09-21, not
assumed:** our key lists 58 models and none is `learnlm-*`; the last callable one
(`learnlm-2.0-flash-experimental`) stopped working for existing projects on 2025-12-03. See §11.

**The gateway teaches; the model performs.** Same split as dictation: the gateway owns the
syllabus, the position and the schedule; the Live model says what the tool returns and
understands the learner. Reasons already measured: the session closes after 8 s of silence in
wake-word mode; Live tool calls are synchronous (STATE: no async on this model), so a "brain"
model cannot be consulted per turn; the model's transcript drops words.

**Session template** (lengths are guesses to calibrate):

1. **Retrieval warm-up** — 2–3 items due for review, asked without a cue.
2. **Teach** — 3–5 new items: hear → repeat → meaning check → use in a sentence.
3. **Practice** — recall without cue, then a short **role-play** in the unit's scenario.
4. **Close** — one "I can…" sentence, what comes next time.

Kids ~8–12 min, adults ~10–20 min. The gateway ends the session when the plan is done or the
learner stops; it never runs on until Live times out.

**Rules the prompt carries** (adapted from LearnLM's behaviours, in Vietnamese like `tutor-v1`):

- Ask, then let the learner produce; do not explain a rule before showing examples.
- **Never correct pronunciation (L7).** We dropped the measurement that would have shown whether
  the model's ear is trustworthy, so any correction of *how* a child said something is a guess, and
  a wrong correction aimed at a six-year-old is the worst failure this product has available to it.
  Instead: say the target again clearly, alone; invite one more try; praise the attempt.
- **Meaning may still be corrected**, at most once per turn, because meaning is checked against the
  item data rather than against phonemes. A wrong answer to "what does *apple* mean?" is checkable;
  a wrong /æ/ is not.
- Praise the attempt and the specific thing done well, not "great!" alone.
- Do not give an answer the learner can retrieve; hint in steps (as `tutor-v1`).
- Explanations in Vietnamese; target English said **alone, as its own utterance**, so the two
  languages never blend inside one sentence. Reduce Vietnamese as the learner advances.
- Anything the model says as "the correct form" comes from the item data returned by the tool,
  not from its own memory.
- Kids keep `SAFETY_RULES` and the age-appropriate persona. Adults get a neutral address
  (anh/chị/bạn, chosen in the portal) instead of "bé".

**Known trap, already seen in this project (DEVLOG 2026-09-18):** when the session says "answer
in Vietnamese" and the learner asks for English, the model blends both in one sentence. The
"target English stands alone" rule and Phase-0 test U3 exist for this.

---

## 5. Assessment — what can be trusted

Two kinds of evidence, kept apart in the data (§6, `source`):

| Signal | How obtained | Trust | Use |
|---|---|---|---|
| Recognition / meaning | Portal quiz on the learner's phone; gateway holds the answer key | **High** (deterministic) | Headline outcome, placement, weekly test |
| Spelling / typed production | Portal quiz | **High** | Same |
| Spoken recall | Model reports the attempt via a tool call | Not established — **U2 was dropped (L7)** | **Coverage only**: "đã luyện 34 từ". Never "mastered", never a score |
| Pronunciation | — | — | **Not judged and not scored, ever (L7).** Bubu models the target; it does not rate the child |
| Roleplay fluency | Model's judgement, human sample in pilot | Low | Qualitative note for the parent/learner |

**Under L7 this is now absolute, not conditional: no number the model produced is ever shown to a
learner or parent.** The app shows coverage ("đã luyện 34 từ") and the deterministic quiz results,
and nothing else. A wrong "bé đã thạo X" is worse than no number.

**What this costs, stated plainly.** Dropping U2 removes the only path to an honest spoken score,
so the quiz becomes the single source of evidence — which makes the taught-vs-held-out control
(§2) more important, not less, and makes weekly parent participation the feature's real dependency
(see `english-curriculum-v1.md` §6.2).

**Pronunciation scoring is out of scope by decision (L7), not merely unsolved.** The options
remain on the table for a later version — the Live model's ear (unmeasured), or a dedicated
assessment service (Azure/Speechace class; voice data would leave our infrastructure, costs not
evaluated) — but v1 ships without either, and the product must not be described as teaching
pronunciation accuracy.

**One tool call per turn.** Tool calls stall the conversation on this model. So the model calls a
single `learn_next(prev_outcome)` per turn: it records the previous attempt **and** returns the
next thing to say. No separate "log result" call.

**Do not trust the transcript for grading.** `inputTranscription` is a model's normalised
guess of accented speech (too lenient, or wrong language — DEVLOG 2026-09-18), and
`outputTranscription` drops words. The grader is what the *model heard*, reported through the
tool, with the transcript kept only as supporting evidence.

---

## 6. Learning history

Chat history is the wrong place: `bubu_chat_turn` and `bubu_tutor_session` are pruned at **30
days** (`store-mysql.ts`, `CHAT_RETENTION_MS`), and clearing chat also clears study markers.
Progress must outlive both, and must not depend on keeping transcripts. So: **a separate,
structured, minimal record.**

New tables in the gateway MySQL, each its own table (the dictation lesson: a row shared with
`bubu_device` gets rolled back by whole-row writes):

```
bubu_learner        learner_id, household_id, device_id, display_name,
                    kind (child|adult), native 'vi', target 'en', address_term,
                    level, started_at, consent_at, curriculum_version

bubu_learn_item     learner_id, item_id, box (SRS stage), due_at, streak, lapses,
                    first_seen_at, last_seen_at, last_outcome           -- one row per learner+item

bubu_learn_event    event_id, learner_id, session_id, unit_id, item_id,
                    kind (exposure|recall|produce|pron|roleplay),
                    outcome (pass|partial|fail|unknown),
                    source (model|quiz|human),          -- who decided; never mixed
                    focus (e.g. 'final-consonant'), prompt_version, curriculum_version, at
                                                                         -- append-only

bubu_learn_session  session_id, learner_id, unit_id, started_at, ended_at,
                    planned_items, done_items, end_reason (plan-done|silence|goaway|learner|limit),
                    prompt_version

bubu_learn_check    learner_id, kind (placement|weekly|delayed), item_ids, score,
                    source (quiz|human), taken_at, taught_flag per item
```

- **Scheduling:** a plain Leitner ladder to start (1, 2, 4, 7, 14, 30 days; a lapse drops one
  box). Numbers are starting values; the pilot's delayed-recall data replaces them.
- **Every event carries `prompt_version` and `curriculum_version`**, the way tutor sessions
  already do, so a change is comparable to the version before it.
- **`source` is never merged.** A model-graded pass and a quiz pass are different facts.
- **Retention:** events, item states and checks are kept until the household deletes the learner;
  sessions likewise. **No audio is stored and no free-text transcript goes in the learning
  record.** If error analysis needs the heard string, store the target item's short heard string
  with its own shorter retention — decide in §9.
- **Portal:** per learner, units done, items practised / due, quiz history, and export and
  delete-everything buttons. Deleting the learner removes all five tables' rows.
- **Legal.** Children's data is sensitive under Vietnam's personal-data decree
  (Nghị định 13/2023/NĐ-CP, as I understand it). Consent flow, retention and deletion need
  someone qualified to confirm before children are enrolled. Not assessed here.

---

## 7. Who is the learner

Today: a device belongs to a household; persona and tutor mode are **per device**; knowledge
facts are per household; tutor mode assumes a parent controls a timed window.

- **v1: one device = one learner** (`bubu_learner.device_id`). A household with a child and an
  adult who both want to learn uses two Bubus, exactly as wake words and tutor mode already work.
  Telling two people apart by voice is not reliable and would need its own consent.
- **v1 is children only (L6)**, so the existing parent-controlled window, `SAFETY_RULES`, child
  name and "tớ / bé" address all carry over unchanged, and the two bullets below become v2 work.
- **Adults do not need a parent's timer.** Tutor mode is a 30/45/60/90-minute window with no
  "until I switch it back" (D14), built to stop a forgotten switch from leaving a toy that
  refuses to play. For an adult learner the subject config needs `mode: "window" | "standing"`.
- **Off-topic handling** for adults is softer than the child rule "refuse and steer back": the
  learner owns the device, and an adult who wants to chat should be able to.
- **Address and tone**: `address_term` replaces the hard-coded "bé" and "tớ".
- Shared device (kid and adult on one Bubu): **not supported in v1**; flagged for a later decision.

---

## 8. Reuse from dictation, and what is new

| Piece | Reuse | Notes |
|---|---|---|
| Position in the gateway, tools return the exact text | yes | tools: `learn_next`, `learn_repeat`, `learn_hint`, `learn_skip`, `learn_end` |
| Guard: a tool that advances is refused unless the learner spoke | yes | stops noise from skipping items |
| Gemini keepalive (100 ms silent PCM / 30 s) | yes | Live drops an idle session at ~150 s |
| Device ping keepalive | yes | |
| `session_end` and cursor resume | yes | a reconnect resumes at the same item |
| Subtitle suppression | **not needed** | showing English text is fine; the subtitle feature is not on OTA anyway |
| Wait for minutes while writing | **no** | speaking waits are seconds; but a learner composing an English sentence may exceed the **8 s** wake-word listening timeout — measure it (U5) |
| Catalog entry in `TUTOR_SUBJECT_CATALOG` | yes, `english` (currently disabled) + a prompt in `tutor.ts` | one prompt per subject rule holds |
| Per-language knowledge (level) | new | `bubu_learner.level` plus the item table replace grade-from-facts |

New: curriculum loader and validator, SRS scheduler, the five tables, portal pages (placement,
weekly quiz, learner report), an evaluation export for the pilot.

**Firmware: none needed for Phase 1.** Voice-only. (Showing English on the round screen and
reusing the eye-tap for "again" are later ideas, not requirements.)

---

## 9. Phases

**Settled as of 2026-09-23** (decisions L1–L8 in §1; content stack in
`english-curriculum-v1.md` §0/§3): children first, pronunciation never graded, parents pick a
course and author nothing, MOET is the spine, CEFR-J filters by level, NGSL sorts by usefulness,
the `choice_vi` portal quiz is the only number anyone sees, and run time is a plain table read
because Live tool calls are synchronous on every model we would ship.

**Three things block, and only one of them is code.** (a) a **named content reviewer** — under L8
there is no other human left who can catch a wrong English item; (b) the **children's-data legal
review**, which is a prerequisite and not a gate; (c) **which words sit in which chủ đề per grade**,
which needs a primary English teacher or the Cambridge permission in `english-curriculum-v1.md` §0.

| phase | ships via | contents / gate |
|---|---|---|
| **0a — measure, build nothing** | host-side harness, no deploy | **U0** choose the Live model, **U1** English voice quality (with and without the `languageCode` pin). Needs no curriculum. **Gate: if U1 fails — Bubu's own English is Vietnamese-accented — stop. There is no product behind that.** |
| **1a — author a thin slice** | authoring machine only | One course (`g3`), **3 units, 6–8 items each**, through the full pipeline: MOET topic → person proposes words → CEFR-J filters by (word, pos) → NGSL sorts → person writes → **reviewer signs off**. Output is 3 `unit-NN.json` plus the validator. **Gate: the reviewer exists and has signed.** |
| **0b — measure on real content** | host-side harness, no deploy | **U3** script discipline (does it invent items, give answers, blend languages in one sentence), **U4** transcript fidelity (Live vs `gemini-3.5-transcribe`), **U5** per-turn latency with one `learn_next` and how often a child answers after the 8 s listening timeout. **Gate: U3.** A model that invents vocabulary cannot be given to a child whose parent cannot check it |
| **2 — build** | gateway + portal | `bubu_english_*` tables, prompt `english-v1`, `learn_*` tools returning exact strings, `ENGLISH_COURSE_CATALOG`, portal course picker + `choice_vi` quiz + Vietnamese progress view, flip `english` to `enabled:true`. Same shape and runbook as dictation. **No firmware change.** |
| **3 — pilot** | same | Children pilot (L6); legal review done and parental consent signed **before the first learner**; taught-vs-held-out split running; weekly quiz |
| **4 — decide** | decision | go / no-go against §2. Adults (`standing` mode, neutral address) only on a go; second language (Mandarin) on the same engine after that |

**Why authoring sits between the two halves of Phase 0:** U0 and U1 need no content and gate the
whole idea, so they run first and cheaply. U3 is meaningless without a real item list — testing
script discipline against invented words tests nothing.

**L6 reversed the original "adults first" reasoning**, which was that consent is simpler and an
adult can report what felt wrong. Children first means those two safeguards are gone, so the
compensating controls are: the legal review happens **before** any learner, the content is
human-reviewed unit by unit (`english-curriculum-v1.md` §5), and Bubu never judges the child
(L7).

### Phase 0 unknowns — each has a pass criterion, set before running

- **U1 Voice.** Does the Live voice say clean English targets inside a Vietnamese-framed session
  — with `speechConfig.languageCode` pinned to `"vi"` (today) and unpinned? Method: fixed
  20-item script, two configs, a human rates each item clear/unclear. Pass: ≥ 95 % clear
  (guess).
- ~~**U2 Ear**~~ — **dropped by decision (L7, 2026-09-21).** No spoken score will be shown, so the
  measurement that would have licensed one is not worth its cost in volunteers and human labelling.
  Consequence: Phase 0 needs no recruitment and can be run entirely from this machine. If a spoken
  score is ever wanted, U2 comes back first — it is the only thing that could justify one.
- **U3 Script discipline.** Over 50 scripted turns, does the model stay on the item list,
  never invent items, never give the answer, never blend languages in one sentence? Count each.
- **U4 Transcript fidelity.** Word error rate of `inputTranscription` against a human transcript
  for accented English. Decides whether the transcript can serve even as supporting evidence.
- **U5 Timing.** Response time per turn with one `learn_next` call; and how often a learner's
  answer starts later than the 8 s listening timeout in wake-word mode.

---

## 10. Open for you

1. **Learner privacy line (§6):** may the learning record keep the *heard string* of each
   attempt (helps error analysis, is speech content) — or strictly outcomes only?
2. **Adult mode:** standing mode with soft off-topic handling, as §7 proposes?
3. **Pilot numbers (§2):** go/no-go thresholds, pilot size, and whether adults come first.
4. **Curriculum source (§3):** which frequency list, after the licence check.
5. **Who reviews content and transcripts** in the pilot — needs a named person with good
   English, not a model.

## 11. Gemini's own "learning" features, checked against what the API actually serves

Everything in this section was measured on **2026-09-21** against the project's own key
(`bubu-gateway/.env`), not read off a blog post: `GET /v1beta/models` and a throwaway host-side
Live probe (audio out, `outputAudioTranscription` on, one `NON_BLOCKING` tool answered
deliberately 6 s late). Probe scripts were deleted afterwards; the method is two dozen lines and
is written out in the DEVLOG entry of the same date.

### 11a. The five features, one by one

**None of the five is an API endpoint.** They are features of the Gemini *app*
(gemini.google.com) — consumer packaging of primitives we already call. So the question is never
"can the gateway call it", it is "is this artifact worth building ourselves for a voice-only toy".

| Feature | What it actually is | Callable? | Verdict for Bubu |
|---|---|---|---|
| **LearnLM** | A tuning direction, folded into Gemini and steered by system instructions. `learnlm-2.0-flash-experimental` stopped working for existing projects on **2025-12-03**. Our key lists **58 models, zero `learnlm-*`**. | **No model exists** | Confirms §4 rather than changing it: LearnLM means *its principles, written into the prompt*. `tutor-v1` already borrows them. Nothing to integrate, nothing to gain. |
| **Quizzes & Flashcards** | Gemini app; generates quizzes with hints, flashcards with shuffle + TTS. 18+ in the app. | No — but trivial to rebuild | **Best fit of the five, and already required by this plan.** It *is* §5's portal quiz (the deterministic headline measure) plus §6's `bubu_learn_item` Leitner ladder. Build with `generateContent` + `responseSchema`; the model **drafts**, a person reviews (§3 authoring rule). Bonus a phone app cannot do: the same item bank drives the **spoken** retrieval warm-up (§4 step 1) hands-free, in the room. |
| **Practice Tests** | Same app surface; the app's version is exam-shaped (SAT / JEE / NEET). | No | **Not a separate build.** It is `bubu_learn_check.kind = placement \| weekly \| delayed` (§6), assembled from the same bank — and it is where §2's taught-vs-held-out control lives. Treat "practice test" as an assembly rule, not a feature. |
| **Storybooks** | Gemini app; ~10 illustrated pages, optional narration, 45+ languages. | No | **Split it.** Illustrated pages cannot reach the device: `assets` is one packed blob that `Assets::Download` overwrites wholesale (STATE), there is no per-file write path, and the touch-during-download history says do not invent one. Pictures would live in the portal; on the device a storybook is **audio only** — which is legitimate graded listening input, seeded with the unit's items, buildable today from a text model + `gemini-3.1-flash-tts-preview`. But it is *content*, not *teaching*: it does not move §2's primary outcome. Park until Phase 3, or treat as a separate product. |
| **"Learning tools"** | The app's umbrella name for the four above. | — | Nothing to adopt. |

**What this costs us: nothing, and that is the point.** Two of the five (quizzes, practice tests)
are work this plan had already specified for its own reasons. One (LearnLM) is a prompt stance we
already hold. One (storybooks) is a content idea that must not be allowed to become the pilot.

**The real gap these features expose** is not generation, it is **the key**. The production text
key is free tier (5 RPM) — it already killed the tutor baseline report (DEVLOG 2026-09-17) and
the gateway-side card writer (2026-09-18). Drafting an item bank, grading a quiz or writing a
storybook all need a paid text key. That, not LearnLM, is the unblocker.

### 11b. Measured: the Live model under us is now Legacy — and upgrading does **not** restore async tools

Two findings, both from the probe, both changing what §4 and §9 can assume.

**1. `gemini-3.1-flash-live-preview` is legacy.** Google's model page now describes it as a
"legacy audio-to-audio preview model. We recommend updating to Gemini 3.8 Live".
`gemini-3.8-live` and `gemini-3.8-live-extended-thinking` went stable on **2026-09-15**, six days
before this plan was first written. The gateway leaves `GEMINI_LIVE_MODEL` unset, so production
is on the legacy model — and *every* painful measurement in this project's tutor history (tool
blocking, language blending, accent drift, dropped output transcripts) was taken on it.

**2. The obvious hope — "3.8 Live brings back `NON_BLOCKING`, so step cards can return" — is
false.** Measured directly:

| model | tool call at | first speech after it | tool response held until | result |
|---|---|---|---|---|
| `gemini-3.1-flash-live-preview` | 3.1 s | 7.5 s | 6.9 s | **blocked** (~4.4 s dead air) |
| `gemini-3.8-live` (run 1) | 1.8 s | 7.8 s | 6.8 s | **blocked** |
| `gemini-3.8-live` (run 2) | 1.6 s | 7.9 s | 6.6 s | **blocked** |
| `gemini-2.5-flash-native-audio-latest` (3 runs) | 3.1 / 4.9 / 4.2 s | 3.8 s / — / 5.5 s | ~6.6 s | **async works** in 2/3 (it *may* talk over a pending call; it is not forced to) |
| `gemini-3.8-live-extended-thinking` | — | — | — | would not connect with this config (20 s timeout), unexplained |

So Google's documented limitation ("not yet supported in Gemini 3.1 Flash Live") understates it:
**3.8 Live blocks too, and worse** — it called the tool at ~1.7 s having said nothing at all, i.e.
6 s of total silence, where 3.1 at least spoke a filler sentence first. Async is available only on
the older `gemini-2.5-flash-native-audio-*` line, which would be a downgrade in every other
respect.

**Consequence for §5's "one tool call per turn".** That rule stands and gets stricter: on any Live
model we would realistically ship, a `learn_next` call is a hard pause in a child's or adult's
conversation. Budget for it explicitly (measure it in U5), keep the tool's work trivial (a table
read, never a model call behind it), and do not design any feature whose value depends on a tool
firing mid-sentence. The step-card post-mortem (DEVLOG 2026-09-18) is not a 3.1 problem to be
outrun by a version bump.

**3. There is a dedicated transcription model now, and it is a partial answer to U4 — not to U2.**
`gemini-3.5-transcribe` / `gemini-3.5-transcribe-live` went GA **2026-08-26**: 85+ languages,
word-level timestamps, speaker diarization, utterance-based language detection and **custom
vocabulary biasing**. The gateway already holds the learner's audio, so it can tee a copy to a
transcription pass *off* the Live turn — no added latency.

- **Helps U4** (transcript fidelity) and very likely the long-standing transcript-language
  misdetection bug (DEVLOG 2026-09-18, where the `languageCodes: ["vi"]` fix had to be reverted
  because a candidate list trades one failure mode for the other): utterance-based detection plus
  vocabulary biasing is a better-shaped tool than a hint list.
- **Does not help U2, and may hurt it.** A transcriber is built to recover the *intended* word
  through an accent — which is exactly the "too lenient" failure §5 already warns about. Word
  timestamps give stress and fluency, not phoneme accuracy. **Do not let a clean transcript be
  read as correct pronunciation.**

### 11c. What this adds to Phase 0

`U0` goes **before** U1–U5, because it decides which model the other four measure:

- **U0 Model choice.** Re-run U1 (voice), U3 (script discipline) and U4 (transcript) on
  `gemini-3.1-flash-live-preview` (today's production) **and** `gemini-3.8-live`, same prompt, same
  script. Decide the model before measuring anything else on it. Already known going in: 3.8 blocks
  on tools at least as badly (11b), and in a first English probe 3.8 taught 1 of the 3 requested
  words before yielding the turn while 3.1 taught all 3 — better pedagogy or worse instruction
  following, undecided on n=1.
- **U1 gains a second arm:** with and without `speechConfig.languageCode: "vi"`. The pin was removed
  from production on 2026-09-21 as a live experiment and is still unverified (STATE, Known gaps); an
  English tutor is the case where it matters most. Four sample recordings from this session's probe
  exist for a human ear to rate — they are not in the repo.
- **U4 gains a third arm:** `inputTranscription` from the Live session vs a `gemini-3.5-transcribe`
  pass on the same audio, both against a human transcript.

---

## Sources

- Google, LearnLM in Gemini 2.5: https://blog.google/products-and-platforms/products/education/google-gemini-learnlm-update/
- Google, LearnLM prompt guide: https://services.google.com/fh/files/misc/learnlm_prompt_guide.pdf
- Google, Guided Learning: https://blog.google/products-and-platforms/products/education/guided-learning/
- Project evidence: `STATE.md` (sync tool calls, 150 s idle close, free-tier text key), `DEVLOG.md`
  2026-09-18 (language blending, transcript drop-outs), `dictation-plan.md` §2, §5, §6.
