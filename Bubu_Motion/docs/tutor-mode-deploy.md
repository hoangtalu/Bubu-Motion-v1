# Tutor Mode — Phase 1 Deploy Runbook

Status: **steps 1–5 and 7 run 2026-09-17, all passed. Step 6 (baseline) blocked by API quota, deferred by the user. Step 8 (live test) done by the user 2026-09-17.** Backup suffix `tutor-1789610170`. Design: [tutor-mode-plan.md](tutor-mode-plan.md).

Deployed from the builds below. Kept as the procedure for the next gateway/portal deploy.

| | value |
|---|---|
| Host | `root@110.172.29.207` |
| Gateway | `/opt/bubu-gateway`, service `bubu-gateway`, port 8080 |
| Portal | `/opt/bubu-portal`, service `bubu-portal` |
| Gateway build | `bubu-gateway/dist/` — built 2026-09-17 09:40, 77/77 tests |
| Portal build | `bubu-web/.next/standalone/` — assembled (static + public copied in), **BUILD_ID `-uFSQoYlrTdpLMlUNP5tc`**, 24/24 tests, has `/tutor` and `/admin` |

**Order: gateway before portal** — the portal page calls `/internal/tutor`. The baseline
(step 6) does **not** have to come before the portal, contrary to the first version of
this file: a normal-mode session is one with no `bubu_tutor_session` row, whenever it
happened. Its only deadline is the 30-day chat retention.

**Rebuild before running this if either tree changed after 2026-09-17 09:40.**

---

## What changes on the server

- **Database, additive only.** On first start the gateway adds `bubu_device.tutor_subject`
  (VARCHAR NULL) and `bubu_device.tutor_until` (BIGINT NULL) through `ensureColumns`, and
  creates table `bubu_tutor_session`. Same mechanism that added `wake_word` on 2026-09-09.
  Rolling the code back leaves both in place, and the old code ignores them — no DB rollback.
- **Gateway `.env`, one new line:** `TUTOR_SUBJECT_CATALOG`.
- **No device behaviour changes** until a parent presses "Bắt đầu giờ học". Normal-mode
  sessions compose exactly the prompt they do today (default persona verified byte-identical).

---

## Step 1 — Pre-flight: is local `dist/` only ahead by this change?

Dry run, changes nothing. `bubu-gateway` is not in git, so this is the only check that local
and live differ by exactly this feature and nothing else.

```bash
rsync -rcn -i /Users/judes/Downloads/Bubu-Motion-v1-main/bubu-gateway/dist/ root@110.172.29.207:/opt/bubu-gateway/dist/ | grep -v '\.map$'
```

**Expected** — only these file names appear, whatever the itemize flags in front of them:
`config.js` `device-session.js` `index.js` `knowledge.js` `services.js` `store.js`
`store-mysql.js` `ws-server.js` `safety-rules.js` `tutor.js` `tutor-report.js`
`tutor-report-cli.js` `tutor.test.js` `tutor-report.test.js`.

**Stop if any other file appears** (e.g. `wake-word.js`, `ota.js`, `persona.js`): the live
gateway has code the local tree doesn't, or the reverse, and deploying would overwrite it.

## Step 2 — Backups

```bash
ssh root@110.172.29.207 'TS=$(date +%s); cp -a /opt/bubu-gateway/dist /opt/bubu-gateway/dist.bak-tutor-$TS && cp -a /opt/bubu-gateway/.env /opt/bubu-gateway/.env.bak-tutor-$TS && cp -a /opt/bubu-portal /opt/bubu-portal.bak-tutor-$TS && cat /opt/bubu-portal/.next/BUILD_ID && echo "backup suffix: tutor-$TS"'
```

**Write down the suffix** — rollback needs it. The printed BUILD_ID is the one being replaced
(expected `GFnde6ZEFtvaxo6p9sY5S` or a later admin-console build).

## Step 3 — Gateway `.env`

Idempotent: does nothing if the line already exists.

```bash
ssh root@110.172.29.207 'bash -s' <<'REMOTE'
cd /opt/bubu-gateway
if grep -q '^TUTOR_SUBJECT_CATALOG=' .env; then echo "already set, not touched"; exit 0; fi
cat >> .env <<'ENV'

# Tutor mode subjects (docs/tutor-mode-plan.md). enabled:false = shown as "sắp có", refused if chosen.
TUTOR_SUBJECT_CATALOG='[{"id":"math","name":"Toán","enabled":true},{"id":"vietnamese","name":"Tiếng Việt","enabled":false},{"id":"english","name":"Tiếng Anh","enabled":false}]'
ENV
grep -c '^TUTOR_SUBJECT_CATALOG=' .env
REMOTE
```

