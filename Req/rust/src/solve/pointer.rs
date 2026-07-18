//! DESIGN + PROTOTYPE for `Req/PLAN.md` A6 — compact index-pointer storage,
//! the one canonical 2016-17 technique (tromp/xenoncat) not yet applied here.
//! Status: prototype, not wired into `all_solvers()` or gated by
//! `all_solvers_agree` yet — see "What this prototype does not do" below.
//!
//! ## The idea, ported from tromp's `equi_miner.c`
//!
//! Every other solver in this crate (`reference`, `arena`, `bucket`) stores
//! `Vec<EhIndex>` per row — a *growing* index list, `2^round` entries wide by
//! the final round. That's the naive representation Req's own profiling
//! confirmed dominates allocation cost (`../../SECURITY_ANALYSIS.md` §2,
//! `../../SOLVERS.md` §0). tromp's `tree` struct (`equi_miner.c` lines 84-131)
//! replaces it with a **packed pointer**: each surviving row after round `r`
//! stores only two references to rows in round `r-1`'s surviving-row array —
//! not the `2^r` raw leaf indices those two rows jointly represent. Full
//! indices are reconstructed only once, at solution time, by walking the
//! pointer tree recursively (tromp's `listindices0`/`listindices1`, lines
//! 332-352) back down to round 0, where a pointer *is* a raw leaf index
//! (`tree_from_idx`, line 89).
//!
//! Tromp's own pointer packs `(bucket_id, slot0, slot1)` into 32 bits because
//! his rows live in fixed-size per-bucket arrays addressed by
//! `(bucket, slot)`. This crate's `bucket` solver doesn't use that fixed
//! addressing — each round's survivors are a plain `Vec`, addressed by plain
//! row index — so the direct port is simpler: a pointer is two `u32`
//! indices into the *previous round's* survivor vector, and **every round's
//! vector is kept resident** (mirroring tromp's `htalloc.trees0`/`trees1`,
//! which keep every round's tree array alive until reconstruction) so a
//! pointer can always be dereferenced. Same idea (parent references, not
//! accumulated leaves), simpler encoding for this codebase's row layout.
//!
//! ## Memory shape
//!
//! Per surviving row: `Row { hash: Vec<u8>, ptr: Ptr }` where `Ptr` is either
//! `Leaf(EhIndex)` (round 0) or `Pair(u32, u32)` (later rounds) — 8 bytes
//! fixed, vs. the `Vec<EhIndex>` (24-byte `Vec` header alone, before the
//! `2^round * 4` bytes of contents and a heap allocation per row) the other
//! solvers carry. This is the `(2^k)/k` space win `ARCHITECTURE.md` §7 and
//! `SECURITY_ANALYSIS.md` §2 describe structurally; this prototype is the
//! first executable evidence of it in this crate specifically, and its own
//! tests prove the pointer tree reconstructs correctly across all rounds,
//! not just locally within one round.
//!
//! ## What this prototype does and does not do
//!
//! Does: generate leaves, run the k merge rounds storing `Ptr` instead of
//! growing index vectors, keep every round's row array resident (the real
//! memory cost this design pays — see "Honest accounting" below), detect
//! the zero-XOR root, and reconstruct full index vectors for any solutions
//! found by walking the pointer tree back through every round to round-0
//! leaves — proving the representation is round-trip correct end to end,
//! not just within one round.
//!
//! Does NOT (deliberately, out of scope for a design+prototype pass):
//! bucket/counting-sort the merge (uses a plain sort to isolate the
//! pointer-storage change from the sort-algorithm change `bucket.rs`
//! already owns, keeping this file reviewable on its own); register in
//! `all_solvers()` or pass `all_solvers_agree` (not validated against the
//! KAT vectors from A14 yet); use tromp's O(1)-per-row duplicate-index
//! filter (`collisiondata`'s xhash early-reject) — this prototype checks
//! duplicates only on the final candidate, same as `bucket.rs`; get
//! measured against the naive-formula/counting-allocator harness
//! (`SIZING.md` §2a). Turning this into the real `solve::pointer` backend
//! PLAN.md A6 wants is follow-up work, staged as three approaches, in order
//! of how much of the existing repo they reuse:
//!
//! 1. **Graft `bucket.rs`'s counting-sort merge onto this file's row/history
//!    structure** (lowest risk, most reuse). Replace the plain `sort_by`
//!    grouping step below with `bucket.rs`'s counting-sort-over-collision-
//!    bytes logic, keeping the `Ptr`/`history` design, `reconstruct`, and
//!    canonical ordering unchanged. Recommended starting point: it isolates
//!    exactly one remaining risk (does the counting-sort bucket boundary
//!    logic still let a pointer address a *specific* prior-round row, not
//!    just "some row with this key") instead of changing multiple things
//!    at once.
//! 2. **Port tromp's `collisiondata` xhash early-reject** (medium effort,
//!    meaningful for A5's steepness numbers later). `leaves_share` below is
//!    O(2^round) per candidate pair (a full tree walk); tromp's design
//!    avoids this with a bounded per-bucket hash table
//!    (`xhashslots`, `XFULL = 16`, `equi_miner.c` lines 460-521). Do this
//!    once (1) lands, since it's the second half of tromp's real
//!    performance profile, not just a correctness nicety.
//! 3. **A bucket-addressed variant matching tromp's `(bucket, slot)` scheme
//!    exactly** (highest effort) — only worth it if profiling after (1) and
//!    (2) shows plain-`Vec`-row-indexing overhead is a real bottleneck;
//!    speculative until measured, not a default next step. The gate is
//!    quantified in `ARCHITECTURE.md` §7a.4: node-array share of peak
//!    T >= 40% (req_memcheck tagged breakdown) OR merge-sample attribution
//!    M >= 20% (sampling profile), at production-scale k.
//!
//! A natural stage 4 exists beyond these three — post-retrieval (don't
//! store full provenance; re-derive indices after root detection),
//! resolving provenance to the same lossy-but-recoverable standard the
//! collision search already uses. Rationale and gating:
//! `ARCHITECTURE.md` §7a.8.
//!
//! **Language/style note, stated once here rather than re-derived later:**
//! stay in Rust, extending this file — do not port tromp's C bit-packing
//! (`tree.bid_s0_s1`, a single `u32` encoding `(bucket, slot0, slot1)`,
//! `equi_miner.c` lines 84-131). That packing is a C-idiomatic optimization
//! specific to his fixed-size-array `(bucket, slot)` addressing, not a
//! portable design requirement — this crate's plain `enum Ptr { Leaf(u32),
//! Pair(u32, u32) }` already gets the same 8-byte-per-row win without
//! `unsafe` or manual bitfields, and matches the safe-Rust, `Vec`-based
//! house style `reference.rs`/`arena.rs`/`bucket.rs` all use.
//!
//! Wiring: fold in whichever merge results from stage 1, register as
//! `solve::pointer` in `all_solvers()`, gate on `all_solvers_agree` plus the
//! A14 KATs (once single-list keying exists, `SPEC.md` §1), and measure
//! real peak memory with `req_memcheck`.
//!
//! ## Honest accounting: what this design actually saves
//!
//! Keeping every round's `Vec<Row>` resident (required so `Ptr::Pair`
//! indices stay valid until reconstruction) means this prototype does NOT
//! save memory round-over-round the way a true streaming/discard design
//! would — it saves memory *per row* (8 bytes vs. an index `Vec`'s
//! ever-growing contents), not by discarding early rounds. This matches
//! tromp's own design (`htalloc` keeps `trees0`/`trees1` for every round,
//! `equi_miner.c` lines 173-232) — the `(2^k)/k` win is specifically about
//! not storing full accumulated index tuples per row, not about discarding
//! history. Don't conflate this with the separate, unbuilt memory-capped
//! (Bernstein truncation) solver `SECURITY_ANALYSIS.md` §8 item 2 describes
//! — that's a different, complementary technique (drop rows, recompute),
//! not what index-pointer storage does.

