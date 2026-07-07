//! Tier 2 (native-parallel) solver: rayon-parallel leaf generation feeding the
//! arena merge. Leaf hashing is embarrassingly parallel (each leaf is an
//! independent BLAKE2b), so this parallelizes the generation phase — 12-24% of
//! solve time per the profile — with zero change to the merge. A fuller Tier-2
//! solver would also bucket-partition the merge across threads; this is the first,
//! simplest parallel example and a clean measurement of the generation-parallel
//! ceiling.

use super::Solver;
use crate::{EhIndex, Requihash};
use rayon::prelude::*;

pub struct ParallelSolver;

impl Solver for ParallelSolver {
    fn solve(&self, engine: &Requihash) -> Vec<Vec<EhIndex>> {
        engine.solve_arena_with_leaves(|nrows, full, hashes| {
            // Fill `hashes` (nrows*full bytes) in parallel by leaf.
            hashes
                .par_chunks_mut(full)
                .enumerate()
                .for_each(|(leaf, slot)| {
                    let row = engine.leaf_row(leaf as EhIndex);
                    slot.copy_from_slice(&row);
                });
            let _ = nrows;
        })
    }
    fn name(&self) -> &'static str {
        "solve-parallel(rayon-gen)"
    }
}
