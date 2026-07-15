//! Native Rust port of the vendored, single-core-stripped tromp Equihash
//! solver (`equihash-0.3.0/tromp/equi_miner.c`) specialized to
//! `(WN=144, WK=5, RESTBITS=4)` only.
//!
//! This is a library with no I/O and no CLI: [`solve_144_4`] takes an
//! `input` byte string and a `nonce` byte string, and returns the raw
//! index-set solutions (each a `Vec<u32>` of length `PROOFSIZE=32`), one
//! per Equihash solution found for that (input, nonce) pair, matching
//! what `equihash-0.3.0/src/tromp.rs`'s `worker()` returns before the
//! crate's separate `minimal_from_indices` compression step.
//!
//! Ported directly from the vendored C's `equi_digit0`/`equi_digitodd`/
//! `equi_digiteven`/`equi_digitK`/`candidate`/`listindices0`/
//! `listindices1`, driven single-core exactly as
//! `equihash-0.3.0/src/tromp.rs::worker` drives the C functions (hardcoded
//! `id=0`, no thread spawn, no call into the C file's own unused
//! `worker()`). See `STATUS.md` in this directory for the full derivation
//! of every constant and `#if`/`#elif` branch taken at this specialization.
//!
//! Not ported: `SLOTDIFF`/`XBITMAP`/`EQUIHASH_TROMP_ATOMIC` variants (none
//! of those macros are defined by this crate's `build.rs`, so the C build
//! being cross-checked against never takes those branches either), and
//! the C's arena-reuse memory layout (`htalloc`/`alloctrees`) — this port
//! uses one plain growable buffer per tree parity instead, which is safe
//! because the algorithm only ever reads the immediately preceding
//! round's data (see STATUS.md's "Storage layout note").

use blake2b_simd::{Params as Blake2bParams, State as Blake2bState};

// ---- compile-time constants for (WN=144, WK=5, RESTBITS=4) ----
// (see STATUS.md for the derivation of each of these from equi.h /
// equi_miner.c's macros)

const WN: u32 = 144;
const WK: u32 = 5;
const RESTBITS: u32 = 4;

const NDIGITS: u32 = WK + 1; // 6
const DIGITBITS: u32 = WN / NDIGITS; // 24
const PROOFSIZE: usize = 1 << WK; // 32
const BASE: u32 = 1 << DIGITBITS; // 16_777_216
const NHASHES: u32 = 2 * BASE; // 33_554_432
const HASHESPERBLAKE: u32 = 512 / WN; // 3
const HASHOUT: usize = (HASHESPERBLAKE * WN / 8) as usize; // 54
const BUCKBITS: u32 = DIGITBITS - RESTBITS; // 20
const NBUCKETS: usize = 1 << BUCKBITS; // 1_048_576
const SLOTBITS: u32 = RESTBITS + 1 + 1; // 6
const SLOTRANGE: u32 = 1 << SLOTBITS; // 64
const NSLOTS: usize = SLOTRANGE as usize; // 64 (SAVEMEM=1 at RESTBITS==4)
const SLOTMASK: u32 = SLOTRANGE - 1; // 63
const XFULL: usize = 16;
const NRESTS: usize = 1 << RESTBITS; // 16
const NBLOCKS: u32 = NHASHES.div_ceil(HASHESPERBLAKE); // 11_184_811
const MAXSOLS: usize = 8;

/// Bytes of hash carried into round `r` (0..=WK), i.e. `hashsize(r)` in
/// the C.
const fn hashsize(r: u32) -> usize {
    let hashbits = WN - (r + 1) * DIGITBITS + RESTBITS;
    ((hashbits + 7) / 8) as usize
}

/// `hashwords(bytes)` in the C: number of 4-byte words needed to hold
/// `bytes` bytes.
const fn hashwords(bytes: usize) -> usize {
    bytes.div_ceil(4)
}

/// Per-round byte layout, mirroring `htlayout_new` in the C exactly.
#[derive(Clone, Copy)]
struct HtLayout {
    prevhashunits: usize,
    dunits: usize,
    prevbo: usize,
    nextbo: usize,
}

