# English curriculum v1 — the spine, the data format, two worked units

Status: **proposal for review, nothing built (2026-09-21).** This is the "chốt giáo trình" step
the user asked for before any code. Companion to [`language-tutor-plan.md`](language-tutor-plan.md);
that document owns *why*, this one owns *what is taught and in what shape*.

Written under three decisions: **L6 children first**, **L7 pronunciation is never graded**
(both 2026-09-21), and **L8 parents do not author content — they pick from a catalogue we
supply** (2026-09-22). L8 is the one that decided the shape of this document: see §1 and §3.

---

## 0. Licences — checked, and one correction to the plan

`language-tutor-plan.md` §3 listed three candidate word lists and said "check each licence before
use". Done:

**Three different things keep getting conflated. They can come from different sources, and only
one of them is hard to obtain freely:**

1. **Cấp độ** — what "beginner" means. CEFR, Vietnam's Bậc 1–6, GSE, CEFR-J.
2. **Syllabus** — which topics, in which order. MOET, Cambridge YLE's thematic list, textbooks.
3. **Word list** — which words, ideally banded by level. NGSL, CEFR-J, YLE, Oxford 3000.

Surveyed 2026-09-22, each licence read at the source rather than from a summary:

| Source | Gives us | Licence, as stated at the source | Usable? |
|---|---|---|---|
| **Chương trình GDPT môn Tiếng Anh** (TT 32/2018/TT-BGDĐT) | **syllabus** — 4 chủ điểm + named chủ đề, 600–700 từ, Bậc 1 = A1 | Government instrument; a topic list is fact, and the document itself calls the topics *gợi ý* that authors may adjust | **Yes — the spine (§3)** |
| **CEFR-J Vocabulary Profile v1.5 + Grammar Profile** (Tono Lab, TUFS) | **word list banded by level**, per (headword, part of speech) — **7,799 entries, of which 1,164 are A1**; levels are **A1 / A2 / B1 / B2** only. Plus a 500-line grammar profile (partially translated from Japanese) | "can be used for research and **commercial** purposes with no charge, provided that you cite the dataset properly"; copyright Tono Laboratory, TUFS | **Yes — fills the gap NGSL leaves.** Inspected 2026-09-23 |
| **NGSL** (2,809 words, ~92% coverage) | **frequency** — which words earn a child's time first | **CC BY-SA 4.0**, commercial use allowed, attribution + share-alike | **Yes** |
| Octanove Vocabulary Profile C1/C2 | C1/C2 vocabulary | CC BY-SA 4.0 | **Deliberately not used.** It ships in the same repo as CEFR-J but under a different licence; ignoring that one file keeps the whole CEFR-J side on **citation-only terms with no share-alike at all** |
| **Cambridge YLE** — Pre A1 Starters / A1 Movers / A2 Flyers wordlists | **the best fit on paper**: built for ages 6–12, and carries a ready-made **thematic** vocabulary list — exactly the topic→word mapping §6.6 says we must not invent | `© 2025 Cambridge University Press & Assessment`, **no licence grant of any kind** in the PDF | **Ask, don't assume** — free to download is not free to build on. See below |
| Pearson **GSE** learning objectives | can-do objectives on a 10–90 scale | Non-commercial use invited with guidelines; **commercial use: contact Pearson first** | Ask permission |
| Oxford 3000 / 5000 | word list | Oxford University Press, commercial publication | No |
| Dolch / Fry sight words | sight words | Old enough to be public domain | Free but **wrong tool** — function words for native children learning to *read*, and Bubu has no text on screen |
| SGK "Tiếng Anh 3/4/5" (Global Success, Family and Friends) | the actual lesson content | NXBGD / OUP, copyright | No |

**On Cambridge YLE specifically, because it is the one worth wanting.** Vietnamese parents recognise
"Starters / Movers / Flyers" far better than they recognise CEFR, the exam targets exactly our age
band, and the thematic list would answer the open question in §6.6 outright. But the PDF carries a
bare copyright line and grants nothing. Three honest paths: (a) a human reads it while authoring —
reading a published document is not redistribution, though producing something substantially
similar to their compilation would be; (b) **write to Cambridge for permission**, which is an
ordinary commercial conversation since they already license YLE content to publishers; (c) drop it.
What we must not do is use the *name* as a course label without the alignment — that promises a
parent something we did not build.

