//! Requihash miner and verifier in Rust, following the zebra
//! `zebra-chain/src/work/equihash.rs` verifier style (a `check`-like validator
//! plus a solver for round-trip tests). Regularity-repaired Equihash per
//! Tang-Sun-Gong, eprint 2025/1351 Sec 5.2. See `../Equihash.md` F-A4.
//!
//! Wire-compatible with the C++ build in `../cpp`: identical BLAKE2b
//! personalization (`"ReqhashPoW" || le32(n) || le16(k)`), identical leaf keying
//! (`H(input || nonce || le32(leaf mod k) || le32(leaf / k))`), and identical
//! minimal (compressed) solution encoding.

mod blake2b;

pub mod hash;
pub mod report;
pub mod solve;
pub mod verify;

pub type EhIndex = u32;

/// Requihash parameters `(n, k)`, Equihash convention: a solution has `2^k`
/// indices and collides on `ell = n/(k+1)` bits per round.
#[derive(Clone, Copy, Debug)]
pub struct Params {
    pub n: u32,
    pub k: u32,
}

#[derive(Debug)]
pub enum Error {
    BadParams(&'static str),
    WrongLength,
    NotDistinct,
    CollisionFailed(u32),
    OrderingFailed(u32),
    NonZeroRoot,
}

impl std::fmt::Display for Error {
    fn fmt(&self, f: &mut std::fmt::Formatter) -> std::fmt::Result {
        write!(f, "{:?}", self)
    }
}
impl std::error::Error for Error {}

impl Params {
    pub fn new(n: u32, k: u32) -> Result<Self, Error> {
        if k >= n {
            return Err(Error::BadParams("k must be < n"));
        }
        if n % 8 != 0 {
            return Err(Error::BadParams("n must be a multiple of 8"));
        }
        if n % (k + 1) != 0 {
            return Err(Error::BadParams("n must be divisible by k+1"));
        }
        Ok(Params { n, k })
    }
    pub fn collision_bit_length(&self) -> usize {
        (self.n / (self.k + 1)) as usize
    }
    pub fn collision_byte_length(&self) -> usize {
        (self.collision_bit_length() + 7) / 8
    }
    pub fn hash_output(&self) -> usize {
        (512 / self.n) as usize * (self.n / 8) as usize
    }
    /// Default (Equihash-compatible) minimal encoding: cbitlen+1 bits per index.
    pub fn solution_width(&self) -> usize {
        (1usize << self.k) * (self.collision_bit_length() + 1) / 8
    }
    /// Requihash compact wire size (paper Table 3): cbitlen bits per index, index
    /// field reconstructed from packet structure. (200,9): 1344 -> 1280 bytes.
    pub fn compact_width(&self) -> usize {
        (1usize << self.k) * self.collision_bit_length() / 8
    }
    fn person(&self) -> [u8; 16] {
        let mut p = [0u8; 16];
        p[..10].copy_from_slice(b"ReqhashPoW");
        p[10..14].copy_from_slice(&self.n.to_le_bytes());
        p[14] = (self.k & 0xFF) as u8;
        p[15] = ((self.k >> 8) & 0xFF) as u8;
        p
    }
}

/// Engine holding the personalized+prefixed BLAKE2b base state.
pub struct Requihash {
    p: Params,
    base: blake2b::State,
}

impl Requihash {
    pub fn new(p: Params, input: &[u8], nonce: &[u8]) -> Self {
        let person = p.person();
        let mut s = blake2b::init(p.hash_output(), &person);
        blake2b::update(&mut s, input);
        blake2b::update(&mut s, nonce);
        Requihash { p, base: s }
    }

    pub fn params(&self) -> Params {
        self.p
    }

    /// Clone of the personalized+prefixed BLAKE2b base state, for solver backends
    /// that generate leaves directly.
    pub(crate) fn base_clone(&self) -> blake2b::State {
        self.base.clone()
    }

    /// Requihash regularity: leaf keyed by (list-class = leaf mod k,
    /// counter = leaf / k). Returns the expanded collision bytes for the leaf.
    pub(crate) fn leaf_row(&self, leaf: EhIndex) -> Vec<u8> {
        let listclass = leaf % self.p.k;
        let counter = leaf / self.p.k;
        let mut s = self.base.clone();
        blake2b::update(&mut s, &listclass.to_le_bytes());
        blake2b::update(&mut s, &counter.to_le_bytes());
        let mut out = vec![0u8; self.p.hash_output()];
        blake2b::finalize(s, &mut out);
        expand_array(
            &out[..(self.p.n / 8) as usize],
            (self.p.k as usize + 1) * self.p.collision_byte_length(),
            self.p.collision_bit_length(),
            0,
        )
    }