use crate::{EhIndex, Requihash};

/// A row's provenance: either a raw leaf index (round 0) or a pair of row
/// indices into the previous round's row array (later rounds). 8 bytes,
/// fixed size, regardless of round — the point of the whole exercise.
#[derive(Clone, Copy, Debug)]
enum Ptr {
    Leaf(EhIndex),
    Pair(u32, u32),
}

struct Row {
    hash: Vec<u8>,
    ptr: Ptr,
}

/// Prototype solver. Not part of `all_solvers()` — see module docs.
pub struct PointerSolverPrototype;

impl PointerSolverPrototype {
    pub fn solve(&self, engine: &Requihash) -> Vec<Vec<EhIndex>> {
        let p = engine.params();
        let cbyte = p.collision_byte_length();
        let init = 1usize << (p.collision_bit_length() + 1);

        // Every round's row array is kept resident (see module docs,
        // "Honest accounting") so Ptr::Pair references stay valid for
        // reconstruction after the loop ends.
        let mut history: Vec<Vec<Row>> = Vec::with_capacity(p.k as usize + 1);

        // Round 0: one row per leaf, pointer is the raw leaf index.
        history.push(
            (0..init as u32)
                .map(|leaf| Row {
                    hash: engine.leaf_row(leaf),
                    ptr: Ptr::Leaf(leaf),
                })
                .collect(),
        );

        for _round in 1..=p.k {
            let prev = history.last().unwrap();
            if prev.is_empty() {
                return Vec::new();
            }

            // Group rows by their leading `cbyte` collision bytes. A plain
            // sort-by-key here, not the bucket solver's counting sort — see
            // module docs on why that's deliberately out of scope.
            let mut order: Vec<u32> = (0..prev.len() as u32).collect();
            order.sort_by(|&a, &b| prev[a as usize].hash[..cbyte].cmp(&prev[b as usize].hash[..cbyte]));

            let new_len = prev[0].hash.len() - cbyte;
            let mut next: Vec<Row> = Vec::new();

            let mut i = 0;
            while i + 1 <= order.len().saturating_sub(1) {
                let ra = order[i] as usize;
                if prev[ra].hash[..cbyte] != prev[order[i + 1] as usize].hash[..cbyte] {
                    i += 1;
                    continue;
                }
                let mut j = i + 1;
                while j < order.len() && prev[order[j] as usize].hash[..cbyte] == prev[ra].hash[..cbyte] {
                    j += 1;
                }
                // All rows in [i, j) share the leading collision bytes: pair
                // every combination, exactly as the bucket solver's group
                // step does, but store a `Ptr::Pair` (row indices into
                // `prev`) instead of merging index vectors.
                for a in i..j {
                    for b in (a + 1)..j {
                        let ra = order[a] as usize;
                        let rb = order[b] as usize;
                        if leaves_share(&history, history.len() - 1, ra, rb) {
                            continue;
                        }
                        let mut h = vec![0u8; new_len];
                        for t in 0..new_len {
                            h[t] = prev[ra].hash[cbyte + t] ^ prev[rb].hash[cbyte + t];
                        }
                        next.push(Row {
                            hash: h,
                            ptr: Ptr::Pair(ra as u32, rb as u32),
                        });
                    }
                }
                i = j;
            }

            if next.is_empty() {
                return Vec::new();
            }
            history.push(next);
        }

        // Final round done: the last history entry holds candidate roots. A
        // zero hash (all collision bits consumed) means a full
        // XOR-to-zero solution.
        let last_round = history.len() - 1;
        let mut sols = Vec::new();
        for ri in 0..history[last_round].len() {
            if !history[last_round][ri].hash.iter().all(|&b| b == 0) {
                continue;
            }
            // Reconstruct: walk the pointer tree back through every round
            // to round-0 leaves — the direct port of tromp's
            // listindices0/listindices1 (equi_miner.c lines 332-352), and
            // the only place this prototype materializes a full index
            // vector, only for actual solutions.
            let idx = reconstruct(&history, last_round, ri);
            let mut u = idx.clone();
            u.sort_unstable();
            u.dedup();
            if u.len() == idx.len() {
                sols.push(idx);
            }
        }
        sols
    }
}

