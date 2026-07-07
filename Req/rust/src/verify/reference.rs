//! Reference verifier: the existing tree-fold, delegated to the canonical method.

use super::Verifier;
use crate::{EhIndex, Error, Requihash};

pub struct ReferenceVerifier;

impl Verifier for ReferenceVerifier {
    fn verify(&self, engine: &Requihash, indices: &[EhIndex]) -> Result<(), Error> {
        engine.is_valid_solution(indices)
    }
    fn name(&self) -> &'static str {
        "verify-reference"
    }
}
