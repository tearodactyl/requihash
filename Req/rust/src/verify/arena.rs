//! Arena verifier: same validity checks as the reference, but leaf rows live in
//! one flat buffer and the fold works in place, avoiding the per-row Vec the
//! reference verifier allocates. Equivalent output; measured against the reference.

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
        let cbyte = p.collision_byte_length();
        let full = (k + 1) * cbyte;

        // Flat leaf hashes: row r at [r*full .. (r+1)*full]. Index tracking uses a
        // parallel Vec of index sub-slices kept as (start,len) into `idx_flat`.
        let n_leaves = indices.len();
        let mut hashes = vec![0u8; n_leaves * full];
        for (r, &leaf) in indices.iter().enumerate() {
            let row = engine.leaf_row(leaf);
            hashes[r * full..(r + 1) * full].copy_from_slice(&row);
        }
        // index vectors per current row; start one-per-leaf
        let mut idx: Vec<Vec<EhIndex>> = indices.iter().map(|&i| vec![i]).collect();

        let mut stride = full;
        let mut nrows = n_leaves;
        for round in 1..=k {
            let off = (round - 1) * cbyte;
            let new_stride = stride; // we keep full width, compare at moving offset
            let mut out_hashes = vec![0u8; (nrows / 2) * new_stride];
            let mut out_idx: Vec<Vec<EhIndex>> = Vec::with_capacity(nrows / 2);
            let mut w = 0;
            let mut r = 0;
            while r < nrows {
                let a = r;
                let b = r + 1;
                let ha = &hashes[a * stride + off..a * stride + off + cbyte];
                let hb = &hashes[b * stride + off..b * stride + off + cbyte];
                if ha != hb {
                    return Err(Error::CollisionFailed(round as u32));
                }
                if !(idx[a] < idx[b]) {
                    return Err(Error::OrderingFailed(round as u32));
                }
                // XOR whole rows into the output slot
                {
                    let (src, dst) = (a * stride, w * new_stride);
                    for t in 0..stride {
                        out_hashes[dst + t] = hashes[src + t] ^ hashes[b * stride + t];
                    }
                }
                let mut merged = idx[a].clone();
                merged.extend_from_slice(&idx[b]);
                out_idx.push(merged);
                w += 1;
                r += 2;
            }
            hashes = out_hashes;
            idx = out_idx;
            stride = new_stride;
            nrows = w;
        }
        if nrows != 1 {
            return Err(Error::WrongLength);
        }
        // root: after k rounds the first k*cbyte bytes must be zero
        if hashes[..k * cbyte].iter().any(|&c| c != 0) {
            return Err(Error::NonZeroRoot);
        }
        Ok(())
    }
    fn name(&self) -> &'static str {
        "verify-arena"
    }
}