impl HtLayout {
    fn new(r: u32) -> Self {
        let nexthashbytes = hashsize(r);
        let nexthashunits = hashwords(nexthashbytes);
        let nextbo = nexthashunits * 4 - nexthashbytes;
        let (prevhashunits, prevbo, dunits) = if r == 0 {
            (0, 0, 0)
        } else {
            let prevhashbytes = hashsize(r - 1);
            let prevhashunits = hashwords(prevhashbytes);
            let prevbo = prevhashunits * 4 - prevhashbytes;
            let dunits = prevhashunits - nexthashunits;
            (prevhashunits, prevbo, dunits)
        };
        HtLayout {
            prevhashunits,
            dunits,
            prevbo,
            nextbo,
        }
    }
}

/// One slot's hash payload, stored as up to 4 little-endian u32 "hash
/// words" the way the C's `hashunit`-array slots are, but here sized
/// generously (4 words = max `HASHWORDS0`/`HASHWORDS1` at this param
/// set) and addressed by byte offset via `hash_bytes()`/`hash_bytes_mut()`
/// to match the C's byte-level `pslot->hash->bytes[...]` accesses exactly.
#[derive(Clone, Copy, Default)]
struct Slot {
    /// Tree attribute: for round 0, the raw leaf index; for later
    /// rounds, the bitpacked `(bucketid, s0, s1)` triple. Mirrors the
    /// C's `tree.bid_s0_s1`.
    attr: u32,
    hash: [u32; 4],
}

impl Slot {
    fn hash_bytes(&self) -> [u8; 16] {
        let mut out = [0u8; 16];
        for (i, word) in self.hash.iter().enumerate() {
            out[i * 4..i * 4 + 4].copy_from_slice(&word.to_le_bytes());
        }
        out
    }

    fn hash_byte(&self, bo: usize) -> u8 {
        let word = self.hash[bo / 4];
        word.to_le_bytes()[bo % 4]
    }

    fn set_hash_word(&mut self, i: usize, value: u32) {
        self.hash[i] = value;
    }

    fn hash_word(&self, i: usize) -> u32 {
        self.hash[i]
    }
}

/// `tree_from_bid` in the C: bitpack `(bucketid, s0, s1)` into one u32.
fn tree_from_bid(bucketid: u32, s0: u32, s1: u32) -> u32 {
    ((bucketid << SLOTBITS) | s0) << SLOTBITS | s1
}

fn tree_bucketid(t: u32) -> u32 {
    t >> (2 * SLOTBITS)
}

fn tree_slotid0(t: u32) -> u32 {
    (t >> SLOTBITS) & SLOTMASK
}

fn tree_slotid1(t: u32) -> u32 {
    t & SLOTMASK
}

/// One digit's worth of storage: `NBUCKETS` buckets, each up to `NSLOTS`
/// slots, plus a per-bucket fill count. Mirrors the C's `bsizes`/
/// `digit0`/`digit1` combined (this port does not replicate the C's
/// interleaved arena reuse -- see the module doc comment).
struct Digit {
    slots: Vec<[Slot; NSLOTS]>,
    nslots: Vec<u32>,
}

impl Digit {
    fn new() -> Self {
        Digit {
            slots: (0..NBUCKETS).map(|_| [Slot::default(); NSLOTS]).collect(),
            nslots: vec![0u32; NBUCKETS],
        }
    }

    /// `getslot`: claim the next free slot index in `bucketid`,
    /// incrementing its fill count unconditionally (matching the C,
    /// which increments even when the returned slot is >= NSLOTS and
    /// the caller must discard).
    fn getslot(&mut self, bucketid: usize) -> usize {
        let s = self.nslots[bucketid] as usize;
        self.nslots[bucketid] += 1;
        s
    }

    /// `getnslots`: read the (capped) fill count for `bucketid` and
    /// reset it to 0, matching the C's read-then-clear semantics (this
    /// is what actually "clears slots" between rounds, despite the
    /// name `equi_clearslots` referring to something else -- see
    /// STATUS.md).
    fn getnslots(&mut self, bucketid: usize) -> usize {
        let n = (self.nslots[bucketid] as usize).min(NSLOTS);
        self.nslots[bucketid] = 0;
        n
    }
}

