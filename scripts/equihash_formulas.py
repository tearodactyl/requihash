#!/usr/bin/env python3
"""equihash_formulas.py — reusable closed-form Equihash/Requihash sizing math.

Every formula here is a data point from one specific source, not an
established truth — see SIZING.md's framing. Each function's docstring names
its source precisely (paper/section/table/page, or "this repo's own model").
Treat outputs as one method's estimate among several, subject to revision
once measured data (req_memcheck, req_bench) accumulates across more
parameters and platforms.

Usage:
    python3 equihash_formulas.py                  # print the full sweep table
    python3 equihash_formulas.py --csv out.csv     # write the sweep as CSV
    python3 equihash_formulas.py --validate         # check formulas against
                                                     # the paper's own published
                                                     # Table 3 rows (2025/1351)
"""
import argparse
import csv
import math
import sys


def collision_bit_length(n: int, k: int) -> float:
    """ell = n / (k+1). Definitional, not a data point."""
    return n / (k + 1)


def init_list_size(n: int, k: int) -> float:
    """N = 2^(ell+1). Definitional."""
    return 2 ** (collision_bit_length(n, k) + 1)


def solution_size_minimal_bytes(n: int, k: int) -> float:
    """(ell+1) bits per index, 2^k indices. Equihash-compatible minimal
    encoding, matches this repo's Params::solution_width (rust/src/lib.rs),
    which is a real measured value from running code, not a formula guess."""
    ell = collision_bit_length(n, k)
    return (2 ** k) * (ell + 1) / 8


def solution_size_compact_bytes(n: int, k: int) -> float:
    """ell bits per index (index field dropped, reconstructed from packet
    structure). Matches this repo's Params::compact_width."""
    ell = collision_bit_length(n, k)
    return (2 ** k) * ell / 8


def verify_hash_count(k: int, m: int = 1) -> int:
    """2^k * m hash calls to verify one solution. Definitional given the
    per-era iteration count m (SPEC.md sections 5-6)."""
    return (2 ** k) * m


def naive_peak_bytes_this_repo_model(n: int, k: int) -> float:
    """N * (n/8 + 4) bytes: this repo's own payload-only model (one full
    n-bit hash row + one u32 leaf index per row at round 0).

    KNOWN TO UNDERSTATE REAL MEMORY BY 20-52x against this repo's own
    solve_reference/solve_arena, measured via req_memcheck (a counting
    global allocator) at (24,5) through (96,5) -- see SIZING.md section 2a.
    Use only as a rough floor, never as an expected real requirement.
    """
    n_list = init_list_size(n, k)
    return n_list * (n / 8 + 4)


def equihash_memory_bits_prop4(n: int, k: int) -> float:
    """O(n*N) bits at constant 1 -- reproduces the paper's own published
    Table 3 Equihash memory column to within +/-0.04 in log2 bits across
    all seven rows the paper lists (Tang-Sun-Gong, eprint 2025/1351,
    Proposition 4 + Table 3, page 31). This is ONE paper's asymptotic
    estimate of ONE construction, not a measurement of any real solver.
    """
    return n * init_list_size(n, k)


def requihash_memory_bits_prop6(n: int, k: int) -> float:
    """((k^2+5k+2)/4 * ell + 2^(k-1)) * N bits -- reproduces the paper's own
    published Table 3 Sequihash memory column (same validation as above).
    Same caveat: one paper's estimate for one specific construction
    (the paper's own regularity-repair proposal, called Sequihash in the
    paper and its artifacts; this project's separate, deliberately-named
    "Requihash" implementation is the same construction under a different,
    project-chosen name).
    """
    ell = collision_bit_length(n, k)
    n_list = init_list_size(n, k)
    return ((k ** 2 + 5 * k + 2) / 4 * ell + 2 ** (k - 1)) * n_list


def human_bytes(byte_count: float) -> str:
    """Format a byte count as a human-readable string with units."""
    units = ["B", "KB", "MB", "GB", "TB", "PB"]
    v = float(byte_count)
    i = 0
    while v >= 1024 and i < len(units) - 1:
        v /= 1024
        i += 1
    return f"{v:.2f} {units[i]}"


# The paper's own published Table 3 (Tang-Sun-Gong, eprint 2025/1351, page 31),
# transcribed verbatim -- log2(bits) for time/memory, bytes for solution size.
# Used only to validate the extrapolation formulas above, never overwritten
# by them. Kept here as the single source of truth for that table so it is
# never re-transcribed differently in two places.
PAPER_TABLE_3 = [
    # (n, k, eq_time_log2, eq_mem_log2, eq_sol_bytes, req_time_log2, req_mem_log2, req_sol_bytes)
    (96, 5, 19.3, 23.6, 68, 22.6, 24.8, 64),
    (128, 7, 19.8, 24.0, 272, 24.6, 25.7, 256),
    (160, 9, 20.2, 24.3, 1088, 26.6, 26.6, 1024),
    (144, 5, 27.3, 32.2, 100, 30.6, 33.4, 96),
    (150, 5, 28.3, 33.2, 104, 31.6, 34.4, 100),
    (200, 9, 24.2, 28.6, 1344, 30.6, 30.8, 1280),
    (288, 8, 36.0, 41.2, 1056, 41.6, 42.9, 1024),
]


