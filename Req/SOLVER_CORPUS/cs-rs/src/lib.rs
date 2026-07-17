//! Native Rust port of the 2025/1351 paper's own Python "Sequihash"
//! k-list Wagner-algorithm reference solver — a faithful re-port of the
//! C++ reference in `SOLVER_CORPUS/cs/src/klist.cpp` (which itself ports
//! `~/Work/ZK/ZKs/Generalized-Birthday-Problem/GBP-solver/k_list_algorithm.py`).
//! Algorithm only: no profiling, logging, or stdio in this module.
//!
//! Two conventions differ from this project's own Rust/C++ code
//! (`Req/rust`, `Req/cpp`) and are reproduced here EXACTLY, not
//! translated (see `SOLVER_CORPUS/cs/README.md`):
//!
//!   1. `k` is a LIST COUNT (a power of 2), matching the paper's own
//!      `(n, K = 2^k)` table — NOT `Req`'s tree-depth convention where k
//!      is the exponent and `2^k` is the solution size. Here `k` in the
//!      public API IS the solution size (= the paper's `K`).
//!   2. Leaf encoding is an ASCII decimal string `"i-j"` (i = list
//!      index, j = item index) appended to the raw 16-byte nonce — NOT
//!      `Req`'s binary `le32(i mod k) || le32(i div k)`. Matches the
//!      Python `self.nonce + f"{i}-{j}".encode()`.
//!
//! The C++ port is the differential oracle for this one (both derive
//! from the same Python reference and are checked against the same
//! vectors); method names track the Python original.

/// One (value, index_vector) pair carried through the merge tree.
/// `value` is a big-endian byte string (most-significant byte first) up
/// to `n` bits, matching Python's `int.from_bytes(bytes, 'big')` — NOT a
/// fixed-width machine integer, since `n` is caller-chosen (up to ~256).
#[derive(Clone)]
struct HashItem {
    value: Vec<u8>,
    index_vector: Vec<u32>,
}

/// Faithful port of `k_list_wagner_algorithm`.
pub struct KListWagnerAlgorithm {
    n: u32,
    k: u32,
    lgk: u32,   // log2(k) — merge-tree depth
    ell: u32,   // n / (lgk + 1) — bits collided per ordinary round
    nonce: Vec<u8>,
    hash_size: usize, // n / 8 bytes
}

// ---- Arbitrary-precision big-unsigned helpers over big-endian byte
// strings, matching the Python integer semantics the reference uses:
// XOR, right-shift by mask_bit, low-bit extraction. ----

fn big_xor(a: &[u8], b: &[u8]) -> Vec<u8> {
    debug_assert_eq!(a.len(), b.len());
    a.iter().zip(b).map(|(x, y)| x ^ y).collect()
}

/// Right-shift a big-endian byte string by `bits`, same byte width out.
/// `v` is MSB-first (`v[0]` most significant). Shifting the underlying
/// integer right moves bits toward the LEAST significant end — the END
/// of the array — with the front filling with zero. This mirrors the
/// C++ `big_shr` exactly (the direction of this shift was the one real
/// bug found+fixed during the original C++ port).
fn big_shr(v: &[u8], bits: u32) -> Vec<u8> {
    let n = v.len();
    let mut out = vec![0u8; n];
    let byte_shift = (bits / 8) as usize;
    let bit_shift = bits % 8;
    if byte_shift >= n {
        return out; // shifted everything out
    }
    for j in 0..(n - byte_shift) {
        let out_idx = n - 1 - j; // fill from the last (LSB) byte backward
        let src_idx = out_idx - byte_shift;
        let lo_part = v[src_idx] as u16;
        let hi_part = if src_idx > 0 { v[src_idx - 1] as u16 } else { 0 };
        out[out_idx] = if bit_shift == 0 {
            lo_part as u8
        } else {
            ((lo_part >> bit_shift) | (hi_part << (8 - bit_shift))) as u8
        };
    }
    out
}