/// Collision-bucketing scratch space for one bucket's worth of
/// same-xhash slot pairs, mirroring the C's `collisiondata` (non-XBITMAP
/// variant, since `XBITMAP` is not defined in this build).
struct CollisionData {
    nxhashslots: [u16; NRESTS],
    xhashslots: [[u16; XFULL]; NRESTS],
}

impl CollisionData {
    fn new() -> Self {
        CollisionData {
            nxhashslots: [0u16; NRESTS],
            xhashslots: [[0u16; XFULL]; NRESTS],
        }
    }

    fn clear(&mut self) {
        self.nxhashslots = [0u16; NRESTS];
    }

    /// Returns `None` (matching the C's `false` / `xfull++` path) if
    /// this xhash bucket is already full.
    fn addslot(&mut self, s1: u32, xh: usize) -> Option<CollisionIter> {
        let n1 = self.nxhashslots[xh] as usize;
        self.nxhashslots[xh] += 1;
        if n1 >= XFULL {
            return None;
        }
        self.xhashslots[xh][n1] = s1 as u16;
        Some(CollisionIter { xh, n1, n0: 0 })
    }
}

/// Iterates the `s0` candidates already recorded for a given xhash
/// bucket, matching the C's `nextcollision`/`slot` pair.
struct CollisionIter {
    xh: usize,
    n1: usize,
    n0: usize,
}

impl CollisionIter {
    fn next(&mut self, cd: &CollisionData) -> Option<u32> {
        if self.n0 < self.n1 {
            let s0 = cd.xhashslots[self.xh][self.n0] as u32;
            self.n0 += 1;
            Some(s0)
        } else {
            None
        }
    }
}

fn htlayout_equal(htl: &HtLayout, a: &Slot, b: &Slot) -> bool {
    a.hash_word(htl.prevhashunits - 1) == b.hash_word(htl.prevhashunits - 1)
}

/// `getxhash0`/`getxhash1` at `(WN=144, RESTBITS=4)`: both branches are
/// byte-identical in the source (see STATUS.md) -- low nibble of the
/// hash byte at `prevbo`.
fn getxhash(htl: &HtLayout, slot: &Slot) -> usize {
    (slot.hash_byte(htl.prevbo) & 0xf) as usize
}

/// Equihash solver state for one (input, nonce) run at (144, 5, 4).
struct Equi {
    base_state: Blake2bState,
    /// `trees0[i]` holds round `2*i` (even rounds: 0, 2, 4).
    trees0: Vec<Digit>,
    /// `trees1[i]` holds round `2*i+1` (odd rounds: 1, 3).
    trees1: Vec<Digit>,
    sols: Vec<[u32; PROOFSIZE]>,
    nsols: usize,
}

impl Equi {
    fn new(base_state: Blake2bState) -> Self {
        Equi {
            base_state,
            // Round r in 0..WK (0..5): even rounds 0,2,4 -> trees0[0,1,2];
            // odd rounds 1,3 -> trees1[0,1]. WK=5 so trees0 needs 3
            // slots ((WK+1)/2 = 3, matching htalloc's bucket0
            // trees0[(WK+1)/2]) and trees1 needs 2 (WK/2 = 2).
            trees0: (0..(WK as usize + 1) / 2).map(|_| Digit::new()).collect(),
            trees1: (0..WK as usize / 2).map(|_| Digit::new()).collect(),
            sols: Vec::new(),
            nsols: 0,
        }
    }