def validate_against_paper_table3(verbose: bool = True) -> bool:
    """Checks equihash_memory_bits_prop4 / requihash_memory_bits_prop6
    against every row of PAPER_TABLE_3. Returns True if every row matches
    to within 0.1 in log2(bits) (the paper's own one-decimal rounding).
    """
    ok = True
    for n, k, _, eq_mem_log2, _, _, req_mem_log2, _ in PAPER_TABLE_3:
        calc_eq = math.log2(equihash_memory_bits_prop4(n, k))
        calc_req = math.log2(requihash_memory_bits_prop6(n, k))
        d_eq = calc_eq - eq_mem_log2
        d_req = calc_req - req_mem_log2
        if verbose:
            print(
                f"({n},{k}): published eq={eq_mem_log2:.1f} calc={calc_eq:.2f} "
                f"(diff {d_eq:+.2f}) | published req={req_mem_log2:.1f} "
                f"calc={calc_req:.2f} (diff {d_req:+.2f})"
            )
        if abs(d_eq) > 0.1 or abs(d_req) > 0.1:
            ok = False
    return ok


# The extrapolated sweep used in SIZING.md section 2 -- kept here so it can
# be regenerated identically rather than hand-copied into a table again.
SWEEP_POINTS = {
    5: [24, 48, 72, 168, 192, 216],
    7: [32, 96, 168, 232, 264, 296],
    9: [40, 120, 240, 280, 320, 360],
}


def sweep_rows():
    """Yields dicts for every (n,k) in SWEEP_POINTS, with a 'source' field
    on every column so a CSV consumer can tell computed/measured/interpolated
    apart per SIZING.md's evidence-grade convention.
    """
    for k, ns in SWEEP_POINTS.items():
        for n in ns:
            ell = collision_bit_length(n, k)
            yield {
                "n": n,
                "k": k,
                "ell": ell,
                "ell_source": "computed",
                "init_list_n": init_list_size(n, k),
                "init_list_n_source": "computed",
                "sol_size_minimal_bytes": solution_size_minimal_bytes(n, k),
                "sol_size_minimal_bytes_source": "computed",
                "sol_size_compact_bytes": solution_size_compact_bytes(n, k),
                "sol_size_compact_bytes_source": "computed",
                "verify_hashes_m1": verify_hash_count(k, 1),
                "verify_hashes_m1_source": "computed",
                "naive_peak_bytes_this_repo_model": naive_peak_bytes_this_repo_model(n, k),
                "naive_peak_bytes_this_repo_model_source": "computed (known low by 20-52x vs measured, see SIZING.md 2a)",
                "equihash_memory_bytes_extrapolated": equihash_memory_bits_prop4(n, k) / 8,
                "equihash_memory_bytes_extrapolated_source": "interpolated (Prop 4, validated vs paper Table 3)",
                "requihash_memory_bytes_extrapolated": requihash_memory_bits_prop6(n, k) / 8,
                "requihash_memory_bytes_extrapolated_source": "interpolated (Prop 6, validated vs paper Table 3)",
            }


def print_sweep():
    header = (
        f"{'k':>2} {'n':>4} {'ell':>4} {'N':>10} "
        f"{'sol(min/compact)':>18} {'verify':>7} "
        f"{'naive(model)':>14} {'equihash(interp)':>17} {'requihash(interp)':>18}"
    )
    print(header)
    for row in sweep_rows():
        n_col = f"2^{math.log2(row['init_list_n']):.0f}"
        sol_col = f"{row['sol_size_minimal_bytes']:.0f}/{row['sol_size_compact_bytes']:.0f} B"
        line = (
            f"{row['k']:>2} {row['n']:>4} {row['ell']:>4.0f} {n_col:>10} "
            f"{sol_col:>18} {row['verify_hashes_m1']:>7} "
            f"{human_bytes(row['naive_peak_bytes_this_repo_model']):>14} "
            f"{human_bytes(row['equihash_memory_bytes_extrapolated']):>17} "
            f"{human_bytes(row['requihash_memory_bytes_extrapolated']):>18}"
        )
        print(line)


def write_csv(path: str):
    rows = list(sweep_rows())
    fieldnames = list(rows[0].keys())
    with open(path, "w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)
    print(f"wrote {len(rows)} rows to {path}", file=sys.stderr)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--csv", metavar="PATH", help="write the sweep table as CSV")
    parser.add_argument(
        "--validate", action="store_true",
        help="check formulas against the paper's own published Table 3 rows"
    )
    args = parser.parse_args()

    if args.validate:
        ok = validate_against_paper_table3()
        print("ALL ROWS MATCH" if ok else "MISMATCH FOUND", file=sys.stderr)
        sys.exit(0 if ok else 1)
    elif args.csv:
        write_csv(args.csv)
    else:
        print_sweep()
