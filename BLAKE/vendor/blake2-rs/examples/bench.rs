//! Exercise the accelerated implementations against blake2ref before any
//! Seam A wiring decision (PLAN A22 gate). Two shapes:
//!  - "leaf": the Equihash miner pattern — 140-byte midstate, then per
//!    leaf clone + 4-byte suffix + finalize(50). Reported as ns/leaf.
//!  - "bulk": 1 MiB single message, digest 64. Reported as MB/s.
//! Repeated trials, min/median reported (no external bench framework —
//! consistent with reqbench's discipline, kept dependency-free here).

use std::time::Instant;

const REPS: usize = 9;
const LEAVES: u32 = 1 << 17;
const BULK: usize = 1 << 20;

fn stats(mut ns: Vec<f64>) -> (f64, f64) {
    ns.sort_by(|a, b| a.partial_cmp(b).unwrap());
    (ns[0], ns[ns.len() / 2])
}

fn main() {
    let mut person = [0u8; 16];
    person[..8].copy_from_slice(b"ZcashPoW");
    person[8..12].copy_from_slice(&200u32.to_le_bytes());
    person[12..16].copy_from_slice(&9u32.to_le_bytes());
    let header: Vec<u8> = (0..140u32).map(|i| (i * 13 % 251) as u8).collect();
    let bulk: Vec<u8> = (0..BULK).map(|i| (i * 7 % 251) as u8).collect();

    println!("arch: {}", std::env::consts::ARCH);

    // --- leaf shape ---
    let mut sink = 0u64;

    let (min, med) = stats(
        (0..REPS)
            .map(|_| {
                let mut mid = blake2ref::Blake2b::with_personal(50, &person);
                mid.update(&header);
                let t = Instant::now();
                for leaf in 0..LEAVES {
                    let mut s = mid.clone();
                    s.update(&leaf.to_le_bytes());
                    sink ^= s.finalize()[0] as u64;
                }
                t.elapsed().as_nanos() as f64 / LEAVES as f64
            })
            .collect(),
    );
    println!("leaf  blake2ref            min {min:7.1} ns/leaf  median {med:7.1}");

    let (min, med) = stats(
        (0..REPS)
            .map(|_| {
                let mut params = blake2b_simd::Params::new();
                params.hash_length(50).personal(&person);
                let mut mid = params.to_state();
                mid.update(&header);
                let t = Instant::now();
                for leaf in 0..LEAVES {
                    let mut s = mid.clone();
                    s.update(&leaf.to_le_bytes());
                    sink ^= s.finalize().as_bytes()[0] as u64;
                }
                t.elapsed().as_nanos() as f64 / LEAVES as f64
            })
            .collect(),
    );
    println!("leaf  blake2b_simd (state) min {min:7.1} ns/leaf  median {med:7.1}");

    // blake2b_simd many:: batch — the accelerated API shape (4-way on AVX2;
    // on aarch64 it degrades to portable one-at-a-time internally).
    let (min, med) = stats(
        (0..REPS)
            .map(|_| {
                let mut params = blake2b_simd::Params::new();
                params.hash_length(50).personal(&person);
                let t = Instant::now();
                let mut leaf = 0u32;
                while leaf < LEAVES {
                    let inputs: Vec<Vec<u8>> = (0..blake2b_simd::many::MAX_DEGREE as u32)
                        .map(|i| {
                            let mut m = header.clone();
                            m.extend_from_slice(&(leaf + i).to_le_bytes());
                            m
                        })
                        .collect();
                    let mut jobs: Vec<blake2b_simd::many::HashManyJob> = inputs
                        .iter()
                        .map(|m| blake2b_simd::many::HashManyJob::new(&params, m))
                        .collect();
                    blake2b_simd::many::hash_many(jobs.iter_mut());
                    for j in &jobs {
                        sink ^= j.to_hash().as_bytes()[0] as u64;
                    }
                    leaf += blake2b_simd::many::MAX_DEGREE as u32;
                }
                t.elapsed().as_nanos() as f64 / LEAVES as f64
            })
            .collect(),
    );
    println!("leaf  blake2b_simd (many)  min {min:7.1} ns/leaf  median {med:7.1}");

    let (min, med) = stats(
        (0..REPS)
            .map(|_| {
                let mut mid = blake3::Hasher::new();
                mid.update(&header);
                let t = Instant::now();
                for leaf in 0..LEAVES {
                    let mut s = mid.clone();
                    s.update(&leaf.to_le_bytes());
                    let mut out = [0u8; 50];
                    s.finalize_xof().fill(&mut out);
                    sink ^= out[0] as u64;
                }
                t.elapsed().as_nanos() as f64 / LEAVES as f64
            })
            .collect(),
    );
    println!("leaf  blake3 (no person)   min {min:7.1} ns/leaf  median {med:7.1}   [different hash, XOF-truncated; no BLAKE2 personalization]");

    // --- bulk shape ---
    let mbps = |ns_total: f64| (BULK as f64 / (ns_total / 1e9)) / 1e6;

    let (min, _med) = stats(
        (0..REPS)
            .map(|_| {
                let t = Instant::now();
                sink ^= blake2ref::blake2b(64, &[0u8; 16], &bulk)[0] as u64;
                t.elapsed().as_nanos() as f64
            })
            .collect(),
    );
    println!("bulk  blake2ref            {:7.1} MB/s", mbps(min));

    let (min, _med) = stats(
        (0..REPS)
            .map(|_| {
                let t = Instant::now();
                sink ^= blake2b_simd::blake2b(&bulk).as_bytes()[0] as u64;
                t.elapsed().as_nanos() as f64
            })
            .collect(),
    );
    println!("bulk  blake2b_simd         {:7.1} MB/s", mbps(min));

    let (min, _med) = stats(
        (0..REPS)
            .map(|_| {
                let t = Instant::now();
                sink ^= blake3::hash(&bulk).as_bytes()[0] as u64;
                t.elapsed().as_nanos() as f64
            })
            .collect(),
    );
    println!("bulk  blake3               {:7.1} MB/s", mbps(min));

    eprintln!("(sink {sink:x})");
}
