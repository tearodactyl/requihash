#include "klist_v4.hpp"
#include "hashmsg.hpp"
#include "blake2.h"

#include <cassert>
#include <stdexcept>
#include <unordered_map>

namespace cs_v4 {

void Arena::init(size_t max_live_rows, unsigned k) {
    lgk_ = 0;
    unsigned kk = k;
    while (kk > 1) { kk >>= 1; ++lgk_; }

    rows.assign(max_live_rows, Row{});
    row_bump_ = 0;
    row_free_list_.clear();
    grow_events = 0;

    // One width pool per round depth (0..lgk_), each initially sized for
    // an even share of max_live_rows -- an estimate, not a hard bound;
    // grow_width_pool handles the exceptional case, same discipline as
    // the row pool itself.
    width_pools.assign(lgk_ + 1, WidthPool{});
    size_t per_depth_estimate = max_live_rows / (lgk_ + 2) + 1;
    for (unsigned depth = 0; depth <= lgk_; ++depth) {
        unsigned width = 1u << depth;
        width_pools[depth].index_pool.assign(per_depth_estimate * (size_t)width, 0);
        width_pools[depth].free_list.clear();
        width_pools[depth].bump = 0;
    }
}

void Arena::grow_row_pool(size_t new_capacity_rows) {
    ++grow_events;
    rows.resize(new_capacity_rows);
}

size_t Arena::alloc_row_slots(size_t count) {
    if (count == 0) return row_bump_;
    for (size_t i = 0; i < row_free_list_.size(); ++i) {
        if (row_free_list_[i].second >= count) {
            size_t base = row_free_list_[i].first;
            if (row_free_list_[i].second == count) {
                row_free_list_.erase(row_free_list_.begin() + i);
            } else {
                row_free_list_[i].first += count;
                row_free_list_[i].second -= count;
            }
            return base;
        }
    }
    size_t base = row_bump_;
    if (base + count > rows.size()) {
        size_t new_cap = rows.size() == 0 ? count : rows.size() * 2;
        while (base + count > new_cap) new_cap *= 2;
        grow_row_pool(new_cap);
    }
    row_bump_ += count;
    return base;
}

void Arena::free_row_slots(size_t base, size_t count) {
    if (count == 0) return;
    row_free_list_.emplace_back(base, count);
}

void Arena::grow_width_pool(unsigned depth, size_t new_capacity_rows) {
    ++grow_events;
    unsigned width = 1u << depth;
    width_pools[depth].index_pool.resize(new_capacity_rows * (size_t)width, 0);
}

size_t Arena::alloc_index_slots(size_t count, unsigned index_width) {
    unsigned depth = width_to_depth(index_width);
    WidthPool& wp = width_pools[depth];
    if (count == 0) return wp.bump;

    for (size_t i = 0; i < wp.free_list.size(); ++i) {
        if (wp.free_list[i].second >= count) {
            size_t base = wp.free_list[i].first;
            if (wp.free_list[i].second == count) {
                wp.free_list.erase(wp.free_list.begin() + i);
            } else {
                wp.free_list[i].first += count;
                wp.free_list[i].second -= count;
            }
            return base;
        }
    }
    size_t base = wp.bump;
    size_t current_cap = wp.index_pool.size() / index_width;
    if (base + count > current_cap) {
        size_t new_cap = current_cap == 0 ? count : current_cap * 2;
        while (base + count > new_cap) new_cap *= 2;
        grow_width_pool(depth, new_cap);
    }
    wp.bump += count;
    return base;
}

void Arena::free_index_slots(size_t base, size_t count, unsigned index_width) {
    if (count == 0) return;
    unsigned depth = width_to_depth(index_width);
    width_pools[depth].free_list.emplace_back(base, count);
}

namespace {

struct RoundRef {
    size_t base;   // row index into arena.rows (shared row pool)
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
    // Allocation-free message + digest construction (../../README.md
    // "Known issues") -- same bytes, same blake2b call, no std::string/
    // std::vector heap traffic per call.
    uint8_t msgbuf[cs_common::kMaxMessageBytes];
    size_t msglen = cs_common::build_leaf_message(nonce_.data(), i, j, msgbuf);

    uint8_t digest[64];
    int rc = blake2b(digest, hash_size_, msgbuf, msglen, nullptr, 0);
    if (rc != 0) throw std::runtime_error("blake2b failed");
    return FixedUint::from_be_bytes(digest, hash_size_);
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

