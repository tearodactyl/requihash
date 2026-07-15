//! Template: measures a port's solve/verify function under the shared
//! `reqbench` discipline (repeated trials, provenance, memory cross-check).
//! Copy to `src/bin/<port>_bench.rs`, rename the crate import and the
//! `solve_under_test` call, adjust `cases()` to this port's real
//! parameters/inputs. See `../../rz/src/bin/rz_bench.rs` for a filled-in
//! example and `Req/BENCH.md` for why each piece exists.
//!
//! Run with: cargo run --release --bin <port>_bench
//!            cargo run --release --bin <port>_bench -- --json baselines/<tag>.jsonl --tag <tag>
//!            cargo run --release --bin <port>_bench -- --baseline baselines/<tag>.jsonl --tag <tag>

use reqbench::mem::{cross_check, CountingAlloc, MemCrossCheck};
use reqbench::provenance::Provenance;
use reqbench::stats::{compare, load_baseline, Record, Verdict};
use reqbench::{sample, DEFAULT_REPS, DEFAULT_WARMUP_MS};
use std::io::Write as _;

#[global_allocator]
static ALLOC: CountingAlloc = CountingAlloc::new();

// TODO: replace with this port's own solve/verify entry point, e.g.:
//   use your_port::solve_under_test;
fn solve_under_test(_input: &[u8], _nonce: &[u8]) -> Vec<Vec<u32>> {
    unimplemented!("TODO: wire up this port's real solve/verify function")
}

fn hex_encode(bytes: &[u8]) -> String {
    bytes.iter().map(|b| format!("{b:02x}")).collect()
}

struct Case {
    label: &'static str,
    input: Vec<u8>,
    nonce: Vec<u8>,
}

fn cases() -> Vec<Case> {
    // TODO: replace with this port's own validated (input, nonce) pairs —
    // ideally the same ones already exercised by tests/cross_check.rs, so
    // bench numbers are directly comparable to what's already proven
    // correct (Req/BENCH.md §5: parameter/window identity must travel
    // with every number).
    vec![Case {
        label: "TODO_case_label",
        input: vec![0u8; 64],
        nonce: vec![0u8; 28],
    }]
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
    // TODO: replace "TODO_port_name" with this port's real binary name.
    println!(
        "TODO_port_name_bench: git={} ({}) profile={} arch={}\n",
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
            let sols = solve_under_test(&case.input, &case.nonce);
            std::hint::black_box(&sols);
        });
        // TODO: replace with this port's own key naming — state the real
        // parameter identity explicitly (e.g. "solve@(WN=..,WK=..,RESTBITS=..)"
        // or "solve@(n=..,k=..)"), not a bare ambiguous number.
        let key = format!("TODO_solve/{}", case.label);
        let rec = Record::from_samples(&key, 1, &timing_samples);

        ALLOC.reset();
        let sols = solve_under_test(&case.input, &case.nonce);
        let peak = ALLOC.peak_bytes() as u64;
        let check = cross_check(peak, 5.0);

        println!("  {} (nonce={}):", case.label, hex_encode(&case.nonce));
        println!(
            "    wall: min {:.1}ms / median {:.1}ms / MAD {:.2}ms over {} reps",
            rec.min_ns as f64 / 1e6,
            rec.median_ns as f64 / 1e6,
            rec.mad_ns as f64 / 1e6,
            rec.reps,
        );
        println!("    solutions: {}", sols.len());
        match check {
            MemCrossCheck::Agree {
                allocator_bytes,
                rss_bytes,
            } => println!(
                "    peak mem: {allocator_bytes} B (allocator) / {rss_bytes} B (OS RSS) -- agree"
            ),
            MemCrossCheck::Disagree {
                allocator_bytes,
                rss_bytes,
                diff_pct,
            } => println!(
                "    peak mem: {allocator_bytes} B (allocator) / {rss_bytes} B (OS RSS) -- DISAGREE by {diff_pct:.1}%, investigate before trusting either"
            ),
            MemCrossCheck::RssUnavailable { allocator_bytes } => println!(
                "    peak mem: {allocator_bytes} B (allocator only -- OS RSS cross-check unavailable)"
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
