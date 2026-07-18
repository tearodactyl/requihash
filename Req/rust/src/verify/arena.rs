//! Arena verifier: same validity checks as the reference, but leaf rows live in
//! one flat buffer and the fold works in place, avoiding the per-row Vec the
//! reference verifier allocates. Equivalent output; measured against the reference.
//!
//! Layout note (T2.3 F5): rows shrink by `cbyte` each round — the collided
//! leading segment is dropped at the XOR, exactly as the solver backends
//! (`solve_arena`, `bucket`) lay out their rows, so comparisons always happen
//! at offset 0. The previous full-width/moving-offset layout also carried a
//! latent bug (F10): its root check inspected the first `k*cbyte` bytes, which
//! are zero by construction for ANY input passing the k collision rounds; the
//! real root condition is the *final* segment, which is precisely the bytes
//! that survive to the end under the shrinking layout.

use super::Verifier;
use crate::{EhIndex, Error, Requihash};

pub struct ArenaVerifier;

impl Verifier for ArenaVerifier {
    fn verify(&self, engine: &Requihash, indices: &[EhIndex]) -> Result<(), Error> {
        let p = engine.params();
        let k = p.k as usize;
        let expected = 1usize << k;
        if indices.len() != expected {
            return Err(Error::WrongLength);
        }
        {
            let mut u = indices.to_vec();
            u.sort_unstable();
            u.dedup();
            if u.len() != indices.len() {
                return Err(Error::NotDistinct);
            }
        }
        // Range check (F11): every index must name a leaf, i.e. < 2^(ell+1).
        let max_leaf = 1u64 << (p.collision_bit_length() + 1);
        if indices.iter().any(|&i| (i as u64) >= max_leaf) {
            return Err(Error::IndexOutOfRange);
        }
        let cbyte = p.collision_byte_length();
        let full = (k + 1) * cbyte;

        // Flat leaf hashes: row r at [r*stride .. (r+1)*stride]; stride shrinks
        // by cbyte per round. Index vectors per current row start one-per-leaf.
        let n_leaves = indices.len();
        let mut stride = full;
        let mut hashes = vec![0u8; n_leaves * stride];
        for (r, &leaf) in indices.iter().enumerate() {
            let row = engine.leaf_row(leaf);
            hashes[r * stride..(r + 1) * stride].copy_from_slice(&row);
        }
        let mut idx: Vec<Vec<EhIndex>> = indices.iter().map(|&i| vec![i]).collect();

        let mut nrows = n_leaves;
        for round in 1..=k {
            let new_stride = stride - cbyte;
            let mut out_hashes = vec![0u8; (nrows / 2) * new_stride];
            let mut out_idx: Vec<Vec<EhIndex>> = Vec::with_capacity(nrows / 2);
            let mut w = 0;
            let mut r = 0;
            while r < nrows {
                let (a, b) = (r, r + 1);
                if hashes[a * stride..a * stride + cbyte]
                    != hashes[b * stride..b * stride + cbyte]
                {
                    return Err(Error::CollisionFailed(round as u32));
                }
                if !(idx[a] < idx[b]) {
                    return Err(Error::OrderingFailed(round as u32));
                }
                // XOR only the surviving suffix into the output slot.
                for t in 0..new_stride {
                    out_hashes[w * new_stride + t] =
                        hashes[a * stride + cbyte + t] ^ hashes[b * stride + cbyte + t];
                }
                // move both halves, no clone (T2.3 F3); ordering was checked above
                let mut merged = std::mem::take(&mut idx[a]);
                merged.reserve(idx[b].len());
                merged.append(&mut idx[b]);
                out_idx.push(merged);
                w += 1;
                r += 2;
            }
            hashes = out_hashes;
            idx = out_idx;
            stride = new_stride;
            nrows = w;
        }
        // Invariant, not an input error: 2^k rows halve exactly k times (T2.3 F6).
        assert_eq!(nrows, 1, "internal: fold must reduce to exactly one row");
        // Root: the surviving final segment (stride == cbyte here) must be zero.
        // This is the real XOR-to-zero condition (F10 fix — see module docs).
        if hashes[..stride].iter().any(|&c| c != 0) {
            return Err(Error::NonZeroRoot);
        }
        Ok(())
    }
    fn name(&self) -> &'static str {
        "verify-arena"
    }
}
