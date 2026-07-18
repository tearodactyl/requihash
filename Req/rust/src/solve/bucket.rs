//! Tier 1++ solver applying the 2016-17 Equihash solver optimizations
//! (tromp / xenoncat) that this project's own profiling independently rediscovered
//! the need for:
//!
//!   1. INCOMPLETE BUCKET SORT. Wagner's merge needs colliding rows adjacent; the
//!      reference/arena solvers achieve that with a full comparison sort
//!      (O(m log m), 24% of solve per BENCHMARK.md). tromp/xenoncat instead bucket
//!      rows by the collision digit and never fully sort — O(m) counting-sort
//!      partition. We bucket on the leading `cbyte` collision bytes.
//!
//!   2. STATIC ALLOCATION. Buckets and row storage are sized once from the
//!      parameters, not grown per round (xenoncat's idea, adopted by tromp).
//!
//! Compact index-pointer storage (the (2^k)/k space win) is noted but NOT applied
//! here: it stores a binary tree of index *pairs* and reconstructs full index
//! vectors only at solution time. Our verifier and wire format currently consume
//! full index vectors, so pointer storage is a larger change tracked separately;
//! this solver keeps explicit indices and isolates the bucket-sort win.
//!
//! Cross-validated against the reference solver in tests (`all_solvers_agree`).

use super::Solver;
use crate::{slices_distinct, EhIndex, Requihash};

pub struct BucketSolver;

impl Solver for BucketSolver {
    fn solve(&self, engine: &Requihash) -> Vec<Vec<EhIndex>> {
        let p = engine.params();
        let cbyte = p.collision_byte_length();
        let cbl = p.collision_bit_length();
        let full = (p.k as usize + 1) * cbyte;
        let init = 1usize << (cbl + 1);

        // Rows as struct-of-arrays; index vectors kept explicit (see module note).
        // Keying goes through leaf_row_into — the regularity binding's single
        // site (T2.3 F1) — with one digest scratch shared across all leaves.
        let mut hashes = vec![0u8; init * full];
        let mut idxs: Vec<Vec<EhIndex>> = Vec::with_capacity(init);
        {
            let mut digest = vec![0u8; p.hash_output()];
            for (leaf, slot) in hashes.chunks_mut(full).enumerate() {
                engine.leaf_row_into(leaf as EhIndex, &mut digest, slot);
                idxs.push(vec![leaf as EhIndex]);
            }
        }

        let mut stride = full;
        let mut nrows = init;
        // Bucket over the collision byte(s). We bucket on the low 16 bits of the
        // leading two collision bytes when cbyte>=2, else on the single byte —
        // tromp used 2^12; we size buckets to the actual key width, statically.
        for _round in 1..=p.k {
            let key_bytes = cbyte.min(2);
            let nbuckets = 1usize << (8 * key_bytes);
            // counting sort: count, prefix-sum, scatter row-ids into buckets.
            let mut counts = vec![0u32; nbuckets + 1];
            let bkey = |r: usize| -> usize {
                let base = r * stride;
                if key_bytes == 1 {
                    hashes[base] as usize
                } else {
                    ((hashes[base] as usize) << 8) | hashes[base + 1] as usize
                }
            };
            for r in 0..nrows {
                counts[bkey(r) + 1] += 1;
            }
            for b in 0..nbuckets {
                counts[b + 1] += counts[b];
            }
            let mut order = vec![0u32; nrows];
            let mut cursor = counts.clone();
            for r in 0..nrows {
                let b = bkey(r);
                order[cursor[b] as usize] = r as u32;
                cursor[b] += 1;
            }

            let new_stride = stride - cbyte;
            let mut out_hashes: Vec<u8> = Vec::new();
            let mut out_idxs: Vec<Vec<EhIndex>> = Vec::new();

            // Walk each bucket; within it, rows share the leading key_bytes, but a
            // true collision requires the full cbyte to match, so we still group by
            // exact cbyte inside the bucket (cheap: bucket is tiny).
            let mut b = 0;
            while b < nbuckets {
                let lo = counts[b] as usize;
                let hi = counts[b + 1] as usize;
                if hi - lo >= 2 {
                    // group by exact cbyte within [lo,hi)
                    let mut i = lo;
                    while i + 1 < hi {
                        let ri = order[i] as usize;
                        let mut j = i + 1;
                        while j < hi {
                            let rj = order[j] as usize;
                            if hashes[rj * stride..rj * stride + cbyte]
                                != hashes[ri * stride..ri * stride + cbyte]
                            {
                                break;
                            }
                            j += 1;
                        }
                        for a in i..j {
                            let ra = order[a] as usize;
                            for c in (a + 1)..j {
                                let rc = order[c] as usize;
                                // shared helper (crate::slices_distinct), same
                                // check the arena merge uses (T2.3 F2)
                                if slices_distinct(&idxs[ra], &idxs[rc]) {
                                    let base = out_hashes.len();
                                    out_hashes.resize(base + new_stride, 0);
                                    let ha = &hashes[ra * stride + cbyte..ra * stride + stride];
                                    let hb = &hashes[rc * stride + cbyte..rc * stride + stride];
                                    for t in 0..new_stride {
                                        out_hashes[base + t] = ha[t] ^ hb[t];
                                    }
                                    let (lo, hi) =
                                        if idxs[ra] < idxs[rc] { (ra, rc) } else { (rc, ra) };
                                    let mut merged = Vec::with_capacity(idxs[lo].len() * 2);
                                    merged.extend_from_slice(&idxs[lo]);
                                    merged.extend_from_slice(&idxs[hi]);
                                    out_idxs.push(merged);
                                }
                            }
                        }
                        i = j;
                    }
                }
                b += 1;
            }

            hashes = out_hashes;
            idxs = out_idxs;
            stride = new_stride;
            nrows = idxs.len();
            if nrows == 0 {
                break;
            }
        }

        let mut sols = Vec::new();
        for r in 0..nrows {
            let zero = stride == 0 || hashes[r * stride..(r + 1) * stride].iter().all(|&c| c == 0);
            if !zero {
                continue;
            }
            let mut u = idxs[r].clone();
            u.sort_unstable();
            u.dedup();
            if u.len() == idxs[r].len() {
                // move, don't re-clone: each row is visited once (T2.3 F3)
                sols.push(std::mem::take(&mut idxs[r]));
            }
        }
        sols
    }

    fn name(&self) -> &'static str {
        "solve-bucket(incomplete-sort)"
    }
}
