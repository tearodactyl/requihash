// V2 -- bucket/counting-sort merge variant, built on V1's fixed-width
// integers. Replaces hash_merge's std::unordered_map hash-join (V1,
// and the canonical cs/) with a counting-sort bucket partition on the
// mask bits, the same "incomplete bucket sort" technique
// Req/rust/src/solve/bucket.rs applies to Requihash's own solver
// (2016-17 tromp/xenoncat technique #2, Req/ARCHITECTURE.md S7).
//
// Mechanics: bucket on min(mask_bit, 16) leading bits (a bounded bucket
// table, same "cap the key width, group exactly within the bucket for
// any residual bits" shape bucket.rs uses via cbyte.min(2)). A counting
// sort partitions L1 by that truncated key in O(|L1|) with no hash-map
// allocation; L2 items probe the same truncated key and only pay an
// exact full-mask_bit comparison within the (typically tiny) bucket.
// Produces the exact same merged-pair set as the hash-join, in a
// different but still Python-reference-index-vector-order-independent
// enumeration -- the differential test sorts before comparing (see
// tests/differential.cpp) since Sequihash's "solutions" are an
// unordered set at any one merge round; final root order is
// deterministic like the reference.
#ifndef CS_V2_KLIST_HPP
#define CS_V2_KLIST_HPP

#include "fixedint.hpp"

#include <cstdint>
#include <vector>

namespace cs_v2 {

struct HashItem {
    FixedUint value;
    std::vector<uint32_t> index_vector;
};

class KListWagnerAlgorithmV2 {
public:
    KListWagnerAlgorithmV2(unsigned n, unsigned k, std::vector<uint8_t> nonce);

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
    static std::vector<HashItem> hash_merge_bucketed(
        const std::vector<HashItem>& l1,
        const std::vector<HashItem>& l2,
        unsigned mask_bit);
    std::vector<std::vector<uint32_t>> solve_internal() const;
};

} // namespace cs_v2

#endif // CS_V2_KLIST_HPP