    /// Basic Wagner solve; returns all solutions as `2^k`-length index vectors.
    /// Canonical body for `solve::reference::ReferenceSolver`.
    pub fn solve_reference(&self) -> Vec<Vec<EhIndex>> {
        let cbyte = self.p.collision_byte_length();
        let init_size = 1usize << (self.p.collision_bit_length() + 1);

        struct Row {
            h: Vec<u8>,
            idx: Vec<EhIndex>,
        }
        let mut x: Vec<Row> = Vec::with_capacity(init_size);
        for leaf in 0..init_size as EhIndex {
            x.push(Row {
                h: self.leaf_row(leaf),
                idx: vec![leaf],
            });
        }

        for _round in 1..=self.p.k {
            x.sort_by(|a, b| a.h[..cbyte].cmp(&b.h[..cbyte]));
            let mut xc: Vec<Row> = Vec::new();
            let mut i = 0;
            while i + 1 < x.len() {
                let mut j = i + 1;
                while j < x.len() && x[j].h[..cbyte] == x[i].h[..cbyte] {
                    j += 1;
                }
                for a in i..j {
                    for b in (a + 1)..j {
                        if !distinct(&x[a].idx, &x[b].idx) {
                            continue;
                        }
                        let remain = x[a].h.len();
                        let mut h = vec![0u8; remain];
                        for t in 0..remain {
                            h[t] = x[a].h[t] ^ x[b].h[t];
                        }
                        h.drain(..cbyte);
                        let idx = if x[a].idx < x[b].idx {
                            let mut v = x[a].idx.clone();
                            v.extend_from_slice(&x[b].idx);
                            v
                        } else {
                            let mut v = x[b].idx.clone();
                            v.extend_from_slice(&x[a].idx);
                            v
                        };
                        xc.push(Row { h, idx });
                    }
                }
                i = j;
            }
            x = xc;
            if x.is_empty() {
                break;
            }
        }

        x.into_iter()
            .filter(|r| r.h.iter().all(|&c| c == 0))
            .map(|r| r.idx)
            .filter(|idx| {
                let mut u = idx.clone();
                u.sort_unstable();
                u.dedup();
                u.len() == idx.len()
            })
            .collect()
    }

    /// Arena-allocated Wagner solve. Same algorithm and output as `solve()`, but
    /// rows live in flat struct-of-arrays buffers instead of per-row `Vec`s, and
    /// each round sorts a permutation of u32 row-ids rather than moving rows.
    /// This removes the per-row heap allocation that the profile showed to be 59%
    /// of solve time (see BENCHMARK.md). Cross-validated against `solve()` in tests.
    pub fn solve_arena(&self) -> Vec<Vec<EhIndex>> {
        // Serial leaf fill, then the shared arena merge.
        self.solve_arena_with_leaves(|_nrows, full, hashes| {
            let mut hout = vec![0u8; self.p.hash_output()];
            let n8 = (self.p.n / 8) as usize;
            let cbl = self.p.collision_bit_length();
            for (leaf, slot) in hashes.chunks_mut(full).enumerate() {
                let leaf = leaf as u32;
                let mut s = self.base.clone();
                blake2b::update(&mut s, &(leaf % self.p.k).to_le_bytes());
                blake2b::update(&mut s, &(leaf / self.p.k).to_le_bytes());
                blake2b::finalize(s, &mut hout);
                let exp = expand_array(&hout[..n8], full, cbl, 0);
                slot.copy_from_slice(&exp);
            }
        })
    }

