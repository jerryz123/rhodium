<!-- Explains the ordered downstream fixes applied to the pinned Bringup-Bench submodule. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Bringup-Bench patches

The pinned [`../bringup-bench/`](../bringup-bench/) checkout remains pristine.
[`series`](series) is applied to a build-local copy with the shared
[`patched_submodule.py`](../../riscv/patched_submodule.py) materializer.
The patch and upstream revision both participate in the ELF cache identity.

`0001-serialize-lz77-coding-words-as-bytes.patch` replaces the upstream
`lz-compress` casts at odd byte offsets with bytewise coding-word accesses.
It preserves the existing compressed format and output-hash oracle while
allowing targets that trap misaligned halfword accesses to run the workload.

`0002-bound-expensive-workloads.patch` bounds checkers (one depth-two minimax
search), lz-compress, pi-calc, and rho-factor. The adapter always enables these
source bounds for its single functional suite. The replacement output hashes
are owned by `build-bringup-bench.py`, not by upstream `.hash` files.
Recalibrate them against an independent host run and verify the resulting ELFs
on standalone Spike and a SoC simulator whenever the patch or compiler changes.

`0003-bound-remaining-long-workloads.patch` scales nine more expensive
programs: Ackermann's sampled grid, Connect Four minimax depth, torus frames
and sampling, HighLife steps, matrix size, N-Queens board size, RANSAC points
and iterations, Sudoku puzzle difficulty, and Tetris moves. Their replacement
hashes follow the same independent-calibration rule.
For Ackermann's `AMAX=4` sample, the reachable largest memo-table row is 12;
the patch allocates 64 rows with the upstream bounds check retained. This
avoids zeroing an otherwise unused 4 MiB table at bare-metal startup.

`0004-remove-unused-anagram-argv-read.patch` removes an unused process-argument
read that traps in a bare-metal startup and bounds the embedded dictionary to
512 entries. Its output hash is recalibrated for the smaller search.

`0005-bound-numeric-workloads.patch` reduces deterministic gene, IDCT,
cellular-automaton, Monte Carlo, N-body, activation, game, and random-statistics
samples while retaining their original algorithm and output-hash checks.

`0006-bound-bubble-sort.patch` sorts 128 deterministic values in the functional
profile instead of the upstream default of 256. It retains the complete
sortedness check and uses a replacement output hash calibrated by the native
hash-alone host build before target simulation.

`0007-reduce-functional-inputs.patch` further bounds twelve workloads that
previously exceeded one million retired instructions on `simple-spike-rva23`:

| Workload | Bounded input; retained coverage |
|---|---|
| banner | Width 64 through its existing option; complete original message |
| bloom-filter | 256 items; linear membership reference for every query |
| congrad | 16-by-16 grid; 25 conjugate-gradient iterations and solution checksum |
| connect4-minimax | Legal six-ply opening; depth-two self-play through game completion |
| donut | Angular increments 0.35 and 0.10; one complete frame |
| knights-tour | Legal eight-square prefix; backtracking completes the 5-by-5 tour |
| mandelbrot | 48-by-32 image; unchanged 100-iteration limit |
| ntt-kernel | 128 elements; three transforms and full quadratic reference comparison |
| rad-to-deg | Five-degree samples; both conversions over a complete turn |
| randshell-sort | 128 values; complete sortedness check |
| spirograph | 256 samples; unchanged ten rotations and curve parameters |
| sudoku-solver | Three additional valid givens; complete solution by backtracking |

Calibrate these hashes with the native `TARGET_HAHOST` build before running
the RISC-V ELFs. Independently recompute FNV-1a over captured native output
bytes and compare it with the hash-alone result; do not derive the oracle from
the DUT. Clang native builds need `-U__clang__` to select the upstream header's
portable type definitions rather than its invalid `signed __SIZE_TYPE__`
typedef. Use `LIBMIN_MALLOC_ALIGN_BYTES=8` to match the RISC-V adapter.
Validate complete Sudoku and knight-tour solutions and a
sorted permutation, not merely successful program exits. The knight-tour
output hash remains unchanged because its prefix belongs to the original tour.

When advancing the submodule, check each patch against the new revision,
remove fixes already upstream, and rerun the `lz-compress` functional test on
both single-core SoCs. The build rejects patches that no longer apply.
