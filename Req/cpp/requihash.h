// Requihash: regularity-repaired Equihash (Tang-Sun-Gong, eprint 2025/1351, Sec 5.2).
//
// Relationship to Equihash (zcash src/crypto/equihash):
//   Equihash generates candidate item j from a SINGLE list:
//       H(person, input || nonce || le32(j / IndicesPerHashOutput))
//   and slices IndicesPerHashOutput indices out of each hash output.
//
//   Requihash adds the sequential regularity constraint: the tree leaf at
//   position i (0-based over 2^K leaves) must be drawn from list-class
//   (i mod K). Concretely the generator becomes
//       H(person, input || nonce || le32(listclass) || le32(j))
//   where listclass = i mod K. Because Wagner's binary tree fixes each leaf
//   position to a definite list-class, a solver can no longer treat all leaves
//   as interchangeable draws from one list -- the single-list index-pointer
//   optimisation (the technique that collapsed Equihash's ASIC resistance,
//   Equihash.md F-A1) does not apply, and index pointers cost >=2x memory
//   against the k-list algorithm (F-A4).
//
// We keep the Equihash (n, k) convention: a solution has 2^k indices, collision
// length ell = n/(k+1), matching the paper's (n, K = 2^k) Table 3 rows. The
// regularity modulus is k (the tree depth), the concrete binding this reference
// commits to; the paper's constraint is x_i from list (i-1 mod K) and any fixed
// leaf->class map that is non-constant across the tree achieves the same effect.
//
// This header is standalone C++17; it bundles blake2b.h and shares its
// minimal/compressed solution encoding byte-for-byte with the Rust build.
#ifndef REQ_REQUIHASH_H
#define REQ_REQUIHASH_H

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <functional>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include "blake2b.h"

