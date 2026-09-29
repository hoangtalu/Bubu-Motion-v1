# Bubu Motion — session instructions

## Current state, then the dev log (required)

This repo keeps two records plus an archive, and the order you read them in
matters. `DEVLOG.md` was condensed on 2026-09-21, so reading both records in full
now costs roughly 11k tokens — cheap enough that there is no reason to skim.

1. **[STATE.md](STATE.md) — read this first, in full.** Short, overwritable, and
   holds what is true *now*: the hardware budget, what is deployed, the rollback
   paths, and a table of claims in `DEVLOG.md` that later work disproved. Plan
   from it. **Where the two records disagree, `STATE.md` wins.**
2. **[DEVLOG.md](DEVLOG.md) — read this next, in full.** The condensed causal
   history: one heading per day, with rejected hypotheses, reverts, releases and
   deployment decisions kept deliberately. The status tags (`PLAN`, `BUILT`,
   `FLASHED`, `DEPLOYED`, `VERIFIED`, `REVERTED`, `OPEN`) are how you tell what
   actually landed from what was only tried.
3. **`docs/archive/DEVLOG-full-through-2026-09-21.md` — do not read this.** It is
   the ~300KB pre-condensation log, kept for forensics only. `grep` it when you
   need an exact measurement, command, commit, backup name, or the hour-by-hour
   order of an incident. Reading it whole costs most of a context window.

**Why the split.** `DEVLOG.md` is append-only on purpose, so it necessarily
contains statements that later entries overturned — stale memory figures, and
hypotheses that were tested and killed by a later entry. A session that quotes an
old entry as current fact will reach a wrong conclusion, and this has already
happened. `STATE.md` exists so the current numbers have one home that is allowed
to be rewritten. The condensation keeps the negative results on purpose: an entry
saying something was `REVERTED` or `REJECTED ON HARDWARE` is not noise, it is the
reason not to try it again.

## Writing to them

- **`DEVLOG.md`: append only.** At the end of a session, or after finishing a
  meaningful chunk, add one dated heading and 1–4 short bullets — outcome, root
  cause, how it was verified, what is left. Tag the status. Never use the
  Write/Edit tools on this file and never rewrite it wholesale: that can silently
  drop an entry another session appended while you were working. Take the lock,
  then append with a shell heredoc:

  ```bash
  cd /Users/judes/Downloads/Bubu-Motion-v1-main/Bubu_Motion
  # acquire lock (mkdir is atomic); break stale locks older than 2 min (crashed session)
  while ! mkdir .devlog.lock 2>/dev/null; do
    if [ -n "$(find .devlog.lock -maxdepth 0 -mmin +2 2>/dev/null)" ]; then
      rmdir .devlog.lock 2>/dev/null
    fi
    sleep 1
  done
  trap 'rmdir .devlog.lock 2>/dev/null' EXIT

  cat >> DEVLOG.md <<'EOF'

  ## YYYY-MM-DD — short title

  - ...
  EOF
  ```

  Do not improvise another write method. Do not re-condense or reorder existing
  history unless the user explicitly asks for it.
- **`STATE.md`: overwrite in place.** When a number or a deployment fact changes,
  **replace** the line rather than adding to it, and move any newly-disproven
  claim into its "DISPROVEN or SUPERSEDED" table. Take the same `.devlog.lock`
  before writing. Update the "Last verified" date, and only from something you
  actually measured — never by copying a figure out of `DEVLOG.md`.
- **Keep `STATE.md` small.** It is the one file every session must read in full,
  so it is the one file whose growth is expensive. Anything a future session must
  not get wrong belongs here; everything else belongs in `DEVLOG.md`. When a line
  stops being current, delete it or move it to the disproven table — do not let
  it accumulate.
