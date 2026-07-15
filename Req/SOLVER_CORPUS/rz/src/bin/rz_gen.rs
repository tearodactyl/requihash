//! Runs the native Rust `(WN=144, WK=5, RESTBITS=4)` Equihash port on one
//! (input, nonce) pair and prints its solutions as JSON, one solution per
//! line: `{"indices":[...]}` -- the same shape `rz_xcheck_144_4` (the
//! vendored-C cross-check binary built by `build.rs`) prints, so the two
//! are directly diffable.
//!
//! Usage: `rz_gen <input_hex> <nonce_hex>`

use std::env;
use std::process::ExitCode;

fn hex_decode(s: &str) -> Result<Vec<u8>, String> {
    if s.len() % 2 != 0 {
        return Err(format!("hex string has odd length: {s}"));
    }
    (0..s.len())
        .step_by(2)
        .map(|i| {
            u8::from_str_radix(&s[i..i + 2], 16)
                .map_err(|e| format!("bad hex byte at offset {i} in {s:?}: {e}"))
        })
        .collect()
}

fn main() -> ExitCode {
    let args: Vec<String> = env::args().collect();
    if args.len() != 3 {
        eprintln!("usage: {} <input_hex> <nonce_hex>", args[0]);
        return ExitCode::from(2);
    }

    let input = match hex_decode(&args[1]) {
        Ok(v) => v,
        Err(e) => {
            eprintln!("bad input hex: {e}");
            return ExitCode::from(2);
        }
    };
    let nonce = match hex_decode(&args[2]) {
        Ok(v) => v,
        Err(e) => {
            eprintln!("bad nonce hex: {e}");
            return ExitCode::from(2);
        }
    };

    let solutions = rz::solve_144_4(&input, &nonce);
    for solution in &solutions {
        let indices: Vec<String> = solution.iter().map(|i| i.to_string()).collect();
        println!("{{\"indices\":[{}]}}", indices.join(","));
    }

    ExitCode::SUCCESS
}
