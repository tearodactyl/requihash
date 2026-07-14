//! Batch minimal-encode/decode helper for test-vector pipelines.
//!
//! Reads lines of `n k idx0,idx1,...,idxN` from stdin (space-separated
//! fields, comma-separated index list), computes each solution's
//! `minimal_hex` via this crate's own `get_minimal_from_indices`, verifies
//! the round trip through `get_indices_from_minimal`, and writes
//! `n k minimal_hex` lines to stdout. Panics (with a clear message) on any
//! round-trip mismatch rather than silently emitting a wrong encoding.
//!
//! Used by `tools/extract_zcash_kat.py` + `tools/assemble_vectors.py` to
//! turn a foreign crate's raw index-list vectors into this repo's
//! `vectors/*.json` schema without reimplementing bit-packing by hand.

use requihash::{get_indices_from_minimal, get_minimal_from_indices};
use std::io::{self, BufRead};

fn main() {
    let stdin = io::stdin();
    let mut ok = 0usize;
    let mut fail = 0usize;
    for line in stdin.lock().lines() {
        let line = line.expect("read stdin line");
        if line.trim().is_empty() {
            continue;
        }
        let parts: Vec<&str> = line.splitn(3, ' ').collect();
        let n: u32 = parts[0].parse().expect("parse n");
        let k: u32 = parts[1].parse().expect("parse k");
        let idxs: Vec<u32> = parts[2]
            .split(',')
            .map(|s| s.parse().expect("parse index"))
            .collect();
        let cbitlen = (n / (k + 1)) as usize;

        let minimal = get_minimal_from_indices(&idxs, cbitlen);
        let decoded = get_indices_from_minimal(&minimal, cbitlen);
        if decoded == idxs {
            ok += 1;
        } else {
            fail += 1;
            eprintln!("ROUNDTRIP MISMATCH n={n} k={k}: encoded then decoded {decoded:?} != original {idxs:?}");
        }

        let hex: String = minimal.iter().map(|b| format!("{b:02x}")).collect();
        println!("{n} {k} {hex}");
    }
    eprintln!("roundtrip: {ok} ok, {fail} fail");
    assert_eq!(fail, 0, "{fail} vector(s) failed round-trip encode/decode");
}
