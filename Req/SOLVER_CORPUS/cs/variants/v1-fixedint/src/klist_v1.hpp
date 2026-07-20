// V1 -- fixed-width integer variant of the canonical CS port
// (../../src/klist.hpp/.cpp). Same algorithm, same public method shape,
// same byte-exact target against the Python reference's vectors -- the
// only change is representation: FixedUint (native uint64_t limbs,
// fixedint.hpp) in place of klist.cpp's arbitrary-width
// std::vector<uint8_t> big-endian byte string. XOR/shift/mask become
// native word ops instead of byte loops; no algorithmic change.
//
// This is the "obvious first step" baseline every other variant builds
// on: removing the arbitrary-precision tax before layering on any
// 2016-17-style structural technique (bucket sort, index pointers,
// static allocation, ...).
#ifndef CS_V1_KLIST_HPP
#define CS_V1_KLIST_HPP

#include "fixedint.hpp"

#include <cstdint>
#include <vector>

namespace cs_v1 {

struct HashItem {
    FixedUint value;
    std::vector<uint32_t> index_vector;
};

class KListWagnerAlgorithmV1 {
public:
    KListWagnerAlgorithmV1(unsigned n, unsigned k, std::vector<uint8_t> nonce);

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

    std::vector<HashItem> compute_hash_list(unsigned i) const;
    static std::vector<HashItem> hash_merge(
        const std::vector<HashItem>& l1,
        const std::vector<HashItem>& l2,
        unsigned mask_bit);
    std::vector<std::vector<uint32_t>> solve_internal() const;
};

} // namespace cs_v1

#endif // CS_V1_KLIST_HPP
