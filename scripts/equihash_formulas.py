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
    python3 equihash_formulas.py --valid-n 5,7,9    # list every valid n per k
                                                     # (Req/Equihash's own rule
                                                     # set: n%8==0, n%(k+1)==0,
                                                     # cbl in [8,25], n<=512)
    python3 equihash_formulas.py --valid-n 5,9 --cs # same, but CS/Sequihash's
                                                     # relaxed rule set (no cbl
                                                     # floor/ceiling, n<=512 only
                                                     # via BLAKE2b's own digest
                                                     # cap, not an explicit assert)
"""
import argparse
import csv
import math
import sys


def collision_bit_length(n: int, k: int) -> float:
    """ell = n / (k+1). Definitional, not a data point."""
    return n / (k + 1)


# --- Valid (n,k) point generation -------------------------------------
#
# The single source of truth for "which n are legal at this k" -- every
# driver (Rust Params::new/n_bounds/valid_n in Req/rust/src/lib.rs,
# every C++ cs_*_bench/cs_*_gen, rk_single_bench/rk_single_gen) should
# derive its own accept/reject check from this same rule set rather than
# each reimplementing a subset of the asserts by hand. Two independent
# rule sets, since Req/Equihash and CS/Sequihash's own Python reference
# genuinely differ here (not a bug in either -- see the constraint-origin
# note in each function's docstring):
#
#   Req/Equihash (req_valid_n):    n%8==0, n%(k+1)==0, cbl in [8,25], n<=512
#   CS/Sequihash reference (cs_valid_n): n%8==0, n%(k+1)==0, n<=512 only
#     (no cbl floor/ceiling -- the Python reference asserts neither; the
#     n<=512 ceiling is not an explicit assert either, it is BLAKE2b's own
#     64-byte/512-bit digest_size hard limit, discovered as a runtime
#     ValueError from hashlib.blake2b rather than checked up front)
#
# Rule origins (see PROFILE_LOG.md and this session's own derivation,
# cross-checked against Req/rust/src/lib.rs::Params::n_bounds's own
# hardcoded test assertions -- lib.rs test n_bounds_match_constructor,
# k=5/7/9/63/64/0 all verified to match this module's independent
# Python re-derivation exactly):
#   1. k >= 1                    -- algorithm-fundamental (k=0 divides by
#                                    zero in the leaf-mod-k regularity binding)
#   2. n % 8 == 0                -- IMPLEMENTATION convenience (byte
#                                    alignment), NOT algorithm-fundamental;
#                                    the paper's own Table 3 includes
#                                    (150,5), which fails this
#   3. n % (k+1) == 0            -- algorithm-fundamental; ell=n/(k+1)
#                                    must be a whole number of bits/round
#   4. cbl = n/(k+1) in [8,25]   -- Req/rust IMPLEMENTATION safety margin
#                                    (F14): below 8, expand_array's
#                                    accumulator under-fills a row; above
#                                    25, the u32 accumulator overflows.
#                                    NOT enforced by the CS/Sequihash
#                                    reference at all.
#   5. n <= 512                  -- physical (one BLAKE2b digest, max 64
#                                    bytes, must cover one row) -- real
#                                    for EVERY implementation, just
#                                    checked explicitly (Req, rule 5) vs.
#                                    discovered at call time (CS, via
#                                    hashlib.blake2b's own digest_size cap).
#                                    VESTIGIAL for k=4..10 specifically:
#                                    n=512 is only reachable at k=31/63
#                                    (checked directly) -- rule 4's
#                                    cbl<=25 always binds first for every
#                                    k in the 4..10 working range (e.g.
#                                    k=9's hi_n=240, ell=24, nowhere near
#                                    512). Present and correct, just not
#                                    something to reason about when
#                                    picking points in this range.
#
# k=1 note: arithmetically valid (req_valid_n(1) = [16,24,32,40,48]) but
# NOT USEFUL for this project's actual purposes -- k=1 means the merge
# loop (`for i in range(1,k)`) never executes even once, so no
# collision/XOR-to-zero check ever runs (a pre-existing quirk in the
# CS/Sequihash Python reference itself, inherited faithfully by this
# repo's own V1-V5 ports -- see PROFILE_LOG.md's corner-case sweep). It
# exercises none of the merge tree, regularity binding, or anything a
# real ladder/comparison is meant to test. Valid as an input, but should
# not appear in any benchmark ladder or comparison table going forward.

def req_valid_n(k: int):
    """Valid n for Req/Equihash's Params::new, ascending. Rules 1,2,3,4,5
    above. Matches Req/rust/src/lib.rs::Params::valid_n(k) exactly --
    cross-checked against that function's own hardcoded test assertions
    (n_bounds(5)=(48,144), n_bounds(7)=(64,200), n_bounds(9)=(80,240),
    n_bounds(63)=(512,512), n_bounds(64)=None, n_bounds(0)=None)."""
    if k < 1:
        return []
    m = k + 1
    step = math.lcm(8, m)
    lo = -(-(8 * m) // step) * step  # ceil(8*m / step) * step -- cbl >= 8
    hi = min(25 * m, 512) // step * step  # cbl <= 25, and n <= 512
    if lo > hi:
        return []
    return list(range(lo, hi + 1, step))


def cs_valid_n(k: int, n_max: int = 512):
    """Valid n for the CS/Sequihash Python reference's constructor
    (k_list_algorithm.py __init__), ascending. Rules 1,2,3 only -- no cbl
    bound (rule 4 not enforced), n<=512 passed explicitly as n_max since
    the reference itself only discovers that ceiling via a BLAKE2b
    ValueError, not an assert (rule 5's real origin is documented above,
    not re-derived from the reference's own source, which has no
    corresponding check to read)."""
    if k < 1:
        return []
    m = k + 1
    step = math.lcm(8, m)
    return list(range(step, n_max + 1, step))


def valid_k_range(lo: int = 1, hi: int = 64):
    """Every k in [lo,hi] with at least one valid Req/Equihash n. In
    practice this is EVERY k in [1,63] -- k=0 fails rule 1, k>=64 fails
    because even the smallest legal n=8(k+1) already exceeds 512 (rule
    5). Kept as a function (not a hardcoded list) so it stays correct if
    the rule constants ever change."""
    return [k for k in range(lo, hi + 1) if req_valid_n(k)]


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


def payload_floor_bytes(n: int, k: int) -> float:
    """N * (n/8 + 4) bytes: an INFORMATION-THEORETIC MINIMUM, not an
    engineering estimate -- one full n-bit hash row + one u32 leaf index,
    the fewest bytes any representation could hold that data in, with
    zero container/allocator overhead of any kind.

    Its only legitimate use is as a sanity floor ("no correct
    implementation can use less than this") -- it is NOT a rough version
    of a real memory estimate and should not be tightened toward measured
    values, because doing so would just re-derive full_index_bytes (the
    model that DOES account for real container/allocator overhead) under
    a different name. The two formulas answer different questions:
    payload_floor_bytes asks "what is the absolute minimum," full_index_bytes
    asks "what does a real full-index solver actually hold."

    KNOWN TO UNDERSTATE REAL MEMORY BY 20-52x against this repo's own
    solve_reference/solve_arena, measured via req_memcheck (a counting
    global allocator) at (24,5) through (96,5) -- see SIZING.md section 2a.
    This gap is not a defect to fix; it is the formula doing exactly what
    it is for.
    """
    n_list = init_list_size(n, k)
    return n_list * (n / 8 + 4)


def full_index_bytes(n: int, k: int) -> float:
    """N * (3*cbyte + 12*2^(k-1) + 96) bytes: the full-index peak model
    (SIZING.md section 1c) for this repo's reference/arena/bucket backends.

    Derivation: the peak is the coexistence (round double-buffering) of the
    last two row generations. A round-r row carries (k+1-r)*cbyte remaining
    hash bytes and 4*2^r index bytes, so generations k-1 and k contribute
    (2+1)*cbyte hash bytes and 4*(2^(k-1) + 2^k) = 12*2^(k-1) index bytes
    per row, plus ~96 B of container headers across both generations.

    Validated against req_memcheck's counting-allocator measurements at the
    valid measured points -- measured/predicted: (48,5) 1.20x, (72,5) 0.87x,
    (96,5) 1.51x; RK's independently measured 10.5 GB at (120,4) is 1.67x
    (different codebase) -- versus the naive payload floor's 20-35x
    understatement. Applies to FULL-INDEX representations only; an
    index-pointer backend (PLAN T2.4) deliberately breaks this model's
    dominant 2^(k-1) term.

    WHERE THE 0.87-1.67x RESIDUAL SPREAD COMES FROM (checked directly,
    not just asserted): at fixed k=5, the model's own component mix is
    IDENTICAL across (48,5)/(72,5)/(96,5) -- the index term
    (12*2^(k-1)=192B) is constant, its SHARE of the per-row total is a
    flat ~65% at all three points -- yet the calibration ratio swings
    0.87x to 1.51x across them anyway. Since the model's own structure
    doesn't change, the spread cannot be a mis-weighted term; it tracks N
    (512 -> 8192 -> 131072 rows live simultaneously), consistent with
    allocator behavior (size-class rounding, Vec growth-doubling slack)
    scaling with row COUNT, not with any one formula term.

    A SEPARATE, not-yet-disentangled source of spread: the "measured/
    predicted" ratios above are calibrated against solve_reference's own
    measurements only. SIZING.md's own measured table shows solve_arena's
    peak genuinely DIFFERS from solve_reference's at the same point (e.g.
    (96,5): 55.4 MB reference vs 66.0 MB arena, a real 19% gap) -- so
    quoting this model as validated for "reference/arena/bucket
    backends" collectively overstates what has actually been checked.
    Treat the stated ratios as solve_reference-specific until arena/
    bucket are calibrated against this same formula independently.
    """
    n_list = init_list_size(n, k)
    cbyte = (collision_bit_length(n, k) + 7) // 8
    return n_list * (3 * cbyte + 12 * 2 ** (k - 1) + 96)


def equihash_memory_bits(n: int, k: int) -> float:
    """O(n*N) bits at constant 1 -- reproduces the paper's own published
    Table 3 Equihash memory column to within +/-0.04 in log2 bits across
    all seven rows the paper lists (Tang-Sun-Gong, eprint 2025/1351,
    Proposition 4 + Table 3, page 31). This is ONE paper's asymptotic
    estimate of ONE construction (single-list Equihash WITH INDEX
    POINTERS, Prop 4's own assumption), not a measurement of any real
    solver. Returns BITS, not bytes -- divide by 8 before comparing to any
    other function in this module.
    """
    return n * init_list_size(n, k)


def sequihash_memory_bits(n: int, k: int) -> float:
    """((k^2+5k+2)/4 * ell + 2^(k-1)) * N bits -- reproduces the paper's own
    published Table 3 Sequihash memory column (same validation as above).
    ONE paper's asymptotic estimate for the k-list construction WITH INDEX
    TRIMMING (Prop 6's own assumption) -- the paper's own regularity-repair
    proposal, called Sequihash in the paper and its artifacts; this
    project's separate, deliberately-named "Requihash" implementation is
    the same construction under a different, project-chosen name. Returns
    BITS, not bytes -- divide by 8 before comparing to any other function
    in this module.
    """
    ell = collision_bit_length(n, k)
    n_list = init_list_size(n, k)
    return ((k ** 2 + 5 * k + 2) / 4 * ell + 2 ** (k - 1)) * n_list


def rk_measured_bytes_per_tuple(k: int) -> float:
    """Empirically-fit bytes/tuple_n constant, from RK's OWN MEASURED peak
    RSS (Req/SOLVER_CORPUS/rk/README.md, "Measured scaling" table,
    Khovratovich's original C++ solver -- LIST_LENGTH=5 tuples/bucket, no
    2016-17 optimizations at all, so this is close to a full-index upper
    bound, not a tuned solver's number).

    Derivation, precisely: bytes/tuple_n is NOT flat across the whole
    measured range -- it runs high at small N (fixed per-solve overhead
    not yet amortized: k=4 starts at 977 B/tuple at N=4096) and settles
    as N grows (629 B/tuple by N=4.2M-16.8M). The constant returned here
    is the CONVERGED-TAIL value (the largest-N rows, where fixed
    overhead is negligible), not a naive average/midpoint across the
    whole table -- averaging in the small-N rows systematically pulls the
    constant high, since their overhead hasn't amortized yet.
        k=4: last 3 rows (N=4.2M, 16.8M and the 1.05M row) average 629.3,
             matches the table's own converged value (629) exactly.
        k=5: last 3 rows (N=262144, 524288, 1048576) average 810.0 --
             CORRECTED from an earlier version of this function that used
             815.5 (the midpoint of the full 807-824 band, including the
             less-converged small-N (90,5)/(96,5) rows at 824). 810 is
             the more defensible value; the 0.7% difference from 815.5
             does not change any conclusion drawn from this file's
             existing outputs, but is fixed here for precision.
    Only k=4 and k=5 have been measured this way; other k fall back to
    None with a printed caveat, NOT a fresh measurement or an
    extrapolation from these two points -- do not treat unmeasured k as
    trustworthy without measuring.

    WHY k=5's constant (810) is ~29% higher than k=4's (629), checked
    structurally, not just observed: the original's own `Tuple::blocks`
    (pow.h) is sized to k uint32_t's at construction (`Tuple(unsigned i)
    { blocks.resize(i); }`), so the raw per-tuple PAYLOAD genuinely grows
    with k -- 16B at k=4 (4 blocks) vs 20B at k=5 (5 blocks), a 25%
    payload increase, tracking the 29% measured-constant increase
    closely but not exactly (629*1.25=786 predicted vs 810 actual, a
    ~3% residual). The residual is plausibly LIST_LENGTH=5-bucket layout
    or std::vector heap-allocation behavior differing slightly at k=5's
    measured N range vs k=4's -- NOT investigated further here (Req-side
    profiling is on hold; this is flagged as a documented open question,
    not resolved). FURTHER WORK, if/when profiling resumes: measure k=6
    or k=7 the same way to see whether the payload-vs-measured tracking
    (close-but-not-exact) holds at a third point, which would confirm
    "mostly payload-driven, small residual from allocator/layout" as the
    real explanation rather than a two-point coincidence.
    """
    if k == 4:
        return 629.0
    if k == 5:
        return 810.0  # converged-tail average (see derivation above), not a band midpoint
    return None  # unmeasured for this k -- caller must not silently extrapolate


def rk_measured_peak_bytes(n: int, k: int):
    """tuple_n * rk_measured_bytes_per_tuple(k), or None if k has no
    measured constant (see that function's own caveat)."""
    per_tuple = rk_measured_bytes_per_tuple(k)
    if per_tuple is None:
        return None
    return init_list_size(n, k) * per_tuple


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
#
# QUESTIONABLE ROW, flagged explicitly: (150,5) fails n%8==0 (150%8=6) --
# confirmed by actually running the CS/Sequihash Python reference's own
# constructor at (150,5): it raises "n should be a multiple of 8" and
# refuses to construct. So this row is real math the paper's Prop 6
# formula happily computes (the formula itself has no byte-alignment
# requirement -- it's continuous in n), but it is NOT a point the paper's
# OWN runnable artifact can solve.
#
# WHY it is kept here -- justified precisely, not just asserted: NOT
# because it is load-bearing for --validate's power to catch a real
# formula bug. Checked directly: (150,5) and (144,5) are adjacent points
# on the SAME k=5 line (ell=25 vs ell=24, differing by exactly 1), and
# both equihash_memory_bits/sequihash_memory_bits are smooth, continuous
# functions of n at fixed k -- a formula bug that passed at n=144 but
# specifically failed at n=150 (and nowhere else) would be a contrived,
# unrealistic failure mode. Removing this row would barely weaken
# --validate's real coverage, since (144,5) already tests nearly the same
# ground. The actual reason it is here is TRANSCRIPTION COMPLETENESS:
# this table's own stated design goal (see the comment two lines up) is
# to be the single, complete, verbatim source of truth for the paper's
# published Table 3 -- all seven rows, not a curated six. It is included
# because the paper published it, not because the validation needs it;
# an earlier version of this comment claimed the latter and overstated
# the row's actual test value.
PAPER_TABLE_3 = [
    # (n, k, eq_time_log2, eq_mem_log2, eq_sol_bytes, req_time_log2, req_mem_log2, req_sol_bytes)
    (96, 5, 19.3, 23.6, 68, 22.6, 24.8, 64),
    (128, 7, 19.8, 24.0, 272, 24.6, 25.7, 256),
    (160, 9, 20.2, 24.3, 1088, 26.6, 26.6, 1024),
    (144, 5, 27.3, 32.2, 100, 30.6, 33.4, 96),
    (150, 5, 28.3, 33.2, 104, 31.6, 34.4, 100),  # NOT constructible by the CS reference -- see note above
    (200, 9, 24.2, 28.6, 1344, 30.6, 30.8, 1280),
    (288, 8, 36.0, 41.2, 1056, 41.6, 42.9, 1024),
]


def validate_against_paper_table3(verbose: bool = True) -> bool:
    """Checks equihash_memory_bits / sequihash_memory_bits against every
    row of PAPER_TABLE_3. Returns True if every row matches to within 0.1
    in log2(bits) (the paper's own one-decimal rounding).
    """
    ok = True
    for n, k, _, eq_mem_log2, _, _, req_mem_log2, _ in PAPER_TABLE_3:
        calc_eq = math.log2(equihash_memory_bits(n, k))
        calc_req = math.log2(sequihash_memory_bits(n, k))
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
# Mirrors SIZING.md §4's tier tables exactly (2026-07-17 restructure): only
# implementable params (ell in [8,25], n <= 512 — REVIEW_REQ.md F12-F14).
# Former TB-scale rows ((168,5)+, (232,7)+, (280,9)+) and sub-8-ell rows
# ((24,5), (32,7), (40,9)) removed — not constructible. Keep in sync with
# SIZING.md §4 in the same pass (A20 discipline).
SWEEP_POINTS = {
    4: [40, 80, 120],
    5: [48, 72, 96, 120, 144],
    7: [96, 128, 168, 192, 200],
    8: [216],
    9: [80, 120, 160, 200, 240],
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
                "naive_peak_bytes_this_repo_model": payload_floor_bytes(n, k),
                "naive_peak_bytes_this_repo_model_source": "computed (payload floor; see SIZING.md 1c)",
                "full_index_model_bytes": full_index_bytes(n, k),
                "full_index_model_bytes_source": "modeled (calibrated 0.7-1.4x vs measured, SIZING.md 1c/3)",
                "equihash_memory_bytes_extrapolated": equihash_memory_bits(n, k) / 8,
                "equihash_memory_bytes_extrapolated_source": "interpolated (Prop 4, validated vs paper Table 3)",
                "requihash_memory_bytes_extrapolated": sequihash_memory_bits(n, k) / 8,
                "requihash_memory_bytes_extrapolated_source": "interpolated (Prop 6, validated vs paper Table 3)",
            }


def formula_comparison_rows(ks):
    """Yields one row per (k, n) for every n in req_valid_n(k), across all
    given ks -- the FULL derived grid (not SWEEP_POINTS' curated subset),
    with every memory formula this project has evaluated applied side by
    side: this repo's own naive payload floor, this repo's calibrated
    full-index model, the paper's Prop 4 (Equihash)/Prop 6 (Requihash)
    asymptotic estimates, and RK's own independently measured
    bytes/tuple_n constant (k=4,5 only -- None elsewhere, not
    extrapolated). Each column is tagged with its evidence grade in a
    same-named _source column, per SIZING.md's convention (measured /
    modeled / interpolated / computed).
    """
    for k in ks:
        for n in req_valid_n(k):
            ell = collision_bit_length(n, k)
            rk_measured = rk_measured_peak_bytes(n, k)
            yield {
                "k": k,
                "K_2^k": 2 ** k,
                "n": n,
                "ell": ell,
                "cbl_binding": "cbl>=8 (floor)" if ell == 8 else ("cbl<=25 (ceiling)" if ell == 25 else "step-grid interior"),
                "init_list_N": init_list_size(n, k),
                "payload_floor_bytes": payload_floor_bytes(n, k),
                "payload_floor_bytes_source": "computed (payload floor, understates real peak 20-52x, SIZING.md 2a)",
                "full_index_bytes": full_index_bytes(n, k),
                "full_index_bytes_source": "modeled (calibrated 0.7-1.4x vs measured, SIZING.md 1c/3)",
                "equihash_memory_bytes": equihash_memory_bits(n, k) / 8,
                "equihash_memory_bytes_source": "interpolated (paper Prop 4, index-pointer assumption, validated vs Table 3 to +/-0.04 log2 bits)",
                "sequihash_memory_bytes": sequihash_memory_bits(n, k) / 8,
                "sequihash_memory_bytes_source": "interpolated (paper Prop 6, index-trim assumption, validated vs Table 3 to +/-0.04 log2 bits)",
                "rk_measured_bytes": rk_measured,
                "rk_measured_bytes_source": (
                    "measured (RK README Measured-scaling table, Khovratovich original C++, no 2016-17 opts)"
                    if rk_measured is not None else "UNMEASURED for this k -- do not trust a None-backed extrapolation"
                ),
            }


def write_formula_comparison_csv(path: str, ks):
    rows = list(formula_comparison_rows(ks))
    if not rows:
        print("no rows (no valid n for the given ks)", file=sys.stderr)
        return
    fieldnames = list(rows[0].keys())
    with open(path, "w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)
    print(f"wrote {len(rows)} rows ({len(set(r['k'] for r in rows))} k values) to {path}", file=sys.stderr)


def print_sweep():
    header = (
        f"{'k':>2} {'n':>4} {'ell':>4} {'N':>10} "
        f"{'sol(min/compact)':>18} {'verify':>7} "
        f"{'naive(floor)':>14} {'full-index(model)':>18} "
        f"{'equihash(interp)':>17} {'requihash(interp)':>18}"
    )
    print(header)
    for row in sweep_rows():
        n_col = f"2^{math.log2(row['init_list_n']):.0f}"
        sol_col = f"{row['sol_size_minimal_bytes']:.0f}/{row['sol_size_compact_bytes']:.0f} B"
        line = (
            f"{row['k']:>2} {row['n']:>4} {row['ell']:>4.0f} {n_col:>10} "
            f"{sol_col:>18} {row['verify_hashes_m1']:>7} "
            f"{human_bytes(row['naive_peak_bytes_this_repo_model']):>14} "
            f"{human_bytes(row['full_index_model_bytes']):>18} "
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


def print_valid_n(ks, use_cs_rules: bool):
    """Prints, for each k in ks: K=2^k (the CS-convention equivalent) and
    the full valid-n list under the selected rule set, plus which rule is
    binding at each end (useful when picking ladder points -- see this
    session's own worked comparison in PROFILE_LOG.md)."""
    fn = cs_valid_n if use_cs_rules else req_valid_n
    ruleset = "CS/Sequihash (n%8, n%(k+1), n<=512 -- no cbl bound)" if use_cs_rules \
        else "Req/Equihash (n%8, n%(k+1), cbl in [8,25], n<=512)"
    print(f"Rule set: {ruleset}\n")
    for k in ks:
        vn = fn(k)
        K = 2 ** k
        if not vn:
            print(f"k={k:>2}  K={K:>5}  NO VALID n under this rule set")
            continue
        lo, hi = vn[0], vn[-1]
        lo_cbl = collision_bit_length(lo, k)
        hi_cbl = collision_bit_length(hi, k)
        binding_lo = "cbl>=8" if (not use_cs_rules and lo_cbl == 8) else "n%8/n%(k+1) step floor"
        binding_hi = "n<=512" if hi == 512 else ("cbl<=25" if (not use_cs_rules and hi_cbl == 25) else "n%8/n%(k+1) step ceiling")
        print(f"k={k:>2}  K={K:>5}  n in {vn}")
        print(f"          lo={lo} (cbl={lo_cbl:.0f}, binding: {binding_lo})  "
              f"hi={hi} (cbl={hi_cbl:.0f}, binding: {binding_hi})  count={len(vn)}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--csv", metavar="PATH", help="write the sweep table as CSV")
    parser.add_argument(
        "--validate", action="store_true",
        help="check formulas against the paper's own published Table 3 rows"
    )
    parser.add_argument(
        "--valid-n", metavar="K1,K2,...",
        help="list every valid n for each given k (comma-separated), plus "
             "which rule binds the lo/hi end of the range"
    )
    parser.add_argument(
        "--cs", action="store_true",
        help="with --valid-n, use CS/Sequihash's relaxed rule set (no cbl "
             "bound) instead of Req/Equihash's own"
    )
    parser.add_argument(
        "--formula-comparison-csv", metavar="PATH",
        help="write every valid (n,k) point (Req/Equihash rules) for "
             "--valid-n's k list to PATH, with all 5 evaluated memory "
             "formulas (naive floor, full-index model, paper Prop4/Prop6, "
             "RK measured bytes/tuple) applied side by side, each tagged "
             "with its evidence grade"
    )
    args = parser.parse_args()

    if args.validate:
        ok = validate_against_paper_table3()
        print("ALL ROWS MATCH" if ok else "MISMATCH FOUND", file=sys.stderr)
        sys.exit(0 if ok else 1)
    elif args.formula_comparison_csv:
        ks = [int(x) for x in args.valid_n.split(",")] if args.valid_n else list(range(4, 11))
        write_formula_comparison_csv(args.formula_comparison_csv, ks)
    elif args.valid_n:
        ks = [int(x) for x in args.valid_n.split(",")]
        print_valid_n(ks, use_cs_rules=args.cs)
    elif args.csv:
        write_csv(args.csv)
    else:
        print_sweep()
