//! CLI driver: `rk_gen <n> <k> <seed>` runs the Rust port's `find_proof`
//! and prints one `{"n":...,"k":...,"seed":...,"nonce":...,"indices":[...],"verified":...}`
//! JSON line, matching the C++ vector generator's (`vecgen.cc`, not
//! committed here -- scratch tooling used once to produce the checked-in
//! `vectors/*.json`) output shape exactly.

use rk::{Equihash, Seed};
use std::env;
use std::process::ExitCode;

fn main() -> ExitCode {
    let args: Vec<String> = env::args().collect();
    if args.len() != 4 {
        eprintln!("usage: {} <n> <k> <seed>", args[0]);
        return ExitCode::FAILURE;
    }
    let n: u32 = args[1].parse().expect("n must be a non-negative integer");
    let k: u32 = args[2].parse().expect("k must be a non-negative integer");
    let seed: u32 = args[3].parse().expect("seed must be a non-negative integer");

    let mut eq = Equihash::new(n, k, Seed::from_u32(seed));
    let proof = eq.find_proof();
    let verified = proof.test();

    let indices: Vec<String> = proof.inputs.iter().map(|i| i.to_string()).collect();
    println!(
        "{{\"n\":{},\"k\":{},\"seed\":{},\"nonce\":{},\"indices\":[{}],\"verified\":{}}}",
        proof.n,
        proof.k,
        seed,
        proof.nonce,
        indices.join(","),
        verified
    );
    ExitCode::SUCCESS
}
