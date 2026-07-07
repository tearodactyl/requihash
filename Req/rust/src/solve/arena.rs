//! Tier 1+ (optimized reference) solver: arena / struct-of-arrays layout. Same
//! algorithm and output as the reference, but rows live in flat buffers and each
//! round sorts a u32 permutation instead of moving rows. Removes the per-row heap
//! allocation the profile showed to be 59% of solve time (BENCHMARK.md); ~1.6x
//! faster. Cross-validated against the reference in tests.

use super::Solver;
use crate::{EhIndex, Requihash};

pub struct ArenaSolver;

impl Solver for ArenaSolver {
    fn solve(&self, engine: &Requihash) -> Vec<Vec<EhIndex>> {
        engine.solve_arena()
    }
    fn name(&self) -> &'static str {
        "solve-arena"
    }
}
