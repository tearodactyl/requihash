//! Early-reject verifier: a fused fold that rejects on the first failing collision
//! or ordering check and never materialises index vectors. Because the tree is
//! perfectly balanced and the input order fixes each subtree, a subtree spanning
//! leaves [lo, hi) has its "index vector" implicitly = indices[lo..hi]; canonical
//! ordering reduces to comparing indices[lo..mid] < indices[mid..hi] as slices,
//! and distinctness need only be checked once globally. This trims allocation to
//! a single hash column per level.

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
        let cbyte = p.collision_byte_length();
        let full = (k + 1) * cbyte;

        // Level 0: one hash row per leaf, flat.
        let mut level: Vec<Vec<u8>> = indices.iter().map(|&i| engine.leaf_row(i)).collect();

        // Fold k rounds. At round r we require pairs (2a, 2a+1) to collide on the
        // r-th cbyte segment and their index sub-slices to be canonically ordered.
        // seg_len shrinks the *number of leaves* per node by 2 each round.
        let mut span = 1usize; // leaves per node at the start of this round
        for round in 1..=k {
            let off = (round - 1) * cbyte;
            let mut next: Vec<Vec<u8>> = Vec::with_capacity(level.len() / 2);
            let mut a = 0;
            while a < level.len() {
                let b = a + 1;
                // collision on this segment
                if level[a][off..off + cbyte] != level[b][off..off + cbyte] {
                    return Err(Error::CollisionFailed(round as u32));
                }
                // canonical ordering: left leaves < right leaves (as slices)
                let lo = (a) * span;
                let mid = lo + span;
                let hi = mid + span;
                if indices[lo..mid] >= indices[mid..hi] {
                    return Err(Error::OrderingFailed(round as u32));
                }
                // XOR the two rows (full width) for the next level
                let mut merged = vec![0u8; full];
                for t in 0..full {
                    merged[t] = level[a][t] ^ level[b][t];
                }
                next.push(merged);
                a += 2;
            }
            level = next;
            span *= 2;
        }
        if level.len() != 1 {
            return Err(Error::WrongLength);
        }
        if level[0][..k * cbyte].iter().any(|&c| c != 0) {
            return Err(Error::NonZeroRoot);
        }
        Ok(())
    }
    fn name(&self) -> &'static str {
        "verify-early"
    }
}
