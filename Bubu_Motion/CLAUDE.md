# Bubu Motion — session instructions

## Dev log (required)

This repo keeps a running short-report log at [DEVLOG.md](DEVLOG.md).

- **At the start of a session**, read `DEVLOG.md` in full before starting other work — it's the record of what prior sessions (including ones running concurrently) did and decided.
- **At the end of a session**, or after finishing a meaningful chunk of work, append a short entry (a few bullets — not a full report) following the exact procedure documented at the top of `DEVLOG.md`. That procedure uses a lock-directory mutex and a shell append (never the Write/Edit tools on that file) specifically so two sessions logging at the same time don't overwrite or interleave each other's entries — follow it as written, don't improvise a different write method.
