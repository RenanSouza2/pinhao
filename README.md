# Pi Threads

A high-performance, out-of-core, multi-processed C program to calculate Pi to billions of digits — 40,000,000,000+ digits computed to date. The project uses binary splitting combined with a custom big number implementation and parallel processing to handle extreme-scale mathematical computations.

## Features

- **Extreme Scale Calculation:** Capable of computing Pi to massive scales — 40B+ digits.
- **Out-of-core Processing:** Intelligently saves intermediate states and large numbers to disk to overcome RAM limitations, using a configurable cache threshold.
- **Multi-Process Parallelization:** Uses `fork` to parallelize chunks of the binary splitting tree, speeding up computation across multiple cores.
- **Custom Big-Number Library:** Relies on the `araucaria` submodule to handle big integer and big float arithmetic, binary splitting types (P, Q, R), and disk serialization.
- **Memory-Budgeted Scheduling:** Worker processes are launched against explicit RAM budgets, so a large run stays inside available memory instead of thrashing.
- **Modern C23:** Written using modern C23 standards with a rigorous compiler flag set (a large warning list under `-Werror`, plus address/undefined/leak sanitizers in debug builds).

## Prerequisites

- **OS:** Linux or macOS (uses POSIX `fork`, `waitpid`, and specific compiler linker flags).
- **Compiler:** `gcc` with support for C23 (`-std=c23`).
- **Disk Space:** Significant high-speed disk space (e.g., NVMe) is required for large runs. The default cache directory is mapped to `./cache`.
- **Git:** To fetch required submodules.

## Setup and Submodules

This project relies on custom git submodules for macros, utilities, and big number processing. Clone the project with submodules:

```bash
git clone --recursive https://github.com/RenanSouza2/pinhao
```
If you already cloned without submodules, run:
```bash
git submodule update --init --recursive
```

## Build Instructions

The build system is managed via standard Makefiles.

- **Build Production Executable:**
  ```bash
  make build
  # or
  make b
  ```
  The executable will be located at `src/main.out`.

- **Build Debug Executable (with Address/Undefined/Leak Sanitizers):**
  ```bash
  make dbg
  # or
  make d
  ```
  The executable will be located at `src/debug.out`.

- **Clean Project:**
  ```bash
  make clean
  # or
  make c
  ```

## Usage

By default, the scale of Pi calculation is hardcoded in `src/main.c`.
`pi()` takes a `size`, the number of processes to fork across, and three
memory budgets.

`size` is the target mantissa size of the result, in 64-bit limbs (`araucaria`
stores numbers as little-endian arrays of 64-bit limbs), **not** decimal
digits. Each limb holds roughly `log10(2^64) ≈ 19.27` decimal digits, so the
approximate digit count is:

```
digits ≈ size × 19.27
```

For example, `size = 32,000,000` limbs yields roughly 616,000,000 decimal
digits of Pi.

### Memory budgets

Every pending task in the binary-splitting tree carries an estimated memory
cost, priced for the number of threads that task will run with. The two memory
arguments bound how much the forked workers may hold at once:

- `mem_launch`: the throttle. Tasks keep launching while total estimated usage
  is below it; at or above it nothing new starts until a running task ends. It
  is the soft target a run normally sits at.
- `mem_max`: the fit ceiling. A task launches alongside others only if it fits
  under this. One that doesn't runs solo — it waits for the scheduler to empty
  and then takes the whole machine, whatever it costs, since with nothing else
  running nothing could free memory for it.

A task that doesn't fit also stops the scheduler from looking for smaller work
to launch in its place. That is deliberate: memory then drains monotonically
until the blocked task fits, instead of smaller tasks holding the pool full
against it.

Scale these together with `size`. The values committed in `main.c` are sized
for a full-scale run on a machine with plenty of RAM; leaving them there for a
small test means the memory policy never binds. Scaling down has a floor,
though: every leaf is priced at a fixed 512 MB (`TREE_LEAF_COST_BYTES`), so a
`mem_max` near or below that makes every leaf run solo and serializes the run.

