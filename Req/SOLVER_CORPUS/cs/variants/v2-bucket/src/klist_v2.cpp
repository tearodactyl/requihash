#include "klist_v2.hpp"
#include "blake2.h"

#include <algorithm>
#include <cassert>
#include <stdexcept>

namespace cs_v2 {

KListWagnerAlgorithmV2::KListWagnerAlgorithmV2(unsigned n, unsigned k, std::vector<uint8_t> nonce)
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

FixedUint KListWagnerAlgorithmV2::compute_item(unsigned i, unsigned j) const {
    std::string suffix = std::to_string(i) + "-" + std::to_string(j);
    std::vector<uint8_t> message(nonce_);
    message.insert(message.end(), suffix.begin(), suffix.end());

    std::vector<uint8_t> digest(hash_size_);
    int rc = blake2b(digest.data(), hash_size_, message.data(), message.size(), nullptr, 0);
    if (rc != 0) throw std::runtime_error("blake2b failed");
    return FixedUint::from_be_bytes(digest.data(), hash_size_);
}

std::vector<HashItem> KListWagnerAlgorithmV2::compute_hash_list(unsigned i) const {
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

// Counting-sort bucket partition of L1 on min(mask_bit,16) leading bits
// of the low-bits key, then a linear scan per L2 item over just that
// bucket, with an exact full-mask_bit equality check on each candidate
// (matching bucket.rs's "bucket on a bounded prefix, exact-match within
// the bucket" two-tier shape). No hash map, no per-item heap node.
std::vector<HashItem> KListWagnerAlgorithmV2::hash_merge_bucketed(
    const std::vector<HashItem>& l1,
    const std::vector<HashItem>& l2,
    unsigned mask_bit) {
    unsigned bucket_bits = std::min(mask_bit, 16u);
    uint64_t bucket_mask = (bucket_bits == 64) ? ~0ull : ((uint64_t(1) << bucket_bits) - 1);
    size_t nbuckets = size_t(1) << bucket_bits;

    auto bucket_of = [&](const FixedUint& v) -> size_t {
        return v.low_bits_key(mask_bit) & bucket_mask;
    };

    // Counting sort: count per bucket, prefix sum, scatter L1 indices.
    std::vector<uint32_t> counts(nbuckets + 1, 0);
    for (const auto& item : l1) counts[bucket_of(item.value) + 1] += 1;
    for (size_t b = 0; b < nbuckets; ++b) counts[b + 1] += counts[b];
    std::vector<uint32_t> order(l1.size());
    std::vector<uint32_t> cursor(counts.begin(), counts.end());
    for (uint32_t i = 0; i < l1.size(); ++i) {
        size_t b = bucket_of(l1[i].value);
        order[cursor[b]++] = i;
    }

    std::vector<HashItem> merged;
    for (const auto& item2 : l2) {
        size_t b = bucket_of(item2.value);
        uint64_t key2 = item2.value.low_bits_key(mask_bit);
        for (uint32_t p = counts[b]; p < counts[b + 1]; ++p) {
            const HashItem& item1 = l1[order[p]];
            if (item1.value.low_bits_key(mask_bit) != key2) continue; // exact match within bucket
            HashItem out;
            out.value = (item1.value ^ item2.value).shr(mask_bit);
            out.index_vector = item1.index_vector;
            out.index_vector.insert(out.index_vector.end(),
                                     item2.index_vector.begin(), item2.index_vector.end());
            merged.push_back(std::move(out));
        }
    }
    return merged;
}

std::vector<std::vector<uint32_t>> KListWagnerAlgorithmV2::solve_internal() const {
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
            merged_list = hash_merge_bucketed(top, merged_list, mask_bit);
            current_depth += 1;
        }
        stack.push_back({std::move(merged_list), current_depth});
    }

    if (stack.size() != 1 || stack.back().depth != lgk_) {
        throw std::runtime_error("merge tree did not reduce to a single root at depth lgk");
    }

    // CORNER CASE at k=1: see ../v1-fixedint/src/klist_v1.cpp's identical
    // guard for the full explanation -- this variant inherits the same
    // Python-reference quirk (256 unverified "solutions" at (n=8,k=1),
    // no XOR-to-zero check ever runs). Byte-exact target, not a bug.
    std::vector<std::vector<uint32_t>> solutions;
    solutions.reserve(stack.back().list.size());
    for (auto& item : stack.back().list) {
        solutions.push_back(std::move(item.index_vector));
    }
    return solutions;
}

std::vector<std::vector<uint32_t>> KListWagnerAlgorithmV2::solve() const {
    return solve_internal();
}

bool KListWagnerAlgorithmV2::verify(const std::vector<std::vector<uint32_t>>& solutions) const {
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

} // namespace cs_v2