/// Extract the low `mask_bit` bits of a big-endian byte string as a u64
/// hash-table key. `mask_bit <= 64` for every parameter point this port
/// targets (mask_bit <= 2*ell, ell = n/(lgk+1), n <= 256).
fn low_bits_key(v: &[u8], mask_bit: u32) -> u64 {
    debug_assert!(mask_bit <= 64);
    let mut key: u64 = 0;
    let mut bits_taken: u32 = 0;
    for &byte in v.iter().rev() {
        if bits_taken >= mask_bit {
            break;
        }
        let take = std::cmp::min(8, mask_bit - bits_taken);
        let mut byte_bits = byte as u64;
        if take < 8 {
            byte_bits &= (1u64 << take) - 1;
        }
        key |= byte_bits << bits_taken;
        bits_taken += take;
    }
    key
}

fn is_all_zero(v: &[u8]) -> bool {
    v.iter().all(|&b| b == 0)
}

impl KListWagnerAlgorithm {
    /// Matches `__init__`: n (hash output bits, multiple of 8), k (list
    /// count, power of 2 — this class's convention), nonce (16 bytes).
    pub fn new(n: u32, k: u32, nonce: Vec<u8>) -> Result<Self, String> {
        if n % 8 != 0 {
            return Err("n should be a multiple of 8".into());
        }
        if nonce.len() != 16 {
            return Err("Nonce should be 16 bytes".into());
        }
        let mut lgk = 0u32;
        let mut kk = k;
        while kk > 1 {
            kk >>= 1;
            lgk += 1;
        }
        if (1u32 << lgk) != k {
            return Err("k should be a power of 2".into());
        }
        if n % (lgk + 1) != 0 {
            return Err("n should be divisible by lg(k) + 1".into());
        }
        let ell = n / (lgk + 1);
        let hash_size = (n / 8) as usize;
        Ok(Self { n, k, lgk, ell, nonce, hash_size })
    }

    pub fn n(&self) -> u32 { self.n }
    pub fn k(&self) -> u32 { self.k }
    pub fn lgk(&self) -> u32 { self.lgk }
    pub fn ell(&self) -> u32 { self.ell }

    /// Matches `compute_item(i, j)`: BLAKE2b(nonce ++ ascii("{i}-{j}")),
    /// digest length n/8 bytes, returned as a big-endian byte string.
    /// Plain unkeyed BLAKE2b, no personalization — exactly the C++ port's
    /// `blake2b(digest, n/8, msg, len, NULL, 0)`.
    pub fn compute_item(&self, i: u32, j: u32) -> Vec<u8> {
        let suffix = format!("{i}-{j}");
        let mut message = self.nonce.clone();
        message.extend_from_slice(suffix.as_bytes());

        let hash = blake2b_simd::Params::new()
            .hash_length(self.hash_size)
            .hash(&message);
        // BLAKE2b's digest bytes are already the big-endian representation
        // Python's int.from_bytes(...,'big') would produce — no transform.
        hash.as_bytes().to_vec()
    }

    /// Matches `compute_hash_list_on_the_fly(i, None, None)`: the full,
    /// untrimmed list for list index `i` — 2^ell items.
    fn compute_hash_list(&self, i: u32) -> Vec<HashItem> {
        let count = 1u64 << self.ell;
        let mut list = Vec::with_capacity(count as usize);
        for j in 0..count {
            list.push(HashItem {
                value: self.compute_item(i, j as u32),
                index_vector: vec![j as u32],
            });
        }
        list
    }

    /// Matches `hash_merge(L1, L2, mask_bit)`: bucket L1 by the low
    /// `mask_bit` bits, then for each L2 item emit `(x1^x2) >> mask_bit`
    /// for every collision, concatenating index vectors `idx1 ++ idx2`
    /// (L1's first, matching Python's `idx1 + idx2`).
    fn hash_merge(l1: &[HashItem], l2: &[HashItem], mask_bit: u32) -> Vec<HashItem> {
        use std::collections::HashMap;
        let mut table: HashMap<u64, Vec<&HashItem>> = HashMap::with_capacity(l1.len() * 2);
        for item in l1 {
            table.entry(low_bits_key(&item.value, mask_bit)).or_default().push(item);
        }
        let mut merged = Vec::new();
        for item2 in l2 {
            if let Some(bucket) = table.get(&low_bits_key(&item2.value, mask_bit)) {
                for item1 in bucket {
                    let mut index_vector = item1.index_vector.clone();
                    index_vector.extend_from_slice(&item2.index_vector);
                    merged.push(HashItem {
                        value: big_shr(&big_xor(&item1.value, &item2.value), mask_bit),
                        index_vector,
                    });
                }
            }
        }
        merged
    }

