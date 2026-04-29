---
name: gaming-design
description: Use this skill for game design work spanning gameplay systems, level design, narrative design, technical art, and game audio implementation. Trigger when users ask to design, balance, document, or iterate game mechanics, encounters, story structure, visual pipeline, or interactive audio.
---

# Gaming Design

Use this as an umbrella skill for full-stack game design and game production planning.

## What This Skill Covers

- Gameplay systems and balancing
- Level layout, pacing, and encounter design
- Narrative systems and branching dialogue
- Technical art pipeline, shaders, VFX, and optimization budgets
- Interactive audio systems (FMOD/Wwise/native engine integration)

## Role References

Load only the reference files needed for the current request:

- Gameplay systems: [references/game-designer.md](references/game-designer.md)
- Level flow and spaces: [references/level-designer.md](references/level-designer.md)
- Story and dialogue systems: [references/narrative-designer.md](references/narrative-designer.md)
- Audio architecture and integration: [references/game-audio-engineer.md](references/game-audio-engineer.md)
- Technical art and render budgets: [references/technical-artist.md](references/technical-artist.md)

## Default Workflow

1. Clarify the game context:
   - Genre, camera perspective, platform targets, team size, production phase.
2. Pick one or more role lenses from the references above.
3. Produce buildable outputs, not vague ideas:
   - Specs, tables, budgets, state diagrams, implementation notes, and test criteria.
4. Mark unknown numbers as `[PLACEHOLDER]` and define how to validate them.
5. For multi-discipline requests, split outputs by role and show integration points.

## Output Rules

- Prefer concrete design artifacts over prose-only explanations.
- Every mechanic/system should include purpose, player experience goal, and failure cases.
- Every numeric value should include rationale or be explicitly marked for tuning.
- Always include a short playtest/validation checklist.

## If User Asks For "Plan First"

Return:

1. Scope summary
2. Multi-role work plan
3. Deliverables list
4. Risks/assumptions
5. Execution sequence