/// Duplicate pre-check during merge: do candidate rows `a` and `b`, both in
/// `history[round]` (the round currently being paired into the next one),
/// share any round-0 leaf. Walks the full pointer tree — O(2^round) per
/// call, same asymptotic cost tromp avoids with the xhash early-reject
/// (`collisiondata`, noted as out of scope above); acceptable for a
/// design-proving prototype, not for the production backend.
fn leaves_share(history: &[Vec<Row>], round: usize, a: usize, b: usize) -> bool {
    let la = reconstruct(history, round, a);
    let lb = reconstruct(history, round, b);
    la.iter().any(|x| lb.contains(x))
}

/// Full pointer-tree walk from `history[round][at]` back to round-0 leaves.
/// This is the real cross-round reconstruction: `Ptr::Pair(a, b)` at round
/// `r` refers to rows `a` and `b` in `history[r - 1]`, recursively, down to
/// `Ptr::Leaf` at round 0 — exactly tromp's listindices0/listindices1
/// pattern, generalized from his fixed bucket/slot addressing to this
/// crate's plain row-index addressing. Applies tromp's `orderindices`
/// (equi_miner.c lines 323-330) at every internal node: SPEC.md §7's
/// "algorithm binding" rule requires the left child's index list to be
/// lexicographically less than the right child's, so a solution's index
/// order is canonical and matches what the other solvers/verifier expect —
/// without this, the pointer tree still finds a *correct* solution (same
/// leaf set) but in the wrong order, which the wire format and
/// `all_solvers_agree`-style equality checks both reject.
fn reconstruct(history: &[Vec<Row>], round: usize, at: usize) -> Vec<EhIndex> {
    match history[round][at].ptr {
        Ptr::Leaf(l) => vec![l],
        Ptr::Pair(a, b) => {
            let mut left = reconstruct(history, round - 1, a as usize);
            let mut right = reconstruct(history, round - 1, b as usize);
            if left > right {
                std::mem::swap(&mut left, &mut right);
            }
            left.extend(right);
            left
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::Params;

    /// Proves the pointer representation is round-trip correct: every
    /// solution the prototype finds is verified against this crate's own
    /// production reference verifier, and matched against what
    /// `solve_reference` finds for the same (params, nonce) — cross-round
    /// reconstruction is exercised for real here, not just locally within
    /// one round, since (48,5)/(72,5) both need k=5 rounds of pointer
    /// chasing to reach a solution.
    #[test]
    fn pointer_prototype_matches_reference_and_verifies() {
        for &(n, k) in &[(48u32, 5u32), (72, 5)] {
            let p = Params::new(n, k).unwrap();
            let mut found_any = false;
            for ni in 0u32..30 {
                let eng = Requihash::new(p, b"pointer-prototype-check", &ni.to_le_bytes());
                let mut proto_sols = PointerSolverPrototype.solve(&eng);
                let mut ref_sols = eng.solve_reference();
                proto_sols.sort();
                ref_sols.sort();
                assert_eq!(
                    proto_sols, ref_sols,
                    "pointer prototype disagrees with solve_reference at ({n},{k}) nonce {ni}"
                );
                let verifier = crate::verify::reference::ReferenceVerifier;
                for sol in &proto_sols {
                    found_any = true;
                    assert!(
                        crate::verify::Verifier::verify(&verifier, &eng, sol).is_ok(),
                        "pointer prototype produced a solution that fails the production verifier at ({n},{k}) nonce {ni}"
                    );
                }
            }
            assert!(found_any, "no solutions found at ({n},{k}) across 30 nonces — test is not exercising cross-round reconstruction");
        }
    }
}