    /// Arena solve with a pluggable leaf-fill. `fill(nrows, full_hash, hashes)`
    /// must write every row's expanded hash into the flat `hashes` buffer
    /// (nrows * full_hash bytes). This is the seam a parallel/SIMD leaf backend
    /// hooks into; the merge below is unchanged.
    pub fn solve_arena_with_leaves<F>(&self, fill: F) -> Vec<Vec<EhIndex>>
    where
        F: FnOnce(usize, usize, &mut [u8]),
    {
        let cbyte = self.p.collision_byte_length();
        let full_hash = (self.p.k as usize + 1) * cbyte;
        let init_size = 1usize << (self.p.collision_bit_length() + 1);

        let mut hstride = full_hash;
        let mut icount = 1usize;
        let mut nrows = init_size;

        let mut hashes = vec![0u8; nrows * hstride];
        let mut idxs = vec![0u32; nrows * icount];
        fill(nrows, full_hash, &mut hashes);
        for leaf in 0..nrows as u32 {
            idxs[leaf as usize] = leaf;
        }

        for _round in 1..=self.p.k {
            // Sort a permutation of row-ids by the leading cbyte of each row's hash.
            let mut order: Vec<u32> = (0..nrows as u32).collect();
            order.sort_by(|&a, &b| {
                let ha = &hashes[a as usize * hstride..a as usize * hstride + cbyte];
                let hb = &hashes[b as usize * hstride..b as usize * hstride + cbyte];
                ha.cmp(hb)
            });

            let new_hstride = hstride - cbyte;
            let new_icount = icount * 2;
            let mut out_hashes: Vec<u8> = Vec::new();
            let mut out_idxs: Vec<u32> = Vec::new();

            let mut i = 0usize;
            while i + 1 < order.len() {
                let ri = order[i] as usize;
                let key = &hashes[ri * hstride..ri * hstride + cbyte];
                let mut j = i + 1;
                while j < order.len() {
                    let rj = order[j] as usize;
                    if hashes[rj * hstride..rj * hstride + cbyte] != *key {
                        break;
                    }
                    j += 1;
                }
                for a in i..j {
                    let ra = order[a] as usize;
                    for b in (a + 1)..j {
                        let rb = order[b] as usize;
                        let ia = &idxs[ra * icount..(ra + 1) * icount];
                        let ib = &idxs[rb * icount..(rb + 1) * icount];
                        if !slices_distinct(ia, ib) {
                            continue;
                        }
                        // XOR full remaining hash, drop the collided cbyte prefix.
                        let base = out_hashes.len();
                        out_hashes.resize(base + new_hstride, 0);
                        let ha = &hashes[ra * hstride + cbyte..ra * hstride + hstride];
                        let hb = &hashes[rb * hstride + cbyte..rb * hstride + hstride];
                        for t in 0..new_hstride {
                            out_hashes[base + t] = ha[t] ^ hb[t];
                        }
                        // canonical order by index slice
                        if ia < ib {
                            out_idxs.extend_from_slice(ia);
                            out_idxs.extend_from_slice(ib);
                        } else {
                            out_idxs.extend_from_slice(ib);
                            out_idxs.extend_from_slice(ia);
                        }
                    }
                }
                i = j;
            }

            hashes = out_hashes;
            idxs = out_idxs;
            hstride = new_hstride;
            icount = new_icount;
            nrows = if hstride == 0 { 0 } else { hashes.len() / hstride.max(1) };
            if new_hstride == 0 {
                nrows = idxs.len() / new_icount;
            }
            if nrows == 0 {
                break;
            }
        }

        // Solutions: rows whose remaining hash is all-zero (or zero-width at end)
        // with distinct indices.
        let mut sols = Vec::new();
        for r in 0..nrows {
            let zero = hstride == 0
                || hashes[r * hstride..(r + 1) * hstride].iter().all(|&c| c == 0);
            if !zero {
                continue;
            }
            let idx = idxs[r * icount..(r + 1) * icount].to_vec();
            let mut u = idx.clone();
            u.sort_unstable();
            u.dedup();
            if u.len() == idx.len() {
                sols.push(idx);
            }
        }
        sols
    }

    /// Backward-compatible default solve (delegates to the reference solver).
    /// Prefer selecting a backend via `solve::Solver` for new code.
    pub fn solve(&self) -> Vec<Vec<EhIndex>> {
        self.solve_reference()
    }

    // ---- instrumentation (not on the consensus path) ----

    /// Generate all `2^(ell+1)` leaf rows and return them, for measuring pure leaf
    /// hashing throughput in isolation from the merge. Returns (rows, bytes hashed).
    pub fn hash_all_leaves(&self) -> (Vec<Vec<u8>>, usize) {
        let init_size = 1usize << (self.p.collision_bit_length() + 1);
        let mut rows = Vec::with_capacity(init_size);
        for leaf in 0..init_size as EhIndex {
            rows.push(self.leaf_row(leaf));
        }
        // bytes fed to BLAKE2b per leaf = prefix(input+nonce) is in base; the
        // per-leaf update is 8 bytes (two u32) plus one compression producing
        // hash_output bytes. Report leaf count for rate math.
        (rows, init_size)
    }

