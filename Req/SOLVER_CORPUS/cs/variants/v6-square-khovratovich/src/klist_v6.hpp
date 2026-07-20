// V6 -- "square" Khovratovich-style variant, built on V1's fixed-width
// integers. A structural mirror of Dmitry Khovratovich's original
// CC0 Equihash reference (~/Work/ZK/ZKs/equihash-khovratovich/Source/C++11/
// pow.h/pow.cc -- already ported once, faithfully, as ../../rk/), adapted
// from single-list Equihash to Sequihash's k-list merge-tree structure.
// This is a structural/idiom exercise, not primarily a performance one:
// "what does Sequihash look like written in the shape of the earliest,
// most literal reference implementation" -- useful as a second
// differential oracle written in a completely different style than
// cs/'s STL-hash-map-heavy one, and as a second-opinion comparison for
// bugs (a bug that survives two independently-shaped ports is much more
// likely a spec ambiguity than an implementation slip).
//
// What is preserved from Khovratovich's shape, exactly:
//   - The five-method public surface: InitializeMemory, FillMemory,
//     ResolveCollisions, FindProof, ResolveTree/ResolveTreeByLevel.
//   - Tuple/Fork as named small structs (his `Tuple{blocks, reference}`,
//     `Fork{ref1, ref2}`), not anonymous pairs or lambdas.
//   - A pre-sized 2D bucket table (tupleList[bucket][slot]) with a
//     parallel occupancy-counter array (filledList), filled by bucketing
//     each item on its leading collision digit -- fixed capacity per
//     bucket, no std::vector growth during the fill.
//   - ResolveCollisions doing double duty for every round via a `store`
//     flag (root round emits solutions; every other round re-buckets).
//   - Fork/ResolveTreeByLevel's recursive per-level reconstruction,
//     round history threaded through a forks[level] list.
//
// What must differ, structurally, because Sequihash is a genuinely
// different algorithm shape (k independent list-classes merged by a
// binary stack tree, not one list refined by k uniform rounds):
//   - InitializeMemory/FillMemory run ONCE PER LIST CLASS (0..K-1), not
//     once globally -- Khovratovich's FillMemory fills one table from
//     one seed; here there are K tables, one per compute_item(i, .).
//   - ResolveCollisions operates on ONE STACK-MERGE STEP (two rounds'
//     worth of tables merging into one), mirroring hash_merge's role in
//     cs/klist.cpp, not "the next uniform round over the single table."
//     The k-list stack/carry structure (see solve_internal below) is
//     unavoidable -- it's the algorithm, not a style choice -- so it is
//     kept identical to every other variant in this family; only the
//     ROUND-LOCAL representation is Khovratovich-shaped.
//   - Deliberate deviation from Khovratovich's own LOSSY design: his
//     LIST_LENGTH truncates each bucket to a fixed small slot count
//     (correct for HIS use case -- probabilistic solving across many
//     nonce retries where dropping some collisions is an acceptable
//     tradeoff) but Sequihash's own reference is EXHAUSTIVE (finds
//     every solution for one fixed nonce, matching the committed
//     vectors exactly) -- so this port sizes each bucket's slot capacity
//     generously (see kSlotSlack in klist_v6.cpp) rather than hard-
//     coding Khovratovich's LIST_LENGTH=5, and documents this explicitly
//     as the one place fidelity to the original was deliberately broken,
//     not silently diverged from.
#ifndef CS_V6_KLIST_HPP
#define CS_V6_KLIST_HPP

#include "fixedint.hpp"

#include <cstdint>
#include <vector>

namespace cs_v6 {

// Matches Khovratovich's Tuple: the remaining collision-digit blocks
// (here just the FixedUint's remaining bits after this round's shift,
// which is enough for one running value, not a per-block array --
// Sequihash's merge produces one shifted XOR value per row like
// klist.cpp's HashItem, not Khovratovich's multi-block per-row layout)
// plus a `reference` back into the previous round's storage.
struct Tuple {
    FixedUint value;
    uint32_t reference; // index into this round's own index-history table
};

// Matches Khovratovich's Fork: the pair of prior-round references that
// produced one collision.
struct ForkPair {
    uint32_t ref1;
    uint32_t ref2;
};

class KListWagnerAlgorithmV6 {
public:
    KListWagnerAlgorithmV6(unsigned n, unsigned k, std::vector<uint8_t> nonce);

    std::vector<std::vector<uint32_t>> solve() const;
    bool verify(const std::vector<std::vector<uint32_t>>& solutions) const;

    unsigned n() const { return n_; }
    unsigned k() const { return k_; }
    unsigned lgk() const { return lgk_; }
    unsigned ell() const { return ell_; }

    FixedUint compute_item(unsigned i, unsigned j) const;

private:
    unsigned n_;
    unsigned k_;
    unsigned lgk_;
    unsigned ell_;
    std::vector<uint8_t> nonce_;
    unsigned hash_size_;

    std::vector<std::vector<uint32_t>> solve_internal() const;
};

} // namespace cs_v6

#endif // CS_V6_KLIST_HPP
