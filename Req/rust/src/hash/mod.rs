//! Seam A: the leaf hash primitive.
//!
//! Requihash needs two operations from its hash, split by cost profile:
//!   - `hash_one`  : one leaf, latency-bound, used by the verifier and tests.
//!   - `hash_many` : a batch of independent leaves, throughput-bound, used by the
//!                   miner. Requihash's regularity keying (leaf -> (leaf mod k,
//!                   leaf / k)) makes leaves independent, so this is embarrassingly
//!                   parallel and is where SIMD / GPU / dataflow backends pay off.
//!
//! A backend implements `hash_one` and gets `hash_many` for free (serial default);
//! a fast backend overrides `hash_many`. Every backend must agree bit-for-bit with
//! the scalar reference (enforced by the self-test gate in `autodetect`).

use crate::Params;

pub mod scalar;
#[cfg(feature = "simd")]
pub mod simd;

/// The leaf-hash seam. `person` and `prefix` (input||nonce) are fixed per solve;
/// `(class, counter)` is the per-leaf key.
pub trait LeafHasher: Send + Sync {
    fn output_len(&self) -> usize;

    /// H(person || prefix || le32(class) || le32(counter)) -> out[..output_len].
    fn hash_one(&self, person: &[u8; 16], prefix: &[u8], class: u32, counter: u32, out: &mut [u8]);

    /// Batch: `keys` are (class, counter); `out` is keys.len()*output_len bytes.
    /// Default = serial loop over hash_one; fast backends override.
    fn hash_many(&self, person: &[u8; 16], prefix: &[u8], keys: &[(u32, u32)], out: &mut [u8]) {
        let n = self.output_len();
        for (i, &(c, j)) in keys.iter().enumerate() {
            self.hash_one(person, prefix, c, j, &mut out[i * n..(i + 1) * n]);
        }
    }

    fn name(&self) -> &'static str;
}

/// Self-test: does candidate agree with the scalar reference on a fixed vector?
pub fn agrees_with_reference(reference: &dyn LeafHasher, cand: &dyn LeafHasher) -> bool {
    if reference.output_len() != cand.output_len() {
        return false;
    }
    let n = reference.output_len();
    let person = *b"ReqhashPoW\x60\x00\x00\x00\x05\x00"; // arbitrary fixed 16 bytes
    let prefix = b"self-test-prefix";
    let keys: Vec<(u32, u32)> = (0..64u32).map(|i| (i % 5, i / 5)).collect();
    let mut a = vec![0u8; keys.len() * n];
    let mut b = vec![0u8; keys.len() * n];
    reference.hash_many(&person, prefix, &keys, &mut a);
    cand.hash_many(&person, prefix, &keys, &mut b);
    a == b
}

/// Pick the fastest available leaf hasher that passes the self-test gate.
/// Falls back to scalar if any candidate disagrees (a 1-bit-off backend would
/// silently fork consensus, so this gate is non-optional).
pub fn autodetect(_p: Params) -> Box<dyn LeafHasher> {
    let reference = scalar::Blake2bScalar::new();
    let mut candidates: Vec<Box<dyn LeafHasher>> = Vec::new();
    #[cfg(feature = "simd")]
    candidates.push(Box::new(simd::Blake2bSimd::new()));
    for c in candidates {
        if agrees_with_reference(&reference, c.as_ref()) {
            return c;
        }
    }
    Box::new(reference)
}