    /// Solve with per-phase timing and per-round list sizes. Returns
    /// (solutions, gen_nanos, merge_nanos, round_sizes).
    pub fn solve_instrumented(&self) -> (Vec<Vec<EhIndex>>, u128, u128, Vec<usize>) {
        use std::time::Instant;
        let cbyte = self.p.collision_byte_length();
        let init_size = 1usize << (self.p.collision_bit_length() + 1);

        struct Row {
            h: Vec<u8>,
            idx: Vec<EhIndex>,
        }
        let t_gen = Instant::now();
        let mut x: Vec<Row> = Vec::with_capacity(init_size);
        for leaf in 0..init_size as EhIndex {
            x.push(Row {
                h: self.leaf_row(leaf),
                idx: vec![leaf],
            });
        }
        let gen_nanos = t_gen.elapsed().as_nanos();

        let t_merge = Instant::now();
        let mut round_sizes = vec![x.len()];
        for _round in 1..=self.p.k {
            x.sort_by(|a, b| a.h[..cbyte].cmp(&b.h[..cbyte]));
            let mut xc: Vec<Row> = Vec::new();
            let mut i = 0;
            while i + 1 < x.len() {
                let mut j = i + 1;
                while j < x.len() && x[j].h[..cbyte] == x[i].h[..cbyte] {
                    j += 1;
                }
                for a in i..j {
                    for b in (a + 1)..j {
                        if !distinct(&x[a].idx, &x[b].idx) {
                            continue;
                        }
                        let remain = x[a].h.len();
                        let mut h = vec![0u8; remain];
                        for t in 0..remain {
                            h[t] = x[a].h[t] ^ x[b].h[t];
                        }
                        h.drain(..cbyte);
                        let idx = if x[a].idx < x[b].idx {
                            let mut v = x[a].idx.clone();
                            v.extend_from_slice(&x[b].idx);
                            v
                        } else {
                            let mut v = x[b].idx.clone();
                            v.extend_from_slice(&x[a].idx);
                            v
                        };
                        xc.push(Row { h, idx });
                    }
                }
                i = j;
            }
            x = xc;
            round_sizes.push(x.len());
            if x.is_empty() {
                break;
            }
        }
        let merge_nanos = t_merge.elapsed().as_nanos();

        let sols: Vec<Vec<EhIndex>> = x
            .into_iter()
            .filter(|r| r.h.iter().all(|&c| c == 0))
            .map(|r| r.idx)
            .filter(|idx| {
                let mut u = idx.clone();
                u.sort_unstable();
                u.dedup();
                u.len() == idx.len()
            })
            .collect();
        (sols, gen_nanos, merge_nanos, round_sizes)
    }

