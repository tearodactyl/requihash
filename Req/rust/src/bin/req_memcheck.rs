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
//!            cargo run --release --bin req_memcheck -- --csv out.csv
//! (--csv writes exact byte counts, not rounded human-readable strings, so
//! the numbers can be checked or reused without a transcription step.)

use requihash::*;
use std::io::Write as _;
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

/// One measured row: exact bytes, never rounded, so CSV output can be
/// trusted without a display-string transcription step.
struct Row {
    n: u32,
    k: u32,
    backend: &'static str,
    formula_bytes: u64,
    measured_peak_bytes: u64,
}

fn measure(backend: &'static str, params: &[(u32, u32)]) -> Vec<Row> {
    let mut rows = Vec::new();
    for &(n, k) in params {
        let p = Params::new(n, k).unwrap();
        let mut nonce = 0u32;
        loop {
            reset();
            let eng = Requihash::new(p, b"memcheck", &nonce.to_le_bytes());
            let sols = if backend == "reference" {
                eng.solve_reference()
            } else {
                eng.solve_arena()
            };
            let peak = peak_bytes() as u64;
            if !sols.is_empty() || nonce > 200 {
                rows.push(Row {
                    n,
                    k,
                    backend,
                    formula_bytes: sizing_naive_model(n, k),
                    measured_peak_bytes: peak,
                });
                break;
            }
            nonce += 1;
        }
    }
    rows
}

fn print_rows(rows: &[Row]) {
    println!(
        "{:>8} {:>10} {:>16} {:>16} {:>10}",
        "(n,k)", "backend", "formula", "measured peak", "ratio"
    );
    for r in rows {
        println!(
            "{:>8} {:>10} {:>16} {:>16} {:>9.2}x",
            format!("({},{})", r.n, r.k),
            r.backend,
            human(r.formula_bytes),
            human(r.measured_peak_bytes),
            r.measured_peak_bytes as f64 / r.formula_bytes as f64
        );
    }
}

fn write_csv(path: &str, rows: &[Row]) {
    let mut f = std::fs::File::create(path).expect("create csv");
    writeln!(
        f,
        "n,k,backend,formula_naive_bytes,measured_peak_bytes,ratio,source"
    )
    .unwrap();
    for r in rows {
        writeln!(
            f,
            "{},{},{},{},{},{:.4},measured",
            r.n,
            r.k,
            r.backend,
            r.formula_bytes,
            r.measured_peak_bytes,
            r.measured_peak_bytes as f64 / r.formula_bytes as f64
        )
        .unwrap();
    }
    eprintln!("wrote {} rows to {path}", rows.len());
}

fn main() {
    let args: Vec<String> = std::env::args().collect();
    let csv_path = args
        .iter()
        .position(|a| a == "--csv")
        .and_then(|i| args.get(i + 1))
        .cloned();

    // Small-end anchor is (40,4): the former (24,5) anchor is invalid — cbl 4
    // under-fills expanded rows (REVIEW_REQ.md F14; Params now rejects it),
    // so SIZING §2a's (24,5) row is historical-only. (40,4) is the smallest
    // valid sweep point (cbl 8) per the T2.2 calibration protocol.
    let params = [(40u32, 4u32), (48, 5), (72, 5), (96, 5)];
    let mut rows = measure("reference", &params);
    rows.extend(measure("arena", &params));

    if let Some(path) = csv_path {
        write_csv(&path, &rows);
    } else {
        println!("Measured peak allocation vs SIZING.md's 'naive' formula (N * (n/8+4)):\n");
        print_rows(&rows);
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
             surprise -- but it had never actually been measured until now.\n\
             Use --csv <path> for exact byte counts instead of rounded display."
        );
    }
}
