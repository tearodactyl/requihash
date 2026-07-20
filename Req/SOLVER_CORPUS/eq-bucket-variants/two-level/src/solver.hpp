// Two-level bucket Equihash solver: same total 16-bit bucket address
// space as Req/rust/src/solve/bucket.rs's flat single-level scheme
// (nbuckets = 2^16 when mask_bit>=16), but factored into two explicit
// 8-bit levels -- 256 top-level buckets, each containing 256 nested
// second-level sub-buckets -- instead of one flat 65536-entry counting
// sort.
//
// Motivation (Req/ARCHITECTURE.md S7a.10, "cache micro-architecture: the
// unexamined dimension" -- this variant makes that question concrete):
// a flat 65536-bucket counting sort's `counts`/`cursor` arrays are
// 2^16 * 4 bytes = 256 KiB each, larger than most L1 caches and
// comparable to L2 -- every counting-sort pass over that structure
// during the scatter phase pays cache-miss cost roughly uniformly.
// Two-level bucketing does the SAME total partition in two passes, each
// over a 256-entry (1 KiB) counts array that comfortably fits L1: pass 1
// buckets on the high 8 bits (256-way, ~1 KiB counting arrays); pass 2,
// run independently PER top-level bucket, buckets that bucket's rows on
// the next 8 bits (again a 256-way, ~1 KiB counting array, but now
// re-used/re-zeroed per outer bucket rather than one giant array touched
// once). This trades "one big scatter" for "many small scatters," which
// is a real cache-locality trade, not an asymptotic-complexity one --
// total work is the same O(rows), only the working-set-per-pass size
// changes.
#ifndef EQ_TWO_LEVEL_SOLVER_HPP
#define EQ_TWO_LEVEL_SOLVER_HPP

#include "equihash_ref.hpp"

#include <cstdint>
#include <vector>

namespace eq_two_level {

using eq_common::FixedUint;
using eq_common::Params;

class Solver {
public:
    Solver(unsigned n, unsigned k, std::vector<uint8_t> nonce);

    std::vector<std::vector<uint32_t>> solve() const;

    const Params& params() const { return p_; }

private:
    Params p_;
    std::vector<uint8_t> nonce_;
};

} // namespace eq_two_level

#endif // EQ_TWO_LEVEL_SOLVER_HPP
