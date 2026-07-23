//! Single-attempt driver: `rk_single_gen <n> <k> <seed> <nonce> [--reps N]
//! [--no-save]` runs `Equihash::single_attempt` repeatedly (default 5
//! reps, `Req/BENCH.md` §2's floor) and reports min/median/MAD wall time +
//! counting-allocator peak memory as one `reqbench::run_record::RunRecord`
//! JSON line.
//!
//! Every invocation writes a NEW file to
//! `Req/SOLVER_CORPUS/rk/runs/<timestamp>.jsonl` (this binary's own
//! `runs/` subdirectory -- resolved relative to `CARGO_MANIFEST_DIR` so it
//! works regardless of the caller's cwd) -- never appends to an existing
//! file, never touches another implementation's directory. See
//! `reqbench::run_record`'s module doc comment for why (no implementation
//! ever writes another's file; cross-implementation comparison reads
//! multiple `runs/*.jsonl` directories as a separate, later step). Pass
//! `--no-save` to print only, e.g. for a quick manual check.
//!
//! Exists to fix a real measurement gap (README.md "Reconcile the
//! allocator/RSS memory disagreement"): `find_proof`'s retry loop makes
//! allocator-peak (single-attempt) and OS-RSS (whole-process,
//! contaminated by macOS's allocator not releasing pages between
//! attempts) answer different questions, so C++-vs-Rust ratios computed
//! from those two different quantities are not trustworthy. This binary
//! and the equivalent C++ `rk_single_bench` (../../original/) both
//! measure the SAME single-attempt quantity, so a ratio computed from
//! their output is a fair comparison.
//!
//! No solution-finding guarantee: single_attempt does not retry on a
//! failed/duplicate nonce, so it may find zero solutions at the given
//! nonce -- that's fine, this tool measures cost, not correctness (the
//! existing rk_gen/cross_check.rs already cover correctness).
use reqbench::mem::CountingAlloc;
use reqbench::provenance::Provenance;
use reqbench::run_record::{run_filename, KConvention, MemInstrument, RunRecord};
use rk::{check_nk, Equihash, Seed};
use std::io::{BufWriter, Write as _};

#[global_allocator]
static ALLOC: CountingAlloc = CountingAlloc::new();

fn main() {
    let args: Vec<String> = std::env::args().collect();
    if args.len() < 5 {
        eprintln!("usage: {} <n> <k> <seed> <nonce> [--reps N] [--no-save]", args[0]);
        std::process::exit(1);
    }
    let n: u32 = args[1].parse().expect("n");
    let k: u32 = args[2].parse().expect("k");
    if let Err(msg) = check_nk(n, k) {
        eprintln!("{}: invalid (n={n}, k={k}): {msg}", args[0]);
        eprintln!("{}: see Req/scripts/equihash_formulas.py --valid-n {k} for the Req/Equihash-side valid-n list (a related but not identical rule set)", args[0]);
        std::process::exit(1);
    }
    let seed: u32 = args[3].parse().expect("seed");
    let nonce: u32 = args[4].parse().expect("nonce");
    let reps: usize = args
        .iter()
        .position(|a| a == "--reps")
        .and_then(|i| args.get(i + 1))
        .and_then(|s| s.parse().ok())
        .unwrap_or(reqbench::DEFAULT_REPS);
    let no_save = args.iter().any(|a| a == "--no-save");

    let mut wall_ms_samples = Vec::with_capacity(reps);
    let mut peak_bytes = 0u64;
    for _ in 0..reps {
        ALLOC.reset();
        let start = std::time::Instant::now();
        let mut eq = Equihash::new(n, k, Seed::from_u32(seed));
        eq.single_attempt(nonce);
        let elapsed = start.elapsed();
        let _ = &eq; // kept alive through the peak-memory read below
        peak_bytes = peak_bytes.max(ALLOC.peak_bytes() as u64);
        wall_ms_samples.push(elapsed.as_secs_f64() * 1000.0);
    }
    wall_ms_samples.sort_by(|a, b| a.partial_cmp(b).unwrap());
    let min_ms = wall_ms_samples[0];
    let median_ms = wall_ms_samples[wall_ms_samples.len() / 2];
    let mut devs: Vec<f64> = wall_ms_samples.iter().map(|&x| (x - median_ms).abs()).collect();
    devs.sort_by(|a, b| a.partial_cmp(b).unwrap());
    let mad_ms = devs[devs.len() / 2];

    let prov = Provenance::capture();
    let rec = RunRecord {
        impl_name: "rk-rust".to_string(),
        lang: "rust".to_string(),
        n,
        k_or_k: k,
        k_convention: KConvention::TreeDepth,
        nonce_or_seed: format!("seed={seed},nonce={nonce}"),
        reps,
        wall_min_ms: min_ms,
        wall_median_ms: median_ms,
        wall_mad_ms: mad_ms,
        peak_mem_bytes: Some(peak_bytes),
        mem_instrument: MemInstrument::AllocatorPeak,
        solutions_found: None,
        git_rev: prov.git_rev.clone(),
        git_dirty: prov.git_dirty,
        machine: format!("{} ({})", std::env::consts::ARCH, std::env::consts::OS),
    };
    let line = rec.to_json_line();
    println!("{line}");

    if !no_save {
        let runs_dir = std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("runs");
        std::fs::create_dir_all(&runs_dir)
            .unwrap_or_else(|e| panic!("create {}: {e}", runs_dir.display()));
        let path = runs_dir.join(run_filename(n, k, std::time::SystemTime::now()));
        // Buffered I/O: one write() at flush/drop, not one per field --
        // matters more once a driver grows to sweep multiple points per
        // invocation (each a separate line) than for today's single line,
        // but the file is new/exclusive-to-this-run either way (create_new
        // fails loudly on any name collision rather than silently
        // overwriting -- collisions would need two runs at the exact same
        // second, which create_new turns into an explicit error, not
        // silent data loss).
        let f = std::fs::OpenOptions::new()
            .write(true)
            .create_new(true)
            .open(&path)
            .unwrap_or_else(|e| panic!("create {}: {e}", path.display()));
        let mut w = BufWriter::new(f);
        writeln!(w, "{line}").unwrap();
        w.flush().unwrap();
        eprintln!("saved: {}", path.display());
    }
}
