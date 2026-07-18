//! Early-reject verifier: a fused fold that rejects on the first failing collision
//! or ordering check and never materialises index vectors. Because the tree is
//! perfectly balanced and the input order fixes each subtree, a subtree spanning
//! leaves [lo, hi) has its "index vector" implicitly = indices[lo..hi]; canonical
//! ordering reduces to comparing indices[lo..mid] < indices[mid..hi] as slices,
//! and distinctness need only be checked once globally.
//!
//! Storage (T2.3 F7): one flat hash buffer per level — a single allocation per
//! round, rows of `stride` bytes, with `stride` shrinking by `cbyte` each round
//! as the collided segment is consumed. This aligns the layout with the arena
//! verifier and the solver backends (comparisons at offset 0), and makes the
//! root check inspect exactly the surviving final segment — the previous
//! full-width layout's root check read the first `k*cbyte` bytes, which are
//! zero by construction for any input passing the collision rounds (F10).

use super::Verifier;
use crate::{EhIndex, Error, Requihash};

pub struct EarlyRejectVerifier;

impl Verifier for EarlyRejectVerifier {
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

        // Level 0: one hash row per leaf, one flat buffer.
        let mut stride = full;
        let mut level = vec![0u8; expected * stride];
        for (r, &i) in indices.iter().enumerate() {
            level[r * stride..(r + 1) * stride].copy_from_slice(&engine.leaf_row(i));
        }

        // Fold k rounds. At round r pairs (2a, 2a+1) must collide on the leading
        // cbyte of their (shrunken) rows, and their index sub-slices must be
        // canonically ordered.
        let mut nrows = expected;
        let mut span = 1usize; // leaves per node at the start of this round
        for round in 1..=k {
            let new_stride = stride - cbyte;
            let mut next = vec![0u8; (nrows / 2) * new_stride];
            let mut a = 0;
            let mut w = 0;
            while a < nrows {
                let b = a + 1;
                // collision on this round's (leading) segment
                if level[a * stride..a * stride + cbyte]
                    != level[b * stride..b * stride + cbyte]
                {
                    return Err(Error::CollisionFailed(round as u32));
                }
                // canonical ordering: left leaves < right leaves (as slices)
                let lo = a * span;
                let mid = lo + span;
                let hi = mid + span;
                if indices[lo..mid] >= indices[mid..hi] {
                    return Err(Error::OrderingFailed(round as u32));
                }
                // XOR only the surviving suffix for the next level
                for t in 0..new_stride {
                    next[w * new_stride + t] =
                        level[a * stride + cbyte + t] ^ level[b * stride + cbyte + t];
                }
                a += 2;
                w += 1;
            }
            level = next;
            stride = new_stride;
            nrows /= 2;
            span *= 2;
        }
        // Invariant, not an input error: 2^k rows halve exactly k times (T2.3 F6).
        assert_eq!(nrows, 1, "internal: fold must reduce to exactly one row");
        // Root: the surviving final segment (stride == cbyte here) must be zero
        // (F10 fix — see module docs).
        if level[..stride].iter().any(|&c| c != 0) {
            return Err(Error::NonZeroRoot);
        }
        Ok(())
    }
    fn name(&self) -> &'static str {
        "verify-early"
    }
}
