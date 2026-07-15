//! Measures ACTUAL peak allocation and wall-clock time of the `rz` Rust
//! port's `solve_144_4`, via a counting global allocator (same pattern as
//! `Req/rust/src/bin/req_memcheck.rs`) plus `std::time::Instant`. Neither
//! had been measured before this — STATUS.md's timing notes were one-off
//! manual runs (`time cargo run ...`), not a repeatable, structured
//! harness, and peak memory was never measured at all.
//!
//! Run with: cargo run --release --bin rz_bench
//!            cargo run --release --bin rz_bench -- --csv out.csv

use rz::solve_144_4;
use std::alloc::{GlobalAlloc, Layout, System};
use std::io::Write as _;
use std::sync::atomic::{AtomicUsize, Ordering};
use std::time::Instant;

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

struct Row {
    nonce_hex: String,
    solutions: usize,
    wall_ms: f64,
    peak_bytes: u64,
}

fn run_one(input: &[u8], nonce: &[u8]) -> Row {
    reset();
    let start = Instant::now();
    let sols = solve_144_4(input, nonce);
    let wall = start.elapsed();
    Row {
        nonce_hex: hex::encode(nonce),
        solutions: sols.len(),
        wall_ms: wall.as_secs_f64() * 1000.0,
        peak_bytes: peak_bytes() as u64,
    }
}

// Minimal hex encode, no external crate dependency for this one use.
mod hex {
    pub fn encode(bytes: &[u8]) -> String {
        bytes.iter().map(|b| format!("{b:02x}")).collect()
    }
}

fn print_rows(rows: &[Row]) {
    println!(
        "{:>60} {:>10} {:>12} {:>14}",
        "nonce", "solutions", "wall (ms)", "peak mem"
    );
    for r in rows {
        println!(
            "{:>60} {:>10} {:>12.1} {:>14}",
            r.nonce_hex,
            r.solutions,
            r.wall_ms,
            human(r.peak_bytes)
        );
    }
}

fn write_csv(path: &str, rows: &[Row]) {
    let mut f = std::fs::File::create(path).expect("create csv");
    writeln!(f, "nonce_hex,solutions,wall_ms,peak_bytes").unwrap();
    for r in rows {
        writeln!(
            f,
            "{},{},{:.3},{}",
            r.nonce_hex, r.solutions, r.wall_ms, r.peak_bytes
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

    // Same input used throughout STATUS.md's manual checks, plus the same
    // 3 nonces tests/cross_check.rs validates against, so these numbers
    // are directly comparable to what's already been checked for
    // correctness.
    let input: Vec<u8> = (0..64).map(|i| (i % 16) as u8).collect(); // 00112233...ee repeated x4
    let nonces: [&[u8]; 3] = [
        &[0u8; 28],
        &[0x01u8; 28],
        &{
            let mut n = [0u8; 28];
            n[27] = 0x2a;
            n
        },
    ];

    let rows: Vec<Row> = nonces.iter().map(|n| run_one(&input, n)).collect();

    if let Some(path) = csv_path {
        write_csv(&path, &rows);
    } else {
        println!("rz (144,5,4) solve_144_4 — measured peak allocation and wall time:\n");
        print_rows(&rows);
        println!(
            "\nNote: peak_bytes is total live heap tracked by this process's global\n\
             allocator across the entire solve_144_4 call (digit0 through\n\
             candidate reconstruction), not a formula estimate — this had never\n\
             been measured before (STATUS.md's timing notes were one-off manual\n\
             `time` runs, and peak memory was never measured at all). The\n\
             STATUS.md \"Storage layout note\" already flags that this port\n\
             allocates trees0/trees1 up front rather than replicating the C's\n\
             arena-reuse layout, so peak memory here is expected to run higher\n\
             than the C original's — this is the first actual number for that,\n\
             not just the qualitative expectation.\n\
             Use --csv <path> for exact byte counts."
        );
    }
}
