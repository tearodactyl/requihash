// Fixed-width unsigned integer over up to 4 uint64_t limbs (256 bits),
// little-endian limb order (limb[0] is least significant), used in place
// of cs/src/klist.cpp's arbitrary-width std::vector<uint8_t> big-endian
// byte-string arithmetic. The Python reference and cs/ both allow n up to
// 256 bits (the class's own docstring); this covers the same range with
// native 64-bit words instead of a byte-at-a-time loop.
//
// Only the three operations klist.cpp's hash_merge/verify actually need
// are implemented: XOR, right-shift by a runtime bit count, and
// low-bits extraction as a uint64_t key. No add/multiply/compare beyond
// equality is needed anywhere in the algorithm.
#ifndef CS_V6_FIXEDINT_HPP
#define CS_V6_FIXEDINT_HPP

#include <array>
#include <cstdint>
#include <cstring>

namespace cs_v6 {

constexpr unsigned kMaxBits = 256;
constexpr unsigned kMaxLimbs = kMaxBits / 64; // 4

struct FixedUint {
    std::array<uint64_t, kMaxLimbs> limb{}; // limb[0] = least significant 64 bits
    unsigned nbits = 0;                     // active width; bits above this are always 0

    static FixedUint zero(unsigned nbits) {
        FixedUint v;
        v.nbits = nbits;
        return v;
    }

    // Loads a big-endian byte string (as produced by BLAKE2b / matching
    // Python's int.from_bytes(bytes, 'big')) into limb form.
    static FixedUint from_be_bytes(const uint8_t* data, unsigned nbytes) {
        FixedUint v;
        v.nbits = nbytes * 8;
        // Walk bytes from the end (least significant) to the front.
        for (unsigned i = 0; i < nbytes; ++i) {
            uint8_t byte = data[nbytes - 1 - i];
            unsigned limb_idx = i / 8;
            unsigned byte_in_limb = i % 8;
            v.limb[limb_idx] |= (uint64_t)byte << (8 * byte_in_limb);
        }
        return v;
    }

    FixedUint operator^(const FixedUint& other) const {
        FixedUint out;
        out.nbits = nbits;
        for (unsigned i = 0; i < kMaxLimbs; ++i) out.limb[i] = limb[i] ^ other.limb[i];
        return out;
    }

    // Right-shift by `bits` (0..nbits), matching Python's `x >> bits` on
    // the arbitrary-precision integer — bits below the shifted-out range
    // are discarded, result occupies the same nominal width (high bits
    // zero-filled), same semantics as klist.cpp's big_shr.
    FixedUint shr(unsigned bits) const {
        FixedUint out;
        out.nbits = nbits;
        if (bits == 0) { out.limb = limb; return out; }
        if (bits >= kMaxBits) return out; // all shifted out
        unsigned limb_shift = bits / 64;
        unsigned bit_shift = bits % 64;
        for (unsigned i = 0; i < kMaxLimbs; ++i) {
            unsigned src = i + limb_shift;
            if (src >= kMaxLimbs) { out.limb[i] = 0; continue; }
            uint64_t lo = limb[src];
            uint64_t hi = (bit_shift != 0 && src + 1 < kMaxLimbs) ? limb[src + 1] : 0;
            out.limb[i] = (bit_shift == 0) ? lo : ((lo >> bit_shift) | (hi << (64 - bit_shift)));
        }
        return out;
    }

    // Low `mask_bit` bits as a hash-table key (mask_bit <= 64 for every
    // parameter point this port targets, same bound klist.cpp documents).
    uint64_t low_bits_key(unsigned mask_bit) const {
        if (mask_bit >= 64) return limb[0];
        return limb[0] & ((uint64_t(1) << mask_bit) - 1);
    }

    bool is_zero() const {
        for (unsigned i = 0; i < kMaxLimbs; ++i) if (limb[i] != 0) return false;
        return true;
    }

    bool operator==(const FixedUint& other) const { return limb == other.limb; }
};

} // namespace cs_v6

#endif // CS_V6_FIXEDINT_HPP
