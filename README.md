# Pi Threads

A high-performance, out-of-core, multi-processed C program to calculate Pi to hundreds of millions of digits. The project uses binary splitting combined with a custom big number implementation and parallel processing to handle extreme-scale mathematical computations.

## Features

- **Extreme Scale Calculation:** Capable of computing Pi to massive scales (e.g., 200,000,000+ digits).
- **Out-of-core Processing:** Intelligently saves intermediate states and large numbers to disk to overcome RAM limitations, using a configurable cache threshold.
- **Multi-Process Parallelization:** Uses `fork` to parallelize chunks of the binary splitting tree, speeding up computation across multiple cores.
- **Custom Big-Number Library:** Relies on a dedicated `number` submodule to handle big integer and big float arithmetic, binary splitting types (P, Q, R), and disk serialization.
- **Modern C23:** Written using modern C23 standards with rigorous compiler flags (warnings, `-fanalyzer`, and sanitizers).

## Prerequisites

- **OS:** Linux or macOS (uses POSIX `fork`, `waitpid`, and specific compiler linker flags).
- **Compiler:** `gcc` with support for C23 (`-std=c23`).
- **Disk Space:** Significant high-speed disk space (e.g., NVMe) is required for large runs. The default cache directory is mapped to `./cache`.
- **Git:** To fetch required submodules.

## Setup and Submodules

This project relies on custom git submodules for macros, utilities, and big number processing. Clone the project with submodules:

```bash
git clone --recursive <repository-url>
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

- **Run Tests:**
  ```bash
  make test
  # or
  make t
  ```

- **Clean Project:**
  ```bash
  make clean
  # or
  make c
  ```

## Usage

By default, the scale of Pi calculation is hardcoded in `src/main.c`. 
Modify `src/main.c` to adjust the size and disk cache path:

```c
// Example: calculate Pi to a size of 200,000,000
pi(200'000'000);
```

Then compile and run:
```bash
make build
./src/main.o
```

### Note on Disk Cache
The application performs disk-based I/O for caching intermediate numbers when they exceed the `disk_threshold` (e.g., 1 GB). Be sure to configure `num_config_t` in `src/main.c` with a valid disk path before running large scale calculations.

## Project Structure

- `src/`: Main entry point and executable Makefiles.
- `lib/`: Core logic for binary splitting (`big`, `linear`, `split`, `union`).
- `mods/`: Git submodules containing shared libraries:
  - `clu`: Utility functions and logging.
  - `macros`: C macros for assertions, timing, static typing, and threading.
  - `number`: The custom big-number arithmetic and representation library.
- `makefiles/`: Shared compiler flags, environments, and linker setup.
- `cache/`: Default location for out-of-core file persistence.
