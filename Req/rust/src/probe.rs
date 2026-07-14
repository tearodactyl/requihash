//! Family measurement probes (SPEC.md §3–§6): spec-accurate generator and
//! verify-cost paths for every (hash, m, keying) configuration point, plus the
//! substitution variants for cache-honest phase attribution (BENCHMARK.md §5
//! gap 2). Bench support, not consensus code — the consensus engines adopt a
//! configuration only after the lab converges.
//!
//! Substitution method: the generation phase runs in code-shape-identical
//! variants — real, hash-stubbed (constant digest, same expand+write), and
//! assembly-stubbed (real hash into a discard buffer, constant row write).
//! Marginal deltas attribute in-context cost; the residual is the interaction
//! term and is reported, never forced to zero.

use crate::{blake2b, expand_array, expand_array_into, Params};

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum HashKind {
    Blake2b,
    /// Same function as `Blake2b` (byte-identical leaves, differentially
    /// tested), computed through `blake2b_simd` with batched `hash_many`
    /// generation — the optimized-implementation comparison point.
    #[cfg(feature = "simd")]
    Blake2bSimd,
    #[cfg(feature = "blake3")]
    Blake3,
}

impl HashKind {
    pub fn name(&self) -> &'static str {
        match self {
            HashKind::Blake2b => "blake2b",
            #[cfg(feature = "simd")]
            HashKind::Blake2bSimd => "blake2b-simd",
            #[cfg(feature = "blake3")]
            HashKind::Blake3 => "blake3",
        }
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Keying {
    Regular,
    Single,
}

/// Substitution mode for `gen_phase_variant`.
#[derive(Clone, Copy, PartialEq, Eq)]
enum Variant {
    Real,
    StubHash,
    StubAssembly,
}

/// One family configuration point, holding the prepared base state.
pub struct GenProbe {
    p: Params,
    m: u32,
    keying: Keying,
    kind: HashKind,
    base2b: blake2b::State,
    /// input || nonce || (le16(m) when m >= 2): the absorbed prefix, kept for
    /// backends whose batched APIs take whole messages instead of midstates.
    prefix: Vec<u8>,
    #[cfg(feature = "simd")]
    simd_params: Option<blake2b_simd::Params>,
    #[cfg(feature = "blake3")]
    base3: Option<blake3::Hasher>,
    /// Empty derive_key(context) hasher, derived once; iteration rounds clone
    /// it instead of re-deriving the context key per leaf per round.
    #[cfg(feature = "blake3")]
    iter3: Option<blake3::Hasher>,
}

impl GenProbe {
    pub fn new(p: Params, kind: HashKind, m: u32, keying: Keying, input: &[u8], nonce: &[u8]) -> Self {
        assert!(m >= 1);
        // blake2b base per SPEC §3/§5.
        let mut s = blake2b::init(p.hash_output(), &person(p));
        blake2b::update(&mut s, input);
        blake2b::update(&mut s, nonce);
        if m >= 2 {
            blake2b::update(&mut s, &(m as u16).to_le_bytes());
        }
        let mut prefix = Vec::with_capacity(input.len() + nonce.len() + 2);
        prefix.extend_from_slice(input);
        prefix.extend_from_slice(nonce);
        if m >= 2 {
            prefix.extend_from_slice(&(m as u16).to_le_bytes());
        }
        #[cfg(feature = "simd")]
        let simd_params = {
            let mut sp = blake2b_simd::Params::new();
            sp.hash_length(p.hash_output());
            sp.personal(&person(p));
            Some(sp)
        };
        #[cfg(feature = "blake3")]
        let (base3, iter3) = {
            let context = format!(
                "ReqPoW/blake3 v1 ctx=ReqhashPoW n={} k={} m={} keying={}",
                p.n,
                p.k,
                m,
                match keying {
                    Keying::Regular => "r",
                    Keying::Single => "s",
                }
            );
            let template = blake3::Hasher::new_derive_key(&context);
            let mut h = template.clone();
            h.update(input);
            h.update(nonce);
            (Some(h), Some(template))
        };
        GenProbe {
            p,
            m,
            keying,
            kind,
            base2b: s,
            prefix,
            #[cfg(feature = "simd")]
            simd_params,
            #[cfg(feature = "blake3")]
            base3,
            #[cfg(feature = "blake3")]
            iter3,
        }
    }

