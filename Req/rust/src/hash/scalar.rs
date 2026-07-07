//! Tier 1 (reference) leaf hasher: the bundled scalar BLAKE2b. Portable,
//! dependency-free, and the ground truth every other backend is checked against.

use super::LeafHasher;
use crate::blake2b;

pub struct Blake2bScalar;

impl Blake2bScalar {
    pub fn new() -> Self {
        Blake2bScalar
    }
}

impl Default for Blake2bScalar {
    fn default() -> Self {
        Self::new()
    }
}

impl LeafHasher for Blake2bScalar {
    fn output_len(&self) -> usize {
        64
    }

    fn hash_one(&self, person: &[u8; 16], prefix: &[u8], class: u32, counter: u32, out: &mut [u8]) {
        // out length is dictated by the caller's Params::hash_output(); we honor it
        // by initialising BLAKE2b to out.len().
        let mut s = blake2b::init(out.len(), person);
        blake2b::update(&mut s, prefix);
        blake2b::update(&mut s, &class.to_le_bytes());
        blake2b::update(&mut s, &counter.to_le_bytes());
        blake2b::finalize(s, out);
    }

    fn name(&self) -> &'static str {
        "blake2b-scalar"
    }
}
