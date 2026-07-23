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
// ../../README.md's measured comparison).
#ifndef CS_V4_KLIST_HPP
#define CS_V4_KLIST_HPP

#include "fixedint.hpp"

#include <cstdint>
#include <vector>

namespace cs_v4 {

struct Row {
    FixedUint value;
    // Fixed-capacity index storage, sized to THIS ROW's actual round
    // depth (index_len = 2^depth: 1 at the leaves, 2, 4, ... up to k_ at
    // the root) -- NOT a flat k_ slots for every row regardless of round
    // (the original design here, found to waste ~99% of reserved index
    // space at early/populous rounds when k_ is large: e.g. at (160,512)
    // every one of the ~10M leaf-round rows reserved 512 slots to hold 1
    // real index, a 16.6GB peak vs. V1/V2/V5's ~350MB at the same point
    // -- root-caused and fixed 2026-07-20). Rows are stored in
    // PER-ROUND-WIDTH pools (Arena::index_pool_for_width), one flat
    // vector<uint32_t> per distinct width (1,2,4,...,k_) actually used,
    // each row within a pool getting exactly `width` slots -- still one
    // bulk allocation per width (not per row), preserving "static
    // allocation, no per-row heap alloc"; just no longer over-provisioned
    // to the GLOBAL maximum width for every row regardless of its own
    // round.
    uint32_t index_off;  // row's slot base WITHIN ITS WIDTH POOL = index_off * index_width
    uint32_t index_len;  // == this row's index_width (kept for clarity/assertions)
};

// One arena for the whole solve: a row pool (Row::value + bookkeeping)
// plus a family of index pools, one per distinct index width actually
// used (1, 2, 4, ..., up to k_), each row using exactly the pool
// matching its own round depth. Pre-sized from an estimated row-count
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

    // Row-bookkeeping pool (Row::value + index_off/index_len) -- ONE
    // shared pool across every round/width, entirely separate from the
    // per-width index-slot pools below. A row's bookkeeping slot and its
    // index-value slot are allocated independently (different pools,
    // different bump cursors, different free-lists) and only linked by
    // Row::index_off/index_len -- conflating the two (using a width
    // pool's allocation offset as if it were a row-pool offset) was this
    // fix's first draft's actual bug (heap-buffer-overflow, ASan-caught,
    // 2026-07-20 at (64,128): a width-0 alloc_rows offset was used to
    // index into the width-independent `rows` array, which has its own,
    // unrelated occupancy). Fixed by giving `rows` its own bump/free-list
    // pair, never shared with any WidthPool's.
    size_t alloc_row_slots(size_t count);
    void free_row_slots(size_t base, size_t count);

    // Allocates `count` contiguous index slots from the pool for
    // `index_width` (reused from that width's free-list if available,
    // else bump-allocated, growing that width's pool if needed); returns
    // the base slot index WITHIN THAT WIDTH POOL (not a row index).
    // `index_width` must be a power of 2 in [1, k_] (a round's actual
    // index length).
    size_t alloc_index_slots(size_t count, unsigned index_width);
    void free_index_slots(size_t base, size_t count, unsigned index_width);

    std::vector<Row> rows; // shared row-bookkeeping pool, independent of width
    size_t grow_events = 0; // diagnostic: how many times any pool's estimate was exceeded

    // Per-width index storage. Keyed by round depth (0..lgk_), not width
    // directly, since depth is what callers naturally have on hand and
    // width = 2^depth is a one-line derivation -- avoids a map lookup by
    // width value.
    struct WidthPool {
        std::vector<uint32_t> index_pool;
        std::vector<std::pair<size_t, size_t>> free_list; // (base, count), in index_width-row units
        size_t bump = 0;
    };
    std::vector<WidthPool> width_pools; // width_pools[depth] for depth in [0, lgk_]

    uint32_t* indices_of(size_t row_idx) {
        const Row& r = rows[row_idx];
        unsigned depth = width_to_depth(r.index_len);
        return &width_pools[depth].index_pool[(size_t)r.index_off * r.index_len];
    }
    const uint32_t* indices_of(size_t row_idx) const {
        const Row& r = rows[row_idx];
        unsigned depth = width_to_depth(r.index_len);
        return &width_pools[depth].index_pool[(size_t)r.index_off * r.index_len];
    }

private:
    unsigned lgk_ = 0;
    static unsigned width_to_depth(unsigned width) {
        unsigned d = 0;
        while ((1u << d) < width) ++d;
        return d;
    }
    void grow_width_pool(unsigned depth, size_t new_capacity_rows);
    void grow_row_pool(size_t new_capacity_rows);

    std::vector<std::pair<size_t, size_t>> row_free_list_; // (base, count), row-pool's own, never mixed with any WidthPool's
    size_t row_bump_ = 0;
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
