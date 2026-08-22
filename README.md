# Pi Threads

A high-performance, out-of-core, multi-processed C program to calculate Pi to billions of digits — 10,000,000,000+ digits computed to date. The project uses binary splitting combined with a custom big number implementation and parallel processing to handle extreme-scale mathematical computations.

## Features

- **Extreme Scale Calculation:** Capable of computing Pi to massive scales — 10B+ digits.
- **Out-of-core Processing:** Intelligently saves intermediate states and large numbers to disk to overcome RAM limitations, using a configurable cache threshold.
- **Multi-Process Parallelization:** Uses `fork` to parallelize chunks of the binary splitting tree, speeding up computation across multiple cores.
- **Custom Big-Number Library:** Relies on the `araucaria` submodule to handle big integer and big float arithmetic, binary splitting types (P, Q, R), and disk serialization.
- **Modern C23:** Written using modern C23 standards with rigorous compiler flags (warnings, `-fanalyzer`, and sanitizers).

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
  The executable will be located at `src/main.o`.

- **Build Debug Executable (with Address/Undefined/Leak Sanitizers):**
  ```bash
  make dbg
  # or
  make d
  ```
  The executable will be located at `src/debug.o`.

- **Clean Project:**
  ```bash
  make clean
  # or
  make c
  ```

## Usage

By default, the scale of Pi calculation is hardcoded in `src/main.c`.
`pi()` takes a `size` and the number of processes to fork across.

`size` is the target mantissa size of the result, in 64-bit limbs (`araucaria`
stores numbers as little-endian arrays of 64-bit limbs), **not** decimal
digits. Each limb holds roughly `log10(2^64) ≈ 19.27` decimal digits, so the
approximate digit count is:

```
digits ≈ size × 19.27
```

For example, `size = 32,000,000` limbs yields roughly 616,000,000 decimal
digits of Pi.

Modify `src/main.c` to adjust `size`, the process count, and the disk cache
path:

```c
// Example: calculate Pi to a size of 32,000,000 limbs (~616M digits)
// across 16 processes
pi(32'000'000, 16);
```

Then build and run with the helper scripts, which capture per-thread timing
output to `thread_log/run.log`:
```bash
./run.sh        # production build
./run_debug.sh  # debug build with sanitizers
```

While a run is in progress (or after one finishes), `./dashboard.py` renders
a live terminal dashboard from `thread_log/run.log`:
```bash
./dashboard.py [path/to/run.log] [--size N] [--n-process N]
```

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
  `disk_path` instead of the heap. Configure this in `src/main.c` (see the
  commented-out example near the top of `main()`, whose `disk_path` is a
  machine-specific placeholder — point it at a directory that exists, such
  as the tracked `./cache/tmp`) only if a single large-scale run is expected
  to exceed available RAM. It's not required just to run the program.

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
- `cache/`: Default location for out-of-core file persistence.
- `dashboard.py`: Live terminal dashboard that visualizes a run's progress
  from `thread_log/run.log`.
