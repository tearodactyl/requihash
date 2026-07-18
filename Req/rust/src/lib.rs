//! Requihash miner and verifier in Rust, following the zebra
//! `zebra-chain/src/work/equihash.rs` verifier style (a `check`-like validator
//! plus a solver for round-trip tests). Regularity-repaired Equihash per
//! Tang-Sun-Gong, eprint 2025/1351 Sec 5.2. See `../Equihash.md` F-A4.
//!
//! Wire-compatible with the C++ build in `../cpp`: identical BLAKE2b
//! personalization (`"ReqPoW" || reserved[4] || le32(n) || le16(k)`), identical leaf keying
//! (`H(input || nonce || le32(leaf mod k) || le32(leaf / k))`), and identical
//! minimal (compressed) solution encoding.

mod blake2b;

pub mod hash;
pub mod probe;
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
    /// An index is >= 2^(ell+1), outside the leaf range (T2.3 F11). The wire
    /// path cannot express such values (minimal encoding is ell+1 bits per
    /// index), but the API path takes raw u32 slices, so the verifiers
    /// enforce the range explicitly as defense in depth.
    IndexOutOfRange,
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
        // T2.3 F13: the regularity binding keys a leaf by (leaf % k, leaf / k);
        // k == 0 is division by zero, not a degenerate-but-defined instance.
        // k == 1 IS well-defined (single-round collision, leaf % 1 == 0).
        if k == 0 {
            return Err(Error::BadParams("k must be >= 1 (regularity binding is leaf mod k)"));
        }
        // T2.3 F12: n > 512 makes hash_output() < n/8 (integer 512/n == 0),
        // and leaf_row's `out[..n/8]` slice would panic — e.g. (520,4) passes
        // the three checks above. One BLAKE2b digest must cover >= one row.
        if n > 512 {
            return Err(Error::BadParams("n must be <= 512 (one BLAKE2b digest per row)"));
        }
        // T2.3 F14: expand/compress_array use a u32 accumulator, so the
        // collision bit length must be in [8, 25] — the same bounds zcash's
        // ExpandArray asserts. Below 8 the expansion silently under-fills
        // rows (e.g. (24,5), cbl=4: half of every row stays zero); above 25
        // the accumulator shifts exceed 32 bits. This is the binding
        // parameter bound — tighter than any flat cap on n (for k=9 it
        // caps n at 250, for k=5 at 150).
        let cbl = n / (k + 1);
        if cbl < 8 {
            return Err(Error::BadParams("collision bit length n/(k+1) must be >= 8"));
        }
        if cbl > 25 {
            return Err(Error::BadParams("collision bit length n/(k+1) must be <= 25"));
        }
        Ok(Params { n, k })
    }
    /// Smallest and largest valid `n` for a given `k`, derived from the full
    /// constraint set `new()` enforces: `n % 8 == 0`, `n % (k+1) == 0`
    /// (together: n is a multiple of `lcm(8, k+1)`), `cbl = n/(k+1)` in
    /// `[8, 25]` (F14), and `n <= 512` (F12); `k >= 1` (F13) and `n > k` are
    /// implied. `None` when no valid `n` exists — k == 0, or k > 63 (where
    /// even cbl = 8 needs n = 8(k+1) > 512). Valid `n` are exactly the
    /// multiples of `lcm(8, k+1)` in the returned inclusive range.
    pub fn n_bounds(k: u32) -> Option<(u32, u32)> {
        if k == 0 {
            return None;
        }
        let m = k + 1;
        let step = 8 / gcd(8, m) * m; // lcm(8, k+1)
        let lo = (8 * m).div_ceil(step) * step; // cbl >= 8
        let hi = (25 * m).min(512) / step * step; // cbl <= 25, n <= 512
        (lo <= hi).then_some((lo, hi))
    }

    /// All valid `n` for `k`, ascending (empty when `n_bounds` is `None`).
    pub fn valid_n(k: u32) -> Vec<u32> {
        match Self::n_bounds(k) {
            None => Vec::new(),
            Some((lo, hi)) => {
                let step = 8 / gcd(8, k + 1) * (k + 1);
                (lo..=hi).step_by(step as usize).collect()
            }
        }
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
        // SPEC.md §3: "ReqPoW"(6) ‖ reserved[6..10)=0 ‖ le32(n) ‖ le16(k).
        let mut p = [0u8; 16];
        p[..6].copy_from_slice(b"ReqPoW");
        // p[6..10) left zero (reserved)
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

    /// Requihash regularity: leaf keyed by (list-class = leaf mod k,
    /// counter = leaf / k). THE single site of the regularity binding on the
    /// Rust side (T2.3 F1): every backend reaches it through here or through
    /// the zero-alloc `leaf_row_into` below — do not inline the keying
    /// elsewhere. Returns the expanded collision bytes for the leaf.
    pub(crate) fn leaf_row(&self, leaf: EhIndex) -> Vec<u8> {
        let mut digest = vec![0u8; self.p.hash_output()];
        let mut out = vec![0u8; (self.p.k as usize + 1) * self.p.collision_byte_length()];
        self.leaf_row_into(leaf, &mut digest, &mut out);
        out
    }

    /// Zero-alloc variant for hot leaf-fill loops: writes the expanded row
    /// into `out`, using `digest` (length `hash_output()`) as scratch. The
    /// regularity binding `(leaf mod k, leaf / k)` lives here and only here.
    pub(crate) fn leaf_row_into(&self, leaf: EhIndex, digest: &mut [u8], out: &mut [u8]) {
        let listclass = leaf % self.p.k;
        let counter = leaf / self.p.k;
        let mut s = self.base.clone();
        blake2b::update(&mut s, &listclass.to_le_bytes());
        blake2b::update(&mut s, &counter.to_le_bytes());
        blake2b::finalize(s, digest);
        expand_array_into(
            &digest[..(self.p.n / 8) as usize],
            out,
            self.p.collision_bit_length(),
            0,
        );
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
                        if !slices_distinct(&x[a].idx, &x[b].idx) {
                            continue;
                        }
                        // XOR only the surviving suffix — the collided leading
                        // cbyte is dropped here, not XORed-then-drained.
                        let remain = x[a].h.len() - cbyte;
                        let mut h = vec![0u8; remain];
                        for t in 0..remain {
                            h[t] = x[a].h[cbyte + t] ^ x[b].h[cbyte + t];
                        }
                        let (lo, hi) = if x[a].idx < x[b].idx { (a, b) } else { (b, a) };
                        let mut idx = Vec::with_capacity(x[lo].idx.len() * 2);
                        idx.extend_from_slice(&x[lo].idx);
                        idx.extend_from_slice(&x[hi].idx);
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
        // Serial leaf fill, then the shared arena merge. Keying goes through
        // leaf_row_into — the binding's single site (F1) — with one shared
        // digest scratch across all leaves.
        self.solve_arena_with_leaves(|_nrows, full, hashes| {
            let mut digest = vec![0u8; self.p.hash_output()];
            for (leaf, slot) in hashes.chunks_mut(full).enumerate() {
                self.leaf_row_into(leaf as EhIndex, &mut digest, slot);
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
            // Row count from whichever buffer still has nonzero stride
            // (T2.3 F4: was a dead `if hstride == 0 { 0 }` branch immediately
            // overwritten by an identical condition — see Req/REVIEW_REQ.md).
            nrows = if new_hstride == 0 {
                idxs.len() / new_icount
            } else {
                hashes.len() / new_hstride
            };
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
                        if !slices_distinct(&x[a].idx, &x[b].idx) {
                            continue;
                        }
                        // XOR only the surviving suffix — the collided leading
                        // cbyte is dropped here, not XORed-then-drained.
                        let remain = x[a].h.len() - cbyte;
                        let mut h = vec![0u8; remain];
                        for t in 0..remain {
                            h[t] = x[a].h[cbyte + t] ^ x[b].h[cbyte + t];
                        }
                        let (lo, hi) = if x[a].idx < x[b].idx { (a, b) } else { (b, a) };
                        let mut idx = Vec::with_capacity(x[lo].idx.len() * 2);
                        idx.extend_from_slice(&x[lo].idx);
                        idx.extend_from_slice(&x[hi].idx);
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
        // Range check (F11): every index must name a leaf, i.e. < 2^(ell+1).
        let max_leaf = 1u64 << (self.p.collision_bit_length() + 1);
        if indices.iter().any(|&i| (i as u64) >= max_leaf) {
            return Err(Error::IndexOutOfRange);
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
                let mut idx = Vec::with_capacity(a.idx.len() + b.idx.len());
                idx.extend_from_slice(&a.idx);
                idx.extend_from_slice(&b.idx);
                xc.push(Row { h, idx });
                i += 2;
            }
            x = xc;
        }
        // Invariant, not an input error: 2^k rows halve exactly k times. A
        // failure here is an internal bug, so fail loud rather than misreport
        // it as a (reachable-looking) input rejection (T2.3 F6).
        assert_eq!(x.len(), 1, "internal: fold must reduce to exactly one row");
        if !x[0].h.iter().all(|&c| c == 0) {
            return Err(Error::NonZeroRoot);
        }
        Ok(())
    }
}

fn gcd(a: u32, b: u32) -> u32 {
    let (mut a, mut b) = (a, b);
    while b != 0 {
        (a, b) = (b, a % b);
    }
    a
}

/// True iff `a` and `b` share no index. The single distinctness helper for all
/// solver backends (consolidated from two identical private copies, T2.3 F2).
/// O(|a|·|b|) pairwise scan — fine for the small per-pair vectors in the merge;
/// the sorted-merge alternative measured no better at these sizes.
pub(crate) fn slices_distinct(a: &[u32], b: &[u32]) -> bool {
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
    let mut out = vec![0u8; out_len];
    expand_array_into(input, &mut out, bit_len, byte_pad);
    out
}

/// In-place [`expand_array`]: writes into a caller-provided buffer, so hot
/// paths (one expansion per leaf) pay no per-leaf allocation. `out` must be
/// exactly the intended output length and is fully overwritten.
pub fn expand_array_into(input: &[u8], out: &mut [u8], bit_len: usize, byte_pad: usize) {
    let out_width = (bit_len + 7) / 8 + byte_pad;
    let bit_len_mask: u32 = (1u32 << bit_len) - 1;
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
    fn all_verifiers_reject_nonzero_root_near_miss() {
        // Regression test for T2.3 F10: a near-miss passes every per-round
        // collision and ordering check but its final hash segment is nonzero.
        // Before the fix, verify-arena and verify-early accepted such inputs:
        // their root check read the first k*cbyte bytes, which are zero by
        // construction for ANY input that passes the k collision rounds.
        use crate::verify::all_verifiers;
        let p = Params::new(48, 5).unwrap();
        let cbyte = p.collision_byte_length();
        let mut tested = 0usize;
        for ni in 0u32..20 {
            let eng = Requihash::new(p, b"near-miss", &ni.to_le_bytes());
            // Wagner merge identical to solve_reference, but harvest the
            // final-round rows whose remaining hash is NONzero.
            struct Row {
                h: Vec<u8>,
                idx: Vec<EhIndex>,
            }
            let init_size = 1usize << (p.collision_bit_length() + 1);
            let mut x: Vec<Row> = (0..init_size as EhIndex)
                .map(|leaf| Row {
                    h: eng.leaf_row(leaf),
                    idx: vec![leaf],
                })
                .collect();
            for _round in 1..=p.k {
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
                            if !slices_distinct(&x[a].idx, &x[b].idx) {
                                continue;
                            }
                            let remain = x[a].h.len() - cbyte;
                            let mut h = vec![0u8; remain];
                            for t in 0..remain {
                                h[t] = x[a].h[cbyte + t] ^ x[b].h[cbyte + t];
                            }
                            let (lo, hi) = if x[a].idx < x[b].idx { (a, b) } else { (b, a) };
                            let mut idx = Vec::with_capacity(x[lo].idx.len() * 2);
                            idx.extend_from_slice(&x[lo].idx);
                            idx.extend_from_slice(&x[hi].idx);
                            xc.push(Row { h, idx });
                        }
                    }
                    i = j;
                }
                x = xc;
            }
            for r in x {
                if r.h.iter().all(|&c| c == 0) {
                    continue; // an actual solution, not a near-miss
                }
                let mut u = r.idx.clone();
                u.sort_unstable();
                u.dedup();
                if u.len() != r.idx.len() {
                    continue; // would (correctly) fail NotDistinct before the root
                }
                tested += 1;
                assert!(
                    matches!(eng.is_valid_solution(&r.idx), Err(Error::NonZeroRoot)),
                    "reference must reject a near-miss with NonZeroRoot"
                );
                for v in all_verifiers() {
                    assert!(
                        v.verify(&eng, &r.idx).is_err(),
                        "{} accepted a nonzero-root near-miss (F10 regression)",
                        v.name()
                    );
                }
            }
        }
        assert!(tested > 0, "no near-miss rows harvested — test exercised nothing");
    }

    #[test]
    fn n_bounds_match_constructor() {
        // n_bounds/valid_n must agree EXACTLY with what Params::new accepts,
        // proven exhaustively over k in [0, 70], n in [1, 560] (covers both
        // boundary regimes: the 25(k+1) cap and the 512 cap, and the
        // no-valid-n region k > 63).
        for k in 0..=70u32 {
            let accepted: Vec<u32> = (1..=560).filter(|&n| Params::new(n, k).is_ok()).collect();
            assert_eq!(accepted, Params::valid_n(k), "valid_n mismatch at k={k}");
            match Params::n_bounds(k) {
                None => assert!(accepted.is_empty(), "k={k}: bounds None but n accepted"),
                Some((lo, hi)) => {
                    assert_eq!(accepted.first(), Some(&lo), "k={k} lo");
                    assert_eq!(accepted.last(), Some(&hi), "k={k} hi");
                }
            }
        }
        // Spot anchors: production and boundary instances.
        assert_eq!(Params::n_bounds(9), Some((80, 240))); // (200,9) inside
        assert_eq!(Params::n_bounds(5), Some((48, 144))); // k=5 sweep ceiling
        assert_eq!(Params::n_bounds(7), Some((64, 200))); // cbl-25 boundary
        assert_eq!(Params::n_bounds(63), Some((512, 512))); // last valid k
        assert_eq!(Params::n_bounds(64), None);
        assert_eq!(Params::n_bounds(0), None); // F13
    }

    #[test]
    fn params_rejected() {
        // BadParams corner cases:
        //   F12: n > 512 previously passed the divisibility gates ((520,4))
        //        and panicked later in leaf_row;
        //   F13: k == 0 is division by zero in the regularity binding;
        //   F14: collision bit length must be in [8, 25] (u32 accumulator in
        //        expand/compress_array; zcash's own ExpandArray bounds) —
        //        (24,5) has cbl 4 (silent row under-fill), (168,5) has 28.
        for (n, k) in [
            (200u32, 200u32), // k >= n
            (50, 5),          // n % 8 != 0
            (48, 6),          // n % (k+1) != 0
            (48, 0),          // F13: k == 0
            (520, 4),         // F12: n > 512
            (24, 5),          // F14: cbl 4 < 8
            (168, 5),         // F14: cbl 28 > 25
            (512, 3),         // F14: cbl 128 > 25
            (0, 0),           // k >= n (both zero)
        ] {
            assert!(
                matches!(Params::new(n, k), Err(Error::BadParams(_))),
                "({n},{k}) must be rejected"
            );
        }
        assert!(Params::new(48, 5).is_ok());
        assert!(Params::new(48, 1).is_ok(), "k == 1 is well-defined (cbl 24)");
        assert!(Params::new(200, 9).is_ok());
        assert!(Params::new(144, 5).is_ok(), "k=5 sweep ceiling (cbl 24)");
        assert!(Params::new(200, 7).is_ok(), "cbl == 25 upper boundary");
        assert!(Params::new(512, 31).is_ok(), "n == 512 boundary (cbl 16)");
    }

    #[test]
    fn rejection_path_matrix() {
        // T2.3 corner-case inventory: for every Error variant an input that
        // fails exactly there, and every verifier must return the SAME
        // variant (they share the check order: length -> distinct -> range ->
        // per-round collision-then-ordering -> root). NonZeroRoot is covered
        // by `all_verifiers_reject_nonzero_root_near_miss`; BadParams by
        // `params_rejected`-style constructor tests.
        use crate::verify::all_verifiers;

        fn assert_all(eng: &Requihash, input: &[EhIndex], expect: &Error, case: &str) {
            let got = eng
                .is_valid_solution(input)
                .expect_err(&format!("{case}: reference accepted"));
            assert_eq!(
                format!("{got:?}"),
                format!("{expect:?}"),
                "{case}: reference variant"
            );
            for v in all_verifiers() {
                let got = v
                    .verify(eng, input)
                    .expect_err(&format!("{case}: {} accepted", v.name()));
                assert_eq!(
                    format!("{got:?}"),
                    format!("{expect:?}"),
                    "{case}: {} variant",
                    v.name()
                );
            }
        }

        let p = Params::new(48, 5).unwrap();
        let k = p.k as usize;
        let cbyte = p.collision_byte_length();
        let (eng, sol) = (0u32..50)
            .find_map(|ni| {
                let eng = Requihash::new(p, b"matrix", &ni.to_le_bytes());
                eng.solve_reference().into_iter().next().map(|s| (eng, s))
            })
            .expect("no solution in 50 nonces");

        // WrongLength: empty, truncated, extended (length gate fires first).
        assert_all(&eng, &[], &Error::WrongLength, "WrongLength/empty");
        assert_all(&eng, &sol[..sol.len() - 1], &Error::WrongLength, "WrongLength/short");
        let mut long = sol.clone();
        long.push(sol[0]);
        assert_all(&eng, &long, &Error::WrongLength, "WrongLength/long");

        // NotDistinct: duplicate one index.
        let mut dup = sol.clone();
        dup[1] = dup[0];
        assert_all(&eng, &dup, &Error::NotDistinct, "NotDistinct");

        // IndexOutOfRange (F11): first non-leaf value and u32::MAX.
        let max_leaf = 1u32 << (p.collision_bit_length() + 1);
        for bad in [max_leaf, u32::MAX] {
            let mut oor = sol.clone();
            oor[0] = bad;
            assert_all(&eng, &oor, &Error::IndexOutOfRange, "IndexOutOfRange");
        }

        // OrderingFailed(r), every round: swap the first two subtrees at
        // level r-1. Blocks of 2^(r-1) leaves stay pair-aligned, so all
        // rounds < r pass untouched; at round r the collision still holds
        // (XOR is symmetric) but left > right breaks canonical ordering.
        for r in 1..=k {
            let span = 1usize << (r - 1);
            let mut t = sol.clone();
            t[..2 * span].rotate_left(span);
            assert_all(
                &eng,
                &t,
                &Error::OrderingFailed(r as u32),
                &format!("OrderingFailed({r})"),
            );
        }

        // CollisionFailed(r), every round: build the input from harvested
        // round-(r-1) rows — each internally valid, mutually leaf-disjoint,
        // with the FIRST TWO differing in their leading collision segment.
        // All rounds < r pass inside each subtree; at round r the first
        // pair's collision check fails before anything else is examined.
        struct HRow {
            h: Vec<u8>,
            idx: Vec<EhIndex>,
        }
        let init = 1usize << (p.collision_bit_length() + 1);
        let mut rounds: Vec<Vec<HRow>> = vec![(0..init as EhIndex)
            .map(|l| HRow {
                h: eng.leaf_row(l),
                idx: vec![l],
            })
            .collect()];
        for _t in 1..k {
            let prev = rounds.last().unwrap();
            let mut order: Vec<usize> = (0..prev.len()).collect();
            order.sort_by(|&a, &b| prev[a].h[..cbyte].cmp(&prev[b].h[..cbyte]));
            let mut next: Vec<HRow> = Vec::new();
            let mut i = 0;
            while i + 1 < order.len() {
                let mut j = i + 1;
                while j < order.len() && prev[order[j]].h[..cbyte] == prev[order[i]].h[..cbyte] {
                    j += 1;
                }
                for a in i..j {
                    for b in (a + 1)..j {
                        let (ra, rb) = (order[a], order[b]);
                        if !slices_distinct(&prev[ra].idx, &prev[rb].idx) {
                            continue;
                        }
                        let remain = prev[ra].h.len() - cbyte;
                        let mut h = vec![0u8; remain];
                        for t2 in 0..remain {
                            h[t2] = prev[ra].h[cbyte + t2] ^ prev[rb].h[cbyte + t2];
                        }
                        let (lo, hi) = if prev[ra].idx < prev[rb].idx { (ra, rb) } else { (rb, ra) };
                        let mut idx = Vec::with_capacity(prev[lo].idx.len() * 2);
                        idx.extend_from_slice(&prev[lo].idx);
                        idx.extend_from_slice(&prev[hi].idx);
                        next.push(HRow { h, idx });
                    }
                }
                i = j;
            }
            rounds.push(next);
        }
        for r in 1..=k {
            let pool = &rounds[r - 1];
            let m = 1usize << (k - r + 1);
            let mut sel: Vec<usize> = Vec::new();
            let mut used = std::collections::HashSet::new();
            for (pi, row) in pool.iter().enumerate() {
                if row.idx.iter().any(|i| used.contains(i)) {
                    continue;
                }
                if sel.len() == 1 && row.h[..cbyte] == pool[sel[0]].h[..cbyte] {
                    continue; // slot 1 must break the collision with slot 0
                }
                used.extend(row.idx.iter().copied());
                sel.push(pi);
                if sel.len() == m {
                    break;
                }
            }
            assert_eq!(sel.len(), m, "harvest: not enough disjoint rows at round {r}");
            let mut input: Vec<EhIndex> = Vec::with_capacity(1 << k);
            for &pi in &sel {
                input.extend_from_slice(&pool[pi].idx);
            }
            assert_all(
                &eng,
                &input,
                &Error::CollisionFailed(r as u32),
                &format!("CollisionFailed({r})"),
            );
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
        // Search a few nonces for one that yields a solution — which nonce
        // does is persona-dependent (SPEC.md §3), so don't pin nonce 0.
        let p = Params::new(48, 5).unwrap();
        let s = (0u32..64)
            .find_map(|nonce| {
                let eng = Requihash::new(p, b"requihash-test-block-header",
                                         &nonce.to_le_bytes());
                eng.solve().first().map(|s| (eng, s.clone()))
            });
        let (eng, s) = s.expect("a solution within the first 64 nonces");
        let mut t = s.clone();
        t.swap(0, 1);
        assert!(eng.is_valid_solution(&t).is_err());
    }

    // ---- A19: expand_array/compress_array vs. the pinned equihash crate ----
    //
    // Closes the gap PLAN.md A19 tracked: SPEC.md previously said this repo's
    // ExpandArray/CompressArray reimplementation was "written to be
    // interoperable, byte-accurate" without independent verification.
    //
    // The pinned crate's compress_array/expand_array/minimal_from_indices
    // ("rough translation of CompressArray()/ExpandArray()/
    // GetMinimalFromIndices() from zcash/zcash@6fdd9f1/src/crypto/equihash.cpp")
    // are `pub(crate)` upstream, so calling them directly required a local,
    // visibility-widened vendored copy rather than the plain crates.io
    // dependency (editing cargo's own content-hash-verified registry cache
    // in place isn't viable -- it's shared across every project on this
    // machine and cargo detects/reverts local edits) --
    // `../third_party/equihash-0.3.0-vendored/`, see its `PATCH.md` for the
    // exact `pub(crate)` -> `pub` diff, nothing else changed. This calls that
    // crate's real, unmodified logic directly, not a reproduction of its
    // test fixtures.
    #[test]
    fn expand_compress_array_round_trips_against_pinned_equihash_crate() {
        use equihash::minimal::{compress_array as eq_compress, expand_array as eq_expand};

        // A pseudo-random byte stream (xorshift, no external RNG dependency)
        // exercised at every bit_len/byte_pad combination this repo's own
        // expand_array/compress_array actually uses (SPEC.md ss4.2/ss8.1: ell
        // and ell+1 bit_len, byte_pad 0 and the 4-byte-index padding
        // get_minimal_from_indices computes) -- broader coverage than a
        // handful of fixed vectors, and every case is checked against the
        // real crate function, not a copy of its expected output.
        fn xorshift(state: &mut u64) -> u64 {
            *state ^= *state << 13;
            *state ^= *state >> 7;
            *state ^= *state << 17;
            *state
        }

        for &(bit_len, byte_pad, n_bits_total) in &[
            (11usize, 0usize, 176usize), // matches the crate's own fixture shape
            (21, 0, 168),
            (14, 0, 224),
            (11, 2, 176),
            (20, 0, 320),  // ell=20 (e.g. (100,4)/(120,5))
            (21, 0, 336),  // ell+1=21 (e.g. (200,9) neighbourhood: n/(k+1)=20)
            (24, 0, 384),  // ell=24 (e.g. (144,5))
            (24, 3, 384),  // byte_pad=3, the get_minimal_from_indices index-array shape
        ] {
            let mut state = 0x2545F4914F6CDD1Du64 ^ ((bit_len as u64) << 32) ^ (byte_pad as u64);
            let in_width = bit_len.div_ceil(8) + byte_pad;
            let n_words = n_bits_total / bit_len;
            let expanded_len = n_words * in_width;
            let mut expanded = vec![0u8; expanded_len];
            for chunk in expanded.chunks_mut(in_width) {
                for x in byte_pad..in_width {
                    chunk[x] = xorshift(&mut state) as u8;
                }
                // Mask each element's top bits to fit bit_len, matching how
                // real leaf/index data is always sub-byte-boundary-masked
                // before packing -- otherwise compress_array/expand_array's
                // round trip is only exact modulo the mask, which would
                // make this test assert something neither implementation
                // actually promises.
                let extra_bits = 8 * (in_width - byte_pad) - bit_len;
                if extra_bits > 0 && in_width > byte_pad {
                    chunk[byte_pad] &= 0xFFu8 >> extra_bits;
                }
            }
            let compact_len = bit_len * n_words / 8;

            let ours_compact = compress_array(&expanded, compact_len, bit_len, byte_pad);
            let theirs_compact = eq_compress(&expanded, bit_len, byte_pad);
            assert_eq!(
                ours_compact, theirs_compact,
                "compress_array diverges from equihash crate at bit_len={bit_len} byte_pad={byte_pad}"
            );

            let ours_expanded = expand_array(&ours_compact, expanded_len, bit_len, byte_pad);
            let theirs_expanded = eq_expand(&theirs_compact, bit_len, byte_pad);
            assert_eq!(
                ours_expanded, theirs_expanded,
                "expand_array diverges from equihash crate at bit_len={bit_len} byte_pad={byte_pad}"
            );
            assert_eq!(
                ours_expanded, expanded,
                "expand(compress(x)) != x for this repo's own pair at bit_len={bit_len} byte_pad={byte_pad}"
            );
        }
    }

    #[test]
    fn get_minimal_from_indices_matches_pinned_equihash_crate() {
        use equihash::minimal::minimal_from_indices;
        use equihash::params::Params;

        // Real solved Requihash index sets at every (n,k) this repo tests
        // elsewhere, fed through both this repo's get_minimal_from_indices
        // and the vendored crate's real minimal_from_indices, asserting
        // byte-exact agreement -- not a comparison against copied fixture
        // bytes.
        for &(n, k) in &[(48u32, 5u32), (72, 5)] {
            let p = Params::new(n, k).expect("valid (n,k) for the vendored crate too");
            let cbitlen = (n / (k + 1)) as usize;
            assert_eq!(cbitlen, p.collision_bit_length());

            let req_p = crate::Params { n, k };
            for nonce in 0u32..10 {
                let eng = Requihash::new(req_p, b"a19-crosscheck", &nonce.to_le_bytes());
                for sol in eng.solve() {
                    let ours = get_minimal_from_indices(&sol, cbitlen);
                    let theirs = minimal_from_indices(p, &sol);
                    assert_eq!(
                        ours, theirs,
                        "minimal encoding diverges from equihash crate at (n={n},k={k}) nonce={nonce}"
                    );
                    assert_eq!(get_indices_from_minimal(&ours, cbitlen), sol);
                }
            }
        }
    }

    // ---- A14: official Zcash Equihash KAT vectors verify against the
    // pinned equihash crate (the correct oracle for single-list keying) ----
    //
    // The vectors/zcash_kat_*.json files are the equihash crate's own
    // test_vectors::valid (keying = "single", "ZcashPoW" persona) — a
    // DIFFERENT construction from Requihash's regular keying. They must be
    // verified with the Zcash verifier, not the Requihash engine (which is
    // why req_xcheck, a Requihash-only tool, correctly skips them). Each
    // file is a JSON list of vectors; every one must validate.
    #[test]
    fn zcash_kat_vectors_verify_against_pinned_equihash_crate() {
        use std::fs;

        // Extract every value for `key` across a JSON list, in order.
        fn all_str_fields(s: &str, key: &str) -> Vec<String> {
            let pat = format!("\"{key}\"");
            let mut out = Vec::new();
            let mut rest = s;
            while let Some(i) = rest.find(&pat) {
                let after = &rest[i + pat.len()..];
                let colon = after.find(':').unwrap();
                let v = after[colon + 1..].trim_start();
                let start = v.find('"').unwrap() + 1;
                let end = v[start..].find('"').unwrap() + start;
                out.push(v[start..end].to_string());
                rest = &after[colon + 1 + end..];
            }
            out
        }
        fn all_u32_fields(s: &str, key: &str) -> Vec<u32> {
            let pat = format!("\"{key}\"");
            let mut out = Vec::new();
            let mut rest = s;
            while let Some(i) = rest.find(&pat) {
                let after = &rest[i + pat.len()..];
                let colon = after.find(':').unwrap();
                let v = after[colon + 1..].trim_start();
                let end = v.find(|c: char| !c.is_ascii_digit()).unwrap_or(v.len());
                out.push(v[..end].parse().unwrap());
                rest = &after[colon + 1 + end..];
            }
            out
        }
        fn hex(h: &str) -> Vec<u8> {
            (0..h.len())
                .step_by(2)
                .map(|i| u8::from_str_radix(&h[i..i + 2], 16).unwrap())
                .collect()
        }

        let mut total = 0usize;
        for name in ["zcash_kat_96_5", "zcash_kat_144_5", "zcash_kat_200_9"] {
            // Located relative to Req/rust (CARGO_MANIFEST_DIR).
            let path = format!("{}/../vectors/{name}.json", env!("CARGO_MANIFEST_DIR"));
            let s = fs::read_to_string(&path)
                .unwrap_or_else(|e| panic!("cannot read {path}: {e}"));

            let ns = all_u32_fields(&s, "n");
            let ks = all_u32_fields(&s, "k");
            let inputs = all_str_fields(&s, "input_hex");
            let nonces = all_str_fields(&s, "nonce_hex");
            let minimals = all_str_fields(&s, "minimal_hex");
            let keyings = all_str_fields(&s, "keying");
            let count = minimals.len();
            assert!(count > 0, "{name}: no vectors parsed");
            assert_eq!(ns.len(), count, "{name}: field-count mismatch");

            for i in 0..count {
                assert_eq!(keyings[i], "single", "{name}[{i}]: expected single keying");
                // The crate takes the minimal (compressed) solution bytes
                // directly and does its own decode + full verification.
                let res = equihash::is_valid_solution(
                    ns[i], ks[i], &hex(&inputs[i]), &hex(&nonces[i]), &hex(&minimals[i]),
                );
                assert!(
                    res.is_ok(),
                    "{name}[{i}] (n={},k={}) rejected by pinned equihash crate: {:?}",
                    ns[i], ks[i], res
                );
                total += 1;
            }
        }
        assert!(total >= 3, "expected multiple KAT vectors, got {total}");
        eprintln!("verified {total} official Zcash Equihash KAT vectors against the pinned crate");
    }
}