I am not a lawyer; this is risk reduction, not a legal opinion.

**Correction to §3: the CEFR descriptors are NOT "public".** The Companion Volume carries an
all-rights-reserved notice — no part may be reproduced or transmitted in any form without prior
written permission from the Council of Europe. Level *names* (A1, A2) are ordinary nomenclature
and fine to use; the descriptor sentences are not ours to copy. **So every "Tớ làm được…" goal in
this document is written by us**, aimed at the same level, never lifted.

**Share-alike is a real constraint, not a formality.** A file that is a selection of NGSL is an
adaptation of it. Mitigation, and the reason the format in §2 splits two ways:

- `ngsl-pool.json` and `cefrj-pool.json` — the two third-party word lists. **These are
  build-time inputs and are never shipped.** Nothing at run time consults them (§3c), so they live
  on the authoring machine beside the validator, not in the deployed gateway and not in either
  public repo. That reduces the licence question to attribution in the credits, because we
  distribute no part of either compilation.
- `unit-NN.json` — **our** work, and the only artifact that ships: which words this unit teaches,
  the Vietnamese glosses, the example sentences, the scenarios, the quiz items. Each word carries
  a boolean saying it passed both checks, never the rank and never the level band.

I am not a lawyer and this separation is a risk-reduction, not a legal opinion. If the English
curriculum is ever meant to be a commercial moat, note that **both project repos are public
today** — where the curriculum lives is an open question (§6).

---

## 1. What L6 and L7 change about the content

**L6 — children first.** No adult skin in v1. The existing machinery carries over unchanged:
tutor mode's 30/45/60/90-minute parent-controlled window, `SAFETY_RULES`, the child's name, the
"tớ / bé" address. `language-tutor-plan.md` §7's `address_term` and `mode: standing` become v2.
**The legal review of children's data (Nghị định 13/2023/NĐ-CP) stops being a Phase-2 gate and
becomes a prerequisite** — it now sits in front of the first real learner, not behind the pilot.

**L7 — pronunciation is never graded.** This is the bigger content change, and it is not only
"hide a number":

- **Bubu does not tell a child their pronunciation is wrong.** We dropped the measurement (U2)
  that would have told us whether its ear is trustworthy, so a correction is a guess, and a wrong
  correction to a 6-year-old is the worst failure this product can have.
- **It models instead of correcting.** Say the target again, clearly, alone; invite one more try;
  praise the attempt. This needs no ear at all and is sound practice.
- **Meaning is still correctable**, because meaning comes from the item data, not from hearing
  phonemes: a wrong answer to "what does *apple* mean?" is checkable, a wrong /æ/ is not.
- So the per-unit `sound_focus` field is **not** an error target. It is the sound Bubu says
  especially clearly and invites the child to echo. Same list of Vietnamese-speaker difficulties,
  opposite use.
- **Everything spoken is coverage, never score.** The portal quiz is the only number anyone sees.

**The honest consequence, stated once.** Without an ear, Bubu v1 teaches *vocabulary, patterns,
listening and speaking practice*. It does not teach pronunciation accuracy. That is a narrower
product than "gia sư tiếng Anh" implies, and the marketing must not imply otherwise.

**L8 — the parent may not speak English, and does not author anything.** They choose a course
from a list we prepared, and that is the whole of their content role. This removes the last human
who could have caught a mistake:

- **Content correctness becomes entirely our liability.** The parent cannot check it, the child
  cannot check it, and under L7 Bubu is not allowed to judge either. The named reviewer in §5.6
  stops being good practice and becomes **the single control that stands between a drafting model
  and a six-year-old**. It is now the top blocker on this feature, ahead of any code.
- **Every parent-facing string carries its Vietnamese meaning.** A progress screen that says
  "đã luyện: apple, three, thirsty" tells a non-English-speaking parent nothing. It must read
  "apple (quả táo)".
