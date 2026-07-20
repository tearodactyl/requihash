#include "klist_v3.hpp"
#include "blake2.h"

#include <cassert>
#include <stdexcept>
#include <unordered_map>

namespace cs_v3 {

KListWagnerAlgorithmV3::KListWagnerAlgorithmV3(unsigned n, unsigned k, std::vector<uint8_t> nonce)
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
    if ((uint64_t(1) << ell_) >= kLeafSentinel)
        throw std::invalid_argument("leaf count exceeds this variant's uint32_t row-index width");
}

FixedUint KListWagnerAlgorithmV3::compute_item(unsigned i, unsigned j) const {
    std::string suffix = std::to_string(i) + "-" + std::to_string(j);
    std::vector<uint8_t> message(nonce_);
    message.insert(message.end(), suffix.begin(), suffix.end());

    std::vector<uint8_t> digest(hash_size_);
    int rc = blake2b(digest.data(), hash_size_, message.data(), message.size(), nullptr, 0);
    if (rc != 0) throw std::runtime_error("blake2b failed");
    return FixedUint::from_be_bytes(digest.data(), hash_size_);
}

std::shared_ptr<Round> KListWagnerAlgorithmV3::compute_leaf_round(unsigned list_index) const {
    auto r = std::make_shared<Round>();
    r->list_index = (int)list_index;
    uint64_t count = uint64_t(1) << ell_;
    r->rows.reserve(count);
    for (uint64_t j = 0; j < count; ++j) {
        PtrRow row;
        row.value = compute_item(list_index, (unsigned)j);
        row.left = (uint32_t)j;
        row.right = kLeafSentinel;
        r->rows.push_back(std::move(row));
    }
    return r;
}

// Same hash-join shape as V1's hash_merge, but emits pointer pairs
// (row indices into r1/r2's own arrays) instead of concatenated index
// vectors -- the representation change this variant exists to measure.
std::shared_ptr<Round> KListWagnerAlgorithmV3::merge_rounds(
    std::shared_ptr<Round> r1, std::shared_ptr<Round> r2, unsigned mask_bit) {
    std::unordered_map<uint64_t, std::vector<uint32_t>> table; // key -> row indices into r1
    table.reserve(r1->rows.size() * 2);
    for (uint32_t idx = 0; idx < r1->rows.size(); ++idx) {
        table[r1->rows[idx].value.low_bits_key(mask_bit)].push_back(idx);
    }

    auto out = std::make_shared<Round>();
    out->parent_left = r1;
    out->parent_right = r2;
    for (uint32_t idx2 = 0; idx2 < r2->rows.size(); ++idx2) {
        const auto& item2 = r2->rows[idx2];
        auto it = table.find(item2.value.low_bits_key(mask_bit));
        if (it == table.end()) continue;
        for (uint32_t idx1 : it->second) {
            PtrRow row;
            row.value = (r1->rows[idx1].value ^ item2.value).shr(mask_bit);
            row.left = idx1;
            row.right = idx2;
            out->rows.push_back(std::move(row));
        }
    }
    return out;
}

void KListWagnerAlgorithmV3::reconstruct(const Round& r, uint32_t row_idx, std::vector<uint32_t>& out) {
    const PtrRow& row = r.rows[row_idx];
    if (row.right == kLeafSentinel) {
        // Leaf row: row.left is the literal j for list r.list_index.
        out.push_back(row.left);
        return;
    }
    reconstruct(*r.parent_left, row.left, out);
    reconstruct(*r.parent_right, row.right, out);
}

std::vector<std::vector<uint32_t>> KListWagnerAlgorithmV3::solve_internal() const {
    struct StackEntry {
        std::shared_ptr<Round> round;
        unsigned depth;
    };
    std::vector<StackEntry> stack;
    round_row_counts.clear();

    auto first = compute_leaf_round(0);
    round_row_counts.push_back(first->rows.size());
    stack.push_back({first, 0});

    for (unsigned i = 1; i < k_; ++i) {
        unsigned current_depth = 0;
        std::shared_ptr<Round> merged_round = compute_leaf_round(i);
        round_row_counts.push_back(merged_round->rows.size());
        while (!stack.empty() && stack.back().depth == current_depth) {
            std::shared_ptr<Round> top = stack.back().round;
            stack.pop_back();
            unsigned mask_bit = (current_depth == lgk_ - 1) ? ell_ * 2 : ell_;
            merged_round = merge_rounds(top, merged_round, mask_bit);
            round_row_counts.push_back(merged_round->rows.size());
            current_depth += 1;
        }
        stack.push_back({merged_round, current_depth});
    }

    if (stack.size() != 1 || stack.back().depth != lgk_) {
        throw std::runtime_error("merge tree did not reduce to a single root at depth lgk");
    }

    // CORNER CASE at k=1: see ../v1-fixedint/src/klist_v1.cpp's identical
    // guard for the full explanation -- this variant inherits the same
    // Python-reference quirk (256 unverified "solutions" at (n=8,k=1),
    // no XOR-to-zero check ever runs). Byte-exact target, not a bug.
    // Pointer reconstruction (reconstruct()) still walks correctly in
    // this case -- every leaf row IS a leaf (right==kLeafSentinel), so
    // reconstruction is a single push_back per "solution," no tree walk.
    const Round& root = *stack.back().round;
    std::vector<std::vector<uint32_t>> solutions;
    solutions.reserve(root.rows.size());
    for (uint32_t idx = 0; idx < root.rows.size(); ++idx) {
        std::vector<uint32_t> indices;
        indices.reserve(k_);
        reconstruct(root, idx, indices);
        solutions.push_back(std::move(indices));
    }
    return solutions;
}

std::vector<std::vector<uint32_t>> KListWagnerAlgorithmV3::solve() const {
    return solve_internal();
}

bool KListWagnerAlgorithmV3::verify(const std::vector<std::vector<uint32_t>>& solutions) const {
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

} // namespace cs_v3
