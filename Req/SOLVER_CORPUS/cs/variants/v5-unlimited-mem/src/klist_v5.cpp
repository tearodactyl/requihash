#include "klist_v5.hpp"
#include "hashmsg.hpp"
#include "blake2.h"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <numeric>
#include <stdexcept>

namespace cs_v5 {

KListWagnerAlgorithmV5::KListWagnerAlgorithmV5(unsigned n, unsigned k, std::vector<uint8_t> nonce)
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

FixedUint KListWagnerAlgorithmV5::compute_item(unsigned i, unsigned j) const {
    // Non-precomputed path: only used by verify() (a handful of calls,
    // not the hot leaf-generation loop, so no prefix-caching benefit to
    // chase there). Still switched to the allocation-free builder
    // (../../README.md) for consistency/correctness parity
    // with every other variant, even though its own perf impact here is
    // negligible (cold path).
    uint8_t msgbuf[cs_common::kMaxMessageBytes];
    size_t msglen = cs_common::build_leaf_message(nonce_.data(), i, j, msgbuf);

    uint8_t digest[64];
    int rc = blake2b(digest, hash_size_, msgbuf, msglen, nullptr, 0);
    if (rc != 0) throw std::runtime_error("blake2b failed");
    return FixedUint::from_be_bytes(digest, hash_size_);
}

// Class-prefix precomputation (H2): absorb nonce+"i-" once into a
// blake2b_state, snapshot it (plain struct copy), then for every j
// resume from the snapshot and absorb only decimal(j) before
// finalizing. blake2b_final is destructive on its internal buffer
// state, so each j resumes from a FRESH copy of the snapshot, not the
// shared state itself -- the whole point is that init+update(prefix) is
// paid once, not that update(suffix)+final is skipped (it can't be,
// each leaf's digest differs).
std::vector<HashItem> KListWagnerAlgorithmV5::compute_hash_list_precomputed(unsigned i) const {
    // Allocation-free prefix construction (see ../../README.md):
    // nonce(16) || decimal(i) || '-', no std::string/std::vector.
    uint8_t prefix_msg[cs_common::kMaxMessageBytes];
    size_t prefix_len = 0;
    for (int b = 0; b < 16; ++b) prefix_msg[prefix_len++] = nonce_[b];
    cs_common::write_uint(i, prefix_msg, &prefix_len);
    prefix_msg[prefix_len++] = '-';

    blake2b_state snapshot;
    if (blake2b_init(&snapshot, hash_size_) != 0) throw std::runtime_error("blake2b_init failed");
    if (blake2b_update(&snapshot, prefix_msg, prefix_len) != 0)
        throw std::runtime_error("blake2b_update (prefix) failed");
    ++class_prefixes_precomputed;

    uint64_t count = uint64_t(1) << ell_;
    std::vector<HashItem> list;
    list.reserve(count);
    for (uint64_t j = 0; j < count; ++j) {
        blake2b_state st = snapshot; // cheap struct copy, resumes the cached prefix state
        // Allocation-free suffix (decimal(j) only, no std::string).
        uint8_t suffix_buf[10];
        size_t suffix_len = 0;
        cs_common::write_uint((unsigned)j, suffix_buf, &suffix_len);
        if (blake2b_update(&st, suffix_buf, suffix_len) != 0)
            throw std::runtime_error("blake2b_update (suffix) failed");
        uint8_t digest[64];
        if (blake2b_final(&st, digest, hash_size_) != 0)
            throw std::runtime_error("blake2b_final failed");
        ++leaf_hashes_computed;

        HashItem item;
        item.value = FixedUint::from_be_bytes(digest, hash_size_);
        item.index_vector = {(uint32_t)j};
        list.push_back(std::move(item));
    }
    return list;
}

// Full-width radix-style merge: sort BOTH lists by the exact mask_bit
// key (no truncated bucket table, no residual within-bucket rescan --
// unlike V2, which caps the bucket address at 16 bits precisely to
// bound memory; V5 assumes that bound is unnecessary). A single
// merge-join over the two sorted sequences then finds every colliding
// pair in one linear pass.
std::vector<HashItem> KListWagnerAlgorithmV5::hash_merge_full_radix(
    const std::vector<HashItem>& l1,
    const std::vector<HashItem>& l2,
    unsigned mask_bit) {
    std::vector<uint32_t> order1(l1.size()), order2(l2.size());
    std::iota(order1.begin(), order1.end(), 0u);
    std::iota(order2.begin(), order2.end(), 0u);

    auto key1 = [&](uint32_t idx) { return l1[idx].value.low_bits_key(mask_bit); };
    auto key2 = [&](uint32_t idx) { return l2[idx].value.low_bits_key(mask_bit); };
    std::sort(order1.begin(), order1.end(), [&](uint32_t a, uint32_t b) { return key1(a) < key1(b); });
    std::sort(order2.begin(), order2.end(), [&](uint32_t a, uint32_t b) { return key2(a) < key2(b); });

    std::vector<HashItem> merged;
    size_t p1 = 0, p2 = 0;
    while (p1 < order1.size() && p2 < order2.size()) {
        uint64_t k1 = key1(order1[p1]);
        uint64_t k2 = key2(order2[p2]);
        if (k1 < k2) { ++p1; continue; }
        if (k2 < k1) { ++p2; continue; }
        // Equal-key runs on both sides -- emit the full cross product
        // (matches hash_merge's own semantics: every L1/L2 pair with a
        // matching key collides, not just adjacent ones).
        size_t run1_end = p1;
        while (run1_end < order1.size() && key1(order1[run1_end]) == k1) ++run1_end;
        size_t run2_end = p2;
        while (run2_end < order2.size() && key2(order2[run2_end]) == k2) ++run2_end;
        for (size_t a = p1; a < run1_end; ++a) {
            const HashItem& item1 = l1[order1[a]];
            for (size_t b = p2; b < run2_end; ++b) {
                const HashItem& item2 = l2[order2[b]];
                HashItem out;
                out.value = (item1.value ^ item2.value).shr(mask_bit);
                out.index_vector = item1.index_vector;
                out.index_vector.insert(out.index_vector.end(),
                                         item2.index_vector.begin(), item2.index_vector.end());
                merged.push_back(std::move(out));
            }
        }
        p1 = run1_end;
        p2 = run2_end;
    }
    return merged;
}

std::vector<std::vector<uint32_t>> KListWagnerAlgorithmV5::solve_internal() const {
    leaf_hashes_computed = 0;
    class_prefixes_precomputed = 0;

    struct StackEntry {
        std::vector<HashItem> list;
        unsigned depth;
    };
    std::vector<StackEntry> stack;
    stack.push_back({compute_hash_list_precomputed(0), 0});

    for (unsigned i = 1; i < k_; ++i) {
        unsigned current_depth = 0;
        std::vector<HashItem> merged_list = compute_hash_list_precomputed(i);
        while (!stack.empty() && stack.back().depth == current_depth) {
            std::vector<HashItem> top = std::move(stack.back().list);
            stack.pop_back();
            unsigned mask_bit = (current_depth == lgk_ - 1) ? ell_ * 2 : ell_;
            merged_list = hash_merge_full_radix(top, merged_list, mask_bit);
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

std::vector<std::vector<uint32_t>> KListWagnerAlgorithmV5::solve() const {
    return solve_internal();
}

bool KListWagnerAlgorithmV5::verify(const std::vector<std::vector<uint32_t>>& solutions) const {
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

} // namespace cs_v5
