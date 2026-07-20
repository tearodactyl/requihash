#include "klist_v4.hpp"
#include "blake2.h"

#include <cassert>
#include <stdexcept>
#include <unordered_map>

namespace cs_v4 {

void Arena::init(size_t max_live_rows, unsigned k) {
    index_cap = k;
    rows.assign(max_live_rows, Row{});
    index_pool.assign(max_live_rows * (size_t)index_cap, 0);
    bump_ = 0;
    free_list_.clear();
    grow_events = 0;
}

void Arena::grow_to(size_t new_capacity_rows) {
    ++grow_events;
    rows.resize(new_capacity_rows);
    index_pool.resize(new_capacity_rows * (size_t)index_cap, 0);
}

size_t Arena::alloc_rows(size_t count) {
    if (count == 0) return bump_;
    for (size_t i = 0; i < free_list_.size(); ++i) {
        if (free_list_[i].second >= count) {
            size_t base = free_list_[i].first;
            if (free_list_[i].second == count) {
                free_list_.erase(free_list_.begin() + i);
            } else {
                free_list_[i].first += count;
                free_list_[i].second -= count;
            }
            return base;
        }
    }
    size_t base = bump_;
    if (base + count > rows.size()) {
        // Geometric growth, same amortized-O(1) discipline as std::vector
        // -- the estimate that sized init() was optimistic, not wrong by
        // design; see header note. Doubling avoids repeated grow calls.
        size_t new_cap = rows.size() == 0 ? count : rows.size() * 2;
        while (base + count > new_cap) new_cap *= 2;
        grow_to(new_cap);
    }
    bump_ += count;
    return base;
}

void Arena::free_rows(size_t base, size_t count) {
    if (count == 0) return;
    free_list_.emplace_back(base, count);
}

namespace {

struct RoundRef {
    size_t base;
    size_t count;
    unsigned depth;
};

} // namespace

KListWagnerAlgorithmV4::KListWagnerAlgorithmV4(unsigned n, unsigned k, std::vector<uint8_t> nonce)
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

FixedUint KListWagnerAlgorithmV4::compute_item(unsigned i, unsigned j) const {
    std::string suffix = std::to_string(i) + "-" + std::to_string(j);
    std::vector<uint8_t> message(nonce_);
    message.insert(message.end(), suffix.begin(), suffix.end());

    std::vector<uint8_t> digest(hash_size_);
    int rc = blake2b(digest.data(), hash_size_, message.data(), message.size(), nullptr, 0);
    if (rc != 0) throw std::runtime_error("blake2b failed");
    return FixedUint::from_be_bytes(digest.data(), hash_size_);
}

std::vector<std::vector<uint32_t>> KListWagnerAlgorithmV4::solve_internal() const {
    // Static budget estimate: every leaf round (k of them) plus every
    // intermediate merge round live simultaneously, at most lgk+1 deep
    // on the stack, each nominally 2^ell rows. This is an ESTIMATE for
    // the common case, not a hard bound -- Arena grows (geometric
    // doubling) if a merge's real output exceeds it. See header note on
    // why a hard cap would be a correctness bug for this algorithm.
    size_t leaf_count = size_t(1) << ell_;
    size_t max_live_rows = leaf_count * (lgk_ + 2);

    Arena arena;
    arena.init(max_live_rows, k_);

    auto set_indices = [&](size_t row_idx, const uint32_t* src, uint32_t len) {
        Row& row = arena.rows[row_idx];
        row.index_off = (uint32_t)row_idx; // slot base = row_idx * index_cap (1:1 mapping, simplest scheme)
        row.index_len = len;
        uint32_t* dst = &arena.index_pool[(size_t)row.index_off * arena.index_cap];
        for (uint32_t t = 0; t < len; ++t) dst[t] = src[t];
    };
    auto get_indices = [&](size_t row_idx) -> const uint32_t* {
        return &arena.index_pool[(size_t)arena.rows[row_idx].index_off * arena.index_cap];
    };

    auto make_leaf_round = [&](unsigned list_index) -> RoundRef {
        size_t base = arena.alloc_rows(leaf_count);
        for (size_t j = 0; j < leaf_count; ++j) {
            arena.rows[base + j].value = compute_item(list_index, (unsigned)j);
            uint32_t jj = (uint32_t)j;
            set_indices(base + j, &jj, 1);
        }
        return {base, leaf_count, 0};
    };

    auto merge = [&](RoundRef r1, RoundRef r2, unsigned mask_bit) -> RoundRef {
        std::unordered_map<uint64_t, std::vector<uint32_t>> table; // key -> local row offset within r1
        table.reserve(r1.count * 2);
        for (size_t off = 0; off < r1.count; ++off) {
            table[arena.rows[r1.base + off].value.low_bits_key(mask_bit)].push_back((uint32_t)off);
        }

        // Count first so the arena range can be allocated in one shot
        // (no growing container mid-merge -- the actual "static
        // allocation" property, at the per-merge granularity).
        size_t out_count = 0;
        for (size_t off2 = 0; off2 < r2.count; ++off2) {
            auto it = table.find(arena.rows[r2.base + off2].value.low_bits_key(mask_bit));
            if (it != table.end()) out_count += it->second.size();
        }

        size_t out_base = arena.alloc_rows(out_count);
        size_t w = 0;
        // One reusable scratch buffer for the whole merge call (not
        // reallocated per pair) -- pairs are processed sequentially, so
        // a single index_cap-wide buffer, overwritten each iteration
        // then copied into the arena via set_indices, is sufficient and
        // keeps this loop allocation-free.
        std::vector<uint32_t> merged(arena.index_cap);
        for (size_t off2 = 0; off2 < r2.count; ++off2) {
            size_t row2_idx = r2.base + off2;
            const Row& row2 = arena.rows[row2_idx];
            auto it = table.find(row2.value.low_bits_key(mask_bit));
            if (it == table.end()) continue;
            const uint32_t* idx2 = get_indices(row2_idx);
            for (uint32_t off1 : it->second) {
                size_t row1_idx = r1.base + off1;
                const Row& row1 = arena.rows[row1_idx];
                const uint32_t* idx1 = get_indices(row1_idx);

                size_t out_idx = out_base + w;
                arena.rows[out_idx].value = (row1.value ^ row2.value).shr(mask_bit);
                uint32_t ml = 0;
                for (uint32_t t = 0; t < row1.index_len; ++t) merged[ml++] = idx1[t];
                for (uint32_t t = 0; t < row2.index_len; ++t) merged[ml++] = idx2[t];
                set_indices(out_idx, merged.data(), ml);
                ++w;
            }
        }
        arena.free_rows(r1.base, r1.count);
        arena.free_rows(r2.base, r2.count);
        return {out_base, out_count, 0};
    };

    std::vector<RoundRef> stack;
    stack.push_back(make_leaf_round(0));
    stack.back().depth = 0;

    for (unsigned i = 1; i < k_; ++i) {
        unsigned current_depth = 0;
        RoundRef merged_round = make_leaf_round(i);
        merged_round.depth = 0;
        while (!stack.empty() && stack.back().depth == current_depth) {
            RoundRef top = stack.back();
            stack.pop_back();
            unsigned mask_bit = (current_depth == lgk_ - 1) ? ell_ * 2 : ell_;
            merged_round = merge(top, merged_round, mask_bit);
            merged_round.depth = current_depth + 1;
            current_depth += 1;
        }
        stack.push_back(merged_round);
    }

    if (stack.size() != 1 || stack.back().depth != lgk_) {
        throw std::runtime_error("merge tree did not reduce to a single root at depth lgk");
    }

    // CORNER CASE at k=1: see ../v1-fixedint/src/klist_v1.cpp's identical
    // guard for the full explanation -- this variant inherits the same
    // Python-reference quirk (256 unverified "solutions" at (n=8,k=1),
    // no XOR-to-zero check ever runs). Byte-exact target, not a bug.
    RoundRef root = stack.back();
    std::vector<std::vector<uint32_t>> solutions;
    solutions.reserve(root.count);
    for (size_t off = 0; off < root.count; ++off) {
        size_t row_idx = root.base + off;
        const uint32_t* idx = get_indices(row_idx);
        solutions.emplace_back(idx, idx + arena.rows[row_idx].index_len);
    }
    return solutions;
}

std::vector<std::vector<uint32_t>> KListWagnerAlgorithmV4::solve() const {
    return solve_internal();
}

bool KListWagnerAlgorithmV4::verify(const std::vector<std::vector<uint32_t>>& solutions) const {
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

} // namespace cs_v4