- **The quiz survives, by luck of the format already chosen.** `choice_vi` asks in Vietnamese and
  offers Vietnamese options, with the answer key stored server-side — so a parent with no English
  can run it and the app does the scoring. Had the quiz been "type the English word", L8 would
  have killed the only measurement this feature has.
- **The course choice itself must be in the parent's frame of reference** — a school grade, not a
  CEFR band or a word count. See §3.

---

## 2. What a unit is

A unit = one situation + one "Tớ làm được…" goal + 6–8 items + 1–2 patterns + one sound focus +
spiral review of earlier units. Sized for a 4–10-year-old: **8–12 minutes of voice**, not a lesson.

```jsonc
{
  "id": "en-a1-01",
  "schema": 1,
  "curriculum_version": "en-v1.0",
  "level": "A1",
  "title_vi": "Chào hỏi và tên",
  "can_do_vi": "Tớ chào được và nói được tên mình.",   // our wording, never CEFR text
  "scenario_vi": "Bubu gặp bé lần đầu ở sân chơi.",
  "sound_focus": {
    "id": "final-l",
    "model_vi": "Bubu đọc thật rõ âm cuối /l/ trong 'hello' rồi mời bé đọc lại."
  },
  "review_from": [],                  // unit ids whose due items are pulled into the warm-up
  "patterns": [
    { "id": "i-am-name", "en": "I am ___.", "vi": "Tớ là ___.",
      "example_en": "I am Linh." }
  ],
  "items": [
    {
      "id": "hello",
      "en": "hello",
      "vi": "xin chào",
      "ngsl": true,                   // verified against ngsl-pool.json at build time, rank not stored
      "say_alone": "hello",           // what Bubu speaks as its own utterance
      "example_en": "Hello! I am Bubu.",
      "example_vi": "Xin chào! Tớ là Bubu.",
      "cue_vi": "Muốn chào bạn bằng tiếng Anh thì nói thế nào?",
      "holdout_eligible": true,       // may be assigned to the held-out half (plan §2)
      "quiz": {
        "kind": "choice_vi",
        "question_vi": "“hello” nghĩa là gì?",
        "options_vi": ["xin chào", "tạm biệt", "cảm ơn"],
        "answer": 0
      }
    }
  ]
}
```

Field rules that exist for a measured reason:

- **`say_alone` is mandatory and is always a bare English phrase.** The prompt rule "target English
  is its own utterance" exists because this project has already watched the model blend English and
  Vietnamese inside one sentence (DEVLOG 2026-09-18, 2026-09-21). The tool returns the string; the
  model is told to say exactly it and nothing else in that turn.
- **`quiz` is per item, deterministic, and never generated at run time.** It is the only evidence
  the plan allows as a score (§5). Generating it live would make the answer key a model's opinion.
- **`holdout_eligible`** is what makes the taught-vs-held-out control (plan §2) implementable: the
  gateway splits a unit's eligible items per learner at random, teaches one half, and the weekly
  quiz covers both. Items a later unit depends on are `false`.
- **No `ngsl_rank`, no CEFR-J band.** Storing either would ship a slice of someone else's
  compilation inside our own file; a boolean "passed the check" does not, and nothing at run time
  needs the number.
- **`review_from` drives the warm-up**, so spiral review is data, not prompt improvisation.

---

## 3. The catalogue the parent chooses from, and what it is anchored to

### 3a. Anchor: the national programme, not an invented A1 spine

An earlier draft of this document proposed a 16-unit spine derived from generic A1 reasoning. Under
L8 that is the wrong anchor: a Vietnamese parent cannot judge "A1", but every one of them knows what
grade their child is in. So the spine is re-anchored on the **national English programme**
(Chương trình GDPT môn Tiếng Anh, ban hành kèm Thông tư 32/2018/TT-BGDĐT), read directly from the
Ministry document rather than from a summary:

- English is **compulsory in grades 3, 4 and 5** (since the 2022–2023 school year), **4 tiết/tuần,
  140 tiết/năm**.