    fn digit0(&mut self) {
        let htl = HtLayout::new(0);
        let hashbytes = hashsize(0);
        for block in 0..NBLOCKS {
            let mut state = self.base_state.clone();
            state.update(&block.to_le_bytes());
            let hash = state.finalize();
            let hash = hash.as_bytes();
            debug_assert!(hash.len() >= HASHOUT);

            for i in 0..HASHESPERBLAKE as usize {
                let ph = &hash[i * (WN as usize / 8)..i * (WN as usize / 8) + (WN as usize / 8)];
                // BUCKBITS==20 && RESTBITS==4 branch:
                let bucketid = (((ph[0] as u32) << 8) | ph[1] as u32) << 4 | (ph[2] as u32) >> 4;
                let bucketid = bucketid as usize;

                let digit = &mut self.trees0[0];
                let slot_idx = digit.getslot(bucketid);
                if slot_idx >= NSLOTS {
                    continue; // eq->bfull++ in the C; not tracked here (stats only)
                }
                let s = &mut digit.slots[bucketid][slot_idx];
                s.attr = block * HASHESPERBLAKE + i as u32;
                // memcpy(s->hash->bytes+htl.nextbo, ph+WN/8-hashbytes, hashbytes)
                let src = &ph[(WN as usize / 8 - hashbytes)..];
                let mut hb = s.hash_bytes();
                hb[htl.nextbo..htl.nextbo + hashbytes].copy_from_slice(&src[..hashbytes]);
                for w in 0..4 {
                    s.set_hash_word(w, u32::from_le_bytes([hb[w * 4], hb[w * 4 + 1], hb[w * 4 + 2], hb[w * 4 + 3]]));
                }
            }
        }
    }

    /// `equi_digitodd`: round `r` is odd (1 or 3 at WK=5), reads
    /// `trees0[(r-1)/2]`, writes `trees1[r/2]`.
    fn digitodd(&mut self, r: u32) {
        let htl = HtLayout::new(r);
        let mut cd = CollisionData::new();
        let src_idx = (r as usize - 1) / 2;
        let dst_idx = r as usize / 2;

        for bucketid in 0..NBUCKETS {
            cd.clear();
            let bsize = self.trees0[src_idx].getnslots(bucketid);
            // Snapshot the bucket's slots up to bsize so we can read s0/s1
            // pairs freely while collecting collisions (matches the C,
            // which only ever reads this bucket during this iteration).
            let bucket: Vec<Slot> = self.trees0[src_idx].slots[bucketid][..bsize].to_vec();

            for s1 in 0..bsize {
                let pslot1 = &bucket[s1];
                let xh = getxhash(&htl, pslot1);
                let Some(mut it) = cd.addslot(s1 as u32, xh) else {
                    continue; // xfull
                };
                while let Some(s0) = it.next(&cd) {
                    let pslot0 = &bucket[s0 as usize];
                    if htlayout_equal(&htl, pslot0, pslot1) {
                        continue; // hfull
                    }
                    let bytes0 = pslot0.hash_bytes();
                    let bytes1 = pslot1.hash_bytes();
                    let pb = htl.prevbo;
                    // WN==144 && BUCKBITS==20 && RESTBITS==4 branch:
                    let xorbucketid = ((((bytes0[pb + 1] ^ bytes1[pb + 1]) as u32) << 8)
                        | (bytes0[pb + 2] ^ bytes1[pb + 2]) as u32)
                        << 4
                        | ((bytes0[pb + 3] ^ bytes1[pb + 3]) as u32) >> 4;
                    let xorbucketid = xorbucketid as usize;

                    let dst = &mut self.trees1[dst_idx];
                    let xorslot = dst.getslot(xorbucketid);
                    if xorslot >= NSLOTS {
                        continue; // bfull
                    }
                    let xs = &mut dst.slots[xorbucketid][xorslot];
                    xs.attr = tree_from_bid(bucketid as u32, s0, s1 as u32);
                    for i in htl.dunits..htl.prevhashunits {
                        xs.set_hash_word(i - htl.dunits, pslot0.hash_word(i) ^ pslot1.hash_word(i));
                    }
                }
            }
        }
    }

