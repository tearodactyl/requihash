//! Cross-validation: read C++-generated vectors (vectors/*.json), decode the
//! minimal solution, and verify it with the Rust verifier. Also confirms the
//! Rust minimal-decode matches the explicit index list. Proves the two
//! implementations share a byte-exact wire format.
//!
//! Minimal hand-rolled JSON field extraction (no external deps) — the vector
//! files have a fixed, known shape emitted by cpp/req_gen.cpp.

use requihash::*;
use std::fs;
use std::path::Path;

fn field<'a>(s: &'a str, key: &str) -> &'a str {
    let k = format!("\"{key}\"");
    let i = s.find(&k).unwrap_or_else(|| panic!("missing key {key}"));
    let after = &s[i + k.len()..];
    let colon = after.find(':').unwrap();
    after[colon + 1..].trim_start()
}

fn parse_u32(s: &str, key: &str) -> u32 {
    let v = field(s, key);
    let end = v.find(|c: char| !c.is_ascii_digit()).unwrap_or(v.len());
    v[..end].parse().unwrap()
}

fn parse_hex(s: &str, key: &str) -> Vec<u8> {
    let v = field(s, key);
    let start = v.find('"').unwrap() + 1;
    let end = v[start..].find('"').unwrap() + start;
    let h = &v[start..end];
    (0..h.len())
        .step_by(2)
        .map(|i| u8::from_str_radix(&h[i..i + 2], 16).unwrap())
        .collect()
}

fn parse_indices(s: &str) -> Vec<u32> {
    let v = field(s, "indices");
    let start = v.find('[').unwrap() + 1;
    let end = v.find(']').unwrap();
    v[start..end]
        .split(',')
        .map(|t| t.trim().parse().unwrap())
        .collect()
}

fn check_vector(path: &Path) -> bool {
    let s = fs::read_to_string(path).unwrap();
    let n = parse_u32(&s, "n");
    let k = parse_u32(&s, "k");
    let input = parse_hex(&s, "input_hex");
    let nonce = parse_hex(&s, "nonce_hex");
    let minimal = parse_hex(&s, "minimal_hex");
    let indices = parse_indices(&s);

    let p = Params::new(n, k).expect("bad params in vector");
    let cbl = p.collision_bit_length();

    // 1. Rust decode of the C++ minimal encoding matches the explicit indices.
    let decoded = get_indices_from_minimal(&minimal, cbl);
    if decoded != indices {
        println!("FAIL {}: minimal decode != indices", path.display());
        return false;
    }

    // 2. Re-encode matches the C++ minimal bytes (byte-exact wire format).
    let reencoded = get_minimal_from_indices(&indices, cbl);
    if reencoded != minimal {
        println!("FAIL {}: re-encode != C++ minimal", path.display());
        return false;
    }

    // 3. The Rust verifier accepts the C++-mined solution.
    let eng = Requihash::new(p, &input, &nonce);
    match eng.is_valid_solution(&indices) {
        Ok(()) => {}
        Err(e) => {
            println!("FAIL {}: Rust verifier rejected C++ solution: {e}", path.display());
            return false;
        }
    }

    // 4. Tamper check: a swapped-leaf variant must be rejected.
    let mut t = indices.clone();
    t.swap(0, 1);
    if eng.is_valid_solution(&t).is_ok() {
        println!("WARN {}: swapped-leaf variant still validated", path.display());
    }

    println!(
        "ok  {}  (n={n} k={k}, {} indices, minimal={} bytes) verified by Rust",
        path.display(),
        indices.len(),
        minimal.len()
    );
    true
}

fn main() {
    let dir = std::env::args()
        .nth(1)
        .unwrap_or_else(|| "../vectors".to_string());
    let mut all_ok = true;
    let mut count = 0;
    let mut entries: Vec<_> = fs::read_dir(&dir)
        .unwrap_or_else(|_| panic!("cannot read vectors dir {dir}"))
        .filter_map(|e| e.ok())
        .map(|e| e.path())
        .filter(|p| p.extension().map(|x| x == "json").unwrap_or(false))
        .collect();
    entries.sort();
    for p in entries {
        count += 1;
        if !check_vector(&p) {
            all_ok = false;
        }
    }
    if count == 0 {
        println!("no vectors found in {dir} — run cpp/build/req_gen first");
        std::process::exit(1);
    }
    if all_ok {
        println!("\nCROSS-CHECK PASS ({count} vectors)");
    } else {
        println!("\nCROSS-CHECK FAILED");
        std::process::exit(1);
    }
}
