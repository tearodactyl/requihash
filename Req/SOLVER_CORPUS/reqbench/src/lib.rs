//! `reqbench` — shared, dependency-free measurement harness for
//! `Req/rust` and every port under `Req/SOLVER_CORPUS/`. See `Req/BENCH.md`
//! for the discipline this crate implements and why.
//!
//! Four pieces, usable independently:
//! - [`stats`]: timing statistics (min/median/MAD), JSON-lines emission,
//!   baseline loading, and the Win/Regression/Noise/New comparison rule.
//! - [`provenance`]: git commit/dirty-tree/build-profile stamping, so a
//!   reported number is traceable back to exactly what produced it.
//! - [`mem`]: a counting global allocator plus an OS-RSS cross-check, so a
//!   peak-memory figure is corroborated by two independent instruments
//!   before being trusted (`Req/BENCH.md` §4).
//! - [`run_record`]: the unified cross-implementation run-record schema
//!   (`RunRecord`) every measurement in `Req/SOLVER_CORPUS/RUN_DATA.jsonl`
//!   converges on — Rust drivers emit it via this module; C++ drivers
//!   (which don't depend on this crate) match its exact JSON shape by hand.
//!
//! No external dependencies, `std` only — a standalone `SOLVER_CORPUS` port
//! must stay usable with no other context needed from this repository
//! (`SOLVER_CORPUS.md`'s own cross-cutting requirement), and this crate is
//! meant to be depended on by exactly that kind of port.

pub mod mem;
pub mod provenance;
pub mod run_record;
pub mod stats;

use std::time::Instant;

/// Runs `f` repeatedly and returns per-rep wall-clock samples in
/// nanoseconds, after an untimed warm-up period. Mirrors
/// `Req/rust/src/bin/req_bench.rs`'s own `sample()` helper: warm-up is by
/// time budget, not call count, because a microsecond-scale bench can
/// finish entirely on an efficiency core before the OS scheduler migrates
/// it to a performance core — timing too early measures the wrong core's
/// throughput.
pub fn sample<F: FnMut()>(reps: usize, warmup_ms: u64, mut f: F) -> Vec<u128> {
    let warm = Instant::now();
    while warm.elapsed().as_millis() < warmup_ms as u128 {
        f();
    }
    let mut samples = Vec::with_capacity(reps);
    for _ in 0..reps {
        let t = Instant::now();
        f();
        samples.push(t.elapsed().as_nanos());
    }
    samples
}

/// Default warm-up budget, matching `req_bench.rs`'s own default.
pub const DEFAULT_WARMUP_MS: u64 = 50;

/// Default trial count. `Req/BENCH.md` §2: at least 5 reps, so `compare`
/// (which requires `reps >= 3` to produce anything but `Verdict::Untracked`)
/// has real headroom above its own minimum.
pub const DEFAULT_REPS: usize = 7;

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn sample_returns_requested_rep_count() {
        let s = sample(5, 1, || {
            std::hint::black_box(1 + 1);
        });
        assert_eq!(s.len(), 5);
    }
}
