# Bubu Motion — session instructions

## Current state, then the dev log (required)

This repo keeps two records, and the order you read them in matters.

1. **[STATE.md](STATE.md) — read this first.** Short, overwritable, and holds what
   is true *now*: the hardware budget, what is deployed, and a list of claims in
   `DEVLOG.md` that later work disproved. Read it before you plan anything.
2. **[DEVLOG.md](DEVLOG.md) — read this next, in full.** The append-only record of
   what each session did and decided, including sessions running concurrently.

**Why the split.** `DEVLOG.md` is append-only on purpose, so it necessarily
contains statements that later entries overturned — stale memory figures, and at
least one hypothesis that was tested and killed by the very next entry. A session
that quotes an old entry as current fact will reach a wrong conclusion, and this
has already happened. `STATE.md` exists so the current numbers have one home that
is allowed to be rewritten.

## Writing to them

- **`DEVLOG.md`: append only.** At the end of a session, or after finishing a
  meaningful chunk, add a short entry (a few bullets — not a full report) using
  the exact procedure documented at the top of that file. It uses a lock-directory
  mutex and a shell append (**never** the Write/Edit tools on that file) so two
  sessions logging at once cannot overwrite or interleave each other. Follow it as
  written; do not improvise another write method.
- **`STATE.md`: overwrite in place.** When a number or a deployment fact changes,
  **replace** the line rather than adding to it, and move any newly-disproven
  claim into its "DISPROVEN or SUPERSEDED" table. Take the same `.devlog.lock`
  before writing. Update the "Last verified" date, and only from something you
  actually measured — never by copying a figure out of `DEVLOG.md`.