    pub fn leaf_count(&self) -> usize {
        1usize << (self.p.collision_bit_length() + 1)
    }
    pub fn row_stride(&self) -> usize {
        (self.p.k as usize + 1) * self.p.collision_byte_length()
    }
    fn leaf_bytes(&self) -> usize {
        (self.p.n / 8) as usize
    }
    fn keying_words(&self, i: u32) -> ([u8; 8], usize) {
        let mut w = [0u8; 8];
        match self.keying {
            Keying::Regular => {
                w[..4].copy_from_slice(&(i % self.p.k).to_le_bytes());
                w[4..].copy_from_slice(&(i / self.p.k).to_le_bytes());
                (w, 8)
            }
            Keying::Single => {
                w[..4].copy_from_slice(&i.to_le_bytes());
                (w, 4)
            }
        }
    }

    /// The n-bit leaf string for leaf `i`, per SPEC §4–§6 (m applied).
    pub fn leaf(&self, i: u32) -> Vec<u8> {
        let (words, wlen) = self.keying_words(i);
        match self.kind {
            #[cfg(feature = "simd")]
            HashKind::Blake2bSimd => {
                let sp = self.simd_params.as_ref().unwrap();
                let mut st = sp.to_state();
                st.update(&self.prefix);
                st.update(&words[..wlen]);
                let mut d = st.finalize().as_bytes().to_vec();
                for _ in 2..=self.m {
                    let mut st = sp.to_state();
                    st.update(&d);
                    st.update(&words[..wlen]);
                    d = st.finalize().as_bytes().to_vec();
                }
                d.truncate(self.leaf_bytes());
                d
            }
            HashKind::Blake2b => {
                let mut s = self.base2b.clone();
                blake2b::update(&mut s, &words[..wlen]);
                let mut d = vec![0u8; self.p.hash_output()];
                blake2b::finalize(s, &mut d);
                for _ in 2..=self.m {
                    let mut s = blake2b::init(self.p.hash_output(), &person(self.p));
                    blake2b::update(&mut s, &d);
                    blake2b::update(&mut s, &words[..wlen]);
                    let mut next = vec![0u8; self.p.hash_output()];
                    blake2b::finalize(s, &mut next);
                    d = next;
                }
                d.truncate(self.leaf_bytes());
                d
            }
            #[cfg(feature = "blake3")]
            HashKind::Blake3 => {
                let w = self.leaf_bytes();
                let mut d = vec![0u8; w];
                let mut reader = match self.keying {
                    Keying::Regular => {
                        let mut h = self.base3.as_ref().unwrap().clone();
                        h.update(&(i % self.p.k).to_le_bytes());
                        let mut r = h.finalize_xof();
                        r.set_position((i / self.p.k) as u64 * w as u64);
                        r
                    }
                    Keying::Single => {
                        let mut r = self.base3.as_ref().unwrap().clone().finalize_xof();
                        r.set_position(i as u64 * w as u64);
                        r
                    }
                };
                std::io::Read::read_exact(&mut reader, &mut d).expect("xof read");
                for _ in 2..=self.m {
                    let mut h = self.iter3.as_ref().unwrap().clone();
                    h.update(&d);
                    h.update(&words[..wlen]);
                    let out = h.finalize();
                    d.copy_from_slice(&out.as_bytes()[..w]);
                }
                d
            }
        }
    }

    fn expand_into(&self, leaf: &[u8], row: &mut [u8]) {
        expand_array_into(leaf, row, self.p.collision_bit_length(), 0);
    }

    /// Generation phase, real: every leaf hashed, expanded, written to the
    /// solver-stride sink. Sink length = leaf_count * row_stride.
    pub fn gen_phase(&self, sink: &mut [u8]) {
        self.gen_phase_variant(sink, Variant::Real)
    }

    /// Hash-stubbed variant: identical loop shape and sink writes, constant
    /// digest in place of hashing.
    pub fn gen_phase_stub_hash(&self, sink: &mut [u8]) {
        self.gen_phase_variant(sink, Variant::StubHash)
    }