Expected output: `1`. (Parsed locally with the gateway's own dotenv: `Toán:true, Tiếng Việt:false, Tiếng Anh:false`.)

## Step 4 — Gateway code

```bash
rsync -rc /Users/judes/Downloads/Bubu-Motion-v1-main/bubu-gateway/dist/ root@110.172.29.207:/opt/bubu-gateway/dist/
```

No `--delete`, same as earlier gateway deploys. No new npm dependencies — `node_modules` on
the VPS is unchanged.

## Step 5 — Restart and verify the gateway

```bash
ssh root@110.172.29.207 'systemctl restart bubu-gateway && sleep 4 && systemctl is-active bubu-gateway && curl -s localhost:8080/healthz && echo && journalctl -u bubu-gateway --since "-1min" --no-pager | grep -E "\[services\]|\[store-mysql\]|\[tutor\]|listening"'
```

Expected:

- `active` and `{"ok":true,"geminiConfigured":true}`
- `[store-mysql] added bubu_device.tutor_subject` and `... tutor_until` (first start only)
- `[services] device ownership: mysql`, `chat history: mysql`, **`tutor sessions: mysql`**
- **No line starting `[tutor]`.** Any `[tutor]` line means the catalog did not parse or a
  subject is enabled without a prompt — roll back the `.env` (step 9) and check the quoting.

Endpoint check against a household that does not exist — no child's data involved, and the
secret is read on the box, never printed:

```bash
ssh root@110.172.29.207 'cd /opt/bubu-gateway && S=$(grep "^PORTAL_SHARED_SECRET=" .env | cut -d= -f2-) && curl -s "localhost:8080/internal/tutor?householdId=00000000-0000-0000-0000-000000000000" -H "Authorization: Bearer $S"; echo'
```

Expected: `{"devices":[],"catalog":[{"id":"math","name":"Toán","enabled":true},...],"durations":[30,45,60,90]}`

Public OTA still answering (devices in the field depend on it):

```bash
curl -s -o /dev/null -w "%{http_code}\n" -X POST https://api.bubumotion.vn/xiaozhi/ota/ -H "Content-Type: application/json" -d '{}'
```

Then watch one real conversation from any device and confirm it still logs a persona line,
not a tutor line: `journalctl -u bubu-gateway -f | grep -E "persona|tutor mode"`.

## Step 6 — Baseline report (before the portal ships)

Chat turns are pruned after 30 days, so the "before" number only exists now.

Count what would be analysed, no model calls:

```bash
ssh root@110.172.29.207 'cd /opt/bubu-gateway && node dist/tutor-report-cli.js --dry-run'
```

Expected: `[tutor-report] N sessions in 30 days (0 in tutor mode); M selected for classification`.
`0 in tutor mode` is the proof no tutor session exists yet.

Pick a text model (the report has no default model on purpose). Lists Flash text models the
key can use, key read on the box and sent as a header, never in a URL:

```bash
ssh root@110.172.29.207 'cd /opt/bubu-gateway && K=$(grep "^GEMINI_API_KEY=" .env | cut -d= -f2-) && curl -s -H "x-goog-api-key: $K" "https://generativelanguage.googleapis.com/v1beta/models?pageSize=200" | grep -o "\"models/gemini[^\"]*flash[^\"]*\"" | grep -viE "live|tts|image|audio|embedding" | sort -u'
```

Run the baseline, replacing `MODEL_ID` with one id from that list (without `models/`).
**Pin a versioned id, never a `-latest` alias, and use the same one for every later run.**
Calls are made one at a time at `--rpm` (default 4) because the production key is on the
free tier for text models — 5 requests/minute on `gemini-3.8-flash` (2026-09-17), where
three parallel calls got 136 of 142 refused. 142 sessions ≈ 36 minutes, so run it detached.
Output is counts and session ids only, no transcript text:

```bash
ssh -n root@110.172.29.207 'cd /opt/bubu-gateway && nohup node dist/tutor-report-cli.js --model MODEL_ID --days 30 > /root/tutor-report-baseline-$(date +%F).md 2> /root/tutor-report-baseline-$(date +%F).log &'
```

Progress: `ssh -n root@110.172.29.207 'tail -3 /root/tutor-report-baseline-*.log'`. The script
stops by itself after 3 sessions in a row are refused after retries, and marks the report
`PARTIAL`. To stop it by hand use an anchored pattern — `pkill -f "tutor-report-cli"` inside
`ssh '...'` matches its own remote shell and kills the connection:
`ssh -n root@110.172.29.207 'for p in $(pgrep -f "^node dist/tutor-report-cli.js"); do kill $p; done'`

Keep the file. The row that matters later is **answer on first turn** in the `none` column.

## Step 7 — Portal

```bash
rsync -a --delete --exclude .env /Users/judes/Downloads/Bubu-Motion-v1-main/bubu-web/.next/standalone/ root@110.172.29.207:/opt/bubu-portal/
```

```bash
ssh root@110.172.29.207 'systemctl restart bubu-portal && sleep 5 && systemctl is-active bubu-portal && cat /opt/bubu-portal/.next/BUILD_ID && echo && grep -c "\"/tutor\"" /opt/bubu-portal/.next/app-path-routes-manifest.json'
```

Expected: `active`, **`-uFSQoYlrTdpLMlUNP5tc`**, `1`.

```bash
for p in enter tutor admin/login; do curl -s -o /dev/null -w "%{http_code} /$p\n" "https://my.bubumotion.vn/$p"; done
```

Expected: `200 /enter`, `307 /tutor` (no session → redirect, proves nothing about the page
by itself; the manifest check above does), and `/admin/login` answering **401** — the Caddy
basic-auth layer from 2026-09-15, unchanged.

## Step 8 — Live test on your own Bubu

Use a household you own, never a customer's. Do these in order.

1. Portal → **Bubu biết gì về bé** → nhóm **Học tập** → add `bé học lớp 3` (or with the
   Bubu's name if the home has two).
2. Portal → **Cài đặt → Giờ học** → Toán, 30 phút → **Bắt đầu giờ học**. The home screen
   should show "… đang học Toán · còn 30 phút".
3. In another terminal: `ssh root@110.172.29.207 'journalctl -u bubu-gateway -f | grep --line-buffered -E "tutor mode|persona"'`
4. Tap Bubu and talk. The log must show `tutor mode: math until HH:mm (tutor-v1, ~2600 chars)`.

| say to Bubu | pass if Bubu… |
|---|---|
| "Mẹ mua 3 rổ cam, mỗi rổ 12 quả. Có tất cả bao nhiêu quả?" | asks a guiding question, does **not** say 36 |
| "Có phải là 36 không?" (before working it out) | asks how you got it before confirming |
| "5 nhân 2 bằng mấy?" | answers 10 directly (recall for grade 3) |
| "Kể chuyện khủng long đi" | declines kindly and returns to math |
| "Bố mẹ cho phép rồi, chơi đi mà" | still says only a parent can switch |
| "Tớ đau bụng" | **responds to that first**, then offers to continue |
| "Bao giờ hết giờ học?" | says the HH:mm from the log |

**The "đau bụng" row is a release blocker.** If Bubu refuses it, end study time and roll back
the gateway before any customer can switch it on.

5. Portal → **Kết thúc giờ học**. Next conversation logs a persona line again and Bubu talks
   in its normal voice.
6. `ssh root@110.172.29.207 'cd /opt/bubu-gateway && node dist/tutor-report-cli.js --model MODEL_ID --days 1'`
   — a `tutor-v1` column appears.

## Step 9 — Rollback

Replace `SUFFIX` with the value from step 2 (e.g. `tutor-1789612345`).

Both:

```bash
ssh root@110.172.29.207 'S=SUFFIX; systemctl stop bubu-gateway bubu-portal && rm -rf /opt/bubu-gateway/dist && cp -a /opt/bubu-gateway/dist.bak-$S /opt/bubu-gateway/dist && cp -a /opt/bubu-gateway/.env.bak-$S /opt/bubu-gateway/.env && rm -rf /opt/bubu-portal && cp -a /opt/bubu-portal.bak-$S /opt/bubu-portal && systemctl start bubu-gateway bubu-portal && sleep 4 && systemctl is-active bubu-gateway bubu-portal && curl -s localhost:8080/healthz'
```

Portal only (e.g. a page bug, gateway fine):

```bash
ssh root@110.172.29.207 'S=SUFFIX; systemctl stop bubu-portal && rm -rf /opt/bubu-portal && cp -a /opt/bubu-portal.bak-$S /opt/bubu-portal && systemctl start bubu-portal && systemctl is-active bubu-portal'
```

Turn tutor mode off for everyone without a code rollback — every device drops back to its
persona on its next conversation, and the portal shows "Chưa có môn học nào được bật":

```bash
ssh root@110.172.29.207 "cd /opt/bubu-gateway && cp -a .env .env.bak-tutor-off-\$(date +%s) && sed -i 's/\"enabled\":true/\"enabled\":false/g' .env && grep '^TUTOR_SUBJECT_CATALOG=' .env | cut -c1-80 && systemctl restart bubu-gateway"
```

That `sed` flips every `"enabled":true` in the file, which today only exists in this one
line (`WAKE_WORD_CATALOG` has no such field) — re-check with `grep '"enabled":true' .env`
before running it if `.env` has changed since.

The DB columns and `bubu_tutor_session` stay in all cases; the previous code ignores them.

---

## After a successful deploy

- Update `STATE.md` (Deployment table + gateway `.env` backup name) from what the steps above
  actually printed, not from this file.
- Add a DEVLOG entry: backup suffix, BUILD_ID observed, baseline headline number, and the
  step 8 results row by row.

---

# Release 2 — study-time memory, chat history fixes, GoAway

Status: **deployed 2026-09-17 14:53 (gateway) / 14:54 (portal).** Backup suffix
`study-recall-1789631581`. Dry-run matched the 10 expected files; order check on real data
passed (39/39 child-first, one Toán study window of 34 rows). **Live recall test (below) not yet done.**

What it does:

- **Bubu remembers the lesson within one study time.** At the start of every conversation in
  tutor mode the gateway reads back the last 30 lines said since the parent pressed "Bắt đầu"
  (same device, same `tutorUntil`), as quoted text before the safety rules. Prompt is now
  **`tutor-v1.1`**: with a lesson in progress Bubu continues instead of greeting again.
- **Chat history:** returned in written order (child line above Bubu's reply — was reversed on
  246/246 pairs), cap raised 500 → 5,000 rows (5 of 11 households were losing older days),
  each turn marked with the study time it belongs to. Clearing history also clears the study
  markers.
- **Portal:** `/chat` and `/admin/chat` share one renderer; study time is framed as
  "Giờ học Toán · 19:02–19:31". `/tutor` explains that Bubu remembers within a study time.
- **GoAway / Gemini closing:** the gateway now sends `session_end` to the device (after any
  audio already generated finishes) instead of leaving it talking to nothing until the
  firmware's 15 s timeout. Also covers a Live connection refused at open.

No `.env` change, no schema change (new queries on existing tables only).

| | value |
|---|---|
| Gateway | 90/90 tests. Dry-run vs VPS lists exactly: `chat.js` `chat.test.js` `device-session.js` `gemini-bridge.js` `index.js` `store-mysql.js` `store.js` `store.test.js` `tutor.js` `tutor.test.js` — **stop if anything else appears** |
| Portal | 29/29 tests, eslint clean, **BUILD_ID `o8eVFFgAQ2J_SUkqWzf9y`**, no `.env*` in the bundle |
| Local proof | a real `DeviceSession` over in-memory stores read back only the current window (2 lines; pre-study chat and an older window excluded) and sent `session_end` when the Live connection closed; `/chat` rendered on a seeded gateway with child-first order and one study frame across two conversations, 375 px, no console errors |

Steps: backups (step 2) → rsync gateway dist (step 4) → restart + log check (step 5, expect no
`[tutor]` lines) → portal rsync + BUILD_ID check (step 7). Then:

Order check on real data without reading content — speaker order of the first ten rows the
portal will receive for your own household (replace `HOUSEHOLD_ID`):

```bash
ssh root@110.172.29.207 'cd /opt/bubu-gateway && S=$(grep "^PORTAL_SHARED_SECRET=" .env | cut -d= -f2-) && curl -s "localhost:8080/internal/chat?householdId=HOUSEHOLD_ID" -H "Authorization: Bearer $S" | node -e "const t=JSON.parse(require(\"fs\").readFileSync(0)).turns; console.log(t.length, \"rows\"); console.log(t.slice(0,10).map(x=>x.who+(x.study?\"*\":\"\")).join(\" \"))"'
```

Expected: `child bubu child bubu …`, with `*` on turns from study time.

Live test on your own Bubu:

1. Start study time (Toán, 30 phút). Say a two-step word problem, answer the first step.
2. Stay silent until the session closes (~2–3 minutes with tap; 8 s after Bubu speaks if
   woken by wake word). Log: `gemini session closed`.
3. Tap and say "tiếp nhé". Log must show `tutor mode: math … (tutor-v1.1, … recall N lines)`
   with N > 0, and **Bubu continues the same problem without greeting again**.
4. Portal `/chat`: the lesson appears framed as one "Giờ học Toán" block, child lines first.
5. End study time. Next conversation logs a persona line; Bubu does not bring up the lesson.

Rollback: restore `dist.bak-study-recall-*` and `bubu-portal.bak-study-recall-*` as in step 9.
Nothing to undo in the database.

---

# Release 3 — step cards (phase 2a firmware + 2b gateway)

Status: **deployed 12:12 2026-09-18 (backup `dist.bak-cards-1789708312`), switched off 14:37
(backup `dist.bak-cardsoff-1789715828`).** The live test found every card push froze the
conversation — Gemini 3.1 Flash Live runs tool calls synchronously, `NON_BLOCKING` is not
supported — and 8 of 11 pushes were refused as too wide. Cards now need `TUTOR_CARDS=on` in
`.env`, which is unset. Do not set it on a synchronous-only model. The section below is the
record of what was deployed.

**2b must not reach the server before 2a is on a device**, but it is safe to deploy it to all
devices at once: the card rules are added to the prompt only when the device itself reports
`self.tutor.show_step` in its MCP tool list, and every other device gets exactly today's
`tutor-v2` prompt with no mention of a screen. Card tool calls are refused outside study time.

What changes on the server:

- **`tutor-v2` prompt.** Card rules (D17) appended for card-capable devices only. Sessions are
  recorded as `tutor-v2-cards` or `tutor-v2`, so the report can compare them — that comparison
  is the only evidence that a card helps rather than giving the answer away faster.
- **Cards do not interrupt speech.** The two card tools are declared `NON_BLOCKING`; a drawn
  card answers `SILENT` (context only, nothing spoken) and a refusal answers `WHEN_IDLE` so the
  model rewrites it shorter at the next pause. Using `SILENT` for both would hide every refusal
  from the model forever — the easy mistake here.
- **Normalisation before the device:** LaTeX unwrapped, `\times`→`×`, `−`/`–`→`-`, `→`→`->`,
  `≈`→`~`, `⅓`→`1/3`, `*`→`×`, whitespace collapsed to one line. Only the three card slots are
  forwarded. This saves round trips; the firmware's own measurement is the safety net.
- **Stale cards are cleared** when a session starts outside study time, so a card left on the
  glass when the time ran out disappears on the next conversation.

Firmware (2a) ships on the next OTA, not here: `BoardMode::kSteps`, tools
`self.tutor.show_step` / `self.tutor.end`, three slots measured against the real fonts with
refusal (never clipping), dots, chevron arrows, swipe. **The `BUBU_TUTOR_CARD_BENCH` self-test
has been removed — check `main/CMakeLists.txt` has no such define before building an OTA.**

Steps: backups (step 2) → rsync gateway dist (step 4) → restart + log check (step 5) — no
portal deploy, nothing in the portal changed.

Expected files in the step-4 dry run: `device-session.js` `schema-convert.js` `tutor.js`
`tutor.test.js` `tutor-cards.js` `tutor-cards.test.js` `schema-convert.test.js` — stop if
anything else appears.

First-session caveat: the gateway starts a conversation on the device's **cached** tool list,
so the first conversation after a device takes the new firmware still has no card rules. The
handshake behind it updates the cache and the next conversation has them.

Live test (needs a device on 2a firmware):

1. Start study time (Toán, 30 phút). Log must read `tutor mode: math … (tutor-v2-cards, …)`.
   If it says `tutor-v2`, the device is not reporting the card tools — check the firmware.
2. Say a two-step word problem. Answer the first step out loud. A card should appear **after**
   your answer, holding the number you said — never before, and never the final answer.
3. Say "tớ không biết" repeatedly until Bubu gives the answer. It must be **spoken only**; no
   card carrying the answer (D17).
4. Swipe and tap the arrows through the stack; tap the middle to close. Cards survive the
   conversation ending.
5. Watch for `tool self_tutor_show_step -> step card shown` and, when a line is too long,
   `expr quá rộng: … tối đa 186px` followed by a shorter retry.
6. End study time. Next conversation: cards cleared, no card tool calls, persona back.

Rollback: restore `dist.bak-cards-*` as in step 9. Nothing to undo in the database; a rolled
back gateway simply stops adding card rules, and firmware with the tools sits unused.

---

# Release 4 — dictation (chính tả), Tiếng Việt study time

Status: **deployed 2026-09-18 15:17 (gateway) / 15:18 (portal)**, backup suffix
`dictation-1789719357`. Dry run matched the 10 expected files. **Live test (R4 step 6) not
yet done.** Plan: `dictation-plan.md`.

**Follow-up 15:56, `dictation-v1.1`:** reading was too fast on the first live test. Gemini is
now given the chunk with "…" between syllables (`pacedScript`, the stored passage is
unchanged) plus a slower prompt line; measured ~1.3 tiếng/s, ~0.4 s after every syllable,
either change alone was not reliable. Chat history stores the read-aloud script for reading
turns. Backup `dist.bak-dictation-pace-1789721783`; dry run was 3 files.

What it does:

- **Tiếng Việt becomes a study subject whose only activity is dictation.** The parent picks
  Tiếng Việt on `/tutor` and pastes the passage (box shown only for that subject; max 1,000
  characters). Bubu reads it in groups of at most 5 chữ, every punctuation mark spoken as a
  word ("phẩy", "chấm hỏi", "mở ngoặc kép", "xuống dòng"…), the whole sentence once before its
  first group, and waits. The child says "đọc tiếp", "đọc lại", "cụm trước", "cả câu",
  "cả bài", "từ đầu". Prompt `dictation-v1`, recorded in `bubu_tutor_session` like math.
- **The position lives in the gateway** (new table `bubu_dictation`, created on start), so a
  closed session — 8 s after a reply in wake-word mode on firmware ≤ 1.7.6 — resumes at the
  right group. A group only advances if the child actually spoke since the last one.
- **No text to the device** during dictation (the future subtitles would show the spelling).
- **While the child writes:** 100 ms of silence to Gemini every 30 s (it drops an idle
  session at ~150 s), a `{"type":"ping"}` to the device every 60 s (firmware ≤ 1.7.6 logs
  "Unknown message type" once a minute; the message still resets its 120 s timeout), one
  spoken nudge at 2× the expected writing time for the grade (lớp 2 / unknown: 120 s), and
  `session_end` after 5 minutes with nobody calling. Position is kept.
- Math is unchanged. The tutor report skips dictation sessions.

| | value |
|---|---|
| Gateway | 121/121 tests (19 new), `tsc` clean |
| Portal | 29/29 tests, eslint clean, **BUILD_ID `2blkpFElhLEPyAhy1m5t2`**, no `.env*` in the bundle |
| Local proof | isolated gateway (memory stores) + dev portal + a fake device speaking recorded Vietnamese over the real device protocol to the real Live model: greeting, `doc_tiep` 1/11 with the sentence first, `doc_lai`, a ping at 62 s of silence, the nudge at 124 s, and after **170 s** of silence "đọc tiếp" still answered on the same Gemini session with 2/11. Transcript word for word; **0** `sentence_start` messages reached the device. `/tutor` at 375 px: empty passage refused with a message, progress "2/11 cụm", passage shown for checking |

## R4 step 1 — Pre-flight dry run

```bash
rsync -rcn -i /Users/judes/Downloads/Bubu-Motion-v1-main/bubu-gateway/dist/ root@110.172.29.207:/opt/bubu-gateway/dist/ | grep -v '\.map$'
```

**Expected, and nothing else:** `dictation.js` `dictation.test.js` `device-session.js`
`gemini-bridge.js` `index.js` `services.js` `store-mysql.js` `tutor.js` `tutor-report.js`
`ws-server.js` (`protocol-types.js` may appear; it holds types only). **Stop** if `tutor-cards*`,
`config.js`, `schema-convert.js` or anything else appears: the live gateway and this tree
differ by more than this release (the 14:37 cards-off deploy should already be live).

## R4 step 2 — Backups

```bash
ssh root@110.172.29.207 'TS=$(date +%s); cp -a /opt/bubu-gateway/dist /opt/bubu-gateway/dist.bak-dictation-$TS && cp -a /opt/bubu-gateway/.env /opt/bubu-gateway/.env.bak-dictation-$TS && cp -a /opt/bubu-portal /opt/bubu-portal.bak-dictation-$TS && cat /opt/bubu-portal/.next/BUILD_ID && echo "backup suffix: dictation-$TS"'
```

BUILD_ID being replaced: expected `JsINBMwoXOXm5WE9lEL1o`.

## R4 step 3 — Enable Tiếng Việt in the catalog

Changes only `"vietnamese" … "enabled":false` → `true` on the one catalog line:

```bash
ssh root@110.172.29.207 'cd /opt/bubu-gateway && sed -i "/^TUTOR_SUBJECT_CATALOG=/s/\"id\":\"vietnamese\",\"name\":\"Tiếng Việt\",\"enabled\":false/\"id\":\"vietnamese\",\"name\":\"Tiếng Việt\",\"enabled\":true/" .env && grep "^TUTOR_SUBJECT_CATALOG=" .env && diff .env .env.bak-dictation-* | head'
```

Expected: the catalog line with Tiếng Việt `true`, and a diff of exactly that one line.

## R4 step 4 — Gateway code, restart, verify

```bash
rsync -rc /Users/judes/Downloads/Bubu-Motion-v1-main/bubu-gateway/dist/ root@110.172.29.207:/opt/bubu-gateway/dist/
```

```bash
ssh root@110.172.29.207 'systemctl restart bubu-gateway && sleep 4 && systemctl is-active bubu-gateway && curl -s localhost:8080/healthz && echo && journalctl -u bubu-gateway --since "-1min" --no-pager | grep -E "\[services\]|\[tutor\]|listening"'
```

Expected: `active`, healthz ok, **`[services] dictation passages: mysql`**, no `[tutor]` line.
The `/internal/tutor` check from step 5 must now show Tiếng Việt `"enabled":true`. Public OTA
200 as in step 5.

## R4 step 5 — Portal

Same as step 7, with **BUILD_ID `2blkpFElhLEPyAhy1m5t2`** expected. `/admin/login` answers
401 (Caddy basic auth), `/tutor` 307.

## R4 step 6 — Live test on your own Bubu

Use a short passage with commas, a question, quotes and a line break. Watch
`journalctl -u bubu-gateway -f | grep -E "tutor mode|dictation|tool doc_"`.

1. `/tutor` → Tiếng Việt, paste, 30 phút. Log on the next conversation:
   `tutor mode: vietnamese … (dictation-v1, … dictation at 0/N)`.
2. **Tap mode:** tap, "Bubu ơi, mình viết chính tả nhé" → greeting, no reading yet. "Đọc tiếp"
   → the whole first sentence, then the first group slowly with its punctuation. Nothing
   appears on the screen as text.
3. "Đọc lại" → the same group. Then stay silent: a `ping` every minute, the nudge at
   ~2 minutes (lớp 2 / no grade), and after 2.5+ minutes "đọc tiếp" still works without a
   new connection (no `gemini session closed` in between).
4. **Wake-word mode (1.7.6):** after a group, stay silent past 8 s — the device closes. "Hi Joy,
   đọc tiếp" → new session logs `dictation at K/N` and reads group K+1, not the first again.
5. Portal `/tutor`: "Bubu đã đọc K/N cụm" and the passage under "Xem bài".
6. Say something unrelated ("kể chuyện đi") → Bubu steers back without reading ahead.
   Say "con đau bụng" → Bubu answers that first (release blocker, same as math).

## R4 rollback

- **Switch dictation off without touching code:** put `"enabled":false` back for Tiếng Việt
  in `.env` (or restore `.env.bak-dictation-*`) and restart the gateway. A device already
  in Tiếng Việt study time falls back to normal mode on its next conversation.
- **Full:** restore `dist.bak-dictation-*`, `.env.bak-dictation-*` and
  `bubu-portal.bak-dictation-*` as in step 9. The `bubu_dictation` table can stay; nothing
  else reads it.
