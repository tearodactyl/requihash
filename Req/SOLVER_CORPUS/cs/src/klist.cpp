#include "klist.hpp"
#include "blake2.h"

#include <cassert>
#include <cstring>
#include <stdexcept>
#include <unordered_map>

namespace cs {

namespace {

// ---- Arbitrary-precision big-unsigned helpers over big-endian byte
// strings (most-significant byte first), matching Python's
// int.from_bytes(bytes, 'big') / int arithmetic exactly for the
// operations k_list_algorithm.py actually performs: XOR, right-shift by
// mask_bit, and masking to the low mask_bit bits. n is caller-chosen and
// can exceed 64 bits (the class's own docstring allows n up to 256
// bits), so this cannot be a fixed machine integer.

std::vector<uint8_t> big_xor(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) {
    assert(a.size() == b.size());
    std::vector<uint8_t> out(a.size());
    for (size_t i = 0; i < a.size(); ++i) out[i] = a[i] ^ b[i];
    return out;
}

// Right-shifts a big-endian byte string by `bits`, returning a
// byte string of the same length (equivalent to Python's `x >> bits`
// on the underlying integer, then treated as still occupying the same
// byte width -- callers only ever look at the low bits they need).
//
// v is MSB-first (v[0] is the most significant byte, v[size-1] the
// least). Shifting the underlying integer right moves every bit toward
// the LEAST significant end, i.e. toward the END of the array (higher
// index) -- out[j] (counting from the end) is built from v[j+byte_shift]
// (counting from the end), with high-index positions dropping off and
// low-index (front) positions filling with zero.
std::vector<uint8_t> big_shr(const std::vector<uint8_t>& v, unsigned bits) {
    size_t n = v.size();
    std::vector<uint8_t> out(n, 0);
    unsigned byte_shift = bits / 8;
    unsigned bit_shift = bits % 8;
    if (byte_shift >= n) return out; // shifted everything out
    // Walk output positions from the end (LSB) toward the front (MSB).
    for (size_t j = 0; j < n - byte_shift; ++j) {
        size_t out_idx = n - 1 - j;         // fill from the last byte backward
        size_t src_idx = out_idx - byte_shift; // corresponding source byte, byte_shift positions toward the front
        uint8_t lo_part = v[src_idx];
        uint8_t hi_part = (src_idx > 0) ? v[src_idx - 1] : 0; // one position further toward the front supplies the carried-in high bits
        if (bit_shift == 0) {
            out[out_idx] = lo_part;
        } else {
            out[out_idx] = (uint8_t)((lo_part >> bit_shift) | (hi_part << (8 - bit_shift)));
        }
    }
    return out;
}

// Extracts the low `mask_bit` bits of a big-endian byte string as a
// uint64_t key (mask_bit <= 64 in all of this port's use -- mask_bit is
// at most 2*ell, and ell = n/(lgk+1) with n <= 256 in practice; the hash
// table key only ever needs the low bits, which always fit well within
// 64 bits for every parameter point this port targets).
uint64_t low_bits_key(const std::vector<uint8_t>& v, unsigned mask_bit) {
    assert(mask_bit <= 64);
    uint64_t key = 0;
    unsigned bits_taken = 0;
    for (auto it = v.rbegin(); it != v.rend() && bits_taken < mask_bit; ++it) {
        unsigned take = std::min<unsigned>(8, mask_bit - bits_taken);
        uint64_t byte_bits = *it;
        if (take < 8) byte_bits &= (uint64_t(1) << take) - 1;
        key |= (byte_bits << bits_taken);
        bits_taken += take;
    }
    return key;
}

bool is_all_zero(const std::vector<uint8_t>& v) {
    for (uint8_t b : v) if (b != 0) return false;
    return true;
}

} // namespace

KListWagnerAlgorithm::KListWagnerAlgorithm(unsigned n, unsigned k, std::vector<uint8_t> nonce)
    : n_(n), k_(k), nonce_(std::move(nonce)) {
    if (n_ % 8 != 0) throw std::invalid_argument("n should be a multiple of 8");
    if (nonce_.size() != 16) throw std::invalid_argument("Nonce should be 16 bytes");

    lgk_ = 0;
    unsigned kk = k_;
    while (kk > 1) { kk >>= 1; ++lgk_; }
    if ((1u << lgk_) != k_) throw std::invalid_argument("k should be a power of 2");
    if (n_ % (lgk_ + 1) != 0) throw std::invalid_argument("n should be divisible by lg(k) + 1");

    ell_ = n_ / (lgk_ + 1);
    hash_size_ = n_ / 8;
}

std::vector<uint8_t> KListWagnerAlgorithm::compute_item(unsigned i, unsigned j) const {
    // Matches: self.nonce + f"{i}-{j}".encode() -- ASCII decimal, no
    // leading zeros, no fixed width, literal '-' separator.
    std::string suffix = std::to_string(i) + "-" + std::to_string(j);
    std::vector<uint8_t> message(nonce_);
    message.insert(message.end(), suffix.begin(), suffix.end());

    std::vector<uint8_t> digest(hash_size_);
    int rc = blake2b(digest.data(), hash_size_, message.data(), message.size(), nullptr, 0);
    if (rc != 0) throw std::runtime_error("blake2b failed");
    return digest; // blake2b's digest bytes are already the big-endian
                    // representation Python's int.from_bytes(...,'big')
                    // would produce from the same raw bytes -- no byte
                    // order transform needed, BLAKE2b output is just a
                    // byte string in both languages.
}

std::vector<HashItem> KListWagnerAlgorithm::compute_hash_list(unsigned i) const {
    // Matches compute_hash_list_on_the_fly(i, None, None): full,
    // untrimmed list, 2^ell items.
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

std::vector<HashItem> KListWagnerAlgorithm::hash_merge(
    const std::vector<HashItem>& l1,
    const std::vector<HashItem>& l2,
    unsigned mask_bit) {
    // Matches hash_merge: bucket L1 by low mask_bit bits, then for each
    // L2 item probe the bucket and emit (x1^x2)>>mask_bit for every
    // collision, concatenating index vectors idx1 ++ idx2 (L1's index
    // vector first, matching Python's `idx1 + idx2`).
    std::unordered_map<uint64_t, std::vector<const HashItem*>> table;
    table.reserve(l1.size() * 2);
    for (const auto& item : l1) {
        table[low_bits_key(item.value, mask_bit)].push_back(&item);
    }

    std::vector<HashItem> merged;
    for (const auto& item2 : l2) {
        auto it = table.find(low_bits_key(item2.value, mask_bit));
        if (it == table.end()) continue;
        for (const HashItem* item1 : it->second) {
            HashItem out;
            out.value = big_shr(big_xor(item1->value, item2.value), mask_bit);
            out.index_vector = item1->index_vector;
            out.index_vector.insert(out.index_vector.end(),
                                     item2.index_vector.begin(), item2.index_vector.end());
            merged.push_back(std::move(out));
        }
    }
    return merged;
}

std::vector<std::vector<uint32_t>> KListWagnerAlgorithm::solve_internal() const {
    // Matches _solve(None, None, verbose): post-order binary merge tree
    // over leaf lists 0..k-1, using a (list, depth) stack -- merges
    // whenever the stack top's depth equals the current running depth,
    // exactly a binary-counter carry pattern.
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

    std::vector<std::vector<uint32_t>> solutions;
    solutions.reserve(stack.back().list.size());
    for (auto& item : stack.back().list) {
        solutions.push_back(std::move(item.index_vector));
    }
    return solutions;
}

std::vector<std::vector<uint32_t>> KListWagnerAlgorithm::solve() const {
    return solve_internal();
}

bool KListWagnerAlgorithm::verify(const std::vector<std::vector<uint32_t>>& solutions) const {
    if (solutions.empty()) return true; // Matches verify_results: "No solution found!" is not a failure.
    for (const auto& indices : solutions) {
        if (indices.size() != k_) return false;
    }
    for (const auto& indices : solutions) {
        std::vector<uint8_t> acc(hash_size_, 0);
        for (unsigned i = 0; i < indices.size(); ++i) {
            acc = big_xor(acc, compute_item(i, indices[i]));
        }
        if (!is_all_zero(acc)) return false;
    }
    return true;
}

} // namespace cs
