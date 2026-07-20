// V4 -- static allocation + slot-reuse variant, built on V1's
// fixed-width integers. Applies 2016-17 techniques #3 and #4
// (Equihash.md:131, Req/ARCHITECTURE.md S7): (3) STATIC ALLOCATION --
// one arena, sized once from (n,K) up front, no per-round Vec growth;
// (4) SLOT REUSE -- once a round's rows are no longer referenced by any
// not-yet-processed merge (the stack-based post-order tree means a
// round is consumed exactly once, immediately after it is produced or
// popped), its arena range is returned to a free-list and the next
// round's output is written into reclaimed space before falling back to
// bump-allocating fresh arena.
//
// Not a byte-for-byte port of zcash's posFree cursor (that read/wrote
// the SAME array in place for a single-list algorithm with bounded
// per-round output size; Sequihash's k-list merge-tree can produce more
// output rows than either input, so an in-place-into-the-same-buffer
// scheme is unsafe here) -- this is the closest safe analogue: a single
// static arena, no per-round std::vector allocation, and immediate
// reuse of freed ranges, which is the allocator-overhead reduction
// technique #3/#4 actually buys (BENCHMARK.md's finding that per-row
// heap allocation dominates solve time applies here too, see
// variants/README.md's measured comparison).
#ifndef CS_V4_KLIST_HPP
#define CS_V4_KLIST_HPP

#include "fixedint.hpp"

#include <cstdint>
#include <vector>

namespace cs_v4 {

struct Row {
    FixedUint value;
    // Fixed-capacity index storage sized to k_ at construction time (see
    // Arena::index_cap) -- avoids std::vector's own heap allocation per
    // row, the other half of "static allocation" at the row level, not
    // just the round level. Stored as an offset (not a raw pointer) into
    // Arena::index_pool so the pool can safely grow (reallocate) without
    // invalidating live rows -- see Arena's growth note.
    uint32_t index_off; // row's slot base within index_pool = index_off * index_cap
    uint32_t index_len;
};

// One arena for the whole solve: a row pool and a matching index pool
// (index_cap = k_ uint32_t's per row, the maximum any row ever needs),
// with a free-list of reclaimed row slots. Pre-sized from an estimated
// budget (STATIC ALLOCATION, technique #3) for the common case of no
// reallocation; grows (geometric doubling, like a normal Vec) only if a
// merge round's real output exceeds the estimate -- Sequihash's k-list
// merge output is not bounded by either input's size in general (a
// per-(n,K) worst case would need real collision-count modeling, not a
// fixed multiplier), so a hard cap would be a correctness bug, not a
// performance choice. Growth is the exceptional path; steady-state
// solves against the estimate hit zero reallocations, which is what
// "static allocation" is actually claiming.
class Arena {
public:
    void init(size_t max_live_rows, unsigned k);
    // Allocates `count` contiguous row slots (reused from the free-list
    // if available, else bump-allocated, growing the pool if needed);
    // returns the base slot index.
    size_t alloc_rows(size_t count);
    // Returns a previously allocated [base, base+count) range to the
    // free-list for reuse by a later alloc_rows call.
    void free_rows(size_t base, size_t count);

    std::vector<Row> rows;
    std::vector<uint32_t> index_pool;
    unsigned index_cap = 0;
    size_t grow_events = 0; // diagnostic: how many times the estimate was exceeded

private:
    std::vector<std::pair<size_t, size_t>> free_list_; // (base, count)
    size_t bump_ = 0;
    void grow_to(size_t new_capacity_rows);
};

class KListWagnerAlgorithmV4 {
public:
    KListWagnerAlgorithmV4(unsigned n, unsigned k, std::vector<uint8_t> nonce);

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

    std::vector<std::vector<uint32_t>> solve_internal() const;
};

} // namespace cs_v4

#endif // CS_V4_KLIST_HPP
