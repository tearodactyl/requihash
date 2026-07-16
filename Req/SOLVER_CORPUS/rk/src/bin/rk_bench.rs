//! Measures `Equihash::find_proof`'s wall time (repeated trials,
//! min/median/MAD) and peak memory (counting allocator, cross-checked
//! against OS RSS), stamped with git provenance — via the shared
//! `reqbench` crate (`Req/SOLVER_CORPUS/reqbench/`). See `Req/BENCH.md`
//! for the discipline this implements and why.
//!
//! Unlike RZ (which is hardcoded to one `(WN,WK,RESTBITS)` triple), RK
//! is parameter-generic, so the "cases" here are `(n,k,seed)` points
//! rather than fixed nonces at one fixed parameter set — chosen to
//! stay well under the timeout/memory ceiling this port's measured
//! scaling (README.md) puts on `(192,7)`/`(200,9)`.
//!
//! Run with: cargo run --release --bin rk_bench
//!            cargo run --release --bin rk_bench -- --baseline baselines/apple-silicon.jsonl --tag apple-silicon
//!            cargo run --release --bin rk_bench -- --json baselines/apple-silicon.jsonl --tag apple-silicon

use reqbench::mem::{cross_check, CountingAlloc, MemCrossCheck};
use reqbench::provenance::Provenance;
use reqbench::stats::{compare, load_baseline, Record, Verdict};
use reqbench::{sample, DEFAULT_REPS, DEFAULT_WARMUP_MS};
use rk::{Equihash, Seed};
use std::io::Write as _;

#[global_allocator]
static ALLOC: CountingAlloc = CountingAlloc::new();

struct Case {
    label: &'static str,
    n: u32,
    k: u32,
    seed: u32,
}

fn cases() -> Vec<Case> {
    // Small/fast points only — this port's naive, non-memory-optimized
    // solver reaches multi-GB/multi-minute cost well before (192,7) or
    // (200,9) (README.md "Measured scaling"); benchmarking at those
    // points is out of scope for a repeated-trial harness.
    vec![
        Case { label: "n60_k4", n: 60, k: 4, seed: 5 },
        Case { label: "n90_k5", n: 90, k: 5, seed: 5 },
        Case { label: "n100_k4", n: 100, k: 4, seed: 5 },
        Case { label: "n108_k5", n: 108, k: 5, seed: 5 },
    ]
}

fn main() {
    let args: Vec<String> = std::env::args().collect();
    let json_path = args
        .iter()
        .position(|a| a == "--json")
        .and_then(|i| args.get(i + 1))
        .cloned();
    let baseline_path = args
        .iter()
        .position(|a| a == "--baseline")
        .and_then(|i| args.get(i + 1))
        .cloned();
    let tag = args
        .iter()
        .position(|a| a == "--tag")
        .and_then(|i| args.get(i + 1))
        .cloned()
        .unwrap_or_else(|| "unknown".to_string());
    let reps: usize = args
        .iter()
        .position(|a| a == "--reps")
        .and_then(|i| args.get(i + 1))
        .and_then(|s| s.parse().ok())
        .unwrap_or(DEFAULT_REPS);

    let prov = Provenance::capture();
    println!(
        "rk_bench: Equihash::find_proof -- git={} ({}) profile={} arch={}\n",
        prov.git_rev,
        if prov.git_dirty { "dirty" } else { "clean" },
        prov.profile,
        std::env::consts::ARCH,
    );
    if prov.profile == "debug" {
        eprintln!(
            "warning: running in debug profile -- timing numbers are not \
             representative of production performance; use --release"
        );
    }

    let baseline = baseline_path
        .as_deref()
        .and_then(|p| std::fs::read_to_string(p).ok())
        .map(|text| load_baseline(&text))
        .unwrap_or_default();

    let mut json_lines: Vec<String> = Vec::new();

    for case in cases() {
        let timing_samples = sample(reps, DEFAULT_WARMUP_MS, || {
            let mut eq = Equihash::new(case.n, case.k, Seed::from_u32(case.seed));
            let proof = eq.find_proof();
            std::hint::black_box(&proof);
        });
        let key = format!("find_proof/{}", case.label);
        let rec = Record::from_samples(&key, 1, &timing_samples);

        ALLOC.reset();
        let mut eq = Equihash::new(case.n, case.k, Seed::from_u32(case.seed));
        let proof = eq.find_proof();
        let peak = ALLOC.peak_bytes() as u64;
        let check = cross_check(peak, 5.0);

        println!("  {} (n={}, k={}, seed={}):", case.label, case.n, case.k, case.seed);
        println!(
            "    wall: min {:.1}ms / median {:.1}ms / MAD {:.2}ms over {} reps",
            rec.min_ns as f64 / 1e6,
            rec.median_ns as f64 / 1e6,
            rec.mad_ns as f64 / 1e6,
            rec.reps,
        );
        println!("    solution size: {}", proof.inputs.len());
        match check {
            MemCrossCheck::Agree {
                allocator_bytes,
                rss_bytes,
            } => println!(
                "    peak mem: {} (allocator) / {} (OS RSS) -- agree",
                human(allocator_bytes),
                human(rss_bytes)
            ),
            MemCrossCheck::Disagree {
                allocator_bytes,
                rss_bytes,
                diff_pct,
            } => println!(
                "    peak mem: {} (allocator) / {} (OS RSS) -- DISAGREE by {:.1}%, investigate before trusting either",
                human(allocator_bytes),
                human(rss_bytes),
                diff_pct
            ),
            MemCrossCheck::RssUnavailable { allocator_bytes } => println!(
                "    peak mem: {} (allocator only -- OS RSS cross-check unavailable on this platform, uncorroborated)",
                human(allocator_bytes)
            ),
        }

        if !baseline.is_empty() {
            match compare(&rec, &baseline, 5) {
                Verdict::Win(pct) => println!("    vs baseline: WIN +{pct:.1}%"),
                Verdict::Regression(pct) => println!("    vs baseline: REGRESSION -{pct:.1}%"),
                Verdict::Noise => println!("    vs baseline: within noise band"),
                Verdict::New => println!("    vs baseline: no prior entry for this key"),
                Verdict::Untracked => println!("    vs baseline: untracked (reps < 3)"),
            }
        }

        json_lines.push(rec.to_json_line(&tag, &prov));
        println!();
    }

    if let Some(path) = json_path {
        let mut f = std::fs::OpenOptions::new()
            .create(true)
            .append(true)
            .open(&path)
            .unwrap_or_else(|e| panic!("open {path}: {e}"));
        for line in &json_lines {
            writeln!(f, "{line}").unwrap();
        }
        eprintln!("appended {} record(s) to {path}", json_lines.len());
    }
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