    auto set_indices = [&](size_t row_idx, unsigned depth, size_t width_base, unsigned width, const uint32_t* src, uint32_t len) {
        Row& row = arena.rows[row_idx];
        row.index_off = (uint32_t)width_base;
        row.index_len = len;
        uint32_t* dst = &arena.width_pools[depth].index_pool[width_base * (size_t)width];
        for (uint32_t t = 0; t < len; ++t) dst[t] = src[t];
    };

    auto make_leaf_round = [&](unsigned list_index) -> RoundRef {
        size_t row_base = arena.alloc_row_slots(leaf_count);
        size_t width_base = arena.alloc_index_slots(leaf_count, 1);
        for (size_t j = 0; j < leaf_count; ++j) {
            arena.rows[row_base + j].value = compute_item(list_index, (unsigned)j);
            uint32_t jj = (uint32_t)j;
            set_indices(row_base + j, /*depth=*/0, width_base + j, 1, &jj, 1);
        }
        return {row_base, leaf_count, 0};
    };

    auto merge = [&](RoundRef r1, RoundRef r2, unsigned mask_bit, unsigned out_depth) -> RoundRef {
        unsigned out_width = 1u << out_depth;
        std::unordered_map<uint64_t, std::vector<uint32_t>> table; // key -> local row offset within r1
        table.reserve(r1.count * 2);
        for (size_t off = 0; off < r1.count; ++off) {
            table[arena.rows[r1.base + off].value.low_bits_key(mask_bit)].push_back((uint32_t)off);
        }

        // Count first so both the row pool and this round's width pool
        // can each be allocated in one shot (no growing container
        // mid-merge -- the actual "static allocation" property, at the
        // per-merge granularity).
        size_t out_count = 0;
        for (size_t off2 = 0; off2 < r2.count; ++off2) {
            auto it = table.find(arena.rows[r2.base + off2].value.low_bits_key(mask_bit));
            if (it != table.end()) out_count += it->second.size();
        }

        // Row-pool slots and this-round's-width index slots are
        // INDEPENDENT allocation domains (see Arena's header comment on
        // why conflating them was a real bug, ASan-caught) -- both sized
        // to out_count, but from separate pools with separate bump
        // cursors/free-lists.
        size_t out_row_base = arena.alloc_row_slots(out_count);
        size_t out_width_base = arena.alloc_index_slots(out_count, out_width);
        size_t w = 0;
        std::vector<uint32_t> merged(out_width);
        for (size_t off2 = 0; off2 < r2.count; ++off2) {
            size_t row2_idx = r2.base + off2;
            const Row& row2 = arena.rows[row2_idx];
            auto it = table.find(row2.value.low_bits_key(mask_bit));
            if (it == table.end()) continue;
            const uint32_t* idx2 = arena.indices_of(row2_idx);
            for (uint32_t off1 : it->second) {
                size_t row1_idx = r1.base + off1;
                const Row& row1 = arena.rows[row1_idx];
                const uint32_t* idx1 = arena.indices_of(row1_idx);

                size_t out_idx = out_row_base + w;
                arena.rows[out_idx].value = (row1.value ^ row2.value).shr(mask_bit);
                uint32_t ml = 0;
                for (uint32_t t = 0; t < row1.index_len; ++t) merged[ml++] = idx1[t];
                for (uint32_t t = 0; t < row2.index_len; ++t) merged[ml++] = idx2[t];
                set_indices(out_idx, out_depth, out_width_base + w, out_width, merged.data(), ml);
                ++w;
            }
        }
        arena.free_row_slots(r1.base, r1.count);
        arena.free_row_slots(r2.base, r2.count);
        // Free r1/r2's own index slots too, from THEIR width pools (one
        // depth shallower than out_depth -- r1/r2's own rows carry
        // index_len == 2^(out_depth-1), except at depth 0 leaf rounds
        // where both are width 1 and out_depth is 1: still correct since
        // free_index_slots derives depth from the width argument, not
        // from out_depth).
        if (r1.count > 0) {
            unsigned r1_width = arena.rows[r1.base].index_len;
            arena.free_index_slots(/*base=*/arena.rows[r1.base].index_off, r1.count, r1_width);
        }
        if (r2.count > 0) {
            unsigned r2_width = arena.rows[r2.base].index_len;
            arena.free_index_slots(/*base=*/arena.rows[r2.base].index_off, r2.count, r2_width);
        }
        return {out_row_base, out_count, 0};
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
            merged_round = merge(top, merged_round, mask_bit, current_depth + 1);
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
        const uint32_t* idx = arena.indices_of(row_idx);
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