- End of primary = **Bậc 1** of Vietnam's 6-level framework, which the document equates with **A1**.
- **Vocabulary for the whole primary cycle: about 600–700 words.**
- Content is organised as four **chủ điểm** repeated in a widening spiral, each holding several
  **chủ đề**:

| Chủ điểm | Chủ đề named in the programme |
|---|---|
| **Em và những người bạn của em** | Bản thân · Những người bạn của em · Những việc có thể làm · Hoạt động hằng ngày · Hoạt động tương lai · Thói quen, sở thích |
| **Em và trường học của em** | Trường học của em · Lớp học của em · Đồ dùng, phương tiện học tập · Thời khoá biểu và các môn học · Hoạt động học tập · Hoạt động ngoại khoá |
| **Em và gia đình em** | Ngôi nhà của em · Phòng và đồ vật trong nhà · Thành viên trong gia đình · Ngoại hình, nghề nghiệp của các thành viên · Hoạt động của các thành viên |
| **Em và thế giới quanh em** | Đồ chơi của em · Động vật · Màu sắc yêu thích · Quần áo · Chỉ đường và biển chỉ dẫn · Mùa và thời tiết · Phương tiện giao thông |

The programme states these are **gợi ý** and that authors may adjust them, so following the theme
order is alignment, not transcription. **The topic list is a syllabus, i.e. fact; the textbooks
built on it are copyrighted and stay out of this repo** — the same line already drawn for HSK
(DEVLOG 2026-09-21) and for the CEFR descriptors (§0).

**Scale correction, and it must not be glossed over.** 600–700 words across three grades is roughly
**200–230 words per grade** — far more than the ~128 the old 16-unit sketch would have covered.
Bubu should not pretend to cover a grade. It is a **supplement that drills the most speakable core
of each chủ đề**, and the portal must say so in those words, because a parent who picks "Lớp 3" will
otherwise assume their child is covered.

### 3b. The catalogue

Four courses, named the way a parent thinks. This is `ENGLISH_COURSE_CATALOG`, a gateway env
variable read at startup — the **same catalogue pattern as `WAKE_WORD_CATALOG` and
`TUTOR_SUBJECT_CATALOG`**, which means the portal hardcodes no list, a course can be added or
disabled without a portal deploy, and a course shown as "sắp có" is refused by the gateway and not
merely hidden by the UI.

| id | What the parent sees | Who it is for | Anchor |
|---|---|---|---|
| `pre` | **Làm quen tiếng Anh** — bé chưa học ở trường | ~4–7 tuổi | Chủ điểm 1 and 4 only, concrete nouns, no reading |
| `g3` | **Lớp 3** — theo chương trình Bộ GD&ĐT | lớp 3 | All four chủ điểm, first pass |
| `g4` | **Lớp 4** | lớp 4 | Same chủ điểm, widened |
| `g5` | **Lớp 5** | lớp 5 | Same chủ điểm, widened again |

- **The default is chosen for them.** The child's grade is already in the household's knowledge
  facts ("bé học lớp 3"), which tutor mode reads today. The portal preselects the matching course,
  so a parent who does not want to decide never has to.
- **Within a course, a unit is one chủ đề**, not an invented theme, so the ordering argument is the
  Ministry's rather than ours.
- **NGSL keeps its job** and it is now complementary, not competing: the programme says *which
  topic*, NGSL says *which words inside that topic are worth a child's time first*. Both checks run
  at authoring (§5).

### 3c. How the four sources combine — they are not four curricula

The most common way to misread §0 is as four competing syllabi to merge. **Three of the four are
not curricula at all.** MOET is a syllabus; CEFR-J and NGSL are word databases with no topics, no
order and no lessons; Cambridge YLE is a syllabus *and* a word list, and is the one we do not own.

They are also **not used at the same time**. Each acts at a different step of authoring, and by the
time a child hears anything, all of them have finished and the only thing left is `unit-NN.json`.