    /// Assembly-stubbed variant: real hashing, one byte written per row
    /// (loop shape and hash work preserved, no expand/pack).
    pub fn gen_phase_stub_assembly(&self, sink: &mut [u8]) {
        self.gen_phase_variant(sink, Variant::StubAssembly)
    }

    /// One implementation, three modes, per-backend loop shape — substitution
    /// attribution is only valid when every variant walks the same loop as
    /// the real path (BENCHMARK.md §6 found this the hard way: leaf-major
    /// stubs against the streamed blake3 path produced negative marginals).
    fn gen_phase_variant(&self, sink: &mut [u8], v: Variant) {
        let stride = self.row_stride();
        let constant = vec![0xA5u8; self.leaf_bytes()];

        // Batched shape: blake2b_simd, m = 1 — hash_many over MAX_JOBS-leaf
        // groups (the 4-way generator shape); iterated leaves fall back to
        // the per-leaf loop below.
        #[cfg(feature = "simd")]
        if self.kind == HashKind::Blake2bSimd && self.m == 1 {
            use blake2b_simd::many::{hash_many, HashManyJob, MAX_DEGREE};
            let sp = self.simd_params.as_ref().unwrap();
            let count = self.leaf_count() as u32;
            let nbytes = self.leaf_bytes();
            let mut inputs: Vec<Vec<u8>> = (0..MAX_DEGREE)
                .map(|_| Vec::with_capacity(self.prefix.len() + 8))
                .collect();
            let mut i = 0u32;
            while i < count {
                let batch = (MAX_DEGREE as u32).min(count - i) as usize;
                for (j, inp) in inputs.iter_mut().enumerate().take(batch) {
                    inp.clear();
                    inp.extend_from_slice(&self.prefix);
                    let (words, wlen) = self.keying_words(i + j as u32);
                    inp.extend_from_slice(&words[..wlen]);
                }
                if v != Variant::StubHash {
                    let mut jobs: Vec<HashManyJob> = inputs[..batch]
                        .iter()
                        .map(|inp| HashManyJob::new(sp, inp))
                        .collect();
                    hash_many(jobs.iter_mut());
                    for (j, job) in jobs.iter().enumerate() {
                        let idx = (i as usize) + j;
                        let row = &mut sink[idx * stride..(idx + 1) * stride];
                        let hash = job.to_hash();
                        let leaf = &hash.as_bytes()[..nbytes];
                        match v {
                            Variant::Real => self.expand_into(leaf, row),
                            Variant::StubAssembly => row[0] = leaf[0],
                            Variant::StubHash => unreachable!(),
                        }
                    }
                } else {
                    for j in 0..batch {
                        let idx = (i as usize) + j;
                        self.expand_into(&constant, &mut sink[idx * stride..(idx + 1) * stride]);
                    }
                }
                i += batch as u32;
            }
            return;
        }

        // Streamed shape: blake3, m = 1, regular keying — class-major loop,
        // sequential XOF reads, writes strided k apart.
        #[cfg(feature = "blake3")]
        if self.kind == HashKind::Blake3 && self.m == 1 && self.keying == Keying::Regular {
            let w = self.leaf_bytes();
            let count = self.leaf_count() as u32;
            let k = self.p.k;
            let mut leaf = vec![0u8; w];
            for class in 0..k {
                let mut reader = match v {
                    Variant::StubHash => None,
                    _ => {
                        let mut h = self.base3.as_ref().unwrap().clone();
                        h.update(&class.to_le_bytes());
                        Some(h.finalize_xof())
                    }
                };
                let mut i = class;
                while i < count {
                    let row = &mut sink[i as usize * stride..(i as usize + 1) * stride];
                    match v {
                        Variant::Real => {
                            std::io::Read::read_exact(reader.as_mut().unwrap(), &mut leaf)
                                .expect("xof read");
                            self.expand_into(&leaf, row);
                        }
                        Variant::StubHash => self.expand_into(&constant, row),
                        Variant::StubAssembly => {
                            std::io::Read::read_exact(reader.as_mut().unwrap(), &mut leaf)
                                .expect("xof read");
                            row[0] = leaf[0];
                        }
                    }
                    i += k;
                }
            }
            return;
        }

        // Per-leaf shape: blake2b, and blake3's iterated/single fallbacks.
        for i in 0..self.leaf_count() as u32 {
            let row = &mut sink[i as usize * stride..(i as usize + 1) * stride];
            match v {
                Variant::Real => self.expand_into(&self.leaf(i), row),
                Variant::StubHash => self.expand_into(&constant, row),
                Variant::StubAssembly => row[0] = self.leaf(i)[0],
            }
        }
    }

