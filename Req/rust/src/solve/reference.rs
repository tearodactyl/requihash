//! Tier 1 (reference) solver: scalar Wagner tree search, one `Vec` per row.
//! Correctness ground truth; the allocation-heavy baseline the profile flagged.

use super::Solver;
use crate::{EhIndex, Requihash};

pub struct ReferenceSolver;

impl Solver for ReferenceSolver {
    fn solve(&self, engine: &Requihash) -> Vec<Vec<EhIndex>> {
        engine.solve_reference()
    }
    fn name(&self) -> &'static str {
        "solve-reference"
    }
}