| Step | Who / what | Question it answers | Output |
|---|---|---|---|
| 1 | **MOET** | Which courses exist, which units exist, in what order, and what the parent sees | An empty shelf: course `g3` → unit "Động vật", unit "Màu sắc yêu thích", … with no words in them |
| 2 | **A person** | Which words could belong in "Động vật"? | dog, cat, bird, fish, cow, pig, elephant, monkey, tiger, hamster… |
| 3 | **CEFR-J** | Which of those are genuinely beginner? | drops what is above the course's band |
| 4 | **NGSL** | Of the survivors, which earn a child's time first? | keeps the top 6–8 |
| 5 | **A person** | The actual teaching content | Vietnamese gloss, example, cue, quiz options — original, ours |
| 6 | **The named reviewer** | Is it correct and age-appropriate? | accept / reject (§5, and the top blocker under L8) |

**MOET is the shelf, CEFR-J is the height limit, NGSL is the order things go on it, and the person
makes what goes there.** No dataset writes a lesson.

**Measured, not assumed (2026-09-23).** Ran CEFR-J v1.5 against five real MOET chủ đề:

| Chủ đề | A1 words found | Above A1 |
|---|---|---|
| Động vật | dog, cat, bird, fish, cow, pig, monkey, tiger, horse (9) | elephant=A2, duck=A2 |
| Màu sắc yêu thích | red, blue, green, yellow, black, white, orange, purple, pink, brown (10) | — |
| Đồ dùng, phương tiện học tập | book, pen, pencil, bag, ruler, desk, notebook (7) | — |
| Mùa và thời tiết | sun, rain, wind, cloud, hot, cold, snow (7) | — |
| Phương tiện giao thông | bus, car, bike, train, plane, boat (6) | **helicopter=B2, motorbike=B2** |

The last row is the conflict rule firing exactly as written above: the topic is the Ministry's, the
cut is CEFR-J's, and the unit ends at six words rather than reaching up to fill a quota. Sanity
check on scale: CEFR-J holds 1,164 A1 entries against MOET's 600–700 words for the whole primary
cycle, so the A1 band is roughly twice the size of the target and NGSL does the prioritising inside
it.

**Implementation rule, and it is not obvious — look up by (headword, part of speech), never by
headword alone.** CEFR-J bands each sense separately, and collapsing them corrupts the check in
both directions: `fish`/noun is **A1** while `fish`/verb is B1; `book`/noun A1, `book`/verb B1;
`wind`/noun A1, `wind`/verb B2; `red`/adjective A1, `red`/noun A2. A validator that takes "the
level of the word" will wrongly reject *fish* from Động vật and wrongly reject *book* from Đồ dùng
học tập — both of which are plainly A1 nouns a six-year-old needs. **This was caught by making the
mistake**: a first pass over the data collapsed the POS rows and produced a result that looked like
dataset noise until it was re-run correctly.

**When they disagree, the rule is fixed:** MOET wins on *which topic*, CEFR-J wins on *which words
inside it*. If a chủ đề turns out to hold too few beginner words, **the unit gets smaller — it never
reaches up a level to fill a quota**. A short unit is honest; a unit padded with A2 words a child
cannot use is not.

**Where Cambridge YLE would sit, if the permission in §0 is ever granted:** it collapses steps 2, 3
and 4 into one lookup, because its thematic list already maps topic → words at a stated level for
ages 6–12. It is a **shortcut, not a fifth ingredient** — it adds nothing the other three cannot
produce, it removes the guessing and most of the labour (and it is exactly what §6.6 is missing).

**At run time none of this exists.** The gateway opens `unit-NN.json` and returns exact strings. No
database lookup, no frequency check, no model deciding what to teach. That is deliberate and it is
forced by a measured constraint: Live tool calls are synchronous on every model we would ship, so a
tool that consulted a word list — let alone another model — mid-turn would freeze the conversation
the way step cards did.

## 4. Two worked units

### `en-a1-01` — Chào hỏi và tên

