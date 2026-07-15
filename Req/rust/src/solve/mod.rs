//! Seam B: the solver backend. All solvers take the engine (which owns the
//! precomputed BLAKE2b base and params) and return the same solution set; they
//! differ only in memory layout and parallelism. The verifier is deliberately NOT
//! part of this seam (it lives in `verify/`), because a miner may be exotic but the
//! consensus-critical validator must stay single, scalar, and auditable.

use crate::{EhIndex, Requihash};

pub mod reference;
pub mod arena;
pub mod bucket;
#[cfg(feature = "rayon")]
pub mod parallel;
/// Design + prototype for PLAN.md A6 (compact index-pointer storage).
/// Deliberately not registered in `all_solvers()` — see module docs for
/// what's proven vs. what a production `solve::pointer` backend still needs.
pub mod pointer;

pub trait Solver {
    fn solve(&self, engine: &Requihash) -> Vec<Vec<EhIndex>>;
    fn name(&self) -> &'static str;
}

/// All registered solver backends, for benchmarking and equivalence testing.
pub fn all_solvers() -> Vec<Box<dyn Solver>> {
    let mut v: Vec<Box<dyn Solver>> = vec![
        Box::new(reference::ReferenceSolver),
        Box::new(arena::ArenaSolver),
        Box::new(bucket::BucketSolver),
    ];
    #[cfg(feature = "rayon")]
    v.push(Box::new(parallel::ParallelSolver));
    v
}
