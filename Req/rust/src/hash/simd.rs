//! Tier 3 (special-instruction) leaf hasher: the `blake2b_simd` crate, which ships
//! AVX2 (x86) and portable backends with runtime CPU dispatch, plus a `many` API
//! for batched parallel hashing — the throughput lever for the miner. Must produce
//! byte-identical output to the scalar reference (checked by the self-test gate in
//! `hash::autodetect`); the personalization and update order below match
//! `hash::scalar` exactly.

use super::LeafHasher;
use blake2b_simd::{many::HashManyJob, many::hash_many, Params as B2Params};

pub struct Blake2bSimd;

impl Blake2bSimd {
    pub fn new() -> Self {
        Blake2bSimd
    }
    fn params(&self, out_len: usize, person: &[u8; 16]) -> B2Params {
        let mut p = B2Params::new();
        p.hash_length(out_len);
        p.personal(person);
        p
    }
}

impl Default for Blake2bSimd {
    fn default() -> Self {
        Self::new()
    }
}

impl LeafHasher for Blake2bSimd {
    fn output_len(&self) -> usize {
        64
    }

    fn hash_one(&self, person: &[u8; 16], prefix: &[u8], class: u32, counter: u32, out: &mut [u8]) {
        let params = self.params(out.len(), person);
        let mut state = params.to_state();
        state.update(prefix);
        state.update(&class.to_le_bytes());
        state.update(&counter.to_le_bytes());
        let h = state.finalize();
        out.copy_from_slice(h.as_bytes());
    }

    fn hash_many(&self, person: &[u8; 16], prefix: &[u8], keys: &[(u32, u32)], out: &mut [u8]) {
        let n = self.output_len();
        // Build one message buffer per leaf (prefix || class || counter), then use
        // the batched hash_many. blake2b_simd's many API takes a slice of jobs.
        let params = self.params(n, person);
        let msgs: Vec<Vec<u8>> = keys
            .iter()
            .map(|&(c, j)| {
                let mut m = Vec::with_capacity(prefix.len() + 8);
                m.extend_from_slice(prefix);
                m.extend_from_slice(&c.to_le_bytes());
                m.extend_from_slice(&j.to_le_bytes());
                m
            })
            .collect();
        let mut jobs: Vec<HashManyJob> =
            msgs.iter().map(|m| HashManyJob::new(&params, m)).collect();
        hash_many(jobs.iter_mut());
        for (i, job) in jobs.iter().enumerate() {
            out[i * n..(i + 1) * n].copy_from_slice(job.to_hash().as_bytes());
        }
    }

    fn name(&self) -> &'static str {
        "blake2b-simd"
    }
}