    /// Matches `_solve(None, None, verbose)`: the stack-based post-order
    /// binary merge tree over the k leaf lists — a binary-counter carry.
    fn solve_internal(&self) -> Result<Vec<Vec<u32>>, String> {
        struct StackEntry {
            list: Vec<HashItem>,
            depth: u32,
        }
        let mut stack: Vec<StackEntry> = vec![StackEntry {
            list: self.compute_hash_list(0),
            depth: 0,
        }];

        for i in 1..self.k {
            let mut current_depth = 0u32;
            let mut merged_list = self.compute_hash_list(i);
            while let Some(top) = stack.last() {
                if top.depth != current_depth {
                    break;
                }
                let top = stack.pop().unwrap();
                let mask_bit = if current_depth == self.lgk - 1 {
                    self.ell * 2
                } else {
                    self.ell
                };
                merged_list = Self::hash_merge(&top.list, &merged_list, mask_bit);
                current_depth += 1;
            }
            stack.push(StackEntry { list: merged_list, depth: current_depth });
        }

        if stack.len() != 1 || stack[0].depth != self.lgk {
            return Err("merge tree did not reduce to a single root at depth lgk".into());
        }
        Ok(stack.pop().unwrap().list.into_iter().map(|item| item.index_vector).collect())
    }

    /// Matches `solve(index_bit_length=None)`: no index-trimming
    /// trade-off (the only path the vectors exercise).
    pub fn solve(&self) -> Vec<Vec<u32>> {
        self.solve_internal().expect("merge tree invariant")
    }

    /// Matches `verify_results`: recompute each solution's XOR-accumulator
    /// from scratch and check it is exactly zero across all n bits.
    pub fn verify(&self, solutions: &[Vec<u32>]) -> bool {
        if solutions.is_empty() {
            return true; // "No solution found!" is not a failure.
        }
        for indices in solutions {
            if indices.len() != self.k as usize {
                return false;
            }
        }
        for indices in solutions {
            let mut acc = vec![0u8; self.hash_size];
            for (i, &j) in indices.iter().enumerate() {
                acc = big_xor(&acc, &self.compute_item(i as u32, j));
            }
            if !is_all_zero(&acc) {
                return false;
            }
        }
        true
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    // The porting bug the C++ README documents (big_shr direction) would
    // manifest as a hash-collision explosion: thousands of spurious
    // "solutions" at a point with one real one. This end-to-end solve at
    // the smallest KAT point catches that regression directly.
    #[test]
    fn solves_and_verifies_small_point() {
        let nonce = (0..16u8).map(|b| b.wrapping_mul(17).wrapping_add(0x11)).collect();
        // (n=24, k=8): lgk=3, ell=6 — the smallest committed vector point.
        let solver = KListWagnerAlgorithm::new(24, 8, nonce).unwrap();
        assert_eq!(solver.lgk(), 3);
        assert_eq!(solver.ell(), 6);
        let sols = solver.solve();
        assert!(solver.verify(&sols), "every returned solution must XOR to zero");
    }

    #[test]
    fn rejects_bad_params() {
        let nonce = vec![0u8; 16];
        assert!(KListWagnerAlgorithm::new(23, 8, nonce.clone()).is_err()); // n not mult of 8
        assert!(KListWagnerAlgorithm::new(24, 7, nonce.clone()).is_err()); // k not power of 2
        assert!(KListWagnerAlgorithm::new(24, 8, vec![0u8; 15]).is_err()); // nonce wrong len
    }
}
