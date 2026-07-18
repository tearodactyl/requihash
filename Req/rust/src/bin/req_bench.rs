//! Benchmark and profile the Requihash hot paths:
//!   1. leaf hashing throughput (the generator phase in isolation)
//!   2. full instrumented solve (gen vs merge split, per-round list sizes)
//!   3. verifier throughput (the consensus-critical latency path)
//!   4. backend families: solvers, verifiers, hash backends (seams A/B)
//!
//! Run with:  cargo run --release --bin req_bench [-- OPTIONS]
//!   --json <path>       append machine-readable records (JSON lines) after
//!                       the run; this is the OUTPUT series
//!   --baseline <path>   compare this run against a saved JSONL baseline (the
//!                       comparison INPUT). Defaults to the --json path when
//!                       omitted: the standard workflow is compare-against-
//!                       the-rolling-series-then-append (comparison happens
//!                       before appending, so a shared file is sound)
//!   --band-pct <n>      relative noise floor for comparisons (percent)
//!   --family            include the SPEC family campaign (gen/substitution/
//!                       verify-cost across hash x m x params; heavy)
//!   --tag <name>        machine tag recorded in emitted lines (also via
//!                       REQ_BENCH_TAG)
//!
//! Standard single-command run on the reference machine: `./bench.sh` (sets
//! --json/--tag for the machine series; --baseline follows --json).
//!
//! Uses only std::time; no external bench framework so it builds
//! dependency-free. Statistics and the comparison decision rule live in
//! `requihash::report` (min + median + MAD; a delta counts only beyond the
//! MAD band — see BENCHMARK.md §5).

use requihash::probe::{GenProbe, HashKind, Keying};
use requihash::report::{compare, load_baseline, BaselineEntry, Record, Verdict};
use requihash::*;
use std::time::Instant;

