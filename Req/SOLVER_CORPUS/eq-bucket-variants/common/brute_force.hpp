// Independent, deliberately naive O(N^2)-per-round Equihash oracle: no
// bucketing at all, every round does a full pairwise scan for
// collisions. Used ONLY as a differential cross-check at small (n,k)
// (where N is small enough this is fast) for the bucket-structure
// variants (fully-sorted/, two-level/), which have no pre-existing
// Python/paper reference the way cs/'s Sequihash vectors do -- this
// plays that role for plain Equihash. Independent from every bucketed
// solver's own logic (shares only leaf generation/verification, which
// is not where a bucketing bug would hide).
#ifndef EQ_COMMON_BRUTE_FORCE_HPP
#define EQ_COMMON_BRUTE_FORCE_HPP

#include "equihash_ref.hpp"

#include <vector>

namespace eq_common {

std::vector<std::vector<uint32_t>> solve_brute_force(const Params& p, const std::vector<uint8_t>& nonce);

} // namespace eq_common

#endif // EQ_COMMON_BRUTE_FORCE_HPP