```jsonc
{
  "id": "en-a1-01", "schema": 1, "curriculum_version": "en-v1.0", "level": "A1",
  "title_vi": "Chào hỏi và tên",
  "can_do_vi": "Tớ chào được và nói được tên mình.",
  "scenario_vi": "Bubu và bé gặp nhau lần đầu.",
  "sound_focus": { "id": "final-l",
    "model_vi": "Bubu đọc rõ âm cuối /l/ của 'hello', rồi mời bé đọc lại một lần." },
  "review_from": [],
  "patterns": [
    { "id": "i-am-name", "en": "I am ___.", "vi": "Tớ là ___.", "example_en": "I am Bubu." },
    { "id": "what-name", "en": "What is your name?", "vi": "Bạn tên gì?",
      "example_en": "What is your name?" }
  ],
  "items": [
    { "id": "hello", "en": "hello", "vi": "xin chào", "ngsl": true,
      "say_alone": "hello", "example_en": "Hello! I am Bubu.", "example_vi": "Xin chào! Tớ là Bubu.",
      "cue_vi": "Muốn chào bạn bằng tiếng Anh thì nói thế nào?", "holdout_eligible": false,
      "quiz": { "kind": "choice_vi", "question_vi": "“hello” nghĩa là gì?",
                "options_vi": ["xin chào", "tạm biệt", "cảm ơn"], "answer": 0 } },

    { "id": "hi", "en": "hi", "vi": "chào (thân mật)", "ngsl": true,
      "say_alone": "hi", "example_en": "Hi, Linh!", "example_vi": "Chào Linh!",
      "cue_vi": "Còn cách chào ngắn hơn 'hello' thì sao?", "holdout_eligible": true,
      "quiz": { "kind": "choice_vi", "question_vi": "Cách chào ngắn, thân mật trong tiếng Anh là gì?",
                "options_vi": ["hi", "bye", "no"], "answer": 0 } },

    { "id": "bye", "en": "bye", "vi": "tạm biệt", "ngsl": true,
      "say_alone": "bye", "example_en": "Bye! See you.", "example_vi": "Tạm biệt! Hẹn gặp lại.",
      "cue_vi": "Lúc chia tay thì nói gì?", "holdout_eligible": true,
      "quiz": { "kind": "choice_vi", "question_vi": "“bye” nghĩa là gì?",
                "options_vi": ["tạm biệt", "xin chào", "cảm ơn"], "answer": 0 } },

    { "id": "name", "en": "name", "vi": "tên", "ngsl": true,
      "say_alone": "name", "example_en": "My name is Bubu.", "example_vi": "Tên tớ là Bubu.",
      "cue_vi": "Từ 'tên' trong tiếng Anh là gì?", "holdout_eligible": false,
      "quiz": { "kind": "choice_vi", "question_vi": "“name” nghĩa là gì?",
                "options_vi": ["tên", "nhà", "bạn"], "answer": 0 } },

    { "id": "i", "en": "I", "vi": "tớ / tôi", "ngsl": true,
      "say_alone": "I", "example_en": "I am happy.", "example_vi": "Tớ vui.",
      "cue_vi": "Muốn nói về chính mình thì dùng từ nào?", "holdout_eligible": false,
      "quiz": { "kind": "choice_vi", "question_vi": "“I” nghĩa là gì?",
                "options_vi": ["tớ", "bạn", "nó"], "answer": 0 } },

    { "id": "am", "en": "am", "vi": "thì, là (đi với I)", "ngsl": true,
      "say_alone": "am", "example_en": "I am Bubu.", "example_vi": "Tớ là Bubu.",
      "cue_vi": "Sau 'I' thì dùng từ nào để nói 'là'?", "holdout_eligible": false,
      "quiz": { "kind": "choice_vi", "question_vi": "Câu nào đúng?",
                "options_vi": ["I am Bubu.", "I is Bubu.", "I are Bubu."], "answer": 0 } },

    { "id": "you", "en": "you", "vi": "bạn", "ngsl": true,
      "say_alone": "you", "example_en": "You are my friend.", "example_vi": "Bạn là bạn của tớ.",
      "cue_vi": "Muốn gọi người đang nói chuyện với mình thì dùng từ nào?", "holdout_eligible": true,
      "quiz": { "kind": "choice_vi", "question_vi": "“you” nghĩa là gì?",
                "options_vi": ["bạn", "tớ", "họ"], "answer": 0 } },

    { "id": "my", "en": "my", "vi": "của tớ", "ngsl": true,
      "say_alone": "my", "example_en": "My name is Linh.", "example_vi": "Tên của tớ là Linh.",
      "cue_vi": "Muốn nói 'của tớ' thì dùng từ nào?", "holdout_eligible": true,
      "quiz": { "kind": "choice_vi", "question_vi": "“my name” nghĩa là gì?",
                "options_vi": ["tên của tớ", "tên của bạn", "tên bạn ấy"], "answer": 0 } }
  ]
}
```

