# Wake word training request — draft for espressif/esp-sr issue #88

Status: **NOT POSTED.** One open decision: whether to include the product link
(see note below). Nothing else is missing.

Link: https://github.com/espressif/esp-sr/issues/88

What the thread actually requires (checked 2026-09-08, not assumed):
- Eligibility since Aug 2024: **an ongoing project with a link + brief overview**,
  OR 5+ upvotes on the request. The overview is the selection gate, not paperwork
  — "We won't train every wake word request here. Only the most popular ones will
  be selected." (sun-xiangyu, 2025-11-12)
- **Do not post an email address** — the maintainer asked people to stop; use
  sales@espressif.com for anything private.
- **No agreement confirmation needed** — "All submissions are deemed to have
  agreed to this agreement by default." The closing line below is customary, not
  required.
- **WakeNet10 + the new TTS pipeline went live 2026-09-07** and they have just
  started working through requests. Ask for WakeNet10, not WakeNet9.
- Vietnamese is not a supported or planned TTS language, so this asks for the
  English phrase only.

Open decision: the overview below cites **https://bubumotion.vn** as the project
link. That satisfies the eligibility rule and is already a public commercial
site, but it does tie this GitHub account to the company in public. Remove the
URL if you would rather not — the overview still reads fine without it, though
the request becomes weaker.

---

### Wake word request: "Hey Bubu" — English / ESP32-S3 / WakeNet10

Hi Espressif team,

We would like to request an English wake-word model for our ongoing Bubu project.

- **Wake phrase:** `Hey Bubu`
- **Language:** English
- **Pronunciation:** "hay BOO-boo"
- **IPA:** `/heɪ ˈbuːbuː/`
- **Syllables:** 3 — Hey · Bu · bu
- **Stress:** light stress on the first "Bu"; both syllables of "Bubu" take
  /uː/ as in *boot*, roughly equal length. It is **not** "BUH-buh" or "BYOO-byoo".
- **Target chip:** ESP32-S3 (16 MB flash, 8 MB octal PSRAM)
- **Preferred model:** **WakeNet10** for ESP32-S3. We currently run esp-sr v2.3.1
  on ESP-IDF v5.5.2 and can upgrade to whichever release WakeNet10 requires.

"Bubu" is a coined name rather than a dictionary word, so we have spelled the
pronunciation out — left to the spelling alone, an English TTS voice is as likely
to produce "BUH-buh" as the sound we need.

#### Project overview

Bubu (https://bubumotion.vn) is an AI companion toy for children that we are
preparing to ship in Vietnam. The ESP32-S3 handles always-on local wake-word
detection, with a single digital I2S MEMS microphone at 16 kHz mono and no AEC
reference channel. The people saying the wake word are Vietnamese children
(roughly ages 4-10) and their parents, pronouncing an English-phonology phrase —
so if the pipeline can weight the sample set toward **Vietnamese-accented
English**, that would match our users considerably better than native speakers.

#### Why we are asking rather than using an existing model

Because no shipped model matches our phrase, we are currently on MultiNet5-en
(`mn5q8_en`) with hand-written phonemes, and it does not work. With 8 pronunciation
variants registered and the threshold floored at 0.05 (default 0.5), a controlled
run of 20 isolated utterances produced **zero detections — not even a losing
candidate** from `get_results()`. Input levels were healthy throughout (peak
1938-19479, average RMS 679-5103, **0.0% clipping on every utterance**), so this
is not a level or clipping problem. In field use the hit rate is roughly 1 in 8.

To rule out our own hardware we flashed `wn9_alexa` behind the full AFE on the
same board as a control test: it detects reliably and testers described it as
working very well. So the microphone, enclosure and audio path are all fine — the
engine is the limit. (`wn9_alexa` was used only for that qualification test and
will never be shipped.)

We are happy to test a candidate model on the real device and report back
detection rate and false-accept counts under normal household noise, including
recordings from the actual target users if that is useful to the pipeline.

We have read and accept the Wake Word Submission Agreement, and confirm we hold
the rights to use "Bubu" for this product.

Thank you!
