// Allocation-free leaf-message construction for compute_item, shared
// verbatim across V1-V5 (each variant's compute_item is otherwise
// byte-identical -- see ../../README.md "Known issues" for the profiling
// evidence this addresses). Replaces the original's three per-call heap
// allocations (std::string suffix via std::to_string, std::vector<uint8_t>
// message, std::vector<uint8_t> digest) with fixed-size stack buffers --
// same bytes in, same bytes out, no algorithmic change.
//
// Bound check: message is nonce(16) + decimal(i) + '-' + decimal(j).
// i is a list index < K <= 512 (<=3 digits); j is a leaf index < 2^ell,
// ell <=32 in this port's own constructor bound (2*ell<=64), so j needs
// at most 10 decimal digits (2^32-1 = 4294967295, 10 digits). Buffer
// sized 16 + 3 + 1 + 10 = 30, rounded up to 40 for margin -- static_assert
// below keeps this honest if the bound ever changes.
#ifndef CS_HASHMSG_HPP
#define CS_HASHMSG_HPP

#include <cstdint>
#include <cstddef>

namespace cs_common {

constexpr size_t kMaxMessageBytes = 40;

// Manual unsigned-to-decimal-ASCII, writing into buf starting at *pos
// (advanced in place). No allocation, no std::string. Standalone (not a
// local lambda) so callers needing just a decimal suffix -- e.g. V5's
// class-prefix-precomputed leaf loop, which already has the nonce+"i-"
// prefix cached in a blake2b_state and only needs decimal(j) appended --
// can reuse it without going through the full nonce+i+'-'+j builder.
inline void write_uint(unsigned v, uint8_t* buf, size_t* pos) {
    char digits[10];
    int nd = 0;
    if (v == 0) {
        buf[(*pos)++] = '0';
        return;
    }
    while (v > 0) {
        digits[nd++] = char('0' + (v % 10));
        v /= 10;
    }
    while (nd > 0) buf[(*pos)++] = uint8_t(digits[--nd]);
}

// Writes `nonce` (16 bytes) || decimal(i) || '-' || decimal(j) into buf,
// returns the total length written. No allocation, no std::string.
inline size_t build_leaf_message(const uint8_t* nonce, unsigned i, unsigned j, uint8_t* buf) {
    size_t pos = 0;
    for (int b = 0; b < 16; ++b) buf[pos++] = nonce[b];
    write_uint(i, buf, &pos);
    buf[pos++] = '-';
    write_uint(j, buf, &pos);
    return pos;
}

static_assert(16 + 3 + 1 + 10 <= kMaxMessageBytes, "kMaxMessageBytes too small for the documented (i,j) digit bound");

} // namespace cs_common

#endif // CS_HASHMSG_HPP
