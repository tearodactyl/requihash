#!/bin/bash
# bench_array_m4.sh — run the full regularized measurement array directly on
# the M4 (or any macOS/ARM machine): Req rust + cpp, uniblake, corpus (RK,
# RZ, cs-rs). Mirrors BENCHMARK.md §10's array dimensions so per-machine
# results are comparable cell-by-cell.
#
# Usage:
#   scripts/bench_array_m4.sh                 # tag defaults to apple-silicon-dev
#   TAG=<machine-tag> scripts/bench_array_m4.sh
#
# Appends JSONL to the per-machine baselines files (comparison against the
# rolling series happens before appending — see req_bench docs) and writes
# all section logs to Req/baselines/runs/<timestamp>-<tag>/.

set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TAG="${TAG:-apple-silicon-dev}"
STAMP="$(date +%Y%m%d-%H%M%S)"
LOGDIR="$ROOT/Req/baselines/runs/$STAMP-$TAG"
mkdir -p "$LOGDIR"
FAILURES=""

note() { printf '\n== %s ==\n' "$*"; }
fail() { FAILURES="$FAILURES  - $*\n"; printf 'SECTION FAILED: %s\n' "$*"; }

note "provenance"
{
  date
  uname -a
  sysctl -n machdep.cpu.brand_string 2>/dev/null || true
  sysctl -n hw.memsize 2>/dev/null || true
  (cd "$ROOT" && git rev-parse HEAD 2>/dev/null) || echo "no-git"
  rustc --version; cc --version | head -1; cmake --version 2>/dev/null | head -1
} | tee "$LOGDIR/provenance.txt"

note "[1/7] Req rust: test gate"
(cd "$ROOT/Req/rust" && cargo test --release) >"$LOGDIR/rust_test.log" 2>&1 \
  || fail "rust tests"

note "[2/7] Req rust: req_bench (2 rounds) + req_memcheck"
(cd "$ROOT/Req/rust" \
  && cargo run --release --bin req_bench -- \
       --json ../baselines/"$TAG".jsonl --tag "$TAG" \
  && cargo run --release --bin req_bench -- \
       --json ../baselines/"$TAG".jsonl --tag "$TAG" \
  && cargo run --release --bin req_memcheck) \
  >"$LOGDIR/rust_bench.log" 2>&1 || fail "rust bench/memcheck"
grep -E "wins|regressions|within noise" "$LOGDIR/rust_bench.log" | tail -2 || true

note "[3/7] Req cpp: build + test + bench"
(cd "$ROOT/Req" \
  && cmake -S cpp -B cpp/build >/dev/null \
  && cmake --build cpp/build >/dev/null \
  && cpp/build/req_test \
  && cpp/build/req_bench) \
  >"$LOGDIR/cpp.log" 2>&1 || fail "cpp build/test/bench"

note "[4/7] uniblake: build + ctest + kernel/dispatch bench"
(cd "$ROOT/BLAKE/uniblake" \
  && cmake -S . -B build >/dev/null \
  && cmake --build build >/dev/null \
  && ctest --test-dir build --output-on-failure \
  && ./build/ub_kbench \
  && ./build/ub_bench) \
  >"$LOGDIR/uniblake.log" 2>&1 || fail "uniblake"
grep -E "speedup|dispatch tax" "$LOGDIR/uniblake.log" | head -4 || true

note "[5/7] corpus RK: rk_bench"
(cd "$ROOT/Req/SOLVER_CORPUS/rk" \
  && cargo run --release --bin rk_bench -- \
       --json baselines/"$TAG".jsonl --tag "$TAG" --reps 5) \
  >"$LOGDIR/rk.log" 2>&1 || fail "rk_bench"

note "[6/7] corpus RZ: rz_bench (memory-gated: needs ~7 GB free)"
MEMBYTES="$(sysctl -n hw.memsize 2>/dev/null || echo 0)"
if [ "$MEMBYTES" -ge 12000000000 ]; then
  (cd "$ROOT/Req/SOLVER_CORPUS/rz" \
    && cargo run --release --bin rz_bench -- \
         --json baselines/"$TAG".jsonl --tag "$TAG") \
    >"$LOGDIR/rz.log" 2>&1 || fail "rz_bench"
else
  echo "skipped: machine RAM below gate (rz peaks 6.27 GB)" | tee "$LOGDIR/rz.log"
fi

note "[7/7] corpus cs-rs: tests (no bench binary yet — T4.2)"
(cd "$ROOT/Req/SOLVER_CORPUS/cs-rs" && cargo test --release) \
  >"$LOGDIR/csrs.log" 2>&1 || fail "cs-rs tests"

note "summary"
if [ -n "$FAILURES" ]; then
  printf 'FAILED SECTIONS:\n%b' "$FAILURES"
  printf 'logs: %s\n' "$LOGDIR"
  exit 1
fi
printf 'ALL SECTIONS OK — logs: %s\n' "$LOGDIR"
printf 'appended: Req/baselines/%s.jsonl, rk/baselines/%s.jsonl' "$TAG" "$TAG"
[ -f "$ROOT/Req/SOLVER_CORPUS/rz/baselines/$TAG.jsonl" ] \
  && printf ', rz/baselines/%s.jsonl' "$TAG"
printf '\nNext: paste the summary + notable deltas into BENCHMARK.md §10-style tables.\n'