namespace requihash {

using eh_index = uint32_t;

// ---- bit-packing helpers (byte-exact port of zcash ExpandArray/CompressArray) ----

inline void ExpandArray(const unsigned char* in, size_t in_len, unsigned char* out,
                        size_t out_len, size_t bit_len, size_t byte_pad = 0) {
    size_t out_width = (bit_len + 7) / 8 + byte_pad;
    uint32_t bit_len_mask = ((uint32_t)1 << bit_len) - 1;
    size_t acc_bits = 0, acc_value = 0, j = 0;
    for (size_t i = 0; i < in_len; i++) {
        acc_value = (acc_value << 8) | in[i];
        acc_bits += 8;
        if (acc_bits >= bit_len) {
            acc_bits -= bit_len;
            for (size_t x = 0; x < byte_pad; x++) out[j + x] = 0;
            for (size_t x = byte_pad; x < out_width; x++) {
                out[j + x] = (acc_value >> (acc_bits + (8 * (out_width - x - 1)))) &
                             ((bit_len_mask >> (8 * (out_width - x - 1))) & 0xFF);
            }
            j += out_width;
        }
    }
}

inline void CompressArray(const unsigned char* in, size_t in_len, unsigned char* out,
                          size_t out_len, size_t bit_len, size_t byte_pad = 0) {
    size_t in_width = (bit_len + 7) / 8 + byte_pad;
    uint32_t bit_len_mask = ((uint32_t)1 << bit_len) - 1;
    size_t acc_bits = 0, acc_value = 0, j = 0;
    for (size_t i = 0; i < out_len; i++) {
        if (acc_bits < 8) {
            acc_value = acc_value << bit_len;
            for (size_t x = byte_pad; x < in_width; x++) {
                acc_value = acc_value | ((in[j + x] & ((bit_len_mask >> (8 * (in_width - x - 1))) & 0xFF))
                                         << (8 * (in_width - x - 1)));
            }
            j += in_width;
            acc_bits += bit_len;
        }
        acc_bits -= 8;
        out[i] = (acc_value >> acc_bits) & 0xFF;
    }
}

inline void EhIndexToArray(eh_index i, unsigned char* array) {
    static_assert(sizeof(eh_index) == 4, "eh_index must be 32-bit");
    eh_index be = ((i & 0xFF) << 24) | ((i & 0xFF00) << 8) | ((i & 0xFF0000) >> 8) |
                  ((i & 0xFF000000) >> 24);
    memcpy(array, &be, sizeof(be));
}

inline eh_index ArrayToEhIndex(const unsigned char* array) {
    eh_index be;
    memcpy(&be, array, sizeof(be));
    return ((be & 0xFF) << 24) | ((be & 0xFF00) << 8) | ((be & 0xFF0000) >> 8) |
           ((be & 0xFF000000) >> 24);
}

// Minimal (compressed) encoding: 2^k indices packed at (collision_len+1) bits each.
inline std::vector<unsigned char> GetMinimalFromIndices(const std::vector<eh_index>& indices,
                                                        size_t cBitLen) {
    size_t lenIndices = indices.size() * sizeof(eh_index);
    size_t minLen = (cBitLen + 1) * indices.size() / 8;
    size_t bytePad = sizeof(eh_index) - ((cBitLen + 1) + 7) / 8;
    std::vector<unsigned char> array(lenIndices);
    for (size_t i = 0; i < indices.size(); i++)
        EhIndexToArray(indices[i], array.data() + (i * sizeof(eh_index)));
    std::vector<unsigned char> ret(minLen);
    CompressArray(array.data(), lenIndices, ret.data(), minLen, cBitLen + 1, bytePad);
    return ret;
}

inline std::vector<eh_index> GetIndicesFromMinimal(const std::vector<unsigned char>& minimal,
                                                   size_t cBitLen) {
    size_t lenIndices = 8 * sizeof(eh_index) * minimal.size() / (cBitLen + 1);
    size_t bytePad = sizeof(eh_index) - ((cBitLen + 1) + 7) / 8;
    std::vector<unsigned char> array(lenIndices);
    ExpandArray(minimal.data(), minimal.size(), array.data(), lenIndices, cBitLen + 1, bytePad);
    std::vector<eh_index> ret(lenIndices / sizeof(eh_index));
    for (size_t i = 0; i < ret.size(); i++)
        ret[i] = ArrayToEhIndex(array.data() + (i * sizeof(eh_index)));
    return ret;
}

// ---- Requihash parameterised engine ----

struct Params {
    unsigned int n;
    unsigned int k;
    Params(unsigned int N, unsigned int K) : n(N), k(K) {
        if (k >= n) throw std::invalid_argument("k must be < n");
        if (n % 8 != 0) throw std::invalid_argument("n must be a multiple of 8");
        if ((n % (k + 1)) != 0) throw std::invalid_argument("n must be divisible by k+1");
        // T2.3 F13: the regularity binding keys a leaf by (leaf % k, leaf / k);
        // k == 0 is division by zero. k == 1 is well-defined (single round).
        if (k == 0) throw std::invalid_argument("k must be >= 1 (regularity binding is leaf mod k)");
        // T2.3 F12: n > 512 makes IndicesPerHashOutput() == 0, so HashOutput()
        // < n/8 and leaf-row expansion reads past the digest (e.g. (520,4)
        // passes the three checks above). One BLAKE2b digest must cover at
        // least one n-bit row.
        if (n > 512) throw std::invalid_argument("n must be <= 512 (one BLAKE2b digest per row)");
        // T2.3 F14: ExpandArray/CompressArray use a 32-bit accumulator, so
        // the collision bit length must be in [8, 25] (zcash's own asserts).
        // Below 8 the expansion silently under-fills rows; above 25 the
        // accumulator shifts exceed 32 bits.
        {
            unsigned int cbl = n / (k + 1);
            if (cbl < 8) throw std::invalid_argument("collision bit length n/(k+1) must be >= 8");
            if (cbl > 25) throw std::invalid_argument("collision bit length n/(k+1) must be <= 25");
        }
    }
    // Smallest/largest valid n for a given k (mirror of the Rust
    // Params::n_bounds; see its doc for the derivation). Valid n are exactly
    // the multiples of lcm(8, k+1) in [lo, hi]. Returns false when no valid
    // n exists (k == 0, or k > 63).
    static bool NBounds(unsigned int k, unsigned int& lo, unsigned int& hi) {
        if (k == 0) return false;
        unsigned int m = k + 1;
        unsigned int a = 8, b = m;
        while (b != 0) { unsigned int t = a % b; a = b; b = t; } // a = gcd(8, m)
        unsigned int step = 8 / a * m;                            // lcm(8, m)
        lo = (8 * m + step - 1) / step * step;                    // cbl >= 8
        hi = std::min(25 * m, 512u) / step * step;                // cbl <= 25, n <= 512
        return lo <= hi;
    }

    size_t IndicesPerHashOutput() const { return 512 / n; }
    size_t HashOutput() const { return IndicesPerHashOutput() * n / 8; }
    size_t CollisionBitLength() const { return n / (k + 1); }
    size_t CollisionByteLength() const { return (CollisionBitLength() + 7) / 8; }
    // Default (Equihash-compatible) minimal encoding: cbitlen+1 bits per index.
    size_t SolutionWidth() const { return (size_t(1) << k) * (CollisionBitLength() + 1) / 8; }

