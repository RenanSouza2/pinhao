# Changes in v10 Branch

Based on the contents of the `diff.txt` file, here is everything implemented and changed in the `v10` branch:

### 1. Build System & Makefiles Refactoring
- **New `bundle.mk`**: Added a new bundle makefile to handle the compilation of consolidated objects (`lib.o`, `debug.o`).
- **Compiler Flags (`flags.mk`)**:
  - Added stricter warning flags (e.g., `-Wimplicit-fallthrough`, `-Wfloat-equal`, `-Wredundant-decls`, `-Wdouble-promotion`, `-Wswitch-enum`).
  - Separated flags into specialized variables: `FLAGS_CMP` (compile), `FLAGS_LNK` (link), and `FLAGS_EXE` (executable).
  - Increased fortification in production flags to `-D_FORTIFY_SOURCE=3` and added `-ffunction-sections -fdata-sections`.
  - Added OS-specific compilation rules (PIE and GCC analyzer for Linux, dead stripping for Darwin/macOS).

### 2. Library Reorganization
- **Module Consolidation**: The `split` and `union` modules were moved inside the `lib/linear/` directory. The structure is now `lib/linear/linear`, `lib/linear/split`, and `lib/linear/union`.
- **Renaming**: The function `split` was renamed to `split_union` and `split_join` to `split_union_join` to disambiguate from other split implementations.

### 3. Core Algorithm (`lib/big`) Optimizations
- **Type Replacements**: Replaced the use of `union_num_t` with `flt_num_t` and `fxd_num_t` across multiple functions. The `pi_big` function now returns a fixed-point number (`fxd_num_t`) instead of a floating-point number.
- **Math Reductions**:
  - The calculation in `split_union` and `split` now uses the multiplier factor `(int128_t)2 * i_0 + 1` (previously `4 * i_0 + 2`), reducing by a factor of 2.
  - The final scaling of Pi now multiplies by 3 instead of 6 to account for the reduction in the split phase.
- **Cache & Terminology Update**: 
  - Renamed internal functions related to saving and loading chunked results: `sig_res` -> `piece` and `union_res` -> `term`.
  - Renamed the cache subdirectories correspondingly: `cache/numbers` -> `cache/pi`, `cache/pieces` -> `cache/piece`, and `cache/res` -> `cache/term`.
- **Logic Improvements**:
  - Introduced `piece_can_convert`, `term_convert_item`, and `term_convert` to handle intermediate type conversions effectively.
  - Optimized `split_join` and added a fallback for simple joining cases (`split_join_is_simple`).

### 4. Miscellaneous Additions
- **`.gitignore`**: Added exceptions for cache folder gitkeeps (`!cache*/**/.gitkeep`) and `.txt` outputs (`**/out*.txt`), while removing the `runner` exception.
- **`report` file**: Created a new file to log benchmarking results for specific `v10` releases (time and memory usage).
- **`src/main.c`**: Cleaned up by removing the `time_1` benchmarking function, updating `pi()` to consume the new `fxd_num_t` type, and adjusting commented-out `prepare` parameters.
- **Git Submodules**: Updated hashes for the `clu`, `macros`, and `number` submodules.