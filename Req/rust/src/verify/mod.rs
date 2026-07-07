//! The verifier seam — the consensus-critical path. Unlike the solver, the
//! verifier's *default* must stay scalar, portable, and auditable, but we support
//! multiple versions so we can measure them and pick the fastest that is provably
//! equivalent. All verifiers implement the same contract as zebra's
//! `equihash::is_valid_solution`: return Ok(()) iff the index vector is a valid
//! Requihash solution.
//!
//! Versions:
//!   - reference : tree-fold with per-row Vec (mirrors the paper's structure).
//!   - arena     : same checks over flat buffers, no per-row allocation.
//!   - early     : fused single pass, rejects on the first failing collision or
//!                 ordering check without building the whole tree.

use crate::{EhIndex, Error, Requihash};

pub mod reference;
pub mod arena;
pub mod early;

pub trait Verifier {
    fn verify(&self, engine: &Requihash, indices: &[EhIndex]) -> Result<(), Error>;
    fn name(&self) -> &'static str;
}

pub fn all_verifiers() -> Vec<Box<dyn Verifier>> {
    vec![
        Box::new(reference::ReferenceVerifier),
        Box::new(arena::ArenaVerifier),
        Box::new(early::EarlyRejectVerifier),
    ]
}