    // Requihash compact wire size (paper Table 3): the sequential regularity
    // constraint removes the per-index disambiguation bit, so indices pack at
    // exactly cbitlen bits, and the index field can be reconstructed from a
    // predefined packet structure. Equals (2^k * cbitlen)/8. For (200,9):
    // Equihash SolutionWidth=1344 B, Requihash CompactWidth=1280 B.
    size_t CompactWidth() const { return (size_t(1) << k) * CollisionBitLength() / 8; }

    void person(unsigned char out[16]) const {
        // SPEC.md §3: "ReqPoW"(6) || reserved[6..10)=0 || le32(n) || le16(k).
        // Distinct from Equihash's "ZcashPoW\0\0". The 4 reserved bytes are
        // held zero for future use.
        memset(out, 0, 16);
        const char* tag = "ReqPoW";     // 6 bytes
        memcpy(out, tag, 6);
        // out[6..10) left zero (reserved)
        uint32_t nn = n, kk = k;
        for (int i = 0; i < 4; i++) out[10 + i] = (nn >> (8 * i)) & 0xFF;
        out[14] = kk & 0xFF;
        out[15] = (kk >> 8) & 0xFF;
    }
};

// Full state carried through the Wagner merge tree: the packed hash prefix plus
// the index list that produced it.
struct Row {
    std::vector<unsigned char> hash;  // CollisionByteLength * (remaining rounds+1) bytes conceptually; we store full remaining
    std::vector<eh_index> indices;
};

class Requihash {
public:
    explicit Requihash(Params p) : P(p) {}

    // Base state = personalized BLAKE2b initialised over (input || nonce).
    reqb2::State InitialiseState(const std::vector<unsigned char>& input,
                                 const std::vector<unsigned char>& nonce) const {
        unsigned char person[16];
        P.person(person);
        reqb2::State s = reqb2::init(P.HashOutput(), person);
        reqb2::update(s, input.data(), input.size());
        reqb2::update(s, nonce.data(), nonce.size());
        return s;
    }

    // Generate the hash-output block for tree-leaf position `leaf`.
    // REQUIHASH REGULARITY: the leaf is keyed by BOTH its list-class (leaf mod k)
    // and its position within that class (leaf / k). The class is the regularity
    // binding; the intra-class counter guarantees every leaf hashes distinctly.
    // (Unlike Equihash we generate one leaf per hash output and use a single slice,
    // so IndicesPerHashOutput does not enter the keying.)
    void GenerateHash(const reqb2::State& base, eh_index leaf, unsigned char* out) const {
        eh_index listclass = leaf % (eh_index)P.k;   // <-- the Requihash constraint
        eh_index counter = leaf / (eh_index)P.k;      // position within the class
        reqb2::State s = base;  // copy the personalized+prefixed state
        unsigned char le[4];
        // list-class first, then counter: both little-endian u32
        le[0] = listclass & 0xFF; le[1] = (listclass >> 8) & 0xFF;
        le[2] = (listclass >> 16) & 0xFF; le[3] = (listclass >> 24) & 0xFF;
        reqb2::update(s, le, 4);
        le[0] = counter & 0xFF; le[1] = (counter >> 8) & 0xFF;
        le[2] = (counter >> 16) & 0xFF; le[3] = (counter >> 24) & 0xFF;
        reqb2::update(s, le, 4);
        reqb2::final(s, out);
    }

    // Expand one n-bit index slice from a hash output into CollisionByteLength-wide bytes.
    void ExpandSlice(const unsigned char* hashOut, size_t slice, unsigned char* out) const {
        size_t cbl = P.CollisionBitLength();
        size_t cByte = P.CollisionByteLength();
        // The slice is n bits starting at slice*n within the hash output; expand to
        // (k+1) collision chunks of cbl bits each. We expand the whole n-bit region.
        size_t hashLen = (P.k + 1) * cByte;
        std::vector<unsigned char> tmp(hashLen);
        ExpandArray(hashOut + (slice * P.n / 8), P.n / 8, tmp.data(), hashLen, cbl);
        memcpy(out, tmp.data(), hashLen);
    }

    // Number of collision bytes carried per row for the whole hash (all rounds).
    size_t HashLength() const { return (P.k + 1) * P.CollisionByteLength(); }

    const Params& params() const { return P; }

private:
    Params P;
};

}  // namespace requihash

#endif
