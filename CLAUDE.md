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

This project must build and run on both Linux and macOS. There are no
`#ifdef __APPLE__` branches in `src/` or `lib/` — the entire platform split
lives in `makefiles/flags.mk`, in the two `ifeq ($(shell uname -s),...)`
blocks. Linux adds warnings macOS doesn't (`-Wduplicated-cond`,
`-Wcast-align=strict`, `-Wtrailing-whitespace`, `-Wleading-whitespace=spaces`,
`-Walloc-zero`, ...) plus `-fsanitize=leak`; macOS adds its own clang-only
set (`-Wshadow-all`, `-Wcomma`, `-Wcovered-switch-default`, ...). Code can
therefore compile clean on one platform and fail on the other. Avoid
platform-specific APIs unless guarded, and prefer the POSIX call that works
on both (`src/main.c` reads the core count with
`sysconf(_SC_NPROCESSORS_ONLN)` rather than a Linux-only query). Keep
whitespace clean too — those whitespace warnings are Linux-only and fatal.

## Submodules (`mods/`)

`mods/clu`, `mods/macros`, `mods/araucaria` are git submodules but are
editable in place from this repo when a task requires it. Committing/pushing
those submodule changes to their own remotes is handled by the user
separately — don't push submodule changes yourself.

## Comments are terse

Comments here label what the code can't say on its own: field names on
otherwise opaque reads (`// sig_num.count`), units, out-parameter layout
(`// out vector length 3, returns P, Q, R in that order`).

Do not write multi-line prose blocks that explain rationale, restate what
the function does, justify a design choice, or describe how one function
relates to another — no "Same as X, but the caller picks ...", no "see
derivation in conversation". They have been stripped out of this repo and
out of `mods/araucaria` more than once; don't reintroduce them. If a change
genuinely needs that much explanation, it belongs in the commit message or
in this file, not above the code.

## Validating changes — there is no test suite

This project has no automated test suite. `make test` / `make t` only builds
and runs the small `test.c` harness under each `lib/*/test/` directory (a
sanity check, not coverage) — run it if convenient but don't treat it as
sufficient.

After any non-trivial change, especially under `lib/` or `src/`:

1. `make build` (production build) — must compile clean.
2. `make dbg` (debug build, ASan/UBSan/leak sanitizer) — must compile clean.
3. If feasible, a small end-to-end run via `./run.sh` or `./run_debug.sh`, to
   confirm things actually work and not just compile.

### Never start a full-scale run on your own

`main()` calls `pi()` with whatever size is currently committed, which is
large (hundreds of millions of limbs) and runs for hours. A run at that size
— and any debug-build run, which is far slower and sanitizer-heavy — can
wedge the machine. Check `nproc`, and confirm with the user, before starting
one.

### Shrinking `pi()` for a smoke test

`pi(size, n_process, mem_launch, mem_max)`:

- `size` — target mantissa size in 64-bit limbs, **not** decimal digits.
  1,000,000 finishes in under 2 minutes on 16 cores and is a good default
  for a smoke test.
- `n_process` — processes to fork. `main.c` already clamps this to
  `sysconf(_SC_NPROCESSORS_ONLN)`, but still check `nproc` rather than
  trusting the committed value (currently a hardcoded 16, on every
  platform); don't request more than are actually available.
- `mem_launch` / `mem_max` — the scheduler's launch band, in bytes (see
  `node_can_launch` / `scheduler_has_memory` in `lib/tree/code.c`). New
  tasks are admitted only while estimated usage is below `mem_launch`; an
  admitted task may overshoot into `mem_launch..mem_max`, which is never
  crossed. The committed values are sized for a full run, so scale them
  down along with `size` — otherwise every task launches immediately and
  the run says nothing about scheduling. Don't scale `mem_max` to within
  reach of the fixed `TREE_LEAF_COST_BYTES` (512 MB) leaf cost, or every
  leaf runs solo and the test serializes.

Revert any temporary change to these before considering the task done,
unless asked to keep it.

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

## Run logs and `dashboard.py`

`./run.sh` and `./run_debug.sh` tee stderr to `thread_log/run.log`, and
`./dashboard.py` renders a live terminal view by parsing that log.

The log format is a contract. The `tprintf` format strings and phase labels
in `lib/big/code.c` (the `LOG_*` macros) and `lib/tree/code.c` are exactly
what the dashboard's parser matches on. Renaming a label, reordering a
column, or adding a field breaks the dashboard silently — nothing fails to
build — so update `dashboard.py` in the same change.

## Cache directory

`cache/` holds generated out-of-core `.bin` data files from real runs.
Treat it as build/run output — don't hand-edit or rely on its contents
being meaningful across runs. `cache/disk.lock` is generated too: it's the
lockfile backing the cross-process I/O serialization gated by `LOCK_DISK_IO`
in `config.h`.

It's fine to clean generated files out of `cache/*/` between runs, but always
keep the `.gitkeep` file in each subdirectory (`numbers/`, `pieces/`, `res/`,
`tmp/`) — those keep the empty dirs tracked in git and must not be deleted.

The commented-out `araucaria_disk_config_t` example near the top of `main()`
names `/mnt/wsl/workspace/tmp`, which does not exist. The real scratch
directory in this repo is `cache/tmp` (README documents it correctly) — use
that if enabling araucaria's disk-backed numbers.

## Line endings

The tree has mixed line endings and no `.gitattributes` — `src/main.c` and
`lib/big/code.c` are CRLF, `lib/union/code.c` is LF, and so on. Preserve
whatever a file already uses; don't let an editor or a `sed`/format pass
normalize a whole file, or the diff stops being reviewable.