    /// `equi_digiteven`: round `r` is even (2 or 4 at WK=5), reads
    /// `trees1[(r-1)/2]`, writes `trees0[r/2]`.
    fn digiteven(&mut self, r: u32) {
        let htl = HtLayout::new(r);
        let mut cd = CollisionData::new();
        let src_idx = (r as usize - 1) / 2;
        let dst_idx = r as usize / 2;

        for bucketid in 0..NBUCKETS {
            cd.clear();
            let bsize = self.trees1[src_idx].getnslots(bucketid);
            let bucket: Vec<Slot> = self.trees1[src_idx].slots[bucketid][..bsize].to_vec();

            for s1 in 0..bsize {
                let pslot1 = &bucket[s1];
                let xh = getxhash(&htl, pslot1);
                let Some(mut it) = cd.addslot(s1 as u32, xh) else {
                    continue;
                };
                while let Some(s0) = it.next(&cd) {
                    let pslot0 = &bucket[s0 as usize];
                    if htlayout_equal(&htl, pslot0, pslot1) {
                        continue;
                    }
                    let bytes0 = pslot0.hash_bytes();
                    let bytes1 = pslot1.hash_bytes();
                    let pb = htl.prevbo;
                    // WN==144 && BUCKBITS==20 && RESTBITS==4 branch
                    // (identical formula to the odd-round branch, but
                    // over the byte offsets from THIS round's htlayout).
                    let xorbucketid = ((((bytes0[pb + 1] ^ bytes1[pb + 1]) as u32) << 8)
                        | (bytes0[pb + 2] ^ bytes1[pb + 2]) as u32)
                        << 4
                        | ((bytes0[pb + 3] ^ bytes1[pb + 3]) as u32) >> 4;
                    let xorbucketid = xorbucketid as usize;

                    let dst = &mut self.trees0[dst_idx];
                    let xorslot = dst.getslot(xorbucketid);
                    if xorslot >= NSLOTS {
                        continue;
                    }
                    let xs = &mut dst.slots[xorbucketid][xorslot];
                    xs.attr = tree_from_bid(bucketid as u32, s0, s1 as u32);
                    for i in htl.dunits..htl.prevhashunits {
                        xs.set_hash_word(i - htl.dunits, pslot0.hash_word(i) ^ pslot1.hash_word(i));
                    }
                }
            }
        }
    }

    /// `equi_digitK`: final round (`WK=5`, odd), reads `trees0[(WK-1)/2]`.
    fn digit_k(&mut self) {
        let htl = HtLayout::new(WK);
        let mut cd = CollisionData::new();
        let src_idx = (WK as usize - 1) / 2;

        for bucketid in 0..NBUCKETS {
            cd.clear();
            let bsize = self.trees0[src_idx].getnslots(bucketid);
            let bucket: Vec<Slot> = self.trees0[src_idx].slots[bucketid][..bsize].to_vec();

            for s1 in 0..bsize {
                let pslot1 = &bucket[s1];
                let xh = getxhash(&htl, pslot1);
                let Some(mut it) = cd.addslot(s1 as u32, xh) else {
                    continue;
                };
                while let Some(s0) = it.next(&cd) {
                    let pslot0 = &bucket[s0 as usize];
                    if htlayout_equal(&htl, pslot0, pslot1) {
                        let t = tree_from_bid(bucketid as u32, s0, s1 as u32);
                        self.candidate(t);
                    }
                }
            }
        }
    }

    /// `candidate`: unfold the tree to 32 leaf indices, sort, reject
    /// duplicates, else record as a solution (subject to `MAXSOLS`
    /// physical storage cap, matching the C exactly).
    fn candidate(&mut self, t: u32) {
        let mut prf = [0u32; PROOFSIZE];
        self.listindices1(WK, t, &mut prf); // assume WK odd (WK=5)

        let mut sorted = prf;
        sorted.sort_unstable();
        for i in 1..PROOFSIZE {
            if sorted[i] <= sorted[i - 1] {
                return;
            }
        }

        let soli = self.nsols;
        self.nsols += 1;
        if soli < MAXSOLS {
            self.sols.push(prf);
        }
    }

    /// `listindices0`: unfold from a `trees1`-sourced tree attr (even
    /// depth in the recursion), or return the raw leaf index at r==0.
    fn listindices0(&self, r: u32, t: u32, indices: &mut [u32]) {
        if r == 0 {
            indices[0] = t;
            return;
        }
        let r = r - 1;
        let src_idx = (r / 2) as usize;
        let bucketid = tree_bucketid(t) as usize;
        let s0 = tree_slotid0(t) as usize;
        let s1 = tree_slotid1(t) as usize;
        let size = 1usize << r;

        let attr0 = self.trees1[src_idx].slots[bucketid][s0].attr;
        let attr1 = self.trees1[src_idx].slots[bucketid][s1].attr;

        let (left, right) = indices.split_at_mut(size);
        self.listindices1(r, attr0, left);
        self.listindices1(r, attr1, right);
        order_indices(indices, size);
    }