**How one session of this unit sounds** (the gateway returns each line; the model speaks it):

1. Warm-up — none, this is unit 1.
2. Teach `hello`: Bubu says `hello` alone → "Nghĩa là xin chào." → `Hello! I am Bubu.` → "Bé thử nói *hello* xem nào." → bé nói → "Hay quá." *(no judgement of how it sounded — L7)*
3. Same for `hi`, `bye`, `name`.
4. Recall without cue: "Muốn chào bạn bằng tiếng Anh thì nói thế nào?"
5. Pattern `I am ___.` with the child's own name.
6. Role-play: Bubu is a new friend at the playground; two exchanges.
7. Close: "Hôm nay bé làm được: chào và nói tên mình. Lần sau tớ dạy bé nói mấy tuổi."

### `en-a1-02` — Tớ mấy tuổi (abridged — numbers are one chunk)

```jsonc
{
  "id": "en-a1-02", "schema": 1, "curriculum_version": "en-v1.0", "level": "A1",
  "title_vi": "Tớ mấy tuổi",
  "can_do_vi": "Tớ đếm được đến mười và nói được tớ mấy tuổi.",
  "scenario_vi": "Bé và Bubu khoe tuổi với nhau.",
  "sound_focus": { "id": "final-n-ng",
    "model_vi": "Bubu đọc rõ đuôi /n/ của 'ten' và /ŋ/ của 'young', rồi mời bé đọc lại." },
  "review_from": ["en-a1-01"],
  "patterns": [
    { "id": "i-am-age", "en": "I am ___ years old.", "vi": "Tớ ___ tuổi.",
      "example_en": "I am six years old." },
    { "id": "how-old", "en": "How old are you?", "vi": "Bạn mấy tuổi?",
      "example_en": "How old are you?" }
  ],
  "items": [
    { "id": "one", "en": "one", "vi": "một", "ngsl": true, "say_alone": "one",
      "example_en": "I have one ball.", "example_vi": "Tớ có một quả bóng.",
      "cue_vi": "Số 1 tiếng Anh là gì?", "holdout_eligible": false,
      "quiz": { "kind": "choice_vi", "question_vi": "“one” là số mấy?",
                "options_vi": ["1", "2", "4"], "answer": 0 } }
    // two … ten follow the same shape; 'two' and 'four' are holdout_eligible,
    // the rest are not, because unit 14 counts on them.
  ]
}
```

---

## 5. Authoring and acceptance — content is reviewed, never trusted from a model

A text model may **draft** glosses, examples and quiz distractors; nothing reaches a child
unreviewed. A unit is accepted only when all of these pass:

