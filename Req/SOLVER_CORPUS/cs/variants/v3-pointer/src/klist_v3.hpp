// V3 -- compact index-pointer storage variant, built on V1's
// fixed-width integers. Implements the single largest 2016-17 memory
// win (Req/README.md, Req/ARCHITECTURE.md S7): instead of carrying a
// growing explicit index_vector through every merge (V1/V2, and the
// canonical cs/), each surviving row stores only a PARENT-PAIR POINTER
// into the previous round's array. Full index tuples are reconstructed
// once, only for rows that survive to the root.
//
// PURPOSE OF THIS VARIANT: this technique is exactly what
// Req/SECURITY_ANALYSIS.md S4.1 and Equihash.md F-A4 argue becomes a
// LIABILITY, not a win, once the k-list regularity constraint is in
// play -- reconstructing which of the K list-classes each leaf belongs
// to needs extra bookkeeping index pointers alone don't carry cheaply
// (Proposition 3's (k^2+5k+2)/4 term). This port exists to MEASURE that
// concretely on Sequihash's own reference algorithm, not to recommend
// pointer storage for production use here. See ../../README.md for
// the measured comparison against V1/V2's explicit-index approach.
//
// Pointer encoding: a row is (FixedUint value, uint32_t left, uint32_t
// right). For a LEAF row (round 0 of a given list), left = j (the
// literal leaf index within that list) and right = LEAF_SENTINEL. For a
// MERGED row, left/right are indices into the two parent rounds' arrays
// that were merged to produce it (round tracked separately per stack
// entry, exactly mirroring compute_hash_list/hash_merge's existing
// round structure -- no change to the algorithm's control flow, only to
// what a row carries).
#ifndef CS_V3_KLIST_HPP
#define CS_V3_KLIST_HPP

#include "fixedint.hpp"

#include <cstdint>
#include <memory>
#include <vector>

namespace cs_v3 {

constexpr uint32_t kLeafSentinel = 0xFFFFFFFFu;

struct PtrRow {
    FixedUint value;
    uint32_t left;  // leaf index j, or a row index into the left parent round
    uint32_t right; // kLeafSentinel if this is a leaf row, else a row index into the right parent round
};

// One round's material: the row array plus enough provenance to
// reconstruct indices -- which original list index (0..k-1) each leaf
// row's list contributed, and (for merged rounds) references to the two
// parent rounds' own row arrays so left/right can be resolved during
// reconstruction. A round is either a LEAF round (list_index set, rows
// are all leaves of that one list) or a MERGE round (parents set).
struct Round {
    std::vector<PtrRow> rows;
    int list_index = -1;                 // >=0 for a leaf round: which original list (0..k-1)
    std::shared_ptr<Round> parent_left;   // set for a merge round
    std::shared_ptr<Round> parent_right;  // set for a merge round
};

class KListWagnerAlgorithmV3 {
public:
    KListWagnerAlgorithmV3(unsigned n, unsigned k, std::vector<uint8_t> nonce);

    std::vector<std::vector<uint32_t>> solve() const;
    bool verify(const std::vector<std::vector<uint32_t>>& solutions) const;

    unsigned n() const { return n_; }
    unsigned k() const { return k_; }
    unsigned lgk() const { return lgk_; }
    unsigned ell() const { return ell_; }

    FixedUint compute_item(unsigned i, unsigned j) const;

    // Peak resident row count observed during the most recent solve() --
    // a coarse, allocator-agnostic memory proxy: rows are the dominant
    // cost centre in both this and V1/V2 (a FixedUint + a small pointer
    // struct vs. a FixedUint + growing index_vector), so summing live
    // row counts * their respective row byte-sizes at each round
    // boundary is what ../../README.md's comparison actually reports
    // (see bench harness) -- this field just exposes the raw counts.
    mutable std::vector<size_t> round_row_counts;

private:
    unsigned n_;
    unsigned k_;
    unsigned lgk_;
    unsigned ell_;
    std::vector<uint8_t> nonce_;
    unsigned hash_size_;

    std::shared_ptr<Round> compute_leaf_round(unsigned list_index) const;
    static std::shared_ptr<Round> merge_rounds(
        std::shared_ptr<Round> r1, std::shared_ptr<Round> r2, unsigned mask_bit);
    std::vector<std::vector<uint32_t>> solve_internal() const;

    // Reconstructs the full index tuple for row `row_idx` in round `r`,
    // appending into `out` in list-index order (matching V1/V2's
    // left-then-right concatenation order).
    static void reconstruct(const Round& r, uint32_t row_idx, std::vector<uint32_t>& out);
};

} // namespace cs_v3

#endif // CS_V3_KLIST_HPP
