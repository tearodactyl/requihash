#include "klist_v6.hpp"
#include "blake2.h"

#include <cassert>
#include <functional>
#include <memory>
#include <stdexcept>
#include <tuple>
#include <unordered_map>
#include <utility>

namespace cs_v6 {

namespace {

// Deliberate deviation from Khovratovich's LIST_LENGTH=5 (see header
// note): sized as a multiplier over the expected average bucket
// occupancy (nrows / nbuckets), generous enough that none of this
// port's vectored KAT points (up to (160,512)) truncate a real
// collision -- verified by the differential test failing loudly
// (a dropped Tuple would silently lose a solution) rather than by
// static proof, matching this whole corpus's "measurement over
// modeling" discipline (Req/SIZING.md S3's own stated preference).
constexpr unsigned kSlotSlack = 64;

// One round's Khovratovich-shaped storage: tupleList[bucket][slot],
// bucketed on the low bucket_bits bits of the row's remaining value
// (the "leading collision digit" in Khovratovich's own framing, here
// taken from the low bits since FixedUint::low_bits_key is the
// operation this port's shared representation exposes -- functionally
// identical bucketing role, bit-position choice is representation
// detail, not an algorithmic difference).
struct RoundTable {
    unsigned bucket_bits;
    std::vector<std::vector<Tuple>> tupleList; // [bucket][slot]
    std::vector<unsigned> filledList;          // occupancy per bucket

    void init(unsigned bucket_bits_in, size_t nbuckets, unsigned slot_capacity) {
        bucket_bits = bucket_bits_in;
        tupleList.assign(nbuckets, std::vector<Tuple>(slot_capacity));
        filledList.assign(nbuckets, 0);
    }

    size_t bucket_of(const FixedUint& v) const {
        return v.low_bits_key(bucket_bits);
    }