1. **Schema valid** and every required field present.
2. **NGSL check**: every content word (not proper nouns, not the child's name) is in
   `ngsl-pool.json`. A word outside it needs a written reason in the PR/notes.
2b. **CEFR-J check**: the word, **at the part of speech this unit teaches it in**, is at or below
   the course's band. Looking it up by headword alone is a bug, not a shortcut (§3c).
3. **No word is used before it is taught** — `example_en` and `patterns` may only contain items
   from this unit or an earlier one. A script enforces this across the whole spine; it is the rule
   most easily broken by hand.
4. **`say_alone` is bare English**, no Vietnamese, no punctuation the model would read aloud.
5. **Quiz answer key is unambiguous** — exactly one option is correct, distractors are plausible
   and are words the child has met.
6. **A named human with good English signs off.** `language-tutor-plan.md` §10 asks who this is and
   it is still unanswered; it blocks content, not the harness.
7. **Child-safety read**: scenarios stay inside the world a 4–10-year-old lives in, and nothing
   contradicts `SAFETY_RULES`.

Cost estimate at free-tier quota, for drafting only: 16 units × ~8 items, batched, is well under a
hundred `generateContent` calls — fine at `gemini-3.5-flash-lite`'s measured ~13–15 RPM (STATE),
and not the reason a paid key is needed. The paid key is for grading and reports, later.

---

## 6. Open, and who decides

1. **Where does the curriculum live?** Both repos are public. The gateway is not in git at all
   today, so `bubu-gateway/curriculum/en/` inherits that and is deployed by rsync like `dist/` —
   which is my default, but it also means **the curriculum has no version control and no backup**,
   in a project whose sharpest open risk is already "no database backup" (STATE). Under L8 this
   gets sharper: the curriculum is now the product, not an input to it. Decide deliberately.
2. **The quiz runs on the parent's phone, and a 4–10-year-old cannot take it alone.** With L7, the
   quiz is the *only* number we will ever show — so the entire measurement of this feature depends
   on a parent sitting down weekly with their child. L8 makes the ask heavier still: that parent
   may not read English, so the quiz must work end to end in Vietnamese (it does, §1) and must
   never require them to judge an English answer. Biggest adoption risk in the plan, and not an
   engineering problem. A voice quiz on the device is the obvious escape and is **not** a
   substitute for the headline number — a transcript this project has already measured as dropping
   words cannot be the answer key.
3. **Children's-data legal review** (Nghị định 13/2023/NĐ-CP) is now a prerequisite, not a gate.
4. **Who reviews content** (§5 item 6) — still unnamed.
5. **How much do we author before measuring?** Recommendation: **one course (`g3`), three units,
   fully authored and reviewed**, then Phase 0 against real content. Authoring four courses before
   the model has said one unit out loud would repeat the step-cards mistake at four times the cost.
6. **Does the MOET anchor need a teacher's eye?** The theme list is read straight from the Ministry
   document, but which words belong to each chủ đề per grade is a textbook-level detail I have not
   verified and should not invent. A primary English teacher settles that in an afternoon; guessing
   it produces a course that says "Lớp 3" and is not. **Cambridge YLE's thematic list would answer
   this off the shelf** — which is the strongest argument for making the permission request in §0.
7. **Is the Cambridge permission request worth making?** It is a letter, not a project, and it
   would both settle §6.6 and unlock a second course axis parents already recognise. Someone has to
   decide whether to send it.

## Sources

- NGSL: https://www.newgeneralservicelist.com/new-general-service-list — "New General Service List
  by Browne, C., Culligan, B., and Phillips, J. is licensed under a Creative Commons
  Attribution-ShareAlike 4.0 International License."
- CEFR Companion Volume (all rights reserved; reproduction needs written permission):
  https://rm.coe.int/cefr-companion-volume-with-new-descriptors-2018/1680787989
- CEFR-J Vocabulary / Grammar Profile (Tono Lab, TUFS; free for research **and commercial** use
  with citation), packaged at https://github.com/openlanguageprofiles/olp-en-cefrj — the citation
  the terms require: *The CEFR-J Wordlist Version 1.5. Compiled by Yukio Tono, Tokyo University of
  Foreign Studies.* Ship this line in the product credits.
- Cambridge YLE wordlists (© 2025 Cambridge University Press & Assessment, no grant):
  https://www.cambridgeenglish.org/Images/506166-starters-movers-flyers-word-list-2025.pdf
- Pearson GSE terms of use (commercial use: contact first):
  https://www.pearson.com/languages/en-us/why-pearson/the-global-scale-of-english/using-the-gse.html
- Chương trình GDPT môn Tiếng Anh, Thông tư 32/2018/TT-BGDĐT (chủ điểm/chủ đề cấp tiểu học,
  600–700 từ, Bậc 1 = A1, 140 tiết/năm):
  https://dienbien.edu.vn/uploads/doi-moi-chuong-trinh-gdpt/22ct_tieng-anh-3_12.pdf
