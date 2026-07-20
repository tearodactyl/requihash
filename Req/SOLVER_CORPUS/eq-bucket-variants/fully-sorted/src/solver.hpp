// Fully-sorted-buckets Equihash solver: contrasts against
// Req/rust/src/solve/bucket.rs's INCOMPLETE bucket sort (counting-sort
// partition on a bounded key prefix, then an exact-match LINEAR SCAN
// within each bucket for the true collision check -- the 2016-17
// tromp/xenoncat technique, Req/ARCHITECTURE.md S7a.2).
//
// This variant does the historically-superseded thing on purpose: after
// the same counting-sort bucket partition, each bucket's rows are fully
// COMPARISON-SORTED (std::sort) by their exact collision-bit key, and
// colliding pairs are found by scanning adjacent equal-key runs in the
// sorted order -- a real O(b log b) sort per bucket of size b, not the
// O(b) incomplete-sort-plus-linear-scan bucket.rs/V2 use. Since buckets
// are small in practice (b ~= nrows/nbuckets, typically single digits
// to low tens of rows), the two approaches usually visit a similar
// number of pairs -- the comparison here is about which one the
// historical record actually adopted (incomplete sort won -- this
// variant measures what was left on the table by going further to a
// full sort, i.e. whether full sorting's extra cost (the log b factor,
// plus sort's own overhead) is measurable at all against bucket.rs's
// linear approach at realistic bucket occupancy).
#ifndef EQ_FULLY_SORTED_SOLVER_HPP
#define EQ_FULLY_SORTED_SOLVER_HPP

#include "equihash_ref.hpp"

#include <cstdint>
#include <vector>

namespace eq_fully_sorted {

using eq_common::FixedUint;
using eq_common::Params;

class Solver {
public:
    Solver(unsigned n, unsigned k, std::vector<uint8_t> nonce);

    // Returns every distinct-index solution found (may be more than one).
    std::vector<std::vector<uint32_t>> solve() const;

    const Params& params() const { return p_; }

private:
    Params p_;
    std::vector<uint8_t> nonce_;
};

} // namespace eq_fully_sorted

#endif // EQ_FULLY_SORTED_SOLVER_HPP