`n_process` is the number of processes to fork and also the thread budget
shared across them. `main.c` clamps it down to the number of online cores
(`sysconf(_SC_NPROCESSORS_ONLN)`), so requesting more than the machine has is
harmless.

Modify `src/main.c` to adjust `size`, the process count, and the memory
budgets:

```c
// Example: calculate Pi to a size of 32,000,000 limbs (~616M digits) across
// 16 processes, throttling new launches at ~15 GB and running any task that
// doesn't fit under 20 GB on its own
uint64_t mem_launch = U64(15) * 1024 * 1024 * 1024;
uint64_t mem_max    = U64(20) * 1024 * 1024 * 1024;
pi(32'000'000, 16, mem_launch, mem_max);
```

Then build and run with the helper scripts, which capture per-thread timing
output to `thread_log/run.log`:
```bash
./run.sh        # production build
./run_debug.sh  # debug build with sanitizers
```

Every run opens with a `=== run <timestamp> | main.c <cksum> ===` marker, and
wipes `thread_log/` first. Pass `--keep` to append to the existing log instead;
`dashboard.py` resets its parser on each marker and shows the newest run:
```bash
./run.sh --keep
```

`--keep` is for stacking runs of one configuration, so it refuses when
`src/main.c` — where every `pi()` argument is a literal — has changed since the
run that wrote the log; `--force` appends anyway. The dashboard checks the same
thing exactly, from the config lines `pi_tree` logs, and warns when a run
disagrees with the one before it in the log.

While a run is in progress (or after one finishes), `./dashboard.py` renders
a live terminal dashboard from `thread_log/run.log`:
```bash
./dashboard.py [path/to/run.log] [--size N] [--n-process N]
```

### Cache file names

A cached result is named after the node that produced it, so a directory
listing reads as the shape of the tree. Fields are zero-padded so `ls` sorts
by index.

- `pieces/p_<begin>_<span>_<end>.bin` — an exact P/Q/R triple over
  `[begin, end]`. It carries no `size`: an exact triple does not depend on the
  target precision, so it is reused across runs of different sizes.
- `numbers/r_<begin>_<level>_<end>_<size>.bin` — a `size`-truncated P/Q/R over
  any range, power-of-two-wide or not. `level` is the depth below the chunk a
  chain fuses, `0` at that chunk and restarting at every fold, so two files at
  level *k* fuse into one at *k-1* wherever they sit in the run.
- `numbers/c_<chunks>_<end>_<size>.bin` — a chain node, named by how many
  chunks it has fused. It carries no `begin`, because every chain node in a run
  starts at the same index. The count runs down to 3; at two chunks the chain
  ends as an ordinary `r_` span.
- `tmp/<name of its node>.bin` — a half-finished join's `P1xR2` checkpoint,
  under exactly the name of the node it belongs to, so `comm` against
  `pieces/` or `numbers/` shows what was in flight when a run stopped.
- `res/pi_<size>.bin` — the finished value.

The run log carries the same number in its third column — a node's `level`,
or a chain's chunk count — so a log line and a filename name the node the same
way. `i_0` and `i_max`, the first two columns, identify a node on their own;
nothing keys off an absolute tree depth. `dashboard.py` labels nodes
`[level, size, first piece]`, the first piece counted in pieces from 0. A
span's size is always its `span`, the form the log lines and the cache
filenames carry; a big or chain node has no such exponent, so its size is a
count of pieces and a node whose extent is not a power of two reads as a plain
count. `p` toggles to raw indices, where a span reads `[level, span, i_0]` and
those piece counts give way to a `B` or `C` kind letter. Each rung of the
chain ladder is named by its place in the chunk sequence instead: `[C, k]` for
the single chunk that ends at boundary *k*, `[C, 0, k]` for the *k* chunks
that are one number — so a fold is named for the rung it just took in — and
`[T, pieces]` for the run's partial last chunk. An expanded rung carries the
node itself as its only child.

### Note on Disk Cache

There are two independent caching layers, and only one of them is optional:

