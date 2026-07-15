//! Cross-checks the native Rust `(WN=144, WK=5, RESTBITS=4)` port
//! (`rz::solve_144_4`) against the vendored-C cross-check binary
//! `rz_xcheck_144_4` (built unmodified from `equihash-0.3.0/tromp/equi_miner.c`
//! by `build.rs`) across several distinct nonces, asserting the index
//! *sets* returned match exactly.
//!
//! `rz_xcheck_144_4`'s path is supplied at compile time via
//! `RZ_XCHECK_BIN_144_4`, an env var `build.rs` sets with
//! `cargo:rustc-env=...` pointing at the binary it just compiled into
//! `OUT_DIR` -- see `build.rs`'s `PARAM_SETS` / bin-naming scheme.

use std::collections::BTreeSet;
use std::process::Command;

const XCHECK_BIN: &str = env!("RZ_XCHECK_BIN_144_4");

const INPUT_HEX: &str =
    "00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff";

/// (144,5) nonces are absorbed as raw bytes into the BLAKE2b state (see
/// `base_state` in `src/lib.rs` / `make_base_state` in
/// `cross_check_c/harness_main.c`) -- any byte length works since BLAKE2b
/// just absorbs them, so these three are arbitrary distinct 28-byte
/// nonces (28 bytes chosen to match the all-zero nonce the user already
/// hand-verified once against the C binary in this session; length
/// itself is not semantically significant here).
const NONCES_HEX: &[&str] = &[
    "000000000000000000000000000000000000000000000000000000",
    "010101010101010101010101010101010101010101010101010101",
    "00000000000000000000000000000000000000000000000000002a",
];

fn hex_decode(s: &str) -> Vec<u8> {
    (0..s.len())
        .step_by(2)
        .map(|i| u8::from_str_radix(&s[i..i + 2], 16).unwrap())
        .collect()
}

/// Parses the C binary's `{"indices":[...]}\n`-per-line stdout into a
/// list of index-sets (order within a solution is not assumed to be
/// meaningful for the set-equality check; we compare as sets, matching
/// the exit criteria's "raw index set" target).
fn parse_json_lines(stdout: &str) -> Vec<BTreeSet<u32>> {
    stdout
        .lines()
        .filter(|l| !l.trim().is_empty())
        .map(|line| {
            let start = line.find('[').expect("missing '[' in JSON line");
            let end = line.rfind(']').expect("missing ']' in JSON line");
            line[start + 1..end]
                .split(',')
                .map(|tok| tok.trim().parse::<u32>().expect("bad index token"))
                .collect::<BTreeSet<u32>>()
        })
        .collect()
}

fn run_c_xcheck(input_hex: &str, nonce_hex: &str) -> Vec<BTreeSet<u32>> {
    let output = Command::new(XCHECK_BIN)
        .arg(input_hex)
        .arg(nonce_hex)
        .output()
        .unwrap_or_else(|e| panic!("failed to run {XCHECK_BIN}: {e}"));
    assert!(
        output.status.success(),
        "{XCHECK_BIN} exited with {:?}, stderr: {}",
        output.status,
        String::from_utf8_lossy(&output.stderr)
    );
    parse_json_lines(&String::from_utf8_lossy(&output.stdout))
}

fn run_rust(input: &[u8], nonce: &[u8]) -> Vec<BTreeSet<u32>> {
    rz::solve_144_4(input, nonce)
        .into_iter()
        .map(|sol| sol.into_iter().collect::<BTreeSet<u32>>())
        .collect()
}

#[test]
fn cross_check_three_nonces() {
    let input = hex_decode(INPUT_HEX);
    assert_eq!(NONCES_HEX.len(), 3, "task requires at least 3 distinct nonces");

    for nonce_hex in NONCES_HEX {
        let nonce = hex_decode(nonce_hex);

        let mut c_solutions = run_c_xcheck(INPUT_HEX, nonce_hex);
        let mut rust_solutions = run_rust(&input, &nonce);

        c_solutions.sort();
        rust_solutions.sort();

        assert_eq!(
            rust_solutions, c_solutions,
            "solution index-sets differ for nonce {nonce_hex}: rust={rust_solutions:?} c={c_solutions:?}"
        );
        assert!(
            !c_solutions.is_empty(),
            "nonce {nonce_hex} produced zero solutions from the C oracle -- test is vacuous, pick a different nonce"
        );
    }
}
