//! Differential test: for every committed vector (generated from the
//! actual Python reference, shared with the C++ port), re-solve with this
//! Rust port and assert the returned index vectors match EXACTLY. This is
//! the same validation the C++ `cs_differential` runs, against the same
//! files — so the two ports and the Python reference all agree byte-for-
//! byte on the solution sets.
//!
//! Vectors live in the sibling C++ port's `../cs/vectors/` (one source of
//! truth, produced once from the Python reference — not regenerated here).

use cs_rs::KListWagnerAlgorithm;
use serde::Deserialize;
use std::path::PathBuf;

#[derive(Deserialize)]
struct Vector {
    n: u32,
    k: u32,
    nonce_hex: String,
    solutions: Vec<Vec<u32>>,
}

fn hex_to_bytes(hex: &str) -> Vec<u8> {
    (0..hex.len() / 2)
        .map(|i| u8::from_str_radix(&hex[i * 2..i * 2 + 2], 16).unwrap())
        .collect()
}

fn vectors_dir() -> PathBuf {
    // cs-rs/tests -> cs-rs -> SOLVER_CORPUS -> cs/vectors
    PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../cs/vectors")
}

/// Solution sets are unordered (both the reference and this port emit
/// them in hash-table iteration order, which is not stable). Compare as
/// sets of index-vectors, each index-vector compared in order.
fn same_solution_set(a: &[Vec<u32>], b: &[Vec<u32>]) -> bool {
    if a.len() != b.len() {
        return false;
    }
    let mut a_sorted = a.to_vec();
    let mut b_sorted = b.to_vec();
    a_sorted.sort();
    b_sorted.sort();
    a_sorted == b_sorted
}

#[test]
fn matches_python_reference_vectors() {
    let dir = vectors_dir();
    let mut checked = 0;
    let mut entries: Vec<_> = std::fs::read_dir(&dir)
        .unwrap_or_else(|e| panic!("cannot read {}: {e}", dir.display()))
        .filter_map(|e| e.ok())
        .map(|e| e.path())
        .filter(|p| p.extension().map(|x| x == "json").unwrap_or(false))
        .collect();
    entries.sort();

    for path in entries {
        let text = std::fs::read_to_string(&path).unwrap();
        let v: Vector = serde_json::from_str(&text)
            .unwrap_or_else(|e| panic!("parse {}: {e}", path.display()));

        let solver = KListWagnerAlgorithm::new(v.n, v.k, hex_to_bytes(&v.nonce_hex))
            .unwrap_or_else(|e| panic!("bad params in {}: {e}", path.display()));
        let got = solver.solve();

        assert!(
            solver.verify(&got),
            "{}: our own solutions failed self-verification",
            path.display()
        );
        assert!(
            same_solution_set(&got, &v.solutions),
            "{} (n={},k={}): solution set differs from the Python reference\n  ours:   {:?}\n  theirs: {:?}",
            path.display(), v.n, v.k, got, v.solutions
        );
        checked += 1;
        eprintln!("ok  {} (n={}, k={}, {} solutions)", path.display(), v.n, v.k, v.solutions.len());
    }
    assert!(checked >= 4, "expected at least 4 CS vectors, checked {checked}");
    eprintln!("cs-rs matches the Python reference on all {checked} vectors");
}