    /// Verify-shaped cost: recompute `2^k` leaves (scattered indices),
    /// expand, and fold the tree by pairwise XOR — the verifier's exact work
    /// on a valid solution, with check outcomes ignored so it runs at any
    /// parameters without needing a mined solution. Returns the root XOR
    /// accumulator so the work cannot be optimized away.
    pub fn verify_cost(&self) -> u8 {
        let count = 1usize << self.p.k;
        let stride = self.row_stride();
        let span = self.leaf_count() as u32 / count as u32;
        let mut rows = vec![0u8; count * stride];
        for j in 0..count {
            let leaf = self.leaf(j as u32 * span.max(1));
            self.expand_into(&leaf, &mut rows[j * stride..(j + 1) * stride]);
        }
        let mut width = stride;
        let mut n = count;
        while n > 1 {
            for pair in 0..n / 2 {
                for t in 0..width {
                    rows[pair * width + t] = rows[2 * pair * width + t] ^ rows[(2 * pair + 1) * width + t];
                }
            }
            n /= 2;
            let cb = self.p.collision_byte_length();
            width = width.saturating_sub(cb).max(cb);
        }
        rows.iter().fold(0u8, |a, &b| a ^ b)
    }
}

fn person(p: Params) -> [u8; 16] {
    let mut out = [0u8; 16];
    out[..10].copy_from_slice(b"ReqhashPoW");
    out[10..14].copy_from_slice(&p.n.to_le_bytes());
    out[14] = (p.k & 0xFF) as u8;
    out[15] = ((p.k >> 8) & 0xFF) as u8;
    out
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::Requihash;

    #[test]
    fn blake2b_m1_regular_matches_consensus_engine() {
        // The probe at the implemented configuration point must agree
        // byte-for-byte with the consensus engine's leaf derivation.
        let p = Params::new(48, 5).unwrap();
        let probe = GenProbe::new(p, HashKind::Blake2b, 1, Keying::Regular, b"probe-vs-engine", &7u32.to_le_bytes());
        let eng = Requihash::new(p, b"probe-vs-engine", &7u32.to_le_bytes());
        let stride = probe.row_stride();
        let mut sink = vec![0u8; probe.leaf_count() * stride];
        probe.gen_phase(&mut sink);
        let (rows, _) = eng.hash_all_leaves();
        for (i, row) in rows.iter().enumerate() {
            assert_eq!(&sink[i * stride..(i + 1) * stride], &row[..], "leaf {i}");
        }
    }

    #[test]
    fn iteration_changes_leaves_and_base() {
        let p = Params::new(48, 5).unwrap();
        let m1 = GenProbe::new(p, HashKind::Blake2b, 1, Keying::Regular, b"x", b"y");
        let m2 = GenProbe::new(p, HashKind::Blake2b, 2, Keying::Regular, b"x", b"y");
        let m3 = GenProbe::new(p, HashKind::Blake2b, 3, Keying::Regular, b"x", b"y");
        assert_ne!(m1.leaf(0), m2.leaf(0));
        assert_ne!(m2.leaf(0), m3.leaf(0));
    }

    #[test]
    fn keying_modes_differ() {
        let p = Params::new(48, 5).unwrap();
        let r = GenProbe::new(p, HashKind::Blake2b, 1, Keying::Regular, b"x", b"y");
        let s = GenProbe::new(p, HashKind::Blake2b, 1, Keying::Single, b"x", b"y");
        assert_ne!(r.leaf(1), s.leaf(1));
    }

    #[test]
    fn stub_variants_run_same_shape() {
        let p = Params::new(48, 5).unwrap();
        let probe = GenProbe::new(p, HashKind::Blake2b, 1, Keying::Regular, b"x", b"y");
        let mut a = vec![0u8; probe.leaf_count() * probe.row_stride()];
        let mut b = a.clone();
        probe.gen_phase_stub_hash(&mut a);
        probe.gen_phase_stub_assembly(&mut b);
        let _ = probe.verify_cost();
    }

    #[cfg(feature = "simd")]
    #[test]
    fn blake2b_simd_probe_matches_scalar() {
        // Same function, different implementation: leaves and full generation
        // sinks must be byte-identical to the scalar backend, across keying
        // modes and iteration counts.
        let p = Params::new(48, 5).unwrap();
        for keying in [Keying::Regular, Keying::Single] {
            for m in [1u32, 2, 3] {
                let a = GenProbe::new(p, HashKind::Blake2b, m, keying, b"simd-vs-scalar", b"n1");
                let b = GenProbe::new(p, HashKind::Blake2bSimd, m, keying, b"simd-vs-scalar", b"n1");
                for i in [0u32, 1, 5, 100, 511] {
                    assert_eq!(a.leaf(i), b.leaf(i), "leaf {i} m={m} {keying:?}");
                }
                let stride = a.row_stride();
                let mut sa = vec![0u8; a.leaf_count() * stride];
                let mut sb = sa.clone();
                a.gen_phase(&mut sa);
                b.gen_phase(&mut sb);
                assert_eq!(sa, sb, "gen sink m={m} {keying:?}");
            }
        }
    }

    #[cfg(feature = "simd")]
    #[test]
    fn blake2b_simd_equals_scalar() {
        // The equivalence gate: same function, different implementation —
        // every leaf byte-identical across params, m, and keying, before any
        // timing of Blake2bSimd counts.
        for &(n, k) in &[(48u32, 5u32), (72, 5)] {
            let p = Params::new(n, k).unwrap();
            for m in [1u32, 2] {
                for keying in [Keying::Regular, Keying::Single] {
                    let a = GenProbe::new(p, HashKind::Blake2b, m, keying, b"equiv-gate", b"n1");
                    let b = GenProbe::new(p, HashKind::Blake2bSimd, m, keying, b"equiv-gate", b"n1");
                    for i in 0..a.leaf_count() as u32 {
                        assert_eq!(
                            a.leaf(i),
                            b.leaf(i),
                            "leaf {i} differs at ({n},{k}) m={m} {keying:?}"
                        );
                    }
                    // The batched hash_many generation path must equal the
                    // scalar midstate path row-for-row as well.
                    let stride = a.row_stride();
                    let mut sa = vec![0u8; a.leaf_count() * stride];
                    let mut sb = sa.clone();
                    a.gen_phase(&mut sa);
                    b.gen_phase(&mut sb);
                    assert_eq!(sa, sb, "gen_phase differs at ({n},{k}) m={m} {keying:?}");
                }
            }
        }
    }

    #[cfg(feature = "blake3")]
    #[test]
    fn blake3_stream_equals_seek() {
        // The miner's sequential class streams and the verifier's per-leaf
        // XOF seeks must produce identical leaves (SPEC §6).
        let p = Params::new(48, 5).unwrap();
        let probe = GenProbe::new(p, HashKind::Blake3, 1, Keying::Regular, b"stream-vs-seek", b"n0");
        let stride = probe.row_stride();
        let mut sink = vec![0u8; probe.leaf_count() * stride];
        probe.gen_phase(&mut sink); // streaming path
        for i in 0..probe.leaf_count() as u32 {
            let leaf = probe.leaf(i); // seek path
            let exp = expand_array(&leaf, stride, p.collision_bit_length(), 0);
            assert_eq!(&sink[i as usize * stride..(i as usize + 1) * stride], &exp[..], "leaf {i}");
        }
    }

    #[cfg(feature = "blake3")]
    #[test]
    fn blake3_iteration_and_context_sensitivity() {
        let p = Params::new(48, 5).unwrap();
        let m1 = GenProbe::new(p, HashKind::Blake3, 1, Keying::Regular, b"x", b"y");
        let m2 = GenProbe::new(p, HashKind::Blake3, 2, Keying::Regular, b"x", b"y");
        assert_ne!(m1.leaf(0), m2.leaf(0));
        // m is in the derive_key context, so even leaf(0) at t=1 differs.
        let p2 = Params::new(72, 5).unwrap();
        let other = GenProbe::new(p2, HashKind::Blake3, 1, Keying::Regular, b"x", b"y");
        assert_ne!(m1.leaf(0), other.leaf(0)[..m1.leaf(0).len()].to_vec());
    }
}
