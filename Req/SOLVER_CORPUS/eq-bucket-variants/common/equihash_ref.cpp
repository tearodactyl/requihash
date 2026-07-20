#include "equihash_ref.hpp"
#include "blake2.h"

#include <algorithm>

namespace eq_common {

FixedUint compute_leaf(const Params& p, const std::vector<uint8_t>& nonce, uint32_t j) {
    std::vector<uint8_t> msg = leaf_message(nonce, j);
    std::vector<uint8_t> digest(p.hash_size);
    int rc = blake2b(digest.data(), p.hash_size, msg.data(), msg.size(), nullptr, 0);
    if (rc != 0) throw std::runtime_error("blake2b failed");
    return FixedUint::from_be_bytes(digest.data(), p.hash_size);
}

bool verify_solution(const Params& p, const std::vector<uint8_t>& nonce,
                      const std::vector<uint32_t>& indices) {
    uint64_t expected = uint64_t(1) << p.k;
    if (indices.size() != expected) return false;
    std::vector<uint32_t> sorted_idx = indices;
    std::sort(sorted_idx.begin(), sorted_idx.end());
    for (size_t i = 1; i < sorted_idx.size(); ++i) {
        if (sorted_idx[i] == sorted_idx[i - 1]) return false; // duplicate index
    }
    FixedUint acc = FixedUint::zero(p.n);
    for (uint32_t idx : indices) {
        acc = acc ^ compute_leaf(p, nonce, idx);
    }
    return acc.is_zero();
}

} // namespace eq_common