- **pinhao's own binary-splitting cache (always on).** As the tree in
  `lib/tree`/`lib/big` splits and joins P/Q/R terms, every intermediate
  result is checkpointed to disk under `./cache/pieces/` and
  `./cache/numbers/`, and the final result under `./cache/res/`. This
  happens unconditionally — it's how the computation stays out-of-core and
  how a finished run can be reused: `pi_tree()`/`pi_finish()` check whether
  a given `size` is already stored (`pi_is_stored`) and load it straight
  from `./cache/res/` instead of recomputing, and likewise individual split
  results are skipped if already on disk (`split_big_res_is_stored`). This
  path needs no configuration and is unrelated to `araucaria`.

- **`araucaria`'s disk-backed numbers (opt-in).** By default `araucaria`
  keeps every big number's limb array on the heap, with no disk spilling
  at all (`disk_threshold_bytes` defaults to unlimited). Its purpose is
  different from pinhao's cache above: it's for the case where a *single*
  number produced during the computation is too large to fit in RAM by
  itself, not for checkpointing intermediate results. To enable it, set a
  disk config before running:
  ```c
  araucaria_disk_config_t config = {
      .disk_path            = "./cache/tmp",  // must already exist
      .disk_threshold_bytes = 8192,           // bytes; larger allocations go to disk
  };
  araucaria_disk_config_set(&config);
  ```
  Once set, any `num` allocation whose backing size (in bytes) exceeds
  `disk_threshold_bytes` is backed by an `mmap`-ed temporary file in
  `disk_path` instead of the heap. `src/main.c` sets this near the top of
  `main()`, pointed at the tracked `./cache/tmp` with a threshold of
  `mem_max / 4`. Drop the call to keep every `num` on the heap.

### Cross-Process Disk Lock

All processes read and write the `cache/*.bin` files over the same physical
disk, so concurrent I/O can be slower than serialized I/O on a spinning
drive. `config.h` gates this with `LOCK_DISK_IO`:

```c
#define LOCK_DISK_IO
```

When defined, cache reads and writes take an exclusive `flock` on
`./cache/disk.lock`, serializing disk access across all forked workers.
Comment the define out on an NVMe or SSD, where parallel I/O is the faster
choice. The run log reports which mode is active on its `disk lock` line.

### Keeping Exact Pieces

An exact `pieces/p_*.bin` triple carries no `size`, so it stays valid across
runs of any precision. `config.h` gates whether a join deletes the pieces it
consumed:

```c
#define KEEP_PIECES
```

When defined, a join leaves its children in `pieces/` for the next run to
reuse. Comment it out to reclaim the disk instead — the run then keeps only
what it is still working on.

## Project Structure

- `src/`: Main entry point and executable Makefiles. `src/main.c` currently
  wires up `lib/tree`.
- `lib/`: Core logic for binary splitting, all built on top of `araucaria`:
  - `tree`: Current implementation used by `src/main.c` — a forked
    binary-splitting tree (`pi_tree`).
  - `big`, `linear`, `split`, `union`: Earlier/alternate implementations and
    building blocks (`pi_big`, `pi_v1`/`pi_v2`/`pi_v3`, splitting and union
    number helpers) kept for reference and experimentation.
- `mods/`: Git submodules containing shared libraries:
  - `clu`: Memory management debug tool.
  - `macros`: C macros for assertions, timing, static typing, and threading.
  - `araucaria`: The custom arbitrary-precision arithmetic library (big
    integers, fixed/floating-point, disk-backed numbers).
- `makefiles/`: Shared compiler flags, environments, and linker setup.
- `config.h`: Build-time switches for the program itself — `LOCK_DISK_IO`
  and `KEEP_PIECES`.
- `cache/`: Default location for out-of-core file persistence — `pieces/`,
  `numbers/` and `res/` hold the binary-splitting checkpoints, `tmp/` holds
  half-finished joins and is also the `disk_path` for `araucaria`'s
  disk-backed numbers, and `disk.lock` is the cross-process I/O lock. See
  *Cache file names* above for the naming scheme.
- `dashboard.py`: Live terminal dashboard that visualizes a run's progress
  from `thread_log/run.log`.
