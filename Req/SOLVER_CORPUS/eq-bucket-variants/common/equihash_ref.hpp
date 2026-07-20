// Standalone, from-first-principles PLAIN (single-list) Equihash
// reference -- NOT Sequihash/Requihash (no i-mod-k / list-class
// regularity constraint; see ../../cs/ for that family). Written fresh
// rather than adapted from Req/cpp/requihash.h (which has the
// regularity term baked directly into GenerateHash) or Req/rust's
// leaf_row (same reason) -- this needs to be the plain, unmodified
// Wagner-tree-over-one-list algorithm Equihash.md/SOLVERS.md describe,
// so both bucket-structure variants (fully-sorted, two-level) compare
// apples-to-apples against the historically accurate baseline, not
// against Requihash's own regularity-repaired variant.
//
// Algorithm, exactly: N = 2^(ell+1) leaves, leaf j keyed by
// BLAKE2b(nonce || le32(j)); k collision rounds, each pairing rows that
// agree on the next `ell` collision bits (round k's final pairing
// checks 2*ell bits per Wagner's own construction, matching cs/klist's
// own final-round doubling and Req's own convention); XOR-to-zero at
// the root; every surviving index tuple must be internally distinct
// (Wagner's own free/duplicate-elimination requirement) to count as a
// solution. ell = n/(k+1).
//
// This header declares only the LEAF GENERATION and VERIFICATION
// surface shared by every bucket-structure variant; each variant
// (fully-sorted/, two-level/) supplies its own merge-round
// implementation using this shared leaf/verify code, so the only
// difference between variants is the bucket/sort structure itself, not
// incidental leaf-hashing differences.
#ifndef EQ_COMMON_EQUIHASH_REF_HPP
#define EQ_COMMON_EQUIHASH_REF_HPP

#include "fixedint.hpp"

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace eq_common {

struct Params {
    unsigned n;
    unsigned k;
    unsigned ell; // n / (k+1)
    unsigned hash_size; // n/8 bytes

    Params(unsigned n_in, unsigned k_in) : n(n_in), k(k_in) {
        if (n % 8 != 0) throw std::invalid_argument("n should be a multiple of 8");
        if (k < 1) throw std::invalid_argument("k must be >= 1");
        if (n % (k + 1) != 0) throw std::invalid_argument("n should be divisible by k+1");
        ell = n / (k + 1);
        hash_size = n / 8;
        if (ell > 64) throw std::invalid_argument("ell exceeds this port's 64-bit bucket-key width");
        if (2 * ell > 64) throw std::invalid_argument("2*ell exceeds this port's 64-bit final-round key width");
    }

    uint64_t leaf_count() const { return uint64_t(1) << (ell + 1); }
};

// le32(j) appended to the raw nonce bytes -- Req/'s own binary leaf
// convention (Req/README.md "What Requihash changes"), used here WITHOUT
// the i-mod-k list-class term (that's the entire Requihash-vs-Equihash
// difference, and this file is deliberately the "without" side).
inline std::vector<uint8_t> leaf_message(const std::vector<uint8_t>& nonce, uint32_t j) {
    std::vector<uint8_t> msg(nonce);
    msg.push_back((uint8_t)(j & 0xFF));
    msg.push_back((uint8_t)((j >> 8) & 0xFF));
    msg.push_back((uint8_t)((j >> 16) & 0xFF));
    msg.push_back((uint8_t)((j >> 24) & 0xFF));
    return msg;
}

FixedUint compute_leaf(const Params& p, const std::vector<uint8_t>& nonce, uint32_t j);

// Verifies a candidate solution (2^k distinct leaf indices): recomputes
// the XOR of all 2^k leaf hashes and checks it is exactly zero, plus
// the distinct-indices requirement.
bool verify_solution(const Params& p, const std::vector<uint8_t>& nonce,
                      const std::vector<uint32_t>& indices);

} // namespace eq_common

#endif // EQ_COMMON_EQUIHASH_REF_HPP
