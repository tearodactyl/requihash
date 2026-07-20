#include "solver.hpp"

#include <algorithm>
#include <stdexcept>

namespace eq_two_level {

namespace {

struct Row {
    FixedUint value;
    std::vector<uint32_t> indices;
};

constexpr unsigned kLevelBits = 8; // 256 buckets per level

} // namespace

Solver::Solver(unsigned n, unsigned k, std::vector<uint8_t> nonce)
    : p_(n, k), nonce_(std::move(nonce)) {
    if (nonce_.size() != 16) throw std::invalid_argument("nonce should be 16 bytes");
}

std::vector<std::vector<uint32_t>> Solver::solve() const {
    uint64_t count = p_.leaf_count();
    std::vector<Row> rows;
    rows.reserve(count);
    for (uint64_t j = 0; j < count; ++j) {
        rows.push_back(Row{eq_common::compute_leaf(p_, nonce_, (uint32_t)j), {(uint32_t)j}});
    }

    for (unsigned round = 1; round <= p_.k; ++round) {
        unsigned mask_bit = (round == p_.k) ? p_.ell * 2 : p_.ell;
        // Total bucket-address width, split into two 8-bit levels (same
        // 16-bit total as bucket.rs's flat cbyte.min(2) scheme) -- if
        // mask_bit < 16, the high level absorbs whatever's left and the
        // low level degenerates to width 0 (still correct, just no
        // second split -- matches bucket.rs's own key_bytes==1 fallback
        // for narrow rounds).
        unsigned total_bits = std::min(mask_bit, 16u);
        unsigned hi_bits = std::min(total_bits, kLevelBits);
        unsigned lo_bits = total_bits - hi_bits;
        uint64_t key_mask = (total_bits == 64) ? ~0ull : ((uint64_t(1) << total_bits) - 1);
        // full_key: the TRUNCATED bucket-address key (<=16 bits), used
        // only to route rows into the two-level bucket structure.
        auto full_key = [&](const FixedUint& v) { return v.low_bits_key(mask_bit) & key_mask; };
        // true_key: the EXACT mask_bit-wide join predicate -- when
        // mask_bit > 16 (large ell), the bucket address alone
        // under-determines a true collision, exactly the gap
        // bucket.rs/V2 close with their own within-bucket exact check;
        // this is that same check, not a relaxation of it.
        auto true_key = [&](const FixedUint& v) { return v.low_bits_key(mask_bit); };
        auto hi_of = [&](uint64_t k) { return (size_t)(k >> lo_bits); };
        auto lo_of = [&](uint64_t k) { return lo_bits == 0 ? 0 : (size_t)(k & ((uint64_t(1) << lo_bits) - 1)); };
        size_t nhi = size_t(1) << hi_bits;
        size_t nlo = lo_bits == 0 ? 1 : (size_t(1) << lo_bits);

        // PASS 1 (outer level): counting sort on the high hi_bits bits.
        // counts1 is nhi+1 entries -- at most 257, comfortably L1-resident.
        std::vector<uint32_t> counts1(nhi + 1, 0);
        for (const auto& r : rows) counts1[hi_of(full_key(r.value)) + 1]++;
        for (size_t b = 0; b < nhi; ++b) counts1[b + 1] += counts1[b];
        std::vector<uint32_t> order1(rows.size());
        {
            std::vector<uint32_t> cursor(counts1.begin(), counts1.end());
            for (uint32_t i = 0; i < rows.size(); ++i) {
                size_t b = hi_of(full_key(rows[i].value));
                order1[cursor[b]++] = i;
            }
        }

        std::vector<Row> next;
        // PASS 2 (inner level), run INDEPENDENTLY per outer bucket: a
        // fresh, small (nlo+1 <= 257 entries) counts array reused across
        // outer buckets -- each pass 2 invocation's working set is
        // small and local, the actual cache-locality claim this variant
        // tests (vs. one 65536-entry array touched once for the whole
        // round).
        std::vector<uint32_t> counts2(nlo + 1);
        std::vector<uint32_t> order2;
        for (size_t hb = 0; hb < nhi; ++hb) {
            uint32_t hi_lo = counts1[hb], hi_hi = counts1[hb + 1];
            size_t hi_count = hi_hi - hi_lo;
            if (hi_count < 2) continue;

            std::fill(counts2.begin(), counts2.end(), 0);
            for (uint32_t p = hi_lo; p < hi_hi; ++p) {
                counts2[lo_of(full_key(rows[order1[p]].value)) + 1]++;
            }
            for (size_t b = 0; b < nlo; ++b) counts2[b + 1] += counts2[b];
            order2.assign(hi_count, 0);
            {
                std::vector<uint32_t> cursor2(counts2.begin(), counts2.end());
                for (uint32_t p = hi_lo; p < hi_hi; ++p) {
                    size_t b = lo_of(full_key(rows[order1[p]].value));
                    order2[cursor2[b]++] = order1[p];
                }
            }

            // Within each (hi,lo) sub-bucket, check every pair against
            // the exact mask_bit key via true_key (not full_key, which
            // is only the truncated <=16-bit bucket address). NOTE: when
            // mask_bit > 16 (total_bits capped at 16, e.g. the final
            // round's 2*ell), full_key under-determines true_key -- rows
            // sharing a (hi,lo) sub-bucket are NOT guaranteed to be
            // sorted or even contiguous by true_key (a row with a
            // different true_key but the same truncated low 16 bits can
            // sit between two truly-matching rows in scatter order,
            // since PASS 1/2's counting sort only orders by full_key).
            // An adjacency-assuming run scan silently misses such splits
            // -- this port originally had exactly that bug (discovered
            // via differential testing against the brute-force oracle:
            // (72,5) at mask_bit=24 dropped 2 of 6 real solutions because
            // an interloper row with matching full_key but different
            // true_key sat between the two truly-colliding rows). Fixed
            // by an explicit O(bucket_size^2) pairwise true_key check --
            // correct and still cheap since sub-buckets are small
            // (~nrows/(nhi*nlo), single digits in practice).
            for (size_t lb = 0; lb < nlo; ++lb) {
                uint32_t lo_lo = counts2[lb], lo_hi = counts2[lb + 1];
                if (lo_hi - lo_lo < 2) continue;
                for (size_t a = lo_lo; a < lo_hi; ++a) {
                    for (size_t c = a + 1; c < lo_hi; ++c) {
                        const Row& ra = rows[order2[a]];
                        const Row& rc = rows[order2[c]];
                        if (true_key(ra.value) != true_key(rc.value)) continue;
                        bool overlap = false;
                        for (uint32_t xi : ra.indices) {
                            for (uint32_t xj : rc.indices) {
                                if (xi == xj) { overlap = true; break; }
                            }
                            if (overlap) break;
                        }
                        if (overlap) continue;
                        Row out;
                        out.value = (ra.value ^ rc.value).shr(mask_bit);
                        bool ra_first = ra.indices < rc.indices;
                        const Row& first = ra_first ? ra : rc;
                        const Row& second = ra_first ? rc : ra;
                        out.indices = first.indices;
                        out.indices.insert(out.indices.end(), second.indices.begin(), second.indices.end());
                        next.push_back(std::move(out));
                    }
                }
            }
        }
        rows = std::move(next);
        if (rows.empty()) break;
    }

    std::vector<std::vector<uint32_t>> solutions;
    for (const auto& r : rows) {
        if (r.value.is_zero() && r.indices.size() == (size_t(1) << p_.k)) {
            solutions.push_back(r.indices);
        }
    }
    return solutions;
}

} // namespace eq_two_level