    /// `listindices1`: unfold from a `trees0`-sourced tree attr.
    fn listindices1(&self, r: u32, t: u32, indices: &mut [u32]) {
        let r = r - 1;
        let src_idx = (r / 2) as usize;
        let bucketid = tree_bucketid(t) as usize;
        let s0 = tree_slotid0(t) as usize;
        let s1 = tree_slotid1(t) as usize;
        let size = 1usize << r;

        let attr0 = self.trees0[src_idx].slots[bucketid][s0].attr;
        let attr1 = self.trees0[src_idx].slots[bucketid][s1].attr;

        let (left, right) = indices.split_at_mut(size);
        self.listindices0(r, attr0, left);
        self.listindices0(r, attr1, right);
        order_indices(indices, size);
    }
}

/// `orderindices`: swap the two halves if the left half's first element
/// is greater than the right half's first element.
fn order_indices(indices: &mut [u32], size: usize) {
    if indices[0] > indices[size] {
        for i in 0..size {
            indices.swap(i, size + i);
        }
    }
}

/// Builds the base BLAKE2b state exactly as
/// `equihash-0.3.0/src/verify.rs::initialise_state` /
/// `src/tromp.rs::solve_200_9_uncompressed` do: personalization
/// `"ZcashPoW" || WN.to_le_bytes() || WK.to_le_bytes()`, digest length
/// `HASHOUT`, with `input` then `nonce` absorbed before cloning per
/// round.
fn base_state(input: &[u8], nonce: &[u8]) -> Blake2bState {
    let mut personalization = Vec::from(*b"ZcashPoW");
    personalization.extend_from_slice(&WN.to_le_bytes());
    personalization.extend_from_slice(&WK.to_le_bytes());

    let mut state = Blake2bParams::new()
        .hash_length(HASHOUT)
        .personal(&personalization)
        .to_state();
    state.update(input);
    state.update(nonce);
    state
}

/// Runs a single Equihash solve at `(WN=144, WK=5, RESTBITS=4)` for the
/// given `input` and `nonce`, returning the raw index-set solutions
/// found (each of length `PROOFSIZE=32`), in the order the underlying
/// algorithm discovers them -- matching
/// `equihash-0.3.0/src/tromp.rs::worker`'s hardcoded single-core,
/// `id=0` driver sequence: `equi_digit0` -> `equi_digitodd`/
/// `equi_digiteven` for `r=1..WK` -> `equi_digitK`.
///
/// No I/O, no CLI -- callers pass `input`/`nonce` bytes directly.
pub fn solve_144_4(input: &[u8], nonce: &[u8]) -> Vec<Vec<u32>> {
    let mut eq = Equi::new(base_state(input, nonce));

    eq.digit0();
    for r in 1..WK {
        if r % 2 == 1 {
            eq.digitodd(r);
        } else {
            eq.digiteven(r);
        }
    }
    eq.digit_k();

    eq.sols.iter().map(|s| s.to_vec()).collect()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn constants_match_derivation() {
        assert_eq!(NDIGITS, 6);
        assert_eq!(DIGITBITS, 24);
        assert_eq!(PROOFSIZE, 32);
        assert_eq!(HASHOUT, 54);
        assert_eq!(BUCKBITS, 20);
        assert_eq!(NBUCKETS, 1_048_576);
        assert_eq!(NSLOTS, 64);
        assert_eq!(NBLOCKS, 11_184_811);
    }

    #[test]
    fn runs_without_panicking() {
        let input = hex_input();
        let nonce = [0u8; 28];
        // Should not panic; solution count is validated against the C
        // cross-check binary in tests/cross_check.rs.
        let _ = solve_144_4(&input, &nonce);
    }

    fn hex_input() -> Vec<u8> {
        let hex = "00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff";
        (0..hex.len())
            .step_by(2)
            .map(|i| u8::from_str_radix(&hex[i..i + 2], 16).unwrap())
            .collect()
    }
}