fn sample<F: FnMut()>(reps: usize, mut f: F) -> Vec<u128> {
    // Untimed warm-up by time budget, not call count: a microsecond-scale
    // bench can finish entirely on an efficiency core before the scheduler
    // migrates it; keep the core busy long enough to be placed and ramped.
    let warm = Instant::now();
    while warm.elapsed().as_millis() < 50 {
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

fn bench_leaf_hash(n: u32, k: u32, out: &mut Vec<Record>) {
    let p = Params::new(n, k).unwrap();
    let eng = Requihash::new(p, b"bench-input", &0u32.to_le_bytes());
    let leaves = 1u64 << (p.collision_bit_length() + 1);

    let samples = sample(15, || {
        let (rows, count) = eng.hash_all_leaves();
        std::hint::black_box(&rows);
        assert_eq!(count as u64, leaves);
    });
    let rec = Record::from_samples("leaf_hash", n, k, leaves, &samples);
    println!(
        "  leaf-hash ({n},{k}): {leaves} leaves in {:.2} ms  |  {:.1} ns/leaf  |  {:.2} M leaves/s",
        rec.min_ns as f64 / 1e6,
        rec.per_unit_ns(),
        1e3 / rec.per_unit_ns(),
    );
    out.push(rec);
}

fn bench_solve(n: u32, k: u32, out: &mut Vec<Record>) {
    let p = Params::new(n, k).unwrap();
    // find a solving nonce first (cheap for these params)
    let mut nonce = 0u32;
    loop {
        let eng = Requihash::new(p, b"bench-input", &nonce.to_le_bytes());
        let (sols, gen, merge, sizes) = eng.solve_instrumented();
        if !sols.is_empty() {
            let total = gen + merge;
            println!(
                "  solve ({n},{k}) nonce={nonce}: {:.2} ms total  [gen {:.1}% / merge {:.1}%]  {} sols",
                total as f64 / 1e6,
                100.0 * gen as f64 / total as f64,
                100.0 * merge as f64 / total as f64,
                sols.len()
            );
            print!("    round list sizes:");
            for s in &sizes {
                print!(" {s}");
            }
            println!();
            // Phase records from the single instrumented run (reps=1 by
            // nature; the split ratio is the datum, not the wall time).
            out.push(Record::from_samples("solve_phase/gen", n, k, 1, &[gen]));
            out.push(Record::from_samples("solve_phase/merge", n, k, 1, &[merge]));
            return;
        }
        nonce += 1;
        if nonce > 4000 {
            println!("  solve ({n},{k}): no solution within nonce budget");
            return;
        }
    }
}

fn bench_verify(n: u32, k: u32, out: &mut Vec<Record>) {
    let p = Params::new(n, k).unwrap();
    // mine one solution to verify repeatedly
    let mut nonce = 0u32;
    let (eng, sol) = loop {
        let eng = Requihash::new(p, b"bench-input", &nonce.to_le_bytes());
        let sols = eng.solve();
        if let Some(s) = sols.into_iter().next() {
            break (eng, s);
        }
        nonce += 1;
        if nonce > 4000 {
            println!("  verify ({n},{k}): no solution to verify");
            return;
        }
    };
    let iters = 2000u64;
    let samples = sample(9, || {
        for _ in 0..iters {
            std::hint::black_box(eng.is_valid_solution(&sol).is_ok());
        }
    });
    let rec = Record::from_samples("verify", n, k, iters, &samples);
    println!(
        "  verify ({n},{k}): {:.1} us/verify  |  {:.0} verifies/s  ({} leaves/verify)",
        rec.per_unit_ns() / 1e3,
        1e9 / rec.per_unit_ns(),
        1usize << k
    );
    out.push(rec);
}

/// Finds a nonce whose attempt has at least one solution.
fn solving_engine(p: Params) -> Option<Requihash> {
    let mut nonce = 0u32;
    loop {
        let eng = Requihash::new(p, b"bench-input", &nonce.to_le_bytes());
        if !eng.solve_arena().is_empty() {
            return Some(eng);
        }
        nonce += 1;
        if nonce > 4000 {
            return None;
        }
    }
}

fn bench_all_solvers(n: u32, k: u32, out: &mut Vec<Record>) {
    use requihash::solve::all_solvers;
    let p = Params::new(n, k).unwrap();
    let Some(eng) = solving_engine(p) else {
        println!("  ({n},{k}): none");
        return;
    };
    let reps = if n >= 96 { 5 } else { 11 };
    print!("  ({n},{k}):");
    for s in all_solvers() {
        let samples = sample(reps, || {
            std::hint::black_box(s.solve(&eng));
        });
        let rec = Record::from_samples(&format!("solve/{}", s.name()), n, k, 1, &samples);
        print!("  {}={:.1}ms", s.name(), rec.median_ns as f64 / 1e6);
        out.push(rec);
    }
    println!();
}

fn bench_all_verifiers(n: u32, k: u32, out: &mut Vec<Record>) {
    use requihash::verify::all_verifiers;
    let p = Params::new(n, k).unwrap();
    let Some(eng) = solving_engine(p) else {
        println!("  ({n},{k}): none");
        return;
    };
    let sol = eng.solve_arena().into_iter().next().expect("has solution");
    let iters = 3000u64;
    print!("  ({n},{k}):");
    for v in all_verifiers() {
        let samples = sample(9, || {
            for _ in 0..iters {
                std::hint::black_box(v.verify(&eng, &sol).is_ok());
            }
        });
        let rec = Record::from_samples(&format!("verify/{}", v.name()), n, k, iters, &samples);
        print!("  {}={:.1}us", v.name(), rec.per_unit_ns() / 1e3);
        out.push(rec);
    }
    println!();
}

#[cfg(feature = "simd")]
fn bench_hash_backends(n: u32, k: u32, out: &mut Vec<Record>) {
    use requihash::hash::{scalar::Blake2bScalar, simd::Blake2bSimd, LeafHasher};
    let p = Params::new(n, k).unwrap();
    let leaves = 1usize << (p.collision_bit_length() + 1);
    let person = *b"ReqPoW\x00\x00\x00\x00\x00\x00\x00\x00\x05\x00"; // "ReqPoW"(6)+reserved(4)+le32(0)+le16(5)... arbitrary fixed 16 B
    let prefix = b"bench-input\x00\x00\x00\x00";
    let keys: Vec<(u32, u32)> = (0..leaves as u32).map(|i| (i % k, i / k)).collect();
    let scalar = Blake2bScalar::new();
    let simd = Blake2bSimd::new();
    print!("  ({n},{k}):");
    for (name, h) in [
        ("scalar", &scalar as &dyn LeafHasher),
        ("simd", &simd as &dyn LeafHasher),
    ] {
        let mut outbuf = vec![0u8; leaves * h.output_len()];
        let samples = sample(5, || {
            h.hash_many(&person, prefix, &keys, &mut outbuf);
            std::hint::black_box(&outbuf);
        });
        let rec = Record::from_samples(&format!("hash/{name}"), n, k, leaves as u64, &samples);
        print!("  {}={:.2}M/s", name, 1e3 / rec.per_unit_ns());
        out.push(rec);
    }
    println!();
}

fn family_hashes() -> Vec<HashKind> {
    #[allow(unused_mut)]
    let mut v = vec![HashKind::Blake2b];
    #[cfg(feature = "simd")]
    v.push(HashKind::Blake2bSimd);
    #[cfg(feature = "blake3")]
    v.push(HashKind::Blake3);
    v
}

/// SPEC-conformant family campaign: generator throughput per (hash, m) at
/// production-relevant parameters, substitution-variant phase attribution,
/// and verify-shaped cost. Heavy; enabled with --family.
fn bench_family(out: &mut Vec<Record>) {
    println!("\n[F1] GENERATOR gen-phase, ns/leaf-unit (leaf-units = leaves x m):");
    // (96,5) sweeps m; the large parameter points run m=1 (runtime budget).
    let matrix: [(u32, u32, &[u32]); 3] =
        [(96, 5, &[1, 4, 16]), (144, 5, &[1]), (200, 9, &[1])];
    for &(n, k, ms) in &matrix {
        let p = Params::new(n, k).unwrap();
        for kind in family_hashes() {
            for &m in ms {
                let probe = GenProbe::new(p, kind, m, Keying::Regular, b"family-bench", &0u32.to_le_bytes());
                let mut sink = vec![0u8; probe.leaf_count() * probe.row_stride()];
                let reps = if probe.leaf_count() >= 1 << 24 { 3 } else { 5 };
                let samples = sample(reps, || {
                    probe.gen_phase(&mut sink);
                    std::hint::black_box(&sink);
                });
                let units = probe.leaf_count() as u64 * m as u64;
                let rec = Record::from_samples(&format!("gen/{}/m{}", kind.name(), m), n, k, units, &samples);
                println!(
                    "  ({n},{k}) {} m={m}: {:.2} ms/phase | {:.1} ns/leaf-unit | {:.2} M leaf-units/s",
                    kind.name(),
                    rec.min_ns as f64 / 1e6,
                    rec.per_unit_ns(),
                    1e3 / rec.per_unit_ns()
                );
                out.push(rec);
            }
        }
    }

    println!("\n[F2] SUBSTITUTION attribution at (96,5), m=1 (marginals; residual = interaction):");
    let p = Params::new(96, 5).unwrap();
    for kind in family_hashes() {
        let probe = GenProbe::new(p, kind, 1, Keying::Regular, b"family-bench", &0u32.to_le_bytes());
        let mut sink = vec![0u8; probe.leaf_count() * probe.row_stride()];
        let mut mins = Vec::new();
        for (variant, f) in [
            ("real", 0u8),
            ("stub_hash", 1),
            ("stub_assembly", 2),
        ] {
            let samples = sample(5, || {
                match f {
                    0 => probe.gen_phase(&mut sink),
                    1 => probe.gen_phase_stub_hash(&mut sink),
                    _ => probe.gen_phase_stub_assembly(&mut sink),
                }
                std::hint::black_box(&sink);
            });
            let rec = Record::from_samples(
                &format!("gensub/{}/{variant}", kind.name()),
                96,
                5,
                probe.leaf_count() as u64,
                &samples,
            );
            mins.push(rec.min_ns);
            out.push(rec);
        }
        let (real, sh, sa) = (mins[0] as i128, mins[1] as i128, mins[2] as i128);
        let hash_marginal = real - sh;
        let asm_marginal = real - sa;
        let residual = real - hash_marginal - asm_marginal;
        println!(
            "  {}: real {:.2} ms | hash marginal {:.2} ms ({:.0}%) | assembly marginal {:.2} ms ({:.0}%) | residual {:.2} ms ({:.0}%)",
            kind.name(),
            real as f64 / 1e6,
            hash_marginal as f64 / 1e6,
            100.0 * hash_marginal as f64 / real as f64,
            asm_marginal as f64 / 1e6,
            100.0 * asm_marginal as f64 / real as f64,
            residual as f64 / 1e6,
            100.0 * residual as f64 / real as f64,
        );
    }

    println!("\n[F3] VERIFY-shaped cost (2^k x m leaf recomputes + fold), us/verify:");
    for &(n, k) in &[(96u32, 5u32), (144, 5), (200, 9)] {
        let p = Params::new(n, k).unwrap();
        for kind in family_hashes() {
            for m in [1u32, 16] {
                let probe = GenProbe::new(p, kind, m, Keying::Regular, b"family-bench", &0u32.to_le_bytes());
                let iters = 100u64;
                let samples = sample(9, || {
                    for _ in 0..iters {
                        std::hint::black_box(probe.verify_cost());
                    }
                });
                let rec = Record::from_samples(
                    &format!("verifycost/{}/m{m}", kind.name()),
                    n,
                    k,
                    iters,
                    &samples,
                );
                println!(
                    "  ({n},{k}) {} m={m}: {:.1} us/verify",
                    kind.name(),
                    rec.per_unit_ns() / 1e3
                );
                out.push(rec);
            }
        }
    }
}

fn report_against_baseline(records: &[Record], baseline: &[BaselineEntry], band_pct: u32) {
    println!("\n== baseline comparison (band: max(MADs, {band_pct}% of baseline min)) ==");
    let (mut wins, mut regs, mut noise, mut new) = (0, 0, 0, 0);
    for rec in records {
        match compare(rec, baseline, band_pct) {
            Verdict::Win(pct) => {
                wins += 1;
                println!("  WIN        {}  -{pct:.1}%", rec.key());
            }
            Verdict::Regression(pct) => {
                regs += 1;
                println!("  REGRESSION {}  +{pct:.1}%", rec.key());
            }
            Verdict::Noise => noise += 1,
            Verdict::New => {
                new += 1;
                println!("  new        {}", rec.key());
            }
            Verdict::Untracked => {}
        }
    }
    println!("  wins {wins} / regressions {regs} / within noise {noise} / new {new}");
}

fn main() {
    let mut json_path: Option<String> = None;
    let mut baseline_path: Option<String> = None;
    let mut family = false;
    let mut band_pct: u32 = 5;
    let mut tag = std::env::var("REQ_BENCH_TAG").unwrap_or_else(|_| "untagged".into());
    let mut args = std::env::args().skip(1);
    while let Some(a) = args.next() {
        match a.as_str() {
            "--json" => json_path = args.next(),
            "--baseline" => baseline_path = args.next(),
            "--help" | "-h" => {
                println!("see the module doc header of req_bench.rs for options");
                return;
            }
            "--band-pct" => band_pct = args.next().and_then(|v| v.parse().ok()).unwrap_or(band_pct),
            "--family" => family = true,
            "--tag" => tag = args.next().unwrap_or(tag),
            other => {
                eprintln!("unknown argument {other:?}");
                std::process::exit(2);
            }
        }
    }
    // Default: compare against the same series we append to (see doc header).
    if baseline_path.is_none() {
        baseline_path = json_path.clone();
    }

    let mut records: Vec<Record> = Vec::new();

    println!("== Requihash benchmark (multi-backend) ==");
    // ell = n/(k+1); init list = 2^(ell+1). Kept <= (96,5): ell=16, 2^17 leaves.
    // (144,5) has ell=24 -> 2^25 leaves (~gigabytes), out of scope for a quick bench.
    let params = [(48u32, 5u32), (72, 5), (96, 5)];

    println!("\n[1] leaf hashing throughput:");
    for &(n, k) in &params {
        bench_leaf_hash(n, k, &mut records);
    }

    println!("\n[2] full solve, gen vs merge split:");
    for &(n, k) in &params {
        bench_solve(n, k, &mut records);
    }

    println!("\n[3] verifier throughput (consensus-critical latency):");
    for &(n, k) in &params {
        bench_verify(n, k, &mut records);
    }

    println!("\n[4] ALL SOLVER BACKENDS (seam B), median ms:");
    for &(n, k) in &params {
        bench_all_solvers(n, k, &mut records);
    }

    println!("\n[5] ALL VERIFIER BACKENDS (verify seam), us/verify:");
    for &(n, k) in &params {
        bench_all_verifiers(n, k, &mut records);
    }

    #[cfg(feature = "simd")]
    {
        println!("\n[6] HASH BACKENDS (seam A) batched hash_many, M leaves/s:");
        for &(n, k) in &params {
            bench_hash_backends(n, k, &mut records);
        }
    }

    if family {
        bench_family(&mut records);
    }

    if let Some(path) = &baseline_path {
        match std::fs::read_to_string(path) {
            Ok(text) => report_against_baseline(&records, &load_baseline(&text), band_pct),
            Err(e) => eprintln!("baseline {path}: {e}"),
        }
    }

    if let Some(path) = &json_path {
        use std::io::Write as _;
        let mut f = std::fs::OpenOptions::new()
            .create(true)
            .append(true)
            .open(path)
            .expect("open json output");
        for rec in &records {
            writeln!(f, "{}", rec.to_json_line(&tag)).expect("write record");
        }
        println!("\n{} records appended to {path}", records.len());
    }
}
