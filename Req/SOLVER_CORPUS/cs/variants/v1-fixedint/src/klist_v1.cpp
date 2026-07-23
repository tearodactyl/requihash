#include "klist_v1.hpp"
#include "hashmsg.hpp"
#include "blake2.h"

#include <cassert>
#include <stdexcept>
#include <unordered_map>

namespace cs_v1 {

KListWagnerAlgorithmV1::KListWagnerAlgorithmV1(unsigned n, unsigned k, std::vector<uint8_t> nonce)
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
    // mask_bit at the final merge round is 2*ell; low_bits_key only
    // supports mask_bit <= 64, same bound klist.cpp documents.
    if (2 * ell_ > 64) throw std::invalid_argument("2*ell exceeds this variant's 64-bit bucket-key width");
}

FixedUint KListWagnerAlgorithmV1::compute_item(unsigned i, unsigned j) const {
    // Allocation-free message + digest construction (../../README.md
    // "Known issues") -- same bytes, same blake2b call, no std::string/
    // std::vector heap traffic per call.
    uint8_t msgbuf[cs_common::kMaxMessageBytes];
    size_t msglen = cs_common::build_leaf_message(nonce_.data(), i, j, msgbuf);

    uint8_t digest[64]; // BLAKE2B_OUTBYTES max; hash_size_ <= 32 for n<=256
    int rc = blake2b(digest, hash_size_, msgbuf, msglen, nullptr, 0);
    if (rc != 0) throw std::runtime_error("blake2b failed");
    return FixedUint::from_be_bytes(digest, hash_size_);
}

std::vector<HashItem> KListWagnerAlgorithmV1::compute_hash_list(unsigned i) const {
    uint64_t count = uint64_t(1) << ell_;
    std::vector<HashItem> list;
    list.reserve(count);
    for (uint64_t j = 0; j < count; ++j) {
        HashItem item;
        item.value = compute_item(i, (unsigned)j);
        item.index_vector = {(uint32_t)j};
        list.push_back(std::move(item));
    }
    return list;
}

std::vector<HashItem> KListWagnerAlgorithmV1::hash_merge(
    const std::vector<HashItem>& l1,
    const std::vector<HashItem>& l2,
    unsigned mask_bit) {
    std::unordered_map<uint64_t, std::vector<const HashItem*>> table;
    table.reserve(l1.size() * 2);
    for (const auto& item : l1) {
        table[item.value.low_bits_key(mask_bit)].push_back(&item);
    }

    std::vector<HashItem> merged;
    for (const auto& item2 : l2) {
        auto it = table.find(item2.value.low_bits_key(mask_bit));
        if (it == table.end()) continue;
        for (const HashItem* item1 : it->second) {
            HashItem out;
            out.value = (item1->value ^ item2.value).shr(mask_bit);
            out.index_vector = item1->index_vector;
            out.index_vector.insert(out.index_vector.end(),
                                     item2.index_vector.begin(), item2.index_vector.end());
            merged.push_back(std::move(out));
        }
    }
    return merged;
}

std::vector<std::vector<uint32_t>> KListWagnerAlgorithmV1::solve_internal() const {
    struct StackEntry {
        std::vector<HashItem> list;
        unsigned depth;
    };
    std::vector<StackEntry> stack;
    stack.push_back({compute_hash_list(0), 0});

    for (unsigned i = 1; i < k_; ++i) {
        unsigned current_depth = 0;
        std::vector<HashItem> merged_list = compute_hash_list(i);
        while (!stack.empty() && stack.back().depth == current_depth) {
            std::vector<HashItem> top = std::move(stack.back().list);
            stack.pop_back();
            unsigned mask_bit = (current_depth == lgk_ - 1) ? ell_ * 2 : ell_;
            merged_list = hash_merge(top, merged_list, mask_bit);
            current_depth += 1;
        }
        stack.push_back({std::move(merged_list), current_depth});
    }

    if (stack.size() != 1 || stack.back().depth != lgk_) {
        throw std::runtime_error("merge tree did not reduce to a single root at depth lgk");
    }

    // CORNER CASE, confirmed by cross-implementation testing: at k=1
    // (lgk_=0), the `for (i=1;i<k_;...)` loop above never runs, so
    // `stack` holds just the one leaf list pushed before the loop, at
    // depth 0 == lgk_ -- the guard just above is satisfied trivially,
    // and every leaf in that unmerged list is returned below as a
    // "solution," with NO XOR-to-zero collision check ever performed
    // (that check only happens inside hash_merge, which the loop body
    // never reaches). This exactly reproduces the Python reference's own
    // behavior (`k_list_wagner_algorithm._solve` has the identical loop
    // shape and the identical gap) -- confirmed by running the actual
    // reference at (n=8,k=1): 256 "solutions," all failing
    // verify_results's own zero-check. V1-V5 all inherit this reference
    // quirk faithfully (byte-exact target, per this corpus's own
    // discipline); V6 (the Khovratovich-style rewrite) deliberately
    // diverges and returns zero solutions instead -- see its own
    // solve_internal for the reasoning. Not a bug introduced by this
    // port; a pre-existing property of the paper's own reference
    // implementation at an unreachable/degenerate parameter point.
    std::vector<std::vector<uint32_t>> solutions;
    solutions.reserve(stack.back().list.size());
    for (auto& item : stack.back().list) {
        solutions.push_back(std::move(item.index_vector));
    }
    return solutions;
}

std::vector<std::vector<uint32_t>> KListWagnerAlgorithmV1::solve() const {
    return solve_internal();
}

bool KListWagnerAlgorithmV1::verify(const std::vector<std::vector<uint32_t>>& solutions) const {
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

} // namespace cs_v1
