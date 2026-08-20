# CLAUDE.md

Guidance for Claude Code when working in this repo. See `README.md` for
project overview, build commands, and directory layout — this file only
covers what isn't obvious from the code.

## Ask before acting

Before starting work on a request, ask clarifying questions first. Don't
jump straight into editing, building, or running things — state back what
you understood the request to be, raise anything ambiguous about scope,
approach, or trade-offs, and wait for the answer before making changes.

This holds even when the request looks obvious: a short question costs far
less than a wrong change in a hot path or a long run started on the wrong
parameters. Once the answers are in, do the whole task without stopping to
re-ask about things already settled.

## Cross-platform: Linux and macOS

This project must build and run on both Linux and macOS. Avoid
platform-specific APIs unless guarded by the appropriate `#ifdef` (see the
existing Apple-platform checks around process/core counts in `src/main.c`
for the pattern), and don't assume a Linux-only toolchain or filesystem
layout when making changes.

## Submodules (`mods/`)

`mods/clu`, `mods/macros`, `mods/araucaria` are git submodules but are
editable in place from this repo when a task requires it. Committing/pushing
those submodule changes to their own remotes is handled by the user
separately — don't push submodule changes yourself.

## Validating changes — there is no test suite

This project has no automated test suite. `make test` / `make t` only builds
and runs the small `test.c` harness under each `lib/*/test/` directory (a
sanity check, not coverage) — run it if convenient but don't treat it as
sufficient.

After any non-trivial change, especially under `lib/` or `src/`:

1. `make build` (production build) — must compile clean.
2. `make dbg` (debug build, ASan/UBSan/leak sanitizer) — must compile clean.
3. If feasible, do a small end-to-end run to confirm things actually work,
   not just compile. `pi()` in `src/main.c` is currently called with a large
   size (32,000,000); for a quick check, prefer temporarily calling it with a
   much smaller size via `./run.sh` or `./run_debug.sh` rather than waiting
   on a full-scale run. A size of 1,000,000 finishes in under 2 minutes on
   16 cores and is a good default for this. Revert any temporary size change
   before considering the task done unless asked to keep it.
4. `pi()`'s second argument is the number of processes to fork — this repo
   may be running on machines with fewer cores than expected (main.c
   currently hardcodes 16 outside of Apple platforms). When adjusting it for
   a test run, prefer checking available cores (e.g. `nproc`) over assuming
   16, and don't request more processes than are actually available.

## Compiler flags are strict — keep code clean under them

`makefiles/flags.mk` enables a large, deliberate warning set (`-Wall
-Wextra -Wpedantic -Werror -Wshadow -Wconversion -Wsign-conversion
-Wnull-dereference -Wcast-qual`, etc.) plus `-fsanitize=address,undefined`
(+`leak` on Linux) in debug builds, with `-Werror`/`-Wl,--fatal-warnings`
making warnings fatal in the real build (note: `.clangd` removes `-Werror`
for editor diagnostics only — the actual Makefile build still treats
warnings as errors). Don't introduce code that only compiles clean because
a warning got suppressed or disabled — fix the underlying issue instead.

## Comments carry the rule, not the reasoning

Comments say what the code does and what rule it enforces. They are not a
record of how that rule was arrived at. When a change involved weighing
alternatives, turned up a caveat, or rested on a measurement, report it as
a short bullet list in the reply at the end of the task, and put it in the
commit message — not in the source, where it becomes noise every future
reader has to wade through.

Don't commit:

- deliberation — alternatives considered and rejected, "note this is only
  exactly true when…", caveats addressed to whoever reviews the change,
  or an account of what an earlier version of the code did.
- benchmark numbers and timings quoted as evidence for a choice. They go
  stale silently, and the commit that made the change is where they
  belong.

A comment stating a non-obvious rule, invariant, unit, or ordering
requirement is wanted. Keep those, and keep them short.

## Performance-sensitive code

`lib/big` (big-number arithmetic) and `lib/tree` (binary-splitting P/Q/R
tree, the core of the Pi computation) are hot paths — this is a
high-performance out-of-core numeric program, not typical app code. Be
careful with changes here: watch allocation patterns, memory layout, and
avoid casual refactors that add overhead (extra copies, indirection,
allocations in inner loops). If a change trades clarity for performance (or
vice versa) in these paths, flag it rather than deciding silently.

## Cache directory

`cache/` holds generated out-of-core `.bin` data files from real runs.
Treat it as build/run output — don't hand-edit or rely on its contents
being meaningful across runs. It's fine to clean generated files out of
`cache/*/` between runs, but always keep the `.gitkeep` file in each
subdirectory (e.g. `cache/numbers/.gitkeep`) — those keep the empty dirs
tracked in git and must not be deleted.