    // Matches Khovratovich's FillMemory's per-item insertion: bucket by
    // leading digit, append if the bucket has spare capacity (silently
    // drops on overflow in HIS reference -- here that would fail the
    // differential test, per kSlotSlack's sizing note).
    bool insert(const FixedUint& value, uint32_t reference) {
        size_t b = bucket_of(value);
        unsigned& count = filledList[b];
        if (count >= tupleList[b].size()) return false; // overflow (should not happen, see kSlotSlack)
        tupleList[b][count] = Tuple{value, reference};
        ++count;
        return true;
    }
};

} // namespace

KListWagnerAlgorithmV6::KListWagnerAlgorithmV6(unsigned n, unsigned k, std::vector<uint8_t> nonce)
    : n_(n), k_(k), nonce_(std::move(nonce)) {
    if (n_ % 8 != 0) throw std::invalid_argument("n should be a multiple of 8");
    if (n_ > kMaxBits) throw std::invalid_argument("n exceeds this variant's 256-bit limb width");
    if (nonce_.size() != 16) throw std::invalid_argument("Nonce should be 16 bytes");

    lgk_ = 0;
    unsigned kk = k_;
    while (kk > 1) { kk >>= 1; ++lgk_; }
    if ((1u << lgk_) != k_) throw std::invalid_argument("k should be a power of 2");
    if (n_ % (lgk_ + 1) != 0) throw std::invalid_argument("n should be divisible by lg(k) + 1");

    ell_ = n_ / (lgk_ + 1);
    hash_size_ = n_ / 8;
    if (2 * ell_ > 64) throw std::invalid_argument("2*ell exceeds this variant's 64-bit bucket-key width");
}

FixedUint KListWagnerAlgorithmV6::compute_item(unsigned i, unsigned j) const {
    std::string suffix = std::to_string(i) + "-" + std::to_string(j);
    std::vector<uint8_t> message(nonce_);
    message.insert(message.end(), suffix.begin(), suffix.end());

    std::vector<uint8_t> digest(hash_size_);
    int rc = blake2b(digest.data(), hash_size_, message.data(), message.size(), nullptr, 0);
    if (rc != 0) throw std::runtime_error("blake2b failed");
    return FixedUint::from_be_bytes(digest.data(), hash_size_);
}

std::vector<std::vector<uint32_t>> KListWagnerAlgorithmV6::solve_internal() const {
    // Per-round history: forks[level] and, for leaf rounds, which list
    // index a reference resolves to -- matches Khovratovich's
    // forks/ResolveTreeByLevel history-threading exactly, generalized
    // from "level = round number" (uniform in his single-list scheme)
    // to "one history entry per STACK-MERGE STEP" (the k-list tree is
    // not uniform-depth per list the way his k uniform rounds are).
    struct MergeHistory {
        std::vector<ForkPair> forks;
        const MergeHistory* parent_left = nullptr;
        const MergeHistory* parent_right = nullptr;
        int leaf_list_index = -1; // >=0 if this history entry is a LEAF round (no forks, references are literal j)
    };
    // Owns every MergeHistory node for the whole solve (parent_left/
    // parent_right point into this, stable addresses via deque-like
    // stable storage -- std::vector<unique_ptr> for that guarantee).
    std::vector<std::unique_ptr<MergeHistory>> history_pool;

    auto bucket_bits_for = [&](size_t nrows) -> unsigned {
        // Choose bucket_bits so nbuckets ~= nrows / kSlotSlack average
        // occupancy per bucket stays well within kSlotSlack's cap; bounded
        // to the low_bits_key width (<=64, and further capped small
        // since 2^bucket_bits buckets must be materialized).
        unsigned bits = 1;
        while (bits < 24 && (size_t(1) << bits) < (nrows / 4 + 1)) ++bits;
        return bits;
    };

    // Leaf round: a Khovratovich-shaped RoundTable filled from
    // compute_item(list_index, j) for j in [0, 2^ell), plus a
    // MergeHistory marking it as a leaf list.
    auto make_leaf_round = [&](unsigned list_index) -> std::pair<RoundTable, MergeHistory*> {
        uint64_t count = uint64_t(1) << ell_;
        unsigned bbits = bucket_bits_for(count);
        RoundTable table;
        table.init(bbits, size_t(1) << bbits, kSlotSlack);
        for (uint64_t j = 0; j < count; ++j) {
            FixedUint v = compute_item(list_index, (unsigned)j);
            bool ok = table.insert(v, (uint32_t)j);
            if (!ok) throw std::runtime_error("leaf bucket overflow -- kSlotSlack too small for this (n,K)");
        }
        history_pool.push_back(std::make_unique<MergeHistory>());
        MergeHistory* h = history_pool.back().get();
        h->leaf_list_index = (int)list_index;
        return {std::move(table), h};
    };

    // ResolveCollisions, Khovratovich-shaped: walk each bucket's slots
    // pairwise is WRONG for a two-table merge (his version merges ONE
    // table against itself within each bucket, since single-list
    // Equihash's every round operates on one shared table); the k-list
    // algorithm merges TWO distinct round tables (r1, r2), so this is
    // ResolveCollisions adapted to a two-table merge -- same
    // bucket-then-pairwise-XOR shape, same store-flag dual-purpose
    // (root round emits solutions, every other round re-buckets into a
    // fresh RoundTable), same Fork-recording discipline.
    auto resolve_collisions = [&](RoundTable& r1, const MergeHistory* h1,
                                   RoundTable& r2, const MergeHistory* h2,
                                   unsigned mask_bit, bool is_root)
        -> std::tuple<RoundTable, MergeHistory*, std::vector<std::vector<uint32_t>>> {
        size_t nrows_est = 0;
        for (unsigned c : r1.filledList) nrows_est += c;
        for (unsigned c : r2.filledList) nrows_est += c;
        unsigned out_bbits = bucket_bits_for(nrows_est);

        RoundTable out;
        std::vector<ForkPair> forks;
        std::vector<std::vector<uint32_t>> solutions;

        if (!is_root) out.init(out_bbits, size_t(1) << out_bbits, kSlotSlack);

        // Join index: r1's rows keyed by the TRUE join predicate
        // (mask_bit low-bits-key), not by RoundTable's own bucket_bits
        // (a locality aid for the leaf-fill phase, sized independently
        // per round -- r1/r2 can have different bucket_bits, so it is
        // not usable as the join key directly). This mirrors
        // Khovratovich's own ResolveCollisions, which only ever compares
        // items already co-located in the SAME bucket by construction
        // (his single shared table's bucketing IS the join key, since he
        // has one table, not two) -- the two-table generalization here
        // needs its own explicit index for the same "only visit
        // matching-key pairs" property, or it degrades to an
        // O(nbuckets^2) cross-product, which does not scale.
        std::unordered_map<uint64_t, std::vector<std::pair<size_t, unsigned>>> index1;
        index1.reserve(nrows_est);
        for (size_t b1 = 0; b1 < r1.tupleList.size(); ++b1) {
            for (unsigned s1 = 0; s1 < r1.filledList[b1]; ++s1) {
                uint64_t key1 = r1.tupleList[b1][s1].value.low_bits_key(mask_bit);
                index1[key1].emplace_back(b1, s1);
            }
        }

        std::function<void(const MergeHistory*, uint32_t, std::vector<uint32_t>&)> reconstruct =
            [&](const MergeHistory* h, uint32_t ref, std::vector<uint32_t>& out_idx) {
                if (h->leaf_list_index >= 0) { out_idx.push_back(ref); return; }
                const ForkPair& f = h->forks[ref];
                reconstruct(h->parent_left, f.ref1, out_idx);
                reconstruct(h->parent_right, f.ref2, out_idx);
            };

        for (size_t b2 = 0; b2 < r2.tupleList.size(); ++b2) {
            for (unsigned s2 = 0; s2 < r2.filledList[b2]; ++s2) {
                const Tuple& t2 = r2.tupleList[b2][s2];
                auto it = index1.find(t2.value.low_bits_key(mask_bit));
                if (it == index1.end()) continue;
                for (const auto& [b1, s1] : it->second) {
                    const Tuple& t1 = r1.tupleList[b1][s1];
                    FixedUint merged_value = (t1.value ^ t2.value).shr(mask_bit);
                    ForkPair fork{t1.reference, t2.reference};
                    if (is_root) {
                        if (merged_value.is_zero()) {
                            // ResolveTree-equivalent, done inline since the
                            // root has no further round to store a fork into.
                            std::vector<uint32_t> indices;
                            reconstruct(h1, fork.ref1, indices);
                            reconstruct(h2, fork.ref2, indices);
                            solutions.push_back(std::move(indices));
                        }
                    } else {
                        uint32_t new_ref = (uint32_t)forks.size();
                        forks.push_back(fork);
                        bool ok = out.insert(merged_value, new_ref);
                        if (!ok) throw std::runtime_error("merge bucket overflow -- kSlotSlack too small for this (n,K)");
                    }
                }
            }
        }

        MergeHistory* h_out = nullptr;
        if (!is_root) {
            history_pool.push_back(std::make_unique<MergeHistory>());
            h_out = history_pool.back().get();
            h_out->forks = std::move(forks);
            h_out->parent_left = h1;
            h_out->parent_right = h2;
        }
        return {std::move(out), h_out, std::move(solutions)};
    };

    struct StackEntry {
        RoundTable table;
        const MergeHistory* history;
        unsigned depth;
    };
    std::vector<StackEntry> stack;
    {
        auto [t0, h0] = make_leaf_round(0);
        stack.push_back({std::move(t0), h0, 0});
    }

    std::vector<std::vector<uint32_t>> all_solutions;

    for (unsigned i = 1; i < k_; ++i) {
        unsigned current_depth = 0;
        auto [merged_table, merged_history] = make_leaf_round(i);
        while (!stack.empty() && stack.back().depth == current_depth) {
            StackEntry top = std::move(stack.back());
            stack.pop_back();
            unsigned mask_bit = (current_depth == lgk_ - 1) ? ell_ * 2 : ell_;
            bool is_root = (i == k_ - 1) && (current_depth == lgk_ - 1);
            auto [out_table, out_history, sols] =
                resolve_collisions(top.table, top.history, merged_table, merged_history, mask_bit, is_root);
            if (is_root) {
                all_solutions.insert(all_solutions.end(), sols.begin(), sols.end());
            }
            merged_table = std::move(out_table);
            merged_history = out_history;
            current_depth += 1;
        }
        stack.push_back({std::move(merged_table), merged_history, current_depth});
    }

    if (stack.size() != 1 || stack.back().depth != lgk_) {
        throw std::runtime_error("merge tree did not reduce to a single root at depth lgk");
    }

    // k=1 (lgk_=0) NOTE, corrected from an earlier wrong comment here:
    // this guard does NOT throw at k=1. With k_==1 the `for i=1;i<k_`
    // loop body never runs (k_==1 means the loop doesn't execute), so
    // `stack` holds exactly the one leaf round pushed before the loop,
    // at depth 0 == lgk_ (0) -- the guard above is satisfied trivially,
    // and `all_solutions` (only ever appended to inside the loop body,
    // which never ran) stays empty. Net effect: V6 returns ZERO
    // solutions at k=1.
    //
    // This is a DELIBERATE, DOCUMENTED DIVERGENCE from the Python
    // reference and every other variant in this family (V1-V5, all of
    // which faithfully port the reference's own loop structure): the
    // reference's `_solve` has the identical `for i in range(1, self.k)`
    // shape, so at k=1 it ALSO never runs its merge/collision-check
    // loop, and returns all 2^ell leaves as "solutions" -- each one
    // completely unverified (no XOR-to-zero check ever happens, since
    // that check only occurs inside the loop body's collision-detection
    // path). Confirmed by running the actual Python reference at
    // (n=8,k=1): 256 "solutions" returned, all of which fail
    // verify_results's own zero-XOR check. This is arguably a bug in the
    // paper's own reference (an unreachable k=1 point that was never
    // meant to be exercised -- Sequihash's own scheme requires k a power
    // of 2 with meaningful collision structure, and k=1 is a degenerate
    // "1-list problem" with no birthday search at all), not a principled
    // design choice.
    //
    // V6 was chosen to NOT reproduce this quirk -- returning zero
    // solutions (the arguably-correct behavior: no XOR-to-zero check
    // ever ran, so nothing should be reported as verified) rather than
    // byte-exact fidelity to the reference's own inherited bug at this
    // one degenerate point. This means V6 is NOT differentially
    // comparable to the Python reference (or to V1-V5) at k=1
    // specifically -- every other parameter point remains byte-exact
    // comparable across all variants, per this corpus's normal
    // discipline; k=1 is the one documented exception.

    return all_solutions;
}

std::vector<std::vector<uint32_t>> KListWagnerAlgorithmV6::solve() const {
    return solve_internal();
}

bool KListWagnerAlgorithmV6::verify(const std::vector<std::vector<uint32_t>>& solutions) const {
    if (solutions.empty()) return true;
    for (const auto& indices : solutions) {
        if (indices.size() != k_) return false;
    }
    for (const auto& indices : solutions) {
        FixedUint acc = FixedUint::zero(n_);
        for (unsigned i = 0; i < indices.size(); ++i) {
            acc = acc ^ compute_item(i, indices[i]);
        }
        if (!acc.is_zero()) return false;
    }
    return true;
}

} // namespace cs_v6
