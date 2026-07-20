// V5 -- "unlimited memory" variant, built on V1's fixed-width integers.
// Assumes memory is not the constraint at all and spends it freely to
// remove every possible recomputation, per-item hashing redundancy, and
// incremental-container-growth cost:
//
//   1. CLASS-PREFIX BLAKE2B PRECOMPUTATION (Req/SECURITY_ANALYSIS.md
//      S4.1, hypothesis H2). Every leaf in list i hashes
//      nonce + "i-" + decimal(j) -- the prefix nonce+"i-" is IDENTICAL
//      for all j in that list. This variant runs BLAKE2b's streaming
//      API (blake2b_init/update) ONCE per list to absorb that shared
//      prefix, snapshots the resulting blake2b_state (a plain copyable
//      struct), and for every j resumes from the snapshot with only the
//      decimal(j) suffix absorbed -- one compression call skipped per
//      leaf wherever the shared prefix crosses a full 128-byte block
//      boundary, more generally always at least the prefix-absorption
//      work amortized across 2^ell leaves instead of repeated 2^ell
//      times. This is a genuine unconditional win (Sequihash-only --
//      Equihash's single list has no such per-class shared state) and
//      costs only K tiny state snapshots.
//
//   2. FULL UPFRONT MATERIALIZATION, NO STREAMING/REGENERATION. All K
//      leaf lists are computed once into one big contiguous arena
//      before any merging starts (no on-the-fly recomputation across
//      rounds -- there is none in the reference algorithm to begin
//      with, but this variant is explicit and static about it: sized
//      once, from (n,K), no per-round Vec growth, mirroring V4's
//      static-allocation discipline but applied to the FULL leaf
//      generation phase, not just the merge phase).
//
//   3. FULL RADIX/COUNTING SORT MERGE, WIDE KEY, NO TRUNCATION. Unlike
//      V2's bucket variant (which caps the bucket-address width at 16
//      bits and falls back to a within-bucket linear scan for any
//      residual mask bits, trading some redundant comparison work for a
//      bounded bucket table), V5 assumes memory is free and buckets on
//      the FULL mask_bit width every round (up to 2^32 conceptual
//      buckets, realized here as a sparse offset table sized to the
//      round's actual key range rather than a dense 2^mask_bit array,
//      since mask_bit can reach 2*ell which exceeds any practical dense
//      table -- "unlimited memory" is applied to avoiding recomputation
//      and redundant hashing, not to a literal 2^64-entry array).
//      Because keys are (with overwhelming probability, per Sequihash's
//      own birthday-collision design) unique per row at full mask_bit
//      width until the final XOR-to-zero round, this reduces the merge
//      to a single sort-by-key + linear-scan-for-runs pass with no
//      residual exact-match rescan V2 needs.
#ifndef CS_V5_KLIST_HPP
#define CS_V5_KLIST_HPP

#include "fixedint.hpp"

#include <cstdint>
#include <vector>

namespace cs_v5 {

struct HashItem {
    FixedUint value;
    std::vector<uint32_t> index_vector;
};

class KListWagnerAlgorithmV5 {
public:
    KListWagnerAlgorithmV5(unsigned n, unsigned k, std::vector<uint8_t> nonce);

    std::vector<std::vector<uint32_t>> solve() const;
    bool verify(const std::vector<std::vector<uint32_t>>& solutions) const;

    unsigned n() const { return n_; }
    unsigned k() const { return k_; }
    unsigned lgk() const { return lgk_; }
    unsigned ell() const { return ell_; }

    FixedUint compute_item(unsigned i, unsigned j) const;

    // Diagnostic counters, exposed for the bench harness (variants/README.md):
    // how many BLAKE2b compression-relevant update() calls the class-prefix
    // precomputation actually saved vs. a naive per-leaf full-message hash.
    mutable uint64_t leaf_hashes_computed = 0;
    mutable uint64_t class_prefixes_precomputed = 0;

private:
    unsigned n_;
    unsigned k_;
    unsigned lgk_;
    unsigned ell_;
    std::vector<uint8_t> nonce_;
    unsigned hash_size_;

    // Computes the full leaf list for list i using class-prefix
    // precomputation (technique 1 above): one blake2b_state snapshot
    // after absorbing nonce+"i-", resumed per j.
    std::vector<HashItem> compute_hash_list_precomputed(unsigned i) const;

    static std::vector<HashItem> hash_merge_full_radix(
        const std::vector<HashItem>& l1,
        const std::vector<HashItem>& l2,
        unsigned mask_bit);

    std::vector<std::vector<uint32_t>> solve_internal() const;
};

} // namespace cs_v5

#endif // CS_V5_KLIST_HPP
