//! Measures ACTUAL peak allocation of this repo's own solvers via a
//! counting global allocator, and reports it alongside the SIZING.md
//! formula predictions for the same (n,k) — so the "naive peak memory"
//! and "index-pointer peak memory" figures in that document can be checked
//! against real running code instead of trusted as formulas.
//!
//! This does NOT validate the paper's own closed-form estimators (this
//! repo has no index-pointer solver, PLAN.md A6) — it validates only
//! whether THIS repo's naive solvers actually hit the memory this repo's
//! own "naive peak memory" formula predicts. That is a narrower, honest
//! claim; do not read more into it.
//!
//! Run with: cargo run --release --bin req_memcheck

use requihash::*;
use std::alloc::{GlobalAlloc, Layout, System};
use std::sync::atomic::{AtomicUsize, Ordering};

struct CountingAlloc;

static CURRENT: AtomicUsize = AtomicUsize::new(0);
static PEAK: AtomicUsize = AtomicUsize::new(0);

unsafe impl GlobalAlloc for CountingAlloc {
    unsafe fn alloc(&self, layout: Layout) -> *mut u8 {
        let ptr = unsafe { System.alloc(layout) };
        if !ptr.is_null() {
            let cur = CURRENT.fetch_add(layout.size(), Ordering::Relaxed) + layout.size();
            PEAK.fetch_max(cur, Ordering::Relaxed);
        }
        ptr
    }
    unsafe fn dealloc(&self, ptr: *mut u8, layout: Layout) {
        unsafe { System.dealloc(ptr, layout) };
        CURRENT.fetch_sub(layout.size(), Ordering::Relaxed);
    }
}

#[global_allocator]
static ALLOC: CountingAlloc = CountingAlloc;

fn reset() {
    CURRENT.store(0, Ordering::Relaxed);
    PEAK.store(0, Ordering::Relaxed);
}
fn peak_bytes() -> usize {
    PEAK.load(Ordering::Relaxed)
}

/// This repo's own SIZING.md "naive peak memory" formula: N * (n/8 + 4).
fn sizing_naive_model(n: u32, k: u32) -> u64 {
    let ell = n / (k + 1);
    let nn = 1u64 << (ell + 1);
    nn * (n as u64 / 8 + 4)
}

fn human(b: u64) -> String {
    let units = ["B", "KB", "MB", "GB"];
    let mut v = b as f64;
    let mut i = 0;
    while v >= 1024.0 && i < units.len() - 1 {
        v /= 1024.0;
        i += 1;
    }
    format!("{:.2} {}", v, units[i])
}

fn main() {
    println!("Measured peak allocation vs SIZING.md's 'naive' formula (N * (n/8+4)):\n");
    println!(
        "{:>8} {:>16} {:>16} {:>10}",
        "(n,k)", "formula", "measured peak", "ratio"
    );

    for &(n, k) in &[(24u32, 5u32), (48, 5), (72, 5), (96, 5)] {
        let p = Params::new(n, k).unwrap();
        let mut nonce = 0u32;
        // Find a solving nonce, then measure the solve that finds it (the
        // peak occurs during solve regardless of whether a solution exists,
        // but using a solving nonce keeps runs comparable across params).
        loop {
            reset();
            let eng = Requihash::new(p, b"memcheck", &nonce.to_le_bytes());
            let sols = eng.solve_reference();
            let peak = peak_bytes();
            if !sols.is_empty() || nonce > 200 {
                let model = sizing_naive_model(n, k);
                println!(
                    "{:>8} {:>16} {:>16} {:>9.2}x",
                    format!("({n},{k})"),
                    human(model),
                    human(peak as u64),
                    peak as f64 / model as f64
                );
                break;
            }
            nonce += 1;
        }
    }

    println!("\nMeasured peak allocation, solve_arena backend, same params:");
    println!(
        "{:>8} {:>16} {:>16} {:>10}",
        "(n,k)", "formula", "measured peak", "ratio"
    );
    for &(n, k) in &[(24u32, 5u32), (48, 5), (72, 5), (96, 5)] {
        let p = Params::new(n, k).unwrap();
        let mut nonce = 0u32;
        loop {
            reset();
            let eng = Requihash::new(p, b"memcheck", &nonce.to_le_bytes());
            let sols = eng.solve_arena();
            let peak = peak_bytes();
            if !sols.is_empty() || nonce > 200 {
                let model = sizing_naive_model(n, k);
                println!(
                    "{:>8} {:>16} {:>16} {:>9.2}x",
                    format!("({n},{k})"),
                    human(model),
                    human(peak as u64),
                    peak as f64 / model as f64
                );
                break;
            }
            nonce += 1;
        }
    }

    println!(
        "\nNote: peak_bytes is total live heap tracked by this process's global\n\
         allocator across the ENTIRE solve call (all rounds, all intermediate\n\
         Vecs), not a hand-modeled 'round-0 only' figure like SIZING.md's\n\
         formula assumes. A large ratio (formula << measured) means the real\n\
         peak is dominated by rounds/structures the formula does not model at\n\
         all (e.g. per-row Vec<u8>/Vec<u32> heap overhead beyond raw payload\n\
         bytes -- allocator bookkeeping, Vec capacity over-allocation) -- this\n\
         is exactly the allocation overhead BENCHMARK.md's profiling already\n\
         identified as 59% of solve_reference's time, so a large ratio here\n\
         is expected and consistent with that earlier finding, not a new\n\
         surprise -- but it had never actually been measured until now."
    );
}
