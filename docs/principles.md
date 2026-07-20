# Development Principles

Wixen Terminal is judged by four questions. Ask them of every change.

## What is it for?

Making the command line legible to people who can't see it. The terminal is one of
the most powerful surfaces in computing and one of the least accessible — a flat
stream of characters where a screen reader has to guess where the prompt is, where a
command ended, whether it failed. Wixen treats terminal output as structured data
(command blocks, exit codes, errors, tables, hyperlinks, prompts) and exposes that
structure through UI Automation, so a blind or low-vision user is a first-class user
rather than one the tool merely tolerates.

Everything else — GPU rendering, tabs, panes, tear-off, shell integration, the
default-terminal handoff — is either in service of that goal or the price of being a
credible terminal, so that accessibility is never a reason to accept a worse tool.

## What does it strengthen?

The independence of the people it is for: the ability to work at the command line
without a sighted intermediary or a degraded, character-by-character experience. And
structurally, the principle that the *application* declares its meaning (OSC 133 says
"this is a command, this is its output, it exited 1") rather than leaving the screen
reader to reverse-engineer it. Truth moves to where truth lives.

## What does it replace?

For its user, it replaces a terminal that is technically usable with a screen reader
but painful. With the handoff work, it is built to replace conhost and Windows
Terminal as the default handler console apps hand off to.

It does **not** replace the screen reader or the Terminal Access for NVDA add-on.
Those handle what the screen reader should do — review cursors, say-all, echo
preferences. Wixen handles what the application should expose. They are complements.
A tool that tried to be both would do one of them badly.

## What does it allow to be done poorly?

This question is the source of our guardrails. A rich accessibility surface makes it
dangerously easy to mistake *structure present* for *experience good*.

- **Accessibility as a checklist.** Exposing UIA nodes can feel like being accessible
  while being unusable. We built table nodes a screen reader could navigate to that
  then resolved to nothing; we shipped a COM bug that froze NVDA entirely, found only
  by running it, not by any test.
- **Feature sprawl as progress.** Breadth invites doing many things adequately instead
  of a few excellently. Whole subsystems have been implemented, tested, and wired to
  nothing — dead code that reads as a feature.
- **Feedback tuned badly.** A system that can announce everything can flood a user into
  uselessness, or under-inform: a mode-off cue that sounded identical to mode-on told
  the user nothing while appearing to.
- **Foot-guns.** Making an unverified terminal the system default; running a hostile
  config on launch. Inherent to the power; mitigated by graceful fallback and
  sandboxing, never eliminated.
- **Absorbing upstream failures.** The better Wixen is at making an opaque surface
  legible, the easier it is for app authors above it to stay inaccessible and never
  notice.

## Guardrails

1. **No feature is done until it runs in production.** Not when it compiles and unit
   tests pass — when a non-test path reaches it and it is exercised end to end. Verify
   reachability before claiming completion.
2. **Accessibility isn't done until a screen reader confirms it.** Tests prove
   structure; only a real NVDA/Narrator run proves experience. Nodes built but not
   surfaced, or surfaced but not built, are worse than absent.
3. **No stubs presented as complete.** If it can't be finished, say so and gate it.
   Document the boundary; don't fake the payoff.
4. **Feedback must be distinct and bounded.** Cues distinguishable from their siblings;
   no flooding under high output. Verify both when touching those paths.
5. **Dangerous capability stays gated and sandboxed.** Explicit opt-in, graceful
   degradation, sandboxed against hostile input.
6. **Prefer few things excellent over many adequate.** For any new subsystem: is it
   wired, tested end to end, and does it raise the bar for the whole?
7. **Don't silently absorb upstream failures.** Note where Wixen papers over a broken
   or inaccessible program. The goal is a better ecosystem, not hidden gaps.

The throughline: our strength — turning an opaque surface into declared structure — is
exactly what lets us mistake structure for experience. The one thing the tool can never
do for us is sit down with a screen reader and actually use it. So we do that.