    /// Verifier: recompute the tree from an index vector and check collision
    /// structure, canonical ordering, distinctness, and XOR-to-zero at the root.
    /// Mirrors zebra's `equihash::is_valid_solution` contract.
    pub fn is_valid_solution(&self, indices: &[EhIndex]) -> Result<(), Error> {
        let expected = 1usize << self.p.k;
        if indices.len() != expected {
            return Err(Error::WrongLength);
        }
        {
            let mut u = indices.to_vec();
            u.sort_unstable();
            u.dedup();
            if u.len() != indices.len() {
                return Err(Error::NotDistinct);
            }
        }
        let cbyte = self.p.collision_byte_length();

        struct Row {
            h: Vec<u8>,
            idx: Vec<EhIndex>,
        }
        let mut x: Vec<Row> = indices
            .iter()
            .map(|&i| Row {
                h: self.leaf_row(i),
                idx: vec![i],
            })
            .collect();

        for round in 1..=self.p.k {
            let off = (round as usize - 1) * cbyte;
            let mut xc: Vec<Row> = Vec::new();
            let mut i = 0;
            while i < x.len() {
                let a = &x[i];
                let b = &x[i + 1];
                if a.h[off..off + cbyte] != b.h[off..off + cbyte] {
                    return Err(Error::CollisionFailed(round));
                }
                if !(a.idx < b.idx) {
                    return Err(Error::OrderingFailed(round));
                }
                let mut h = vec![0u8; a.h.len()];
                for t in 0..a.h.len() {
                    h[t] = a.h[t] ^ b.h[t];
                }
                let mut idx = a.idx.clone();
                idx.extend_from_slice(&b.idx);
                xc.push(Row { h, idx });
                i += 2;
            }
            x = xc;
        }
        if x.len() != 1 {
            return Err(Error::WrongLength);
        }
        if !x[0].h.iter().all(|&c| c == 0) {
            return Err(Error::NonZeroRoot);
        }
        Ok(())
    }
}

fn distinct(a: &[EhIndex], b: &[EhIndex]) -> bool {
    for &x in a {
        for &y in b {
            if x == y {
                return false;
            }
        }
    }
    true
}

fn slices_distinct(a: &[u32], b: &[u32]) -> bool {
    for &x in a {
        for &y in b {
            if x == y {
                return false;
            }
        }
    }
    true
}

// ---- bit-packing (matches C++ ExpandArray/CompressArray) ----

pub fn expand_array(input: &[u8], out_len: usize, bit_len: usize, byte_pad: usize) -> Vec<u8> {
    let out_width = (bit_len + 7) / 8 + byte_pad;
    let bit_len_mask: u32 = (1u32 << bit_len) - 1;
    let mut out = vec![0u8; out_len];
    let mut acc_bits = 0usize;
    let mut acc_value: u32 = 0;
    let mut j = 0usize;
    for &b in input {
        acc_value = (acc_value << 8) | (b as u32);
        acc_bits += 8;
        if acc_bits >= bit_len {
            acc_bits -= bit_len;
            for x in 0..byte_pad {
                out[j + x] = 0;
            }
            for x in byte_pad..out_width {
                out[j + x] = ((acc_value >> (acc_bits + (8 * (out_width - x - 1))))
                    & ((bit_len_mask >> (8 * (out_width - x - 1))) & 0xFF))
                    as u8;
            }
            j += out_width;
        }
    }
    out
}

pub fn compress_array(input: &[u8], out_len: usize, bit_len: usize, byte_pad: usize) -> Vec<u8> {
    let in_width = (bit_len + 7) / 8 + byte_pad;
    let bit_len_mask: u32 = (1u32 << bit_len) - 1;
    let mut out = vec![0u8; out_len];
    let mut acc_bits = 0usize;
    let mut acc_value: u32 = 0;
    let mut j = 0usize;
    for i in 0..out_len {
        if acc_bits < 8 {
            acc_value <<= bit_len;
            for x in byte_pad..in_width {
                acc_value |= ((input[j + x] as u32)
                    & ((bit_len_mask >> (8 * (in_width - x - 1))) & 0xFF))
                    << (8 * (in_width - x - 1));
            }
            j += in_width;
            acc_bits += bit_len;
        }
        acc_bits -= 8;
        out[i] = ((acc_value >> acc_bits) & 0xFF) as u8;
    }
    out
}

/// Minimal (compressed) encoding of `2^k` indices at `(cbitlen+1)` bits each.
pub fn get_minimal_from_indices(indices: &[EhIndex], cbitlen: usize) -> Vec<u8> {
    let len_indices = indices.len() * 4;
    let min_len = (cbitlen + 1) * indices.len() / 8;
    let byte_pad = 4 - ((cbitlen + 1) + 7) / 8;
    let mut array = vec![0u8; len_indices];
    for (i, &idx) in indices.iter().enumerate() {
        array[i * 4..i * 4 + 4].copy_from_slice(&idx.to_be_bytes());
    }
    compress_array(&array, min_len, cbitlen + 1, byte_pad)
}

pub fn get_indices_from_minimal(minimal: &[u8], cbitlen: usize) -> Vec<EhIndex> {
    let len_indices = 8 * 4 * minimal.len() / (cbitlen + 1);
    let byte_pad = 4 - ((cbitlen + 1) + 7) / 8;
    let array = expand_array(minimal, len_indices, cbitlen + 1, byte_pad);
    array
        .chunks_exact(4)
        .map(|c| u32::from_be_bytes([c[0], c[1], c[2], c[3]]))
        .collect()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn blake2b_known_answer() {
        let s = blake2b::init(64, &[0u8; 16]);
        let mut out = [0u8; 64];
        blake2b::finalize(s, &mut out);
        // BLAKE2b-512("") prefix
        assert_eq!(&out[..4], &[0x78, 0x6a, 0x02, 0xf7]);
    }

    #[test]
    fn params_reject() {
        assert!(Params::new(200, 200).is_err());
        assert!(Params::new(50, 5).is_err()); // 50 % 6 != 0
        assert!(Params::new(48, 5).is_ok());
    }

    fn solve_verify(n: u32, k: u32) {
        let p = Params::new(n, k).unwrap();
        for ni in 0u32..2000 {
            let eng = Requihash::new(p, b"requihash-test-block-header", &ni.to_le_bytes());
            let sols = eng.solve();
            if let Some(s) = sols.first() {
                eng.is_valid_solution(s).expect("solver output must verify");
                let minimal = get_minimal_from_indices(s, p.collision_bit_length());
                assert_eq!(minimal.len(), p.solution_width());
                let back = get_indices_from_minimal(&minimal, p.collision_bit_length());
                assert_eq!(&back, s);
                return;
            }
        }
        panic!("no solution within nonce budget for ({n},{k})");
    }

    #[test]
    fn solve_verify_48_5() {
        solve_verify(48, 5);
    }

    #[test]
    fn solve_verify_72_5() {
        solve_verify(72, 5);
    }

    #[test]
    fn table3_wire_sizes() {
        // Paper Table 3 at Zcash production params.
        let p = Params::new(200, 9).unwrap();
        assert_eq!(p.solution_width(), 1344); // Equihash-compatible
        assert_eq!(p.compact_width(), 1280); // Requihash compact
    }

    #[cfg(feature = "simd")]
    #[test]
    fn simd_hasher_matches_scalar() {
        use crate::hash::{agrees_with_reference, scalar::Blake2bScalar, simd::Blake2bSimd};
        let r = Blake2bScalar::new();
        let s = Blake2bSimd::new();
        assert!(
            agrees_with_reference(&r, &s),
            "SIMD BLAKE2b must be byte-identical to scalar reference (self-test gate)"
        );
    }

    #[test]
    fn all_solvers_agree() {
        use crate::solve::all_solvers;
        for &(n, k) in &[(48u32, 5u32), (72, 5)] {
            let p = Params::new(n, k).unwrap();
            for ni in 0u32..30 {
                let eng = Requihash::new(p, b"seam-check", &ni.to_le_bytes());
                let mut sets: Vec<Vec<Vec<EhIndex>>> = all_solvers()
                    .iter()
                    .map(|s| {
                        let mut r = s.solve(&eng);
                        r.sort();
                        r
                    })
                    .collect();
                let first = sets.remove(0);
                for other in sets {
                    assert_eq!(first, other, "solver mismatch at ({n},{k}) nonce {ni}");
                }
            }
        }
    }

    #[test]
    fn all_verifiers_agree() {
        use crate::verify::all_verifiers;
        for &(n, k) in &[(48u32, 5u32), (72, 5)] {
            let p = Params::new(n, k).unwrap();
            for ni in 0u32..30 {
                let eng = Requihash::new(p, b"seam-check", &ni.to_le_bytes());
                let sols = eng.solve_arena();
                let vs = all_verifiers();
                for s in &sols {
                    // every verifier accepts a real solution
                    for v in &vs {
                        v.verify(&eng, s).unwrap_or_else(|e| {
                            panic!("{} rejected valid solution at ({n},{k}) nonce {ni}: {e}", v.name())
                        });
                    }
                    // every verifier rejects a swapped-leaf tamper
                    let mut t = s.clone();
                    t.swap(0, 1);
                    for v in &vs {
                        assert!(
                            v.verify(&eng, &t).is_err(),
                            "{} accepted tampered solution",
                            v.name()
                        );
                    }
                }
            }
        }
    }

    #[test]
    fn arena_matches_reference() {
        // solve_arena must produce exactly the same solution set as solve(),
        // and every arena solution must verify.
        for &(n, k) in &[(48u32, 5u32), (72, 5)] {
            let p = Params::new(n, k).unwrap();
            for ni in 0u32..40 {
                let eng = Requihash::new(p, b"arena-check", &ni.to_le_bytes());
                let mut a = eng.solve();
                let mut b = eng.solve_arena();
                a.sort();
                b.sort();
                assert_eq!(a, b, "arena != reference at ({n},{k}) nonce {ni}");
                for s in &b {
                    eng.is_valid_solution(s).expect("arena solution must verify");
                }
            }
        }
    }

    #[test]
    fn regularity_rejects_swapped_leaves() {
        // A valid solution with two leaves swapped must fail (ordering/collision).
        let p = Params::new(48, 5).unwrap();
        let eng = Requihash::new(p, b"requihash-test-block-header", &0u32.to_le_bytes());
        let sols = eng.solve();
        let s = sols.first().expect("need a solution");
        let mut t = s.clone();
        t.swap(0, 1);
        assert!(eng.is_valid_solution(&t).is_err());
    }
}
